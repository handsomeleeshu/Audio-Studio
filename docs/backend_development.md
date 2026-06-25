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
| POST | `/api/runtime/audio/playback/stream` | File Input playback PCM 数据面；前端推送一帧 bytes，backend 入队后立即返回 |
| POST | `/api/runtime/audio/playback/frame` | File Input playback 控制面；前端报告 frame 元信息，backend 返回 queue/backpressure 状态 |
| POST | `/api/runtime/audio/playback/eos` | File Input playback EOF/EOS，backend drain queue 后关闭 playback session |
| GET | `/api/runtime/audio/capture/frame` | File Output capture 从 backend 拉取一帧 PCM 数据 |
| POST | `/api/runtime/stop` | 停止 runtime |
| POST | `/api/param/update` | 动态参数下发 |
| POST | `/api/node/action` | 节点点击/控制扩展 |
| POST | `/api/project/save` | build 成功后把 workspace JSON 写回源项目 JSON |

## GUI Build Orchestration

`BuildOrchestrator` 是 GUI backend 和 `as_server`/simulator 验证之间的胶水层：

1. `GET /api/config?project=...` 将 `configs/platform/<project>` 拷贝到 `/tmp/audio-studio-gui-workspaces/.../${platform}_pipeline_all.json`，并把 all copy 返回前端。
2. `POST /api/pipeline/build` 接受 frontend 直接提交的裸 layout snapshot，也兼容 `{ "snapshot": ... }` 包装格式。Build 按 snapshot 中的 working groups 编译；单 pipeline 视图提交单个 working group 时就是 scoped build。
3. Backend 根据 snapshot 的 working groups 重建 `${platform}_pipeline_all.json` 的根 `pipelines[]` 和 `frontend_connections[]`。SOF 节点直接写入 `{ node_id, name, module_type, params }`；File Input/Output 节点写入 `frontend_connections[]`。
4. 顶层 `frontend_connections[]` 是 GUI layout/runtime metadata，结构与 `pipelines[]` 一样使用 `nodes[]` 和 `edges[]`，描述 `builtin.file_input` / `builtin.file_output` 与 HOST external port 的连接关系。`as_config` 编译时忽略这个 section。
5. 重生成时会把 external/debug 连接放入 `frontend_connections[]`，SOF 内部连接保留在 `pipelines[]`。HOST、DAI 和算法节点都不再引用独立 module instance。
6. 编译阶段先尝试连接常驻 `as_server` 的 `config.compile` JSON-RPC；如果 socket 不可用，则直接调用 `as_server --rpc-once <jsonrpc>`，避免 GUI build 依赖手工预启动一个编译 server。
7. 编译成功后生成 `audio_studio_test_list.txt`，内容包含 `ac_run --endpoint as_datalink --mtu 512`、`trace on`、`pipeinstall <tplg>`。
8. 默认验证 runner 调用 `application/rv32qemu/sof-build-test.py -t <test_list> --audio-controller-log --gui-keep-alive --gui-ready-marker <file>`，该脚本负责启动 simulator 和验证用 as_server。
9. `sof-test-run.py` 看到 test list 的 `ac_run`、`trace on` 和 `pipeinstall <tplg>` 成功后写 ready marker，并保持 qemu/as_server 运行；backend 看到 marker 后返回 `runtime_state:"PIPE_LOADED"`。
10. 每次 build 开始前会停止旧 validation session；build 失败或 `POST /api/pipeline/unload` 会对 session 进程组发送 Ctrl+C 等价清理。
11. `POST /api/project/save` 只有最近一次 build 成功后才允许把 `${platform}_pipeline_all.json` 写回源 JSON。

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
如果未设置上述 path 变量，backend 会从启动目录和父目录向上查找 `out/linux/simulator/rpc_socket/Debug/as_server`、兼容旧构建的 `out/linux/a2/as_config/Debug/as_server`、`application/rv32qemu/sof-build-test.py` 和 `application/rv32qemu/build/sof.ldc` 等默认位置。

