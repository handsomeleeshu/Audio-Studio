# Audio Studio

Audio Studio 是面向 VASS/SOF simulator 的 Web 工具链。当前主线已经不再是纯 mock demo：前端通过 C++ GUI backend 调用 `as_server` JSON-RPC，backend 再驱动 `audio_controller`、rv32qemu simulator 和 SOF FILE_IO_DAI，完成 topology build、playback、record 和运行态监控。

前端仍是 standalone HTML/CSS/JS，不需要 npm build。C++ backend 直接托管 `GUI/frontend/index.html` 和 `/api/*`。

## 1. 工程目标

- 从产品 JSON 加载 module catalog、pipeline、节点、端口、参数和 GUI-only frontend connections。
- 在 Web UI 中编辑 pipeline layout，并把 build 前参数写回 workspace JSON。
- Build 时把 layout 里所有 working pipelines 一次性编译成 tplg，并在 simulator 中 pipeinstall。
- RUN 时按当前选中 pipeline 独立启动 playback 或 capture。
- File Input 通过独立 binary stream URL 推 PCM，metadata API 返回 queue/backpressure。
- File Output 通过 capture frame API 定时拉 PCM，不在 backend 建 capture queue。
- 用 System Info 替代 runtime mock，驱动 PER-ALGORITHM COST、DSP CORE LOADING、SYSTEM HEALTH。
- `as_log` 保持普通 firmware log 可读，内部 `ASINFO|` telemetry 被 log service 拦截，不出现在 as_log。

## 2. 当前能力

### 2.1 Product JSON

当前主要测试源是：

```text
configs/platform/simulator/simulator.json
```

JSON 顶层结构：

```text
meta
imports
resource_catalog
module_types
pipelines
frontend_connections
presets
```

设计原则：

- `pipelines[]` 是 SOF/tplg 编译输入。
- `frontend_connections[]` 与 `pipelines[]` 并列，描述 `builtin.file_input/output` 和 HOST external port 的 GUI/runtime 连接。
- `module_instances` 已取消；node 自己携带 `module_type` 和 `params`。
- node `params` 只能使用 module parameter schema 中声明过的参数。
- HOST/DAI 的 binding 信息放入各自 params，不额外拆 endpoint section。
- `audio_studio_gui` 是临时 workspace section，save 时会 strip。

### 2.2 Pipeline Layout

Pipeline layout 支持：

- 加载 `pipelines[] + frontend_connections[]`。
- 添加、删除、移动、连接、断开节点。
- 一对一手动连接约束。
- 框选、多选、Copy/Cut/Paste。
- Undo/Redo、Auto Arrange、Save/Export。
- Build 成功后结构锁定，Unload 后解除。

Build 是全量语义：只要 layout 里有多个 working pipeline，点击 Build 就一起 build/load；前端选中一个 pipeline 只影响 RUN/STOP，不影响 Build 范围。

### 2.3 Inspector 与参数状态

参数 settable state 只有三种：

```text
PIPE_UNLOADED
PIPE_LOADED
PIPE_RUNNING
```

前端根据 `apply.settable_states` 自动置灰。Build 前可改参数写入 `pipelines[].nodes[].params`；RUN 中可改参数调用 `/api/param/update`，backend 更新 `inspector_preset`，并保留后续接入 as_control 的接口。

### 2.4 File Input / Output

- `builtin.file_input` 和 `builtin.file_output` 是前端 runtime 节点，只保存于 `frontend_connections[]`。
- File Input 必须选择合法 WAV 后才能 RUN playback。
- File Output 必须选择可写 `.wav` 输出路径后才能 RUN capture。
- 前端写一帧 PCM 到 `/api/runtime/audio/playback/stream`，随后用 `/api/runtime/audio/playback/frame` 上报 frame 元信息。
- backend 立即把 PCM 入队并返回 `accepted`、`queued_audio_ms`、`next_push_ms`、`stalled`、`blocked_edge_key`。
- 前端文件读完后发 `/api/runtime/audio/playback/eos`；backend 等 queue drain 和 playback tail flush 后关闭 session。
- Capture 不建 backend queue，前端定时 GET `/api/runtime/audio/capture/frame`，收到 PCM 后写入本地 WAV。

### 2.5 FILE_IO_DAI

`builtin.dai` + params 表示 FILE_IO_DAI：

```json
{
  "module_type": "builtin.dai",
  "params": {
    "dai_type": "file_io_dai",
    "dai_index": 0,
    "link_name": "FILE_IO_PLAYBACK_DAI0",
    "device_id": "FILEIO0",
    "direction": "playback",
    "sample_rate": 48000,
    "channels": 2,
    "sample_bits": 16,
    "tdm_slots": 2,
    "slot_width": 16
  }
}
```

Capture pipeline 中 FILE_IO_DAI 作为输入源时，前端选择 WAV 后会解析并填入 DAI 已声明的格式参数。Playback pipeline 中 FILE_IO_DAI 作为输出 sink 时，UI 可配置输出格式。

### 2.6 Runtime Dashboard

Dashboard 数据来自 backend live APIs：

- `PER-ALGORITHM COST`：`/api/algorithm/cost/live`
- `DSP CORE LOADING`：`/api/dsp/core/loading`
- `SYSTEM HEALTH`：`/api/system/health/live`

这些 API 优先调用 as_server `systemInfo.*`。SOF 端 `audio_studio` task 每 100ms 输出 `ASINFO|` trace；as_server `SystemInfoService` 解析后维护 snapshot。1s 没收到 heartbeat 时状态断连，前端不继续显示历史值。

SYSTEM HEALTH 会展开 heap 信息，包括 memory category、block size、free_count、total_count、used/free bytes 等细项。

