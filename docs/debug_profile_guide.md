# Debug and Profile Guide

本文记录当前可用的 Audio Studio debug/profile 入口。完整 GUI + simulator 调试以 vass 根目录 `.vscode` 为准。

## Prerequisites

先构建必要目标：

```bash
cmake --build Audio-Studio/out/linux/simulator/rpc_socket/Debug --parallel 16
cmake --build Audio-Studio/out/linux/simulator/gui_backend/Debug --parallel 16
cmake --build Audio-Studio/out/linux/simulator/audio_controller/Debug --parallel 16
python3 application/rv32qemu/sof-build-test.py -b -d
```

## Full Stack Debug

根 `.vscode/launch.json` 提供 GUI1 组合调试：

- Audio Studio GUI1 frontend
- Audio Studio GUI backend
- as_server
- simulator keep alive
- QEMU gdbstub / RISC-V gdb

`Audio Studio GUI: Simulator Keep Alive` 通过 `application/rv32qemu/sof-build-test.py` 启动 QEMU，并传入：

```text
--gui-keep-alive
--gui-ready-after-pipeinstall
--qemu-gdb-port <port>
--qemu-gdb-wait
--gui-as-server-ready-marker <path>
--gui-test-list <path>
```

RISC-V gdb 由 `.vscode/riscv-gdb-wrapper.sh` 包装。验证目标是可以连接到 QEMU gdbstub，并在 `application/rv32qemu/main.c` 的 `main` 断下。

## Backend Debug

backend 通过 argv 注入依赖：

```bash
Audio-Studio/out/linux/simulator/gui_backend/Debug/audio_studio_gui_server Audio-Studio 18080 \
  --as-server Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_server \
  --alsatplg Audio-Studio/third_party/alsatplg/bin/alsatplg \
  --as-server-host 127.0.0.1 \
  --as-server-port 9900 \
  --helper-python python3 \
  --helper-script application/rv32qemu/sof-build-test.py \
  --as-log Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_log \
  --trace-ldc application/rv32qemu/build/sof.ldc \
  --audio-driver-factory simulator
```

不要再依赖 `AUDIO_STUDIO_*` 环境变量做路径发现。

## as_server / as_log Debug

手动启动 simulator as_server：

```bash
Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_server \
  --rpc --max-requests 0 \
  --log-driver-factory simulator \
  --log-trace-ldc application/rv32qemu/build/sof.ldc \
  --audio-driver-factory simulator \
  --audio-device pcm_playback \
  --datalink /tmp/audio-studio-debug/as_datalink \
  --port 9901
```

观察普通 log：

```bash
Audio-Studio/out/linux/simulator/rpc_socket/Debug/as_log --level debug --follow --port 9901
```

预期：普通 firmware log 可见，`ASINFO|` telemetry 不应出现在 as_log 输出中。

## GUI E2E Debug

一条命令覆盖 build/play/record/System Info/as_log/stall：

```bash
cd Audio-Studio
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts
```

失败时查看：

```text
/tmp/audio-studio-gui-e2e-artifacts/gui-backend.log
/tmp/audio-studio-gui-e2e-artifacts/input.wav
/tmp/audio-studio-gui-e2e-artifacts/capture-output.wav
```

## Frontend Profile

```bash
npm run profile:frontend
AUDIO_STUDIO_PROFILE_SCENARIO=running npm run profile:frontend
```

Frontend profile 仍用于渲染性能，和 simulator runtime 正确性分开看。

## Backend Profile

可继续使用已有 backend profile script；建议 profile 前先用 E2E 确认功能正确：

```bash
python3 tests/gui-simulator-audio-e2e.py --artifacts-dir /tmp/audio-studio-gui-e2e-artifacts --no-stall-check
```

## Troubleshooting

- Build 后只有一个 pipeline 变 Loaded：frontend snapshot 或 backend build scope 出错；Build 必须 all-pipeline。
- RUN 后按钮卡住：检查 `/api/runtime/audio/playback/eos` 或 `/api/runtime/stop` 是否返回 result。
- as_log timeout：检查 System Info pump 是否独占 log service；当前 LogService mirror session 和 intercepted-entry return contract 应避免该问题。
- File Input 无法 RUN：确认前端真的选择了合法 WAV，不允许 fallback/debug file。
- Capture 空文件：检查 FILE_IO_DAI input WAV 是否 stage，capture frame 是否持续返回 bytes。
- as_server debug 弹 `Parameter 'arch'`：不要用 GDB `customLaunchSetupCommands`
  手动 attach，也不要让外部 gdbserver attach helper 子进程；Ubuntu 默认
  ptrace/Yama 会拒绝这种跨父子关系 attach。GUI full-stack 会把
  `--as-server-gdbserver-port` 传给 helper，helper 直接用 gdbserver 启动
  唯一的 helper-owned as_server，VSCode 通过 `miDebuggerServerAddress`
  连接该 gdbserver 并自动 continue。
- QEMU gdb 不能断下：确认 `--qemu-gdb-wait` 已传入，gdb port 未被占用，`riscv-gdb-wrapper.sh` 能找到 riscv gdb。
