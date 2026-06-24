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
| GET | `/api/telemetry?nodes=A,B` | 返回 runtime telemetry；PER-ALGORITHM COST / DSP CORE LOADING / SYSTEM HEALTH 优先来自 as_server `systemInfo.*` |
| POST | `/api/pipeline/validate` | Pipeline 校验 |
| POST | `/api/pipeline/build` | 用完整 layout snapshot 重建 workspace all JSON，通过 as_server `config.compile` 编译 tplg，生成 SOF test list 并启动 keep-alive 验证 session |
| POST | `/api/pipeline/unload` | 停止当前 keep-alive validation session，并清理全局 loaded 状态 |
| POST | `/api/runtime/run` | 启动 runtime；可携带 File Input 的 WAV playback 描述 |
| POST | `/api/runtime/audio/frame` | 前端推送一帧 PCM data，backend 入队后立即返回 queue/backpressure 状态 |
| POST | `/api/runtime/stop` | 停止 runtime |
| POST | `/api/param/update` | 动态参数下发 |
| POST | `/api/node/action` | 节点点击/控制扩展 |
| POST | `/api/project/save` | build 成功后把 workspace JSON 写回源项目 JSON |

## GUI Build Orchestration

`BuildOrchestrator` 是 GUI backend 和 `as_server`/simulator 验证之间的胶水层：

1. `GET /api/config?project=...` 将 `configs/platform/<project>` 拷贝到 `/tmp/audio-studio-gui-workspaces/.../${platform}_pipeline_all.json`，并把 all copy 返回前端。
2. `POST /api/pipeline/build` 接受 frontend 直接提交的裸 layout snapshot，也兼容 `{ "snapshot": ... }` 包装格式。Build 永远面向全量 layout，不依赖当前 UI 选中的单个 pipeline。
3. Backend 根据 snapshot 的全部 working groups 重建 `${platform}_pipeline_all.json` 的根 `module_instances[]` 和 `pipelines[]`，该 all JSON 是唯一 `as_config` 输入。
4. 重生成时会剔除 `builtin.file_input` / `builtin.file_output` debug 节点以及 external/debug 连接；HOST/DAI 节点必须引用已有 module instance，普通新增算法节点会生成稳定 `module_instances[]` entry。
5. 编译阶段先尝试连接常驻 `as_server` 的 `config.compile` JSON-RPC；如果 socket 不可用，则直接调用 `as_server --rpc-once <jsonrpc>`，避免 GUI build 依赖手工预启动一个编译 server。
6. 编译成功后生成 `audio_studio_test_list.txt`，内容包含 `ac_run --endpoint as_datalink --mtu 512`、`trace on`、`pipeinstall <tplg>`。
7. 默认验证 runner 调用 `application/rv32qemu/sof-build-test.py -t <test_list> --audio-controller-log --gui-keep-alive --gui-ready-marker <file>`，该脚本负责启动 simulator 和验证用 as_server。
8. `sof-test-run.py` 看到 test list 成功结束后写 ready marker，并保持 qemu/as_server 运行；backend 看到 marker 后返回 `runtime_state:"PIPE_LOADED"`。
9. 每次 build 开始前会停止旧 validation session；build 失败或 `POST /api/pipeline/unload` 会对 session 进程组发送 Ctrl+C 等价清理。
10. `POST /api/project/save` 只有最近一次全局 build 成功后才允许把 `${platform}_pipeline_all.json` 写回源 JSON。

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
AUDIO_STUDIO_VALIDATION_READY_TIMEOUT_MS=120000
```

`9901` 用于验证脚本启动的 as_server，避免和 `config.compile` 常驻服务抢默认 `9900`。
如果未设置上述 path 变量，backend 会从启动目录和父目录向上查找 `out/linux/a2/as_config/Debug/as_server`、`out/linux/simulator/rpc_socket/Debug/as_server`、`application/rv32qemu/sof-build-test.py` 和 `application/rv32qemu/build/sof.ldc` 等默认位置。

当前 simulator 原始 JSON 包含 playback、capture 和 DSP filter coverage 多条 pipeline。GUI build 即使在单 pipeline 视图中触发，也会提交和编译全部 working groups；任一子 pipeline workspace/compile/validation 失败都会让全局 build 失败。成功响应返回 `runtime_state:"PIPE_LOADED"`、`updated_pipelines` 和 `updated_module_instances`，前端用这些字段同步内存中的 all config。

Build 成功后 frontend 锁定结构编辑：不能新增/删除组件、连接、端口方向、pipeline rename 或 build-affecting 参数；仍允许拖动节点位置、Auto Arrange、选择、缩放和 Inspector 查看。`Unload` 后解除结构锁。

## Mock 到真实实现的过渡

`GuiRuntimeEngine` 是生产路径的 `IRuntimeEngine` 实现；`buildPipeline()` / `unloadPipeline()` 委托 `BuildOrchestrator` 完成 workspace JSON、`config.compile` RPC 和 simulator validation。`MockRuntimeEngine` 不再继承 `IRuntimeEngine`，只保留为 node/parameter 等非真实 runtime 控制的测试替身。

RUN/playback 采用双阶段数据路径：

1. `/api/runtime/run` 接收 frontend 的 `playback` 描述，包括 `sample_rate`、`channels`、`bits_per_sample`、`frame_bytes`、`data_offset`、`data_bytes` 和 `edge_key`。
2. `GuiRuntimeEngine` 创建 as_server playback session，并启动独立 worker thread。
3. frontend 通过 `/api/runtime/audio/frame?...` 发送 `application/octet-stream` PCM frame；backend 只入队并立即返回 `{accepted, queued_bytes, next_push_ms, stalled, blocked_edge_key}`。
4. worker thread 从 queue 中取 frame，通过 `AudioRpcClient` 写入 as_server；queue 高水位、RPC 未就绪或 100ms 没有 dequeue 时返回 `stalled:true`，frontend 按 `blocked_edge_key` 标红闪烁对应连接。

System info 采用 as_server 侧 `server/framework/system_info`。`LogService` 拦截 SOF trace 中以 `ASINFO|` 开头的 decoded entry，更新 `SystemInfoService` snapshot，并从普通 `as_log` 输出中隐藏这些内部 telemetry 行。GUI/backend controller 通过 `systemInfo.snapshot`、`systemInfo.components`、`systemInfo.buffers`、`systemInfo.health` 读取真实状态；1s 没有 heartbeat 时返回 disconnected 和清空后的 runtime 值。

后续替换 runtime 能力时，优先在现有接口后面挂真实实现，保持 HTTP/RPC API 不变：

```cpp
class GuiRuntimeEngine final : public IRuntimeEngine {
public:
  std::string validatePipeline(const std::string& pipeline_json) override;
  std::string buildPipeline(const std::string& pipeline_json) override;
  std::string unloadPipeline(const std::string& pipeline_json) override;
  std::string run(const std::string& session_id) override;
  std::string stop(const std::string& session_id) override;
  std::string pushAudioFrame(const std::string& query, const std::string& frame) override;
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
