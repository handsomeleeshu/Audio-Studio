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
- Kconfig non-interactive targets：`olddefconfig`、`savedefconfig`、`alldefconfig`、`overrideconfig`、`*_defconfig`、`genconfig`。
- `menuconfig` target 可由 CMake 暴露，自动测试只验证 target 存在，不打开 curses 交互界面。
- Linux/GCC host `as_server` 最小程序 CMake configure/build。
- `as_server --version` 与 `as_server --health` 输出 tool OS 和 target platform。
- Windows/MinGW 平台映射 dry-run；如果当前 host 安装了 `x86_64-w64-mingw32-g++`，同时编译 `out/windows/a2/Debug/as_server.exe`。

### Server Host-Alone Tests

```bash
out/linux/a2/Debug/as_server --version
out/linux/a2/Debug/as_server --health
```

覆盖：

- 当前阶段只验证独立 `server/as_server/main.cpp` 可按 OS/toolchain 与 Kconfig target platform 编译。
- framework、driver interface、driver implementation、platform backend 将按设计文档返工后再恢复单元测试。

### CLI Host-Alone Tests

```bash
out/linux/a2/Debug/as_control --target dummy --action get-health
out/linux/a2/Debug/as_play --target dummy --file demo.wav
```

覆盖：

- 待 framework/driver interface 返工完成后恢复。

## 建议后续增加

- 浏览器 E2E：Playwright 检查拖拽、连线、参数修改。
- JSON Schema 校验：module_types/pipelines/presets/module_instances。
- DSP runtime contract test：验证 TLV 编码和算法参数下发。
- Telemetry 压测：多节点、多 session、长时间运行。