当前 simulator 原始 JSON 包含 playback、capture 和 DSP filter coverage 多条 pipeline。GUI build 按当前 layout snapshot 的 working groups 编译；单 pipeline 视图只编译该 pipeline，但 backend 会保留源 JSON 中未显示 pipeline。成功响应返回 `runtime_state:"PIPE_LOADED"`、`updated_pipelines` 和 `updated_frontend_connections`，前端用这些字段同步内存中的当前 workspace config。

平台 JSON 中 `frontend_connections[]` 与 `pipelines[]` 并列，例如 File Input 到 playback HOST 的连接：

```json
{
  "pipeline_id": "PLAYBACK_MAIN",
  "nodes": [
    {
      "node_id": "FILE_IN",
      "name": "File Input Playback",
      "module_type": "builtin.file_input",
      "params": {"enable": true, "file_path": "", "loop": true},
      "ui": {"dx": -185, "dy": 0}
    }
  ],
  "edges": [
    {"from": "FILE_IN:out", "to": "HOST_IN:in"}
  ]
}
```

Frontend 初始化 pipeline layout 时用 `frontend_connections[] + pipelines[]` 渲染 File Input/File Output 节点和连接；`as_config`、`ConfigService` 和 workspace regeneration 只消费 SOF `pipelines[]`。

Build 成功后 frontend 锁定结构编辑：不能新增/删除组件、连接、端口方向、pipeline rename 或 build-affecting 参数；仍允许拖动节点位置、Auto Arrange、选择、缩放和 Inspector 查看。`Unload` 后解除结构锁。

## Mock 到真实实现的过渡

`GuiRuntimeEngine` 是生产路径的 `IRuntimeEngine` 实现；`buildPipeline()` / `unloadPipeline()` 委托 `BuildOrchestrator` 完成 workspace JSON、`config.compile` RPC 和 simulator validation。`MockRuntimeEngine` 不再继承 `IRuntimeEngine`，只保留为 node/parameter 等非真实 runtime 控制的测试替身。

RUN/playback 采用双阶段数据路径：

1. `/api/runtime/run` 接收 frontend 的 `playback` 描述，包括 `sample_rate`、`channels`、`bits_per_sample`、`frame_bytes`、`data_offset`、`data_bytes` 和 `edge_key`。
2. `GuiRuntimeEngine` 创建 as_server playback session，并启动独立 worker thread。
3. frontend 通过 `/api/runtime/audio/playback/stream?...` 发送 `application/octet-stream` PCM frame；backend 只入队并立即返回。
4. frontend 随后通过 `/api/runtime/audio/playback/frame` 发送 `frame_index`、`offset`、`bytes_written` 等 JSON 元信息；backend 返回 `{accepted, queued_bytes, queued_audio_ms, next_push_ms, stalled, blocked_edge_key}`。
5. `queued_audio_ms` 和 `next_push_ms` 基于 queue 内 stream bytes、channel count、sample rate、sample bits 计算；当 queue 超过目标音频时长时，backend 增大 `next_push_ms`，让 frontend 延后推下一帧。
6. worker thread 从 queue 中取 frame，通过 `AudioRpcClient` 写入 as_server；queue 高水位、RPC 未就绪或 100ms 没有 dequeue 时返回 `stalled:true`，frontend 按 `blocked_edge_key` 标红闪烁对应连接。
7. File Output/capture 不在 backend 建 queue。frontend 按 `/api/runtime/audio/capture/frame?max_bytes=...` 定时读取，backend 每次直接从 as_server capture stream 读取一帧，返回 `{bytes, queued_bytes:0, next_poll_ms, data_base64}`；`next_poll_ms` 按本次读取 bytes 对应的音频时长计算。

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
  std::string pushAudioStream(const std::string& query, const std::string& frame) override;
  std::string pushAudioFrame(const std::string& query, const std::string& frame) override;
  std::string finishAudioInput(const std::string& request_json) override;
  std::string captureAudioFrame(const std::string& query) override;
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
