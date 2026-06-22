# Backend Development Guide

## 构建

```bash
./scripts/build_all.sh --profile gui_backend -r linux a2
```

## 运行

```bash
./out/linux/a2/gui_backend/Release/audio_studio_server . 8080
```

## API

| Method | Path | 说明 |
|---|---|---|
| GET | `/` | 前端页面 |
| GET | `/api/config` | 返回产品 JSON |
| GET | `/api/telemetry?nodes=A,B` | 返回 mock telemetry |
| POST | `/api/pipeline/validate` | Pipeline 校验 |
| POST | `/api/pipeline/build` | Build / session 创建 |
| POST | `/api/runtime/run` | 启动 runtime |
| POST | `/api/runtime/stop` | 停止 runtime |
| POST | `/api/param/update` | 动态参数下发 |
| POST | `/api/node/action` | 节点点击/控制扩展 |

## 扩展真实 DSP runtime

替换 `MockRuntimeEngine`：

```cpp
class VassRuntimeEngine final : public IRuntimeEngine {
public:
  std::string validatePipeline(const std::string& pipeline_json) override;
  std::string buildPipeline(const std::string& pipeline_json) override;
  std::string run(const std::string& session_id) override;
  std::string stop(const std::string& session_id) override;
  std::string telemetry(const std::vector<std::string>& node_ids) override;
};
```

建议真实实现中继续拆分：

```text
ProductConfigParser
PipelineGraphValidator
CoreScheduler
BufferAllocator
DspRuntimeSession
ProbeManager
TlvParamEncoder
```

## 参数下发建议

`/api/param/update` 收到：

```json
{
  "session_id": "sess_1234",
  "node_id": "VOLUME",
  "param_id": "volume_db",
  "value": -6,
  "apply": { "mode": "next_frame" }
}
```

后续真实实现可转换为：

```text
node_id + param_id -> kcontrol/TLV id -> encoded payload -> runtime command queue
```
