#!/usr/bin/env python3

import argparse
import base64
import json
import math
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import wave


ROOT = Path(__file__).resolve().parents[1]
VASS_ROOT = ROOT.parent


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(url, method="GET", body=None, content_type="application/json", timeout=240):
    data = body
    if body is not None and not isinstance(body, (bytes, bytearray)):
        data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers={"content-type": content_type},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            payload = response.read()
            return response.status, response.headers.get_content_type(), payload
    except urllib.error.HTTPError as error:
        return error.code, error.headers.get_content_type(), error.read()


def request_json(base, path, method="GET", body=None, timeout=240):
    status, _, payload = request(base + path, method, body, timeout=timeout)
    decoded = json.loads(payload.decode("utf-8"))
    if status >= 400:
        raise RuntimeError(f"{method} {path} returned {status}: {decoded}")
    return decoded


def request_binary_json(base, path, payload):
    status, _, body = request(
        base + path,
        "POST",
        payload,
        "application/octet-stream",
    )
    decoded = json.loads(body.decode("utf-8"))
    if status >= 400:
        raise RuntimeError(f"POST {path} returned {status}: {decoded}")
    return decoded


def wait_http(base, process, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"GUI backend exited early with status {process.returncode}")
        try:
            request_json(base, "/api/projects", timeout=1)
            return
        except Exception:
            time.sleep(0.05)
    raise RuntimeError("timed out waiting for GUI backend")


def graph_snapshot(config):
    frontend_by_pipeline = {
        item["pipeline_id"]: item for item in config.get("frontend_connections", [])
    }
    nodes = []
    connections = []
    groups = []
    for pipeline in config.get("pipelines", []):
        pipeline_id = pipeline["pipe_id"]
        frontend = frontend_by_pipeline.get(pipeline_id, {})
        graph_nodes = frontend.get("nodes", []) + pipeline.get("nodes", [])
        graph_edges = frontend.get("edges", []) + pipeline.get("edges", [])
        group_nodes = []
        group_edges = []
        for node in graph_nodes:
            graph_id = f"{pipeline_id}__{node['node_id']}"
            group_nodes.append(graph_id)
            nodes.append({
                "id": graph_id,
                "name": node.get("name", node["node_id"]),
                "module_type": node["module_type"],
                "pipelineId": pipeline_id,
                "pipelineNodeId": node["node_id"],
                "params": node.get("params", {}),
                "debug_file_io": node["module_type"] in (
                    "builtin.file_input", "builtin.file_output"
                ),
            })
        for edge in graph_edges:
            from_node, from_port = edge["from"].split(":", 1)
            to_node, to_port = edge["to"].split(":", 1)
            graph_from = f"{pipeline_id}__{from_node}"
            graph_to = f"{pipeline_id}__{to_node}"
            group_edges.append(f"{graph_from}:{from_port}->{graph_to}:{to_port}")
            connections.append({
                "from": f"{graph_from}.{from_port}",
                "to": f"{graph_to}.{to_port}",
            })
        groups.append({
            "id": pipeline_id,
            "pipeline_id": pipeline_id,
            "name": pipeline.get("name", pipeline_id),
            "origin_pipeline_ids": [pipeline_id],
            "nodes": group_nodes,
            "edges": group_edges,
        })
    return {
        "workspace_id": config["workspace_id"],
        "project": "simulator/simulator.json",
        "active_pipeline": "PLAYBACK_MAIN",
        "group_id": "ALL",
        "build_scope": "all_pipelines",
        "scope": "all_working_groups",
        "runtime_state": "PIPE_UNLOADED",
        "working_groups": groups,
        "nodes": nodes,
        "connections": connections,
    }


def make_tone_wav(path, duration=0.5, rate=48000, channels=2, frequency=1000):
    samples = round(duration * rate)
    pcm = bytearray()
    for index in range(samples):
        value = round(12000 * math.sin(2 * math.pi * frequency * index / rate))
        pcm.extend(struct.pack("<h", value) * channels)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm)
    return bytes(pcm)


def write_pcm_wav(path, pcm, rate=48000, channels=2):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(pcm)


