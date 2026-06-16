# Test Plan

## 已包含测试

### Frontend Logic Tests

```bash
./scripts/run_tests.sh
```

覆盖：

- 产品 JSON module registry 构建。
- Pipeline 预加载。
- 多 input/output 端口解析。
- Auto Arrange 最小距离。
- 静态/动态参数分类。
- 新版统一 `parameters` 模型根据 `apply.settable_states` 映射为 static/runtime 默认值。
- 参数类型和 UI 策略基础校验。

前端逻辑测试运行在当前 host Node 环境，测试入口和被直接 import 的 `GUI/frontend/assets/js` 逻辑模块保持 Node 12 可解析语法，避免 `node:` builtin import、optional chaining 和 nullish coalescing 导致 `run_tests.sh` 在旧 Node 上失败。

### Backend Tests

```bash
ctest --test-dir build --output-on-failure
```

覆盖：

- validate/build/run/stop/telemetry mock runtime。

### Build System Tests

```bash
python3 tests/build-system.test.py
```

覆盖：

- `scripts/build_all.sh` shell 入口用 OS 选择 toolchain，用 platform 选择 defconfig。
- SOF-style `scripts/kconfig` 生成 `generated/.config` 与 `generated/include/autoconfig.h`。
- Root `Kconfig` 通过 `source` 延伸到 `GUI/`、`server/`、`drivers/<module>/`、`rpc/`、`server/platform/`、`cli/`、`tests/` 等子目录。
- Driver CMake 和 Kconfig 一样以模块目录为入口，例如 `drivers/audio/CMakeLists.txt` 与 `drivers/audio/Kconfig`；`src/` 目录只放实现源码。
- Kconfig non-interactive targets：`olddefconfig`、`savedefconfig`、`alldefconfig`、`overrideconfig`、`*_defconfig`、`genconfig`。
- `menuconfig` target 可由 CMake 暴露，自动测试只验证 target 存在，不打开 curses 交互界面。
- Linux/GCC host `as_server` 最小程序 CMake configure/build。
- `as_server --version` 与 `as_server --health` 输出 tool OS 和 target platform。
- `--profile gui_backend -r` 构建 GUI/backend mock server，确认 `audio_studio_server` 与 `audio_studio_backend_tests` 产物存在，并运行 backend CTest。
- `--profile driver_interface_tests` 构建全部 Linux host driver 测试实现，并运行 `driver_interface_tests` CTest。
- `driver_interface_tests` 只 include `driver_manager.hpp`，通过 `DriverManager`、各 driver singleton Registry 和 public `I*` interface 覆盖 OS/socket/filesystem/pipe/dynlib/transport/audio/control/log/dump；不直接 include 或实例化 `linux_host_*` 实现类。
- `--profile rpc_socket` 构建同一 platform 下的 `as_server` 与 CLI，启动 `as_server --rpc socket`，通过 `as_control --rpc socket` 调用 `server.health`。
- `--profile rpc_pipe` 构建同一 platform 下的 `as_server` 与 CLI，启动 `as_server --rpc pipe`，通过 `as_control --rpc pipe` 调用 `server.health`。
- `rpc_socket` 和 `rpc_pipe` build dir 都运行 CTest，覆盖对应 profile 下的 CLI common 单元测试。
- Windows/MinGW 平台映射 dry-run；如果当前 host 安装了 `x86_64-w64-mingw32-g++`，同时编译 `out/windows/a2/as_server_minimal/Debug/as_server.exe`。

### Server Host-Alone Tests

```bash
out/linux/a2/as_server_minimal/Debug/as_server --version
out/linux/a2/as_server_minimal/Debug/as_server --health
```

覆盖：

- 当前阶段验证独立 `server/as_server/main.cpp` 可按 OS/toolchain 与 Kconfig target platform 编译。
- driver 层通过 `server/tests/driver_interface_tests.cpp` 覆盖 `DriverManager` 初始化、driver metadata、各模块 Registry，以及每个 `I*` interface 的 Linux host 测试实现。

### CLI Host-Alone Tests

```bash
out/linux/a2/as_server_minimal/Debug/as_control --target dummy --action get-health
out/linux/a2/as_server_minimal/Debug/as_play --target dummy --file demo.wav

out/linux/a2/rpc_socket/Debug/as_server --rpc socket 127.0.0.1 18765 --max-requests 1
out/linux/a2/rpc_socket/Debug/as_control --rpc socket --host 127.0.0.1 --port 18765 --action get-health

out/linux/a2/rpc_pipe/Debug/as_server --rpc pipe /tmp/audio-studio.req /tmp/audio-studio.rsp --max-requests 1
out/linux/a2/rpc_pipe/Debug/as_control --rpc pipe --request-pipe /tmp/audio-studio.req --response-pipe /tmp/audio-studio.rsp --action get-health
```

覆盖：

- dummy mode 验证 CLI 参数解析和本地输出不依赖 server。
- RPC mode 验证 CLI 通过 socket/pipe transport 与 as_server 进行 JSON-RPC 通信。

## 建议后续增加

- 浏览器 E2E：Playwright 检查拖拽、连线、参数修改。
- JSON Schema 校验：module_types/pipelines/presets/module_instances。
- DSP runtime contract test：验证 TLV 编码和算法参数下发。
- Telemetry 压测：多节点、多 session、长时间运行。
