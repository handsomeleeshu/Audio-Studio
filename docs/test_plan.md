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

- `scripts/build_all` 解析 defconfig。
- 生成 `.config`、`autoconf.h`、`autoconf.hpp`、`config.cmake`、`config.json`。
- Linux/GCC host-alone CMake configure/build。
- `CONFIG_SERVER=y` 时构建 `as_server`、framework common、dummy driver，并运行 server CTest。

### Server Host-Alone Tests

```bash
ctest --test-dir out/test-build-system/linux-gcc/Debug --output-on-failure
out/test-build-system/linux-gcc/Debug/as_server --self-test
```

覆盖：

- `Status` 和 `ServiceRegistry` common 模块。
- `SessionRegistry` create/close/list/activeCount。
- `ControlService` set/get/list。
- `AudioService` stream create/start/stop/get/list。
- `LogService` append/tail/clear/size。
- `DumpService` start/write/stop/get/list。
- `PluginManager` register/unregister/get/list/findByCapability 和 active 状态。
- `FrameCodec` encode/decode 和 `TransportManager` logical channel 统计。
- `DriverManager` register/unregister/get/listByCategory 和 active 状态。
- JSON-RPC request parse/result/error response helper。
- dummy driver open/start/command/stop/telemetry。
- `as_server` host-alone dummy self-test。

### CLI Host-Alone Tests

```bash
out/test-build-system/linux-gcc/Debug/as_control --target dummy --action get-health
out/test-build-system/linux-gcc/Debug/as_play --target dummy --file demo.wav
```

覆盖：

- `cli/common` 参数解析和 JSON 输出。
- `as_control/as_play/as_record/as_log/as_dump` 目标构建。
- `as_control`、`as_play` dummy target 执行路径。

## 建议后续增加

- 浏览器 E2E：Playwright 检查拖拽、连线、参数修改。
- JSON Schema 校验：module_types/pipelines/presets/module_instances。
- DSP runtime contract test：验证 TLV 编码和算法参数下发。
- Telemetry 压测：多节点、多 session、长时间运行。
