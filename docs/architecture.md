# Audio Studio Architecture

## 目标

Audio Studio 将产品 JSON 配置转换为可交互的 Pipeline UI，并通过 C++ 后端 mock 出真实 DSP runtime 后续需要的接口。

```text
Browser Frontend
  ├─ Product Config Loader
  ├─ Module Library
  ├─ Pipeline Canvas
  ├─ Parameter Inspector
  ├─ Runtime Dashboard
  └─ HTTP API Client
        │
        ▼
C++ Backend
  ├─ Config Provider
  ├─ Pipeline Validate / Build API
  ├─ Runtime Engine Interface
  ├─ Node Controller Interface
  ├─ Parameter Controller Interface
  └─ Mock Telemetry Generator
```

## 前端核心数据流

1. `GET /api/config` 加载 `config/A2.json`。
2. `module_types` 自动生成左侧算法库。
3. `module_instances + pipelines` 自动实例化 Pipeline 节点。
4. `io.in_ports/out_ports` 自动生成节点端口。
5. `static_schema.fields` 自动生成静态参数 UI。
6. `runtime_params` 自动生成动态参数 UI。
7. 用户手动连接端口后，前端更新 `edges`。
8. Build/Run/Stop 调用后端 API。

## 后端核心扩展点

- `IRuntimeEngine`：接入真实 DSP runtime / simulator。
- `INodeController`：节点点击、probe、debug、dump 等扩展。
- `IParameterController`：参数下发、TLV 编码、kcontrol 转换。

当前实现为 `MockRuntimeEngine`，用于维持 UI demo 功能。