def assert_wav(payload, expected_pcm_bytes, label):
    if payload[:4] != b"RIFF" or payload[8:12] != b"WAVE":
        raise AssertionError(f"{label} is not a RIFF/WAVE file")
    with tempfile.NamedTemporaryFile(suffix=".wav") as tmp:
        tmp.write(payload)
        tmp.flush()
        with wave.open(tmp.name, "rb") as wav:
            pcm = wav.readframes(wav.getnframes())
            if wav.getframerate() != 48000 or wav.getnchannels() != 2 or wav.getsampwidth() != 2:
                raise AssertionError(f"{label} format is not 48 kHz, stereo, signed 16-bit")
    if len(pcm) < expected_pcm_bytes:
        raise AssertionError(f"{label} has {len(pcm)} PCM bytes, expected at least {expected_pcm_bytes}")
    if not any(pcm):
        raise AssertionError(f"{label} contains only silence")
    return pcm


def left_channel_samples(pcm):
    samples = struct.unpack("<" + "h" * (len(pcm) // 2), pcm)
    return samples[::2]


def best_tone_correlation(reference_pcm, captured_pcm, max_offset_samples=2400, window_samples=2000):
    reference = left_channel_samples(reference_pcm)
    captured = left_channel_samples(captured_pcm)
    if len(reference) < window_samples or len(captured) < window_samples:
        return 0.0, 0
    window = reference[:window_samples]
    window_energy = sum(sample * sample for sample in window)
    if window_energy == 0:
        return 0.0, 0
    best = (0.0, 0)
    limit = min(max_offset_samples, len(captured) - window_samples)
    for offset in range(max(0, limit + 1)):
        candidate = captured[offset:offset + window_samples]
        candidate_energy = sum(sample * sample for sample in candidate)
        if candidate_energy == 0:
            continue
        score = sum(a * b for a, b in zip(window, candidate))
        score /= math.sqrt(window_energy * candidate_energy)
        if score > best[0]:
            best = (score, offset)
    return best


def descendants(pid):
    found = set()
    pending = [pid]
    while pending:
        parent = pending.pop()
        children_path = Path(f"/proc/{parent}/task/{parent}/children")
        try:
            children = [int(item) for item in children_path.read_text().split()]
        except (FileNotFoundError, ProcessLookupError):
            continue
        for child in children:
            if child not in found:
                found.add(child)
                pending.append(child)
    return found


def qemu_descendant(backend_pid, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for pid in descendants(backend_pid):
            try:
                command = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ")
            except (FileNotFoundError, ProcessLookupError):
                continue
            if b"qemu-system-riscv32" in command:
                return pid
        time.sleep(0.05)
    raise RuntimeError("could not find validation QEMU below GUI backend")


def pipeline_group(snapshot, pipeline_id):
    return next(group for group in snapshot["working_groups"] if group["id"] == pipeline_id)


def run_playback(base, snapshot, pcm, session_id, backend_pid, test_stall,
                 pipeline_id="PLAYBACK_MAIN", device="as_config_playback", dai_index=0):
    group = pipeline_group(snapshot, pipeline_id)
    file_node_id = f"{pipeline_id}__FILE_IN"
    edge_key = f"{pipeline_id}__FILE_IN:out->{pipeline_id}__HOST_IN:in"
    audio = {
        "enabled": True,
        "node_id": file_node_id,
        "file_name": "tone.wav",
        "device": device,
        "edge_key": edge_key,
        "sample_rate": 48000,
        "channels": 2,
        "bits_per_sample": 16,
        "frame_samples": 960,
        "frame_bytes": 3840,
        "frame_ms": 20,
        "data_bytes": len(pcm),
    }
    response = request_json(base, "/api/runtime/run", "POST", {
        "session_id": session_id,
        "project": "simulator/simulator.json",
        "pipeline_id": pipeline_id,
        "group_id": pipeline_id,
        "runtime_state": "PIPE_RUNNING",
        "nodes": group["nodes"],
        "edges": group["edges"],
        "playback": audio,
    })
    if not response.get("ok") or response.get("runtime_state") != "PIPE_RUNNING":
        raise AssertionError(f"{pipeline_id} playback RUN rejected: {response}")

    qemu_pid = qemu_descendant(backend_pid) if test_stall else None
    stopped = False
    offset = 0
    frame_index = 0
    stall_checked = not test_stall
    try:
        while offset < len(pcm):
            frame = pcm[offset:offset + audio["frame_bytes"]]
            query = urllib.parse.urlencode({
                "edge_key": edge_key,
                "frame_index": frame_index,
                "sample_rate": 48000,
                "channels": 2,
                "bits": 16,
            })
            stream = request_binary_json(base, "/api/runtime/audio/playback/stream?" + query, frame)
            if not stream.get("ok") or stream.get("accepted_bytes") != len(frame):
                raise AssertionError(f"playback stream rejected frame {frame_index}: {stream}")

            if test_stall and frame_index == 2:
                os.kill(qemu_pid, signal.SIGSTOP)
                stopped = True
            if stopped and frame_index == 3:
                time.sleep(0.15)

            status = request_json(base, "/api/runtime/audio/playback/frame?" + query, "POST", {
                "edge_key": edge_key,
                "frame_index": frame_index,
                "frame_bytes": len(frame),
                "bytes_written": stream["accepted_bytes"],
                "stream_ok": True,
            })
            if not status.get("ok") or not status.get("accepted"):
                raise AssertionError(f"playback frame metadata rejected: {status}")
            if stopped and frame_index == 3:
                if not status.get("stalled") or status.get("blocked_edge_key") != edge_key:
                    raise AssertionError(f"100 ms blockage was not mapped to GUI edge: {status}")
                stall_checked = True
                os.kill(qemu_pid, signal.SIGCONT)
                stopped = False

            offset += len(frame)
            frame_index += 1
            time.sleep(max(0.001, min(0.05, status.get("next_push_ms", 20) / 1000)))
    finally:
        if stopped:
            os.kill(qemu_pid, signal.SIGCONT)
    if not stall_checked:
        raise AssertionError("playback blockage check did not run")

    eos = request_json(base, "/api/runtime/audio/playback/eos", "POST", {
        "session_id": session_id,
        "group_id": pipeline_id,
        "edge_key": edge_key,
        "frame_index": frame_index,
        "timeout_ms": 5000,
        "reason": "eof",
    })
    if not eos.get("ok") or eos.get("runtime_state") != "PIPE_LOADED":
        raise AssertionError(f"playback EOS failed: {eos}")
    if eos.get("playback", {}).get("played_bytes", 0) != len(pcm):
        raise AssertionError(f"playback byte count mismatch: {eos}")

    query = urllib.parse.urlencode({
        "workspace_id": snapshot["workspace_id"],
        "dai_index": dai_index,
        "file_name": f"{session_id}.wav",
    })
    status, content_type, output = request(base + "/api/runtime/audio/dai/output?" + query)
    if status != 200 or content_type != "audio/wav":
        raise AssertionError(f"{pipeline_id} FILE_IO_DAI playback output is unavailable: HTTP {status}")
    assert_wav(output, len(pcm), f"{pipeline_id} playback output {session_id}")


def run_capture(base, snapshot, input_wav, expected_pcm, artifacts_dir):
    stage_query = urllib.parse.urlencode({
        "workspace_id": snapshot["workspace_id"],
        "node_id": "CAPTURE_MAIN__DAI_IN",
        "dai_index": 0,
        "file_name": "capture-input.wav",
    })
    staged = request_binary_json(base, "/api/runtime/audio/dai/input?" + stage_query, input_wav)
    if not staged.get("ok") or staged.get("accepted_bytes") != len(input_wav):
        raise AssertionError(f"FILE_IO_DAI capture input was not staged: {staged}")

    group = pipeline_group(snapshot, "CAPTURE_MAIN")
    capture = {
        "enabled": True,
        "node_id": "CAPTURE_MAIN__FILE_OUT",
        "file_name": "capture-output.wav",
        "device": "as_config_capture",
        "edge_key": "CAPTURE_MAIN__HOST_OUT:out->CAPTURE_MAIN__FILE_OUT:in",
        "sample_rate": 48000,
        "channels": 2,
        "bits_per_sample": 16,
        "frame_samples": 960,
        "frame_bytes": 3840,
        "expected_data_bytes": len(expected_pcm),
    }
    response = request_json(base, "/api/runtime/run", "POST", {
        "session_id": "gui-e2e-capture",
        "project": "simulator/simulator.json",
        "pipeline_id": "CAPTURE_MAIN",
        "group_id": "CAPTURE_MAIN",
        "runtime_state": "PIPE_RUNNING",
        "nodes": group["nodes"],
        "edges": group["edges"],
        "capture": capture,
    })
    if not response.get("ok") or response.get("runtime_state") != "PIPE_RUNNING":
        raise AssertionError(f"capture RUN rejected: {response}")

    captured = bytearray()
    deadline = time.monotonic() + 15
    while len(captured) < len(expected_pcm) and time.monotonic() < deadline:
        limit = min(capture["frame_bytes"], len(expected_pcm) - len(captured))
        query = urllib.parse.urlencode({"max_bytes": limit, "node_id": capture["node_id"]})
        response = request_json(base, "/api/runtime/audio/capture/frame?" + query)
        if not response.get("ok"):
            raise AssertionError(f"capture frame failed: {response}")
        frame = base64.b64decode(response.get("capture", {}).get("data_base64", ""))
        captured.extend(frame)
        time.sleep(max(0.001, min(0.05, response.get("capture", {}).get("next_poll_ms", 20) / 1000)))
    stopped = request_json(base, "/api/runtime/stop", "POST", {
        "session_id": "gui-e2e-capture",
        "group_id": "CAPTURE_MAIN",
        "runtime_state": "PIPE_LOADED",
    })
    if not stopped.get("ok") or stopped.get("runtime_state") != "PIPE_LOADED":
        raise AssertionError(f"capture stop failed: {stopped}")
    if len(captured) < len(expected_pcm) or not any(captured):
        raise AssertionError(f"capture produced {len(captured)} invalid PCM bytes")
    write_pcm_wav(artifacts_dir / "capture-output.wav", bytes(captured[:len(expected_pcm)]))
    correlation, offset = best_tone_correlation(expected_pcm, captured[:len(expected_pcm)])
    if correlation < 0.90:
        raise AssertionError(
            f"captured PCM does not match FILE_IO_DAI input: "
            f"captured={len(captured)} expected={len(expected_pcm)} correlation={correlation:.3f} offset={offset}"
        )
    return bytes(captured[:len(expected_pcm)])


def verify_system_info(base, snapshot, timeout=6):
    node_ids = [
        node["id"] for node in snapshot["nodes"]
        if not node.get("debug_file_io")
    ]
    deadline = time.monotonic() + timeout
    last = {}
    while time.monotonic() < deadline:
        costs = request_json(base, "/api/algorithm/cost/live?nodes=" + urllib.parse.quote(",".join(node_ids)))
        cores = request_json(base, "/api/dsp/core/loading?cores=4")
        health = request_json(base, "/api/system/health/live")
        last = {"costs": costs, "cores": cores, "health": health}
        if (costs.get("connected") and cores.get("connected") and
                health.get("connected") and health.get("rows")):
            if len(costs.get("costs", [])) != len(node_ids):
                raise AssertionError(f"algorithm cost node mapping is incomplete: {costs}")
            return
        time.sleep(0.1)
    raise AssertionError(f"System Info UI APIs did not become connected: {last}")


def verify_as_log(as_log, rpc_port):
    command = [
        str(as_log),
        "--host", "127.0.0.1",
        "--port", str(rpc_port),
        "--level", "debug",
        "--count", "64",
        "--no-color",
    ]
    completed = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=8,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    if completed.returncode != 0:
        raise AssertionError(f"as_log failed with {completed.returncode}: {output[-2000:]}")
    if "ASINFO" in output:
        raise AssertionError(f"as_log leaked Audio Studio info traces: {output[-2000:]}")
    if not output.strip():
        raise AssertionError("as_log completed without decoded log output")


def terminate(process):
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def main():
    parser = argparse.ArgumentParser(description="Audio Studio GUI simulator audio end-to-end test")
    parser.add_argument("--no-stall-check", action="store_true")
    parser.add_argument("--artifacts-dir", type=Path)
    args = parser.parse_args()

    backend = ROOT / "out/linux/simulator/gui_backend/Debug/audio_studio_gui_server"
    as_server = ROOT / "out/linux/simulator/rpc_socket/Debug/as_server"
    as_log = ROOT / "out/linux/simulator/rpc_socket/Debug/as_log"
    alsatplg = ROOT / "third_party/alsatplg/bin/alsatplg"
    helper = VASS_ROOT / "application/rv32qemu/sof-build-test.py"
    trace_ldc = VASS_ROOT / "application/rv32qemu/build/sof.ldc"
    for required in (backend, as_server, as_log, alsatplg, helper, trace_ldc):
        if not required.exists():
            raise SystemExit(f"required E2E artifact is missing: {required}")

    temporary = tempfile.TemporaryDirectory(prefix="audio-studio-gui-e2e-")
    work = Path(temporary.name)
    artifacts = args.artifacts_dir or work
    artifacts.mkdir(parents=True, exist_ok=True)
    backend_log = artifacts / "gui-backend.log"
    input_wav = artifacts / "input.wav"
    pcm = make_tone_wav(input_wav)
    input_bytes = input_wav.read_bytes()
    http_port = free_port()
    rpc_port = free_port()
    base = f"http://127.0.0.1:{http_port}"
    command = [
        str(backend), str(ROOT), str(http_port),
        "--as-server", str(as_server),
        "--alsatplg", str(alsatplg),
        "--as-server-rpc-mode", "socket",
        "--as-server-host", "127.0.0.1",
        "--as-server-port", str(rpc_port),
        "--validation-python", "python3",
        "--validation-script", str(helper),
        "--validation-as-log", str(as_log),
        "--validation-trace-ldc", str(trace_ldc),
        "--validation-datalink", str(work / "as_datalink"),
        "--runtime-as-server-host", "127.0.0.1",
        "--runtime-as-server-port", str(rpc_port),
        "--audio-driver-factory", "simulator",
    ]
    with backend_log.open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_http(base, process)
            config = request_json(base, "/api/config?project=simulator%2Fsimulator.json")
            snapshot = graph_snapshot(config)
            if len(snapshot["working_groups"]) != len(config.get("pipelines", [])):
                raise AssertionError("GUI build snapshot omitted a pipeline")
            pipeline_ids = {group["id"] for group in snapshot["working_groups"]}
            expected_pipeline_ids = {"PLAYBACK_MAIN", "CAPTURE_MAIN", "DSP_FILTER_COVERAGE"}
            missing_pipeline_ids = expected_pipeline_ids - pipeline_ids
            if missing_pipeline_ids:
                raise AssertionError(f"GUI snapshot omitted simulator pipeline(s): {sorted(missing_pipeline_ids)}")
            result = request_json(base, "/api/pipeline/build", "POST", snapshot)
            if not result.get("ok") or result.get("runtime_state") != "PIPE_LOADED":
                raise AssertionError(f"all-pipeline build failed: {result}")

            verify_system_info(base, snapshot)
            verify_as_log(as_log, rpc_port)
            run_playback(base, snapshot, pcm, "gui-e2e-play-1", process.pid, not args.no_stall_check)
            run_playback(base, snapshot, pcm, "gui-e2e-play-2", process.pid, False)
            run_playback(
                base,
                snapshot,
                pcm,
                "gui-e2e-dsp-filter",
                process.pid,
                False,
                pipeline_id="DSP_FILTER_COVERAGE",
                device="as_config_dsp_filter",
                dai_index=1,
            )
            captured = run_capture(base, snapshot, input_bytes, pcm, artifacts)
            capture_wav = artifacts / "capture-output.wav"
            write_pcm_wav(capture_wav, captured)
            assert_wav(capture_wav.read_bytes(), len(pcm), "GUI capture output")

            unload = request_json(base, "/api/pipeline/unload", "POST", snapshot)
            if not unload.get("ok") or unload.get("runtime_state") != "PIPE_UNLOADED":
                raise AssertionError(f"pipeline unload failed: {unload}")
            stall_label = "stall" if not args.no_stall_check else "no-stall"
            print(f"GUI simulator E2E passed: build, System Info, as_log, {stall_label}, playback main x2, DSP filter playback, capture, unload")
            print(f"backend log: {backend_log}")
        finally:
            terminate(process)
            if process.returncode not in (0, -signal.SIGINT):
                print(backend_log.read_text(errors="replace"))
                raise RuntimeError(f"GUI backend exited with status {process.returncode}")
    if args.artifacts_dir is None:
        temporary.cleanup()


if __name__ == "__main__":
    main()
