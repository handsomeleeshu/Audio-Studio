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
| POST | `/api/pipeline/build` | 生成单条 pipeline JSON、upsert 回 workspace all JSON、通过 as_server `config.compile` 编译 tplg、生成 SOF test list 并触发验证 |
| POST | `/api/pipeline/unload` | v1 只清理已加载 pipeline 状态，真实 as_server 拆除后续接入 |
| POST | `/api/runtime/run` | 启动 runtime |
| POST | `/api/runtime/stop` | 停止 runtime |
| POST | `/api/param/update` | 动态参数下发 |
| POST | `/api/node/action` | 节点点击/控制扩展 |
| POST | `/api/project/save` | build 成功后把 workspace JSON 写回源项目 JSON |

## GUI Build Orchestration

`BuildOrchestrator` 是 GUI backend 和 `as_server`/simulator 验证之间的胶水层：

1. `GET /api/config?project=...` 将 `configs/platform/<project>` 拷贝到 `/tmp/audio-studio-gui-workspaces/.../${platform}_pipeline_all.json`，并把 all copy 返回前端。
2. `POST /api/pipeline/build` 接受 frontend 直接提交的裸 layout snapshot，也兼容 `{ "snapshot": ... }` 包装格式。Build 必须指向单个 working group；backend 从 snapshot 生成该 group 的单条 pipeline。
3. Backend 将单条 pipeline 写入 `${platform}_pipeline_<pipe_id>.json`，该文件根 `pipelines[]` 只有这一条，用作 `as_config` 输入；同时把同 `pipe_id`/同名 pipeline upsert 回 `${platform}_pipeline_all.json`，保留其他 pipeline。
4. 重生成时会剔除 `builtin.file_input` / `builtin.file_output` debug 节点以及 external/debug 连接；`as_config` 只读取根 `pipelines[]`，不读取 `audio_studio_gui.as_config_payload`。
5. 编译阶段先尝试连接常驻 `as_server` 的 `config.compile` JSON-RPC；如果 socket 不可用，则直接调用 `as_server --rpc-once <jsonrpc>`，避免 GUI build 依赖手工预启动一个编译 server。
6. 编译成功后生成 `audio_studio_test_list.txt`，内容包含 `ac_run --endpoint as_datalink --mtu 512`、`trace on`、`pipeinstall <tplg>`。
7. 默认验证 runner 调用 `application/rv32qemu/sof-build-test.py -t <test_list> --audio-controller-log`，该脚本负责启动 simulator 和验证用 as_server。
8. Build 成功后该 pipeline 状态为 `PIPE_LOADED`，再次 build 会被拒绝并要求先 `POST /api/pipeline/unload`；unload v1 仅清理 backend 状态。
9. `POST /api/project/save` 只有 dirty pipeline 均已成功 build 后才允许把 `${platform}_pipeline_all.json` 写回源 JSON。

默认环境变量：

```text
AUDIO_STUDIO_AS_SERVER_HOST=127.0.0.1
AUDIO_STUDIO_AS_SERVER_PORT=9900
AUDIO_STUDIO_AS_SERVER_TIMEOUT_MS=5000
AUDIO_STUDIO_AS_SERVER_PATH=<optional as_server for --rpc-once config.compile>
AUDIO_STUDIO_VALIDATION_AS_SERVER_HOST=127.0.0.1
AUDIO_STUDIO_VALIDATION_AS_SERVER_PORT=9901
AUDIO_STUDIO_VALIDATION_AS_SERVER_PATH=<optional simulator/rpc_socket as_server>
AUDIO_STUDIO_VALIDATION_AS_LOG_PATH=<optional simulator/rpc_socket as_log>
AUDIO_STUDIO_VALIDATION_TRACE_LDC=<optional rv32qemu sof.ldc>
```

`9901` 用于验证脚本启动的 as_server，避免和 `config.compile` 常驻服务抢默认 `9900`。
如果未设置上述 path 变量，backend 会从启动目录和父目录向上查找 `out/linux/a2/as_config/Debug/as_server`、`out/linux/simulator/rpc_socket/Debug/as_server`、`application/rv32qemu/sof-build-test.py` 和 `application/rv32qemu/build/sof.ldc` 等默认位置。

当前 simulator 原始 JSON 包含 playback、capture 和 DSP filter coverage 多条 pipeline。GUI build 应使用当前选中 node/working group 对应的 `group_id`，backend 只编译该 pipeline，成功后返回 `runtime_state:"PIPE_LOADED"` 和 `updated_pipeline`，前端用它同步内存中的 all config。

## Mock 到真实实现的过渡

`GuiRuntimeEngine` 是生产路径的 `IRuntimeEngine` 实现；`buildPipeline()` / `unloadPipeline()` 委托 `BuildOrchestrator` 完成 workspace JSON、`config.compile` RPC 和 simulator validation。`MockRuntimeEngine` 不再继承 `IRuntimeEngine`，只保留为 node/parameter 等非真实 runtime 控制的测试替身。

后续替换 runtime 能力时，优先在现有接口后面挂真实实现，保持 HTTP/RPC API 不变：

```cpp
class GuiRuntimeEngine final : public IRuntimeEngine {
public:
  std::string validatePipeline(const std::string& pipeline_json) override;
  std::string buildPipeline(const std::string& pipeline_json) override;
  std::string unloadPipeline(const std::string& pipeline_json) override;
  std::string run(const std::string& session_id) override;
  std::string stop(const std::string& session_id) override;
  std::string telemetry(const std::vector<std::string>& node_ids) override;
};
```

建议真实实现中继续拆分，并像 `BuildOrchestrator` 一样把外部依赖放在可替换 driver 后面：

```text
ProductConfigParser
PipelineGraphValidator
CoreScheduler
BufferAllocator
DspRuntimeSession
ProbeManager
TlvParamEncoder
ConfigCompileClient
ValidationRunner
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
