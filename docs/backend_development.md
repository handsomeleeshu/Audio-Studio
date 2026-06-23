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
| GET | `/api/config` | 打开产品 JSON，创建临时 workspace，并返回 workspace copy |
| GET | `/api/telemetry?nodes=A,B` | 返回 mock telemetry |
| POST | `/api/pipeline/validate` | Pipeline 校验 |
| POST | `/api/pipeline/build` | 更新 workspace JSON、通过 as_server `config.compile` 编译 tplg、生成 SOF test list 并触发验证 |
| POST | `/api/runtime/run` | 启动 runtime |
| POST | `/api/runtime/stop` | 停止 runtime |
| POST | `/api/param/update` | 动态参数下发 |
| POST | `/api/node/action` | 节点点击/控制扩展 |
| POST | `/api/project/save` | build 成功后把 workspace JSON 写回源项目 JSON |

## GUI Build Orchestration

`BuildOrchestrator` 是 GUI backend 和 `as_server`/simulator 验证之间的胶水层：

1. `GET /api/config?project=...` 将 `configs/platform/<project>` 拷贝到 `/tmp/audio-studio-gui-workspaces/.../project.json`，并把 copy 返回前端。
2. `POST /api/pipeline/build` 把 frontend snapshot upsert 到 `audio_studio_gui`，同步 pipeline rename 后的 `pipelines[].name`，再调用 as_server JSON-RPC `config.compile`。
3. 编译成功后生成 `audio_studio_test_list.txt`，内容包含 `ac_run --endpoint as_datalink --mtu 512`、`trace on`、`pipeinstall <tplg>`、`ac_run --stop`。
4. 默认验证 runner 调用 `application/rv32qemu/sof-build-test.py -t <test_list> --audio-controller-log`，该脚本负责启动 simulator 和验证用 as_server。
5. `POST /api/project/save` 只有 workspace 至少 build 成功一次后才允许写回源 JSON。

默认环境变量：

```text
AUDIO_STUDIO_AS_SERVER_HOST=127.0.0.1
AUDIO_STUDIO_AS_SERVER_PORT=9900
AUDIO_STUDIO_AS_SERVER_TIMEOUT_MS=5000
AUDIO_STUDIO_VALIDATION_AS_SERVER_HOST=127.0.0.1
AUDIO_STUDIO_VALIDATION_AS_SERVER_PORT=9901
```

`9901` 用于验证脚本启动的 as_server，避免和 `config.compile` 常驻服务抢默认 `9900`。

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
