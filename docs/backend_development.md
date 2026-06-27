# Backend Development Guide

GUI backend 是浏览器和 Audio Studio runtime 之间的 HTTP/stream adapter。它不替代 `as_server`，也不直接实现 SOF runtime；它负责 workspace JSON、build orchestration、前端状态语义、File Input/Output 数据面和 dashboard API。

## 构建

```bash
cmake --build out/linux/simulator/gui_backend/Debug --parallel 16
cmake --build out/linux/simulator/rpc_socket/Debug --parallel 16
cmake --build out/linux/simulator/audio_controller/Debug --parallel 16
```

## 运行

backend 依赖路径通过参数传入，不再从环境变量猜测：

```bash
out/linux/simulator/gui_backend/Debug/audio_studio_server . 8080 \
  --as-server out/linux/simulator/rpc_socket/Debug/as_server \
  --alsatplg third_party/alsatplg/bin/alsatplg \
  --as-server-rpc-mode once \
  --validation-python python3 \
  --validation-script ../application/rv32qemu/sof-build-test.py \
  --validation-as-server out/linux/simulator/rpc_socket/Debug/as_server \
  --validation-as-log out/linux/simulator/rpc_socket/Debug/as_log \
  --validation-trace-ldc ../application/rv32qemu/build/sof.ldc \
  --validation-as-server-host 127.0.0.1 \
  --validation-as-server-port 9901 \
  --runtime-as-server-host 127.0.0.1 \
  --runtime-as-server-port 9901 \
  --audio-driver-factory simulator
```

`BackendRuntimeConfig` 是唯一配置入口。测试可以直接构造该 struct，VSCode debug 也通过 argv 传入同一组字段。

## API

| Method | Path | 说明 |
|---|---|---|
| GET | `/` | 前端页面 |
| GET | `/api/projects` | 扫描 `configs/platform/*/*.json` |
| GET | `/api/config?project=...` | 打开产品 JSON，创建 workspace copy |
| POST | `/api/pipeline/build` | 全 layout build/load |
| POST | `/api/pipeline/unload` | 停止 keep-alive simulator |
| POST | `/api/runtime/run` | 启动 selected pipeline playback/capture |
| POST | `/api/runtime/audio/playback/stream` | File Input PCM binary stream |
| POST | `/api/runtime/audio/playback/frame` | Playback frame metadata/backpressure |
| POST | `/api/runtime/audio/playback/eos` | Playback EOS/drain/close |
| GET | `/api/runtime/audio/capture/frame` | File Output capture frame |
| POST | `/api/runtime/stop` | 停止 selected runtime |
| POST | `/api/runtime/file-io-dai/input` | stage FILE_IO_DAI input WAV |
| GET | `/api/runtime/file-io-dai/output` | fetch FILE_IO_DAI output WAV |
| POST | `/api/param/update` | RUN 中参数更新 |
| GET | `/api/algorithm/cost/live` | PER-ALGORITHM COST |
| GET | `/api/dsp/core/loading` | DSP CORE LOADING |
| GET | `/api/system/health/live` | SYSTEM HEALTH |
| POST | `/api/project/save` | 保存源 JSON，strip `audio_studio_gui` |

## Build Orchestration

`BuildOrchestrator` owns workspace lifecycle:

1. `GET /api/config` copies the source JSON into `/tmp/audio-studio-gui-workspaces/...`.
2. frontend sends a full layout snapshot to `/api/pipeline/build`.
3. backend regenerates `pipelines[]` and `frontend_connections[]`.
4. `audio_studio_gui` is kept only in the temporary workspace and removed before save/compile.
5. `ConfigCompileClient` calls `as_server config.compile`; default mode is `--rpc-once`.
6. backend creates an audio-controller test list with `ac_run`, `trace on`, and `pipeinstall <tplg>`.
7. `ProcessValidationRunner` starts `application/rv32qemu/sof-build-test.py --audio-controller-log --gui-keep-alive`.
8. helper writes a ready marker after `pipeinstall`; backend returns `PIPE_LOADED`.
9. `unload` or destructor stops the validation process group.

Build is all-pipeline by design. A selected pipeline controls only RUN/STOP.

## Runtime Engine

`GuiRuntimeEngine` is the production runtime implementation behind `IRuntimeEngine`.

Playback:

- `/api/runtime/run` creates an as_server playback session.
- `/api/runtime/audio/playback/stream` accepts binary PCM and enqueues it.
- `/api/runtime/audio/playback/frame` returns queue size, queued audio duration, `next_push_ms`, and blockage status.
- worker thread immediately forwards queued frames to as_server JSON-RPC.
- `/api/runtime/audio/playback/eos` waits for queue drain and closes the session.

Capture:

- `/api/runtime/run` creates an as_server capture session.
- `/api/runtime/audio/capture/frame` reads directly from as_server and returns base64 PCM.
- backend does not build a capture queue.

Blockage:

- local queue: RPC not ready, high water, or queued frame not dequeued for 100ms.
- system buffer: `systemInfo.buffers` reports stalled or produced/available data with no consumed progress for 100ms.
- response includes `blocked_edge_key` so frontend can flash the connection buffer point.

## System Info Controllers

`SystemInfoControllers` call as_server `systemInfo.*` RPC and transform the snapshot into frontend panel data:

- algorithm cost rows are mapped by requested node ids.
- DSP core loading uses core frequency/load from ASINFO.
- System Health expands heap rows by category/index/block size and reports `free_count/total_count`.
- disconnected snapshots return non-historical empty values.

## Parameter Updates

`/api/param/update` accepts:

```json
{
  "workspace_id": "ws_simulator_simulator_json",
  "pipeline_id": "PLAYBACK_MAIN",
  "node_id": "VOLUME",
  "param_id": "volume_db",
  "value": -6
}
```

The backend updates the temporary workspace `inspector_preset` immediately and returns `control_apply:"pending_as_control"`. This keeps frontend behavior deterministic while leaving the actual SOF control path for as_control integration.

## Tests

```bash
python3 tests/system-info-runtime-contract.test.py
python3 tests/backend-process-lifecycle-contract.test.py
ctest --test-dir out/linux/simulator/gui_backend/Debug --output-on-failure
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts
```

The E2E test builds `configs/platform/simulator/simulator.json`, verifies System Info, verifies `as_log` does not leak ASINFO, runs playback twice, checks stall reporting by pausing QEMU, and records a WAV through capture.
