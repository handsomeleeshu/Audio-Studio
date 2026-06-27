# Test Plan

本文列出当前 Audio Studio 必须保持通过的测试面。新增功能优先补 contract，再接入 E2E。

## Frontend Logic

```bash
node tests/frontend/gui-runtime-contract.test.mjs
node tests/frontend/parameter-policy.test.mjs
node tests/frontend/pipeline-runtime-build-button.test.mjs
./scripts/run_tests.sh
```

覆盖：

- `pipelines[] + frontend_connections[]` layout 渲染。
- Build 全量 pipeline 语义。
- RUN/STOP 独立 pipeline 语义。
- File Input/Output 的前端状态机。
- `PIPE_UNLOADED/PIPE_LOADED/PIPE_RUNNING` 参数置灰。
- 运行中参数更新调用 `/api/param/update`。
- Dashboard 不在前端伪造 runtime 数据。

## Backend Contracts

```bash
python3 tests/system-info-runtime-contract.test.py
python3 tests/backend-process-lifecycle-contract.test.py
python3 tests/vscode-qemu-debug-contract.test.py
ctest --test-dir out/linux/simulator/gui_backend/Debug --output-on-failure
```

覆盖：

- System Info 驱动 PER-ALGORITHM COST、DSP CORE LOADING、SYSTEM HEALTH。
- heap rows 展开到 category/index/block size/free_count/total_count。
- queue/backpressure/blocked edge 语义。
- 100ms 无 buffer consumed 的 stall 判定。
- backend 子进程 SIGINT/SIGTERM、FD_CLOEXEC、process group cleanup。
- backend startup 从 argv/config 注入，不依赖环境变量。
- VSCode full-stack debug 配置字段和 QEMU gdbstub 参数。

## Server / CLI / Framework

```bash
ctest --test-dir out/linux/simulator/driver_interface_tests/Debug --output-on-failure
ctest --test-dir out/linux/simulator/rpc_socket/Debug --output-on-failure
```

覆盖：

- `config.compile` strict compile。
- product JSON -> tplg name shortening and decode validation。
- log session / mirror session。
- `ASINFO|` 被 System Info 拦截，不进入 as_log。
- System Info heartbeat、module、buffer、heap parsing。
- as_play/as_record/as_log CLI common behavior。

## Audio Controller

```bash
ctest --test-dir out/linux/simulator/audio_controller/Debug --output-on-failure
```

覆盖：

- slot 初始化传入 ops。
- HOST PTABLE 两个 4K page。
- 非 HOST PTABLE 保留 period/period_bytes sizing。
- playback EOS tail drain/flush。
- transport read/write framing。

## GUI Simulator E2E

```bash
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts --no-stall-check
```

覆盖：

- 使用 `configs/platform/simulator/simulator.json`。
- frontend snapshot -> backend workspace -> as_server config.compile。
- all pipeline build/load。
- System Info connected and dashboard APIs。
- as_log 可以读普通 decoded log 且不泄漏 ASINFO。
- playback pipeline 连续 RUN 两次。
- File Input -> GUI backend queue -> as_server -> audio_controller -> SOF FILE_IO_DAI output WAV。
- QEMU SIGSTOP 触发 stall，恢复后继续。
- capture pipeline File_IO_DAI input WAV -> as_server capture -> GUI File Output WAV。
- unload cleanup。

## VASS / SOF Side

从 vass 根目录运行：

```bash
python3 Misc/sof_test/tests/test_audio_studio_info_contract.py
python3 Misc/sof_test/tests/test_splay_host_buffer_contract.py
python3 Misc/sof_test/tests/test_rv32qemu_file_io_capacity_contract.py
python3 application/rv32qemu/sof-build-test.py -t Misc/sof_test/simple_test/tplg-splay-test-lists.txt
python3 application/rv32qemu/sof-build-test.py -t Misc/sof_test/simple_test/rv32qemu-as-audio-controller-test-lists.txt --audio-controller-log --as-server Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_server --as-log Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_log --trace-ldc application/rv32qemu/build/sof.ldc
python3 application/rv32qemu/sof-build-test.py -t Misc/sof_test/simple_test/rv32qemu-as-log-test-lists.txt --audio-controller-log --as-server Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_server --as-log Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_log --trace-ldc application/rv32qemu/build/sof.ldc
```

覆盖：

- SOF `audio_studio` ASINFO trace contract。
- splay HOST page sizing and tail drain。
- rv32qemu FILE_IO DMA channel capacity for multi-pipeline simulator JSON。
- baseline tplg pipeinstall + splay。
- audio_controller log path。
- long-running as_log trace path。

## Build System

```bash
python3 tests/build-system.test.py
```

覆盖：

- Kconfig/CMake profile wiring。
- driver registry boundaries。
- rpc_socket/rpc_pipe build profiles。
- Windows/MinGW dry-run where available。

## Future Additions

- Browser Playwright coverage for real file picker events when CI can expose browser file dialogs.
- Long-run telemetry soak with repeated play/record cycles.
- as_control real parameter apply once SOF control path is ready.
