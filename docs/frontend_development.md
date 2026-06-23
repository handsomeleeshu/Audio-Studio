# Frontend Development Guide

## 入口与运行方式

当前前端是 standalone HTML 实现：

```text
GUI/frontend/index.html
configs/built-in-algorithm.json
GUI/frontend/assets/verisilicon-logo.png
```

C++ Mock Server 直接托管 `GUI/frontend/index.html`、静态资源和 `/api/*` 接口。本地调试推荐：

```bash
./scripts/build_all.sh --profile gui_backend -r linux a2
./out/linux/a2/gui_backend/Release/audio_studio_server . 8080
```

然后打开：

```text
http://127.0.0.1:8080
```

根目录 `index.html` 只负责跳转到 `GUI/frontend/`。

## 当前代码组织

`GUI/frontend/index.html` 包含当前可交互 UI 的 HTML、CSS 和运行时代码。这样做的原因是 GitHub Pages 预览和 C++ Mock Server 都可以不经过前端构建链直接服务页面。

`GUI/frontend/assets/js/` 下保留的是测试覆盖的纯逻辑模块：

```text
configParser.js              # 产品 JSON 转 pipeline/model 的解析逻辑
layout.js                    # pipeline 节点布局、端口坐标和连线路径
pipelineRules.js             # 连接规则
pipelineEditCallbackModel.js # pipeline 编辑回调 payload/model
topbarPanelMenuModel.js      # panel 菜单状态 model
utils.js                     # 解析和格式化辅助函数
```

这些模块不是页面入口脚本；它们用于把关键策略拆出来做单元测试，避免所有行为都只能靠扫描巨大 HTML 字符串验证。

## 主要 UI 区域

```text
Topbar
├─ Project / DSP / Cores / Frequency
├─ Validate / Build / Run / Stop
├─ Auto Arrange / Undo / Redo / Save / Export

Algorithm Library
Pipeline Canvas
Inspector
Runtime Dashboard
├─ Per-Algorithm Cost
├─ DSP Core Loading
├─ Real-Time Signal Probe
├─ System Health
├─ Audio I/O
└─ Event Log
```

## 数据流

1. `GET /api/projects` 获取可用 project。
2. `GET /api/config?project=...` 加载产品 JSON。
3. `imports` 指向的 module catalog、项目私有 `module_types` 和 `configs/built-in-algorithm.json` 共同生成算法库。
4. `pipelines` 生成节点、端口、连线和初始 layout。
5. UI 编辑通过 `/api/pipeline/edit`、`/api/pipeline/tool`、`/api/node/action` 回调后端。
6. Validate/Build/Run/Stop 调用对应 `/api/*`。
7. running 或 stopped 状态下的 dashboard/inspector 数据都通过后端 live API 获取。

## 参数与运行态规则

- Frontend/backend runtime state enum 固定为 `NOT_READY`、`PIPE_LOADED`、`RUNNING`、`ERROR`。
- `parameters[]`：统一参数模型；没有 `RUNNING` settable state 的参数作为 install/static 参数处理。
- 带 `RUNNING` settable state 的 `bool`、`enum`、`int/float` 参数会在 Inspector 自动显示为 toggle、select、slider 或 number input，running 时调用 `/api/param/update`。
- Inspector 显示参数值时优先使用 `presets[].preset_id == "inspector_preset"`；不存在时前端自动创建。该 preset 中没有值时，再回退到 module parameter default。
- `PIPE_LOADED` 参数只在对应 pipeline build 成功后可编辑；进入 `RUNNING` 后，未声明 `RUNNING` settable state 的参数会置灰。
- `static_schema.fields` 和 `runtime_params` 只作为旧 catalog 的兼容输入，新 built-in catalog 不再以它们为主模型。
- 节点新增、删除、移动、连线、删线：仅 stopped 状态允许。
- buffer dump 和 real-time probe 数据必须来自后端接口，前端不生成 fake PCM 或 fake spectrum 数据。

## Endpoint 与 File I/O

- `builtin.host` 和 `builtin.dai` 是普通 module type，pipeline nodes 使用 `kind: "module"` 引用 module instances，不再使用旧的 pipeline `ports` / `audio_endpoints` 描述。
- `builtin.host` module instance 使用 `stream_name` 字符串参数；配置中仍可保留 `stream_id` 作为 as_config 兼容字段。
- FILE_IO DAI 不是单独 module type。它是 `builtin.dai` instance，参数包含 `dai_type: "file_io_dai"`、`dai_index`、`direction` 和 `file_path`。
- `value_type: "file_io"` 由 Inspector 根据 DAI 当前方向显示为打开 WAV 或选择保存路径；port 方向切换会同步更新 `direction`。
- `builtin.host` 在 pipeline layout 中始终显示一个 input 和一个 output，其中一侧是 SOF internal port，另一侧是 external debug port。external port 和 file input/output port 渲染为实心小圆孔，颜色仍按 input/output 原规则。
- `builtin.file_input` 和 `builtin.file_output` 是 debug-only 前端虚拟组件：它们显示为特殊颜色，只能连接 HOST external port，并且不会进入 backend 重生成后的根 `pipelines[]`。
- Build 请求必须提交完整 layout snapshot，同时用 `group_id` 指明本次 build 的 working group；`group_id:"ALL"` 才表示请求全量 pipeline build。
- Build 成功只接受 backend 返回 `ok: true` 且 `runtime_state: "PIPE_LOADED"`；无 backend、HTTP 失败或 `null` response 都必须显示 Build failed。

## 连接策略

连接规则由当前 standalone 入口实现，并由 `GUI/frontend/assets/js/pipelineRules.js` 的单元测试覆盖：

- 必须从 output port 连到 input port。
- `external` 只能连接 `external`，`sof` 只能连接 `sof`。
- 不允许同一节点自连。
- UI 手动编辑时，一个 output port 只能连接一个 input port。
- 一个 input port 也只能有一个上游 output。
- 新连接如果占用了旧 output 或旧 input，会替换旧连接。

如果产品 JSON 需要 fanout，建议后续用显式 splitter/copier 节点描述 fanout，这样更利于资源估算和 DSP buffer ownership 管理。

## 性能调试

前端性能命令行基准：

```bash
npm run profile:frontend
```

常用场景：

```bash
AUDIO_STUDIO_PROFILE_SCENARIO=running npm run profile:frontend
AUDIO_STUDIO_PROFILE_SCENARIO=both AUDIO_STUDIO_PROFILE_MS=30000 npm run profile:frontend
```

报告会写入 `profiles/frontend/`，指标包括 Chrome `Performance.getMetrics`、Long Task、frame interval、DOM node 数、JS heap、API 请求数量和页面注册的 interval delay。
