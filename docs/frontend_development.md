# Frontend Development Guide

前端是 standalone HTML/CSS/JS：

```text
GUI/frontend/index.html
GUI/frontend/assets/js/
configs/built-in-algorithm.json
```

它由 C++ GUI backend 托管，不需要 npm build。根 `index.html` 只做跳转。

## 运行

```bash
out/linux/simulator/gui_backend/Debug/audio_studio_gui_server . 8080 \
  --as-server out/linux/simulator/rpc_socket/Debug/as_server \
  --alsatplg third_party/alsatplg/bin/alsatplg \
  --as-server-host 127.0.0.1 \
  --as-server-port 9900 \
  --helper-script ../application/rv32qemu/sof-build-test.py \
  --as-log out/linux/simulator/rpc_socket/Debug/as_log \
  --trace-ldc ../application/rv32qemu/build/sof.ldc \
  --audio-driver-factory simulator
```

打开：

```text
http://127.0.0.1:8080
```

## 代码组织

`GUI/frontend/index.html` 包含当前 UI shell、CSS 和主要 runtime glue。`GUI/frontend/assets/js/` 下是可单测的纯逻辑模块：

```text
configParser.js
layout.js
pipelineRules.js
pipelineEditCallbackModel.js
topbarPanelMenuModel.js
utils.js
```

这些模块面向 Node contract tests，保持旧 Node 可解析语法。

## UI 区域

```text
Topbar
├─ Project / DSP / Cores / Frequency
├─ Validate / Build / Run / Stop
├─ Auto Arrange / Undo / Redo / Save / Export

Algorithm Library
Pipeline Canvas
Inspector
Runtime Dashboard
├─ Per-Algorithm Cost
├─ DSP Core Loading
├─ Real-Time Signal Probe
├─ System Health
├─ Audio I/O
└─ Event Log
```

## Config Loading

1. `GET /api/projects` lists platform JSON files.
2. `GET /api/config?project=simulator/simulator.json` loads a workspace copy.
3. `imports` + `module_types` + `configs/built-in-algorithm.json` build the module registry.
4. `pipelines[]` render SOF nodes and SOF edges.
5. `frontend_connections[]` render `builtin.file_input/output` nodes and external HOST edges.

`frontend_connections[]` uses the same node/edge shape as `pipelines[]`, but it never goes into tplg compile.

## Build / Run State

Runtime state enum:

```text
PIPE_UNLOADED
PIPE_LOADED
PIPE_RUNNING
```

Rules:

- Build sends the full current layout snapshot.
- Build always compiles all working pipeline groups together.
- A selected pipeline only affects RUN/STOP.
- Build button becomes busy/disabled during build.
- Build success moves all pipelines to `PIPE_LOADED`.
- RUN button becomes running/disabled for the active pipeline until stop/EOS/error result.
- On any runtime error frontend stops animation and restores pre-run controls.

## Parameters

Inspector uses module parameter schema:

- `apply.settable_states` controls disabled state.
- `PIPE_UNLOADED` means build/load before editable.
- `PIPE_LOADED` means loaded but not running editable.
- `PIPE_RUNNING` means runtime editable.

Build-time updates go into `pipelines[].nodes[].params`. Runtime updates call `/api/param/update`; backend updates `inspector_preset` and returns `pending_as_control`.

## File Input / Output

`builtin.file_input`:

- UI must select a valid local PCM WAV before RUN.
- Frontend parses WAV header and sends playback params in `/api/runtime/run`.
- PCM bytes go to `/api/runtime/audio/playback/stream` as binary.
- Metadata goes to `/api/runtime/audio/playback/frame`.
- Frontend schedules next frame based on backend `next_push_ms`.
- After file EOF, frontend calls `/api/runtime/audio/playback/eos`.

`builtin.file_output`:

- UI must choose a writable `.wav` output target before RUN.
- Frontend polls `/api/runtime/audio/capture/frame`.
- Returned base64 PCM is appended to the output WAV.
- Capture does not use backend queue.

`builtin.dai` FILE_IO_DAI:

- Playback sink can configure output format in Inspector.
- Capture source can choose a WAV; frontend reads WAV format and writes only declared DAI params (`sample_rate`, `channels`, `sample_bits`, `tdm_slots`, `slot_width`).

## Connection Rules

- output connects to input only.
- `external` connects to `external`; `sof` connects to `sof`.
- no self connection.
- one output and one input per manual connection.
- connecting a used output/input replaces the previous edge.
- products that need fanout should use explicit splitter/copier nodes.

## Dashboard

Frontend never fabricates DSP runtime data for these panels when backend is available:

- PER-ALGORITHM COST uses `/api/algorithm/cost/live`.
- DSP CORE LOADING uses `/api/dsp/core/loading`.
- SYSTEM HEALTH uses `/api/system/health/live`.

SYSTEM HEALTH shows heap rows expanded by category, block size and sub-map fields such as `free_count` and `total_count`.

## Tests

```bash
node tests/frontend/gui-runtime-contract.test.mjs
node tests/frontend/parameter-policy.test.mjs
node tests/frontend/pipeline-runtime-build-button.test.mjs
npm run profile:frontend
```
