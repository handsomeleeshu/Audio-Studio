# Frontend Development Guide — React Edition

## 入口与运行方式

```text
frontend/index.html
frontend/assets/js/react-app.js
frontend/assets/css/styles.css
frontend/assets/js/configParser.js
frontend/assets/js/layout.js
frontend/assets/js/pipelineRules.js
frontend/assets/js/api.js
frontend/assets/js/telemetry.js
```

当前前端已经切换为 React 组件化实现。为了让 C++ Mock Server 仍能直接服务静态文件，`index.html` 默认通过 UMD CDN 加载 React 18，然后执行 `react-app.js`。

如需公司内网离线运行，建议后续二次开发时改成 Vite 方式：

```bash
npm install
npm run dev
```

然后把构建产物复制到 `frontend/` 或让 C++ 服务 `dist/`。

## UI 组件结构

`react-app.js` 里主要组件：

```text
App
├─ Topbar
│  ├─ Audio Studio logo
│  ├─ Project select
│  ├─ DSP/Core/Rate controls
│  └─ Validate/Build/Run/Stop/Auto Arrange/Delete/Export
├─ AlgorithmLibrary
├─ PipelineHeader
├─ EdgeLayer
├─ PipelineNode
├─ Inspector
├─ ParamPanel / ParameterControl
├─ Dashboard
└─ Panel Dock
```

## Project 与 Pipeline 的关系

顶部左上角是 Project，而不是 Pipeline。当前 `A2.json` 是一个 Project 示例。

Project JSON 仍然描述：

```text
meta / naming / hardware_hints
module_types
presets
module_instances
pipelines
scenes
```

Pipeline 是 Project 内的一个工作图。UI 中 Pipeline/Scene 切换放在 Pipeline Header 区域，而不是左上角主 Project 下拉框。

## 算法库扩展

算法库完全由 `module_types` 自动生成。新增算法时，只需要在产品 JSON 里增加：

```json
{
  "type_id": "fx.demo",
  "category": "playback/fx",
  "io": {
    "in_ports": [{ "name": "in", "max_ch": 2 }],
    "out_ports": [{ "name": "out", "max_ch": 2 }]
  },
  "static_schema": {
    "fields": [
      { "key": "quality", "type": "enum", "enum": ["low", "high"], "default": "high" }
    ]
  },
  "runtime_params": [
    { "param_id": "enable", "param_name": "Enable", "value_type": "bool", "default": true },
    { "param_id": "gain", "param_name": "Gain", "value_type": "int16", "range": { "min": -12, "max": 12, "step": 1 }, "default": 0 }
  ]
}
```

刷新后：

- 左侧算法库自动出现该模块。
- 算法 list item 自动获得一个小图标。
- Pipeline 里仍按原方框比例显示节点。
- 输入/输出端口数量自动来自 `io.in_ports/out_ports`。
- Inspector 自动生成静态/动态参数 UI。

## 参数 UI 自动适配

| JSON 类型 | UI 控件 |
|---|---|
| `bool` | Switch |
| `enum` | Select |
| `int16/uint8/uint16/float + range` | Slider + Number Input |
| `bytes` | Textarea placeholder，后续可替换成文件/二进制上传控件 |

运行态规则：

- `static_schema.fields`：running 时锁定。
- `runtime_params`：running 时仍可修改，调用 `/api/param/update`。
- Pipeline 节点新增、删除、移动、连线、删线：仅 Stop 状态允许。

## 连线策略

连线规则在 `frontend/assets/js/pipelineRules.js` 中：

- 必须从 output port 连到 input port。
- 不允许同一节点自连。
- UI 手动编辑时，一个 output port 只能连接一个 input port。
- 一个 input port 也只能有一个上游 output。
- 新连接如果占用了旧 output 或旧 input，会替换旧连接。
- 点击连线后，可以点击顶部 Delete 删除该 in-out 连接。

注意：如果旧产品 JSON 存在 fanout，例如一个 `EQ:out` 同时连多个节点，UI 会先按配置显示；后续手动编辑会按单 output 连接规则执行。建议产品 JSON 后续用显式 splitter/copier 节点描述 fanout，这样更利于资源估算和 DSP buffer ownership 管理。

## 布局与连线显示

- 节点与 SVG 连线统一使用 world coordinate。
- Auto Arrange 使用较大的 `MIN_X_DISTANCE` / `MIN_Y_DISTANCE`，避免节点过近时连线被覆盖或视觉上断开。
- `pipeline-world` 只让节点接收事件；空白区域和连线点击由 `EdgeLayer` 接收。
- 小地图根据真实 world coordinate 计算。

## 子窗口显示/隐藏

Panel Dock 按钮：

```text
Library / Inspector / Dashboard
```

关闭某个 panel 后，可通过右下角 dock 按钮恢复。布局会自动扩展 pipeline 区域。