## 3. 工程结构

```text
Audio-Studio/
├── GUI/frontend/                 # standalone UI
├── GUI/backend/                  # local HTTP server and runtime orchestration
├── server/framework/             # config/audio/log/system_info/transport services
├── server/platform/simulator/    # simulator drivers
├── audio_controller/             # C control/data endpoint
├── configs/                      # product JSON and module catalogs
├── plugins/                      # third-party module extension point
├── cli/common/                   # as_play/as_record/as_log shared code
└── tests/                        # contracts and E2E
```

相关 VASS 路径：

```text
../sof/src/audio_studio/
../sof/src/drivers/file_io/
../application/rv32qemu/sof-build-test.py
../Misc/sof_test/
```

## 4. 快速启动

构建 simulator 相关目标：

```bash
cmake --build out/linux/simulator/rpc_socket/Debug --parallel 16
cmake --build out/linux/simulator/gui_backend/Debug --parallel 16
cmake --build out/linux/simulator/audio_controller/Debug --parallel 16
```

启动 GUI backend：

```bash
out/linux/simulator/gui_backend/Debug/audio_studio_gui_server . 8080 \
  --as-server out/linux/simulator/rpc_socket/Debug/as_server \
  --alsatplg third_party/alsatplg/bin/alsatplg \
  --as-server-rpc-mode socket \
  --as-server-host 127.0.0.1 \
  --as-server-port 9900 \
  --validation-python python3 \
  --validation-script ../application/rv32qemu/sof-build-test.py \
  --validation-as-log out/linux/simulator/rpc_socket/Debug/as_log \
  --validation-trace-ldc ../application/rv32qemu/build/sof.ldc \
  --runtime-as-server-host 127.0.0.1 \
  --runtime-as-server-port 9900 \
  --audio-driver-factory simulator
```

GUI Build 会通过 `sof-build-test.py` 创建唯一的 `as_server`/QEMU/audio_controller session；Build 成功后该 session 保持存活，供后续 Run/Record 和 `as_log` 复用。

打开：

```text
http://127.0.0.1:8080
```

推荐项目：

```text
simulator/simulator.json
```

## 5. 主要 API

| Method | Path | 说明 |
|---|---|---|
| GET | `/api/projects` | 列出 platform JSON |
| GET | `/api/config?project=...` | 创建 workspace copy |
| POST | `/api/pipeline/build` | 全 layout build/load |
| POST | `/api/pipeline/unload` | 停止 simulator keep-alive |
| POST | `/api/runtime/run` | 启动 selected pipeline |
| POST | `/api/runtime/audio/playback/stream` | Playback PCM binary stream |
| POST | `/api/runtime/audio/playback/frame` | Playback frame metadata/backpressure |
| POST | `/api/runtime/audio/playback/eos` | Playback EOS/drain/close |
| GET | `/api/runtime/audio/capture/frame` | Capture PCM frame |
| POST | `/api/runtime/stop` | 停止 selected pipeline |
| POST | `/api/param/update` | RUN 中参数更新 |
| GET | `/api/algorithm/cost/live` | PER-ALGORITHM COST |
| GET | `/api/dsp/core/loading` | DSP CORE LOADING |
| GET | `/api/system/health/live` | SYSTEM HEALTH |
| POST | `/api/project/save` | 保存 JSON，strip `audio_studio_gui` |

## 6. 测试

常用 contract：

```bash
python3 tests/system-info-runtime-contract.test.py
python3 tests/backend-process-lifecycle-contract.test.py
python3 tests/vscode-qemu-debug-contract.test.py
node tests/frontend/gui-runtime-contract.test.mjs
node tests/frontend/parameter-policy.test.mjs
node tests/frontend/pipeline-runtime-build-button.test.mjs
```

C++ tests：

```bash
ctest --test-dir out/linux/simulator/gui_backend/Debug --output-on-failure
ctest --test-dir out/linux/simulator/audio_controller/Debug --output-on-failure
ctest --test-dir out/linux/simulator/driver_interface_tests/Debug --output-on-failure
ctest --test-dir out/linux/simulator/rpc_socket/Debug --output-on-failure
```

GUI/simulator E2E：

```bash
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts
```

VASS/SOF side contracts：

```bash
python3 ../Misc/sof_test/tests/test_audio_studio_info_contract.py
python3 ../Misc/sof_test/tests/test_splay_host_buffer_contract.py
python3 ../Misc/sof_test/tests/test_rv32qemu_file_io_capacity_contract.py
python3 ../application/rv32qemu/sof-build-test.py -t ../Misc/sof_test/simple_test/tplg-splay-test-lists.txt
```

## 7. VSCode Debug

根 `.vscode` 提供 full-stack debug group，可同时起：

- GUI1 frontend
- GUI backend
- as_server
- rv32qemu simulator keep-alive
- QEMU gdbstub / RISC-V gdb

`Audio Studio GUI: Simulator Keep Alive` 已改成可 debug QEMU 的方式：backend 把 `--validation-qemu-gdb-port` 和 `--validation-qemu-gdb-wait` 传给 `sof-build-test.py`，`.vscode/riscv-gdb-wrapper.sh` 负责连接 gdbstub。

## 8. 扩展原则

- 新三方算法走 `plugins/` 或 `module_types[]`，不要恢复 `module_instances`。
- 新 frontend-only 节点写入 `frontend_connections[]`。
- 新 runtime 能力优先复用 as_server RPC，GUI backend 只做 HTTP/stream 适配。
- 新平台能力放进 driver/framework/platform 层。
- 新 ASINFO 字段必须同步 SOF producer、SystemInfo parser、backend API、前端显示和测试。
