# Test Plan

## 已包含测试

### Frontend Logic Tests

```bash
node tests/frontend/config-parser.test.mjs
node tests/frontend/layout.test.mjs
node tests/frontend/parameter-policy.test.mjs
```

覆盖：

- 产品 JSON module registry 构建。
- Pipeline 预加载。
- 多 input/output 端口解析。
- Auto Arrange 最小距离。
- 静态/动态参数分类。
- 参数类型和 UI 策略基础校验。

### Backend Tests

```bash
ctest --test-dir build --output-on-failure
```

覆盖：

- validate/build/run/stop/telemetry mock runtime。

## 建议后续增加

- 浏览器 E2E：Playwright 检查拖拽、连线、参数修改。
- JSON Schema 校验：module_types/pipelines/presets/module_instances。
- DSP runtime contract test：验证 TLV 编码和算法参数下发。
- Telemetry 压测：多节点、多 session、长时间运行。
