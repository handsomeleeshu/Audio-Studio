# Audio Studio

Audio Studio 是一个面向音频 DSP / VASS（VeriSilicon Advanced Sound System）产品化验证的 Web 工程 Demo。它用于展示基于产品 JSON 配置的音频 pipeline 设计、算法组件编排、运行态监控、参数调试、buffer probe / dump，以及后端 DSP runtime 接入接口。

当前工程已经从早期 React 方案切换为 **standalone HTML 前端 + C++17 Mock Backend**。前端不需要 npm build；本地调试时由 C++ mock server 直接托管 `GUI/frontend/index.html`、静态资源和 `/api/*` 后端接口。

---

## 1. 工程目标

Audio Studio 的目标不是做一个纯前端静态画图工具，而是模拟真实 Audio Studio / DSP runtime 工具链的工作方式：

- 从产品 JSON 加载算法库、pipeline、节点、端口、参数和默认布局。
- 在 Web UI 中编辑 pipeline，包括添加、删除、连接、拆分、合并和自动排布。
- 将 UI 操作通过 backend API 回调给后端，为后续接真实 DSP / simulator / runtime 保留接口。
- 在 pipeline running 状态下展示实时监控数据，包括算法开销、core loading、system health、audio I/O、Inspector、buffer probe 和实时信号 probe。
- 支持本地 C++ mock backend 生成合理 mock 数据，便于没有真实 DSP 设备时演示和调试 UI。
- 禁止关键运行态数据由前端自己伪造；实时 DSP 相关数据应通过后端 controller 返回。

---

## 2. 当前能力概览

### 2.1 Project / JSON 配置

- 支持从 `config/*.json` 扫描 project。
- 当前示例 project 为 `config/A2.json`。
- Project JSON 描述内容包括：
  - module types
  - module instances
  - pipelines
  - nodes
  - edges
  - runtime parameters
  - static schema
  - notes / description
  - performance hints

前端会根据 JSON 自动生成算法库、pipeline graph、节点端口、Inspector 参数区和说明区。

### 2.2 Pipeline Layout 编辑

Pipeline layout 支持：

- 从 Algorithm Library 拖拽新增算法组件。
- 点击节点查看 Inspector。
- 点击 buffer connection 查看 buffer Inspector / Dump / Probe。
- output port 到 input port 手动连线。
- 单 output / 单 input 一对一连接约束。
- 删除节点和连线。
- 框选、多选、Ctrl/Cmd 多选。
- Copy / Cut / Paste。
- Delete / Backspace 删除选中内容。
- Undo / Redo。
- Save / Export。
- Auto Arrange 自动排布。

Auto Arrange 当前支持：

- 长 pipeline 横向自然延展，不再限制一行算法数量。
- 不相连的 pipeline component 自动上下分 band 排布。
- fan-in / fan-out 按端口顺序尽量保持视觉关系。
- layout world bounds 自动扩展，支持滚动和缩放。

### 2.3 Working Pipeline Groups

Pipeline list 不再只是原始 JSON 中的 pipeline 列表。前端会基于当前 layout 的 connected components 自动识别 working pipeline group，并显示状态：

- `Saved`
- `Modified`
- `New`
- `Unsaved split`
- `Unsaved merge`

典型场景：

- 修改一个已保存 pipeline，但图仍连通：显示 `Modified`。
- 把一个 pipeline 手动拆成多个不相连图块：显示 `Unsaved split`。
- 新建一个和其它 pipeline 无连接的新图块：显示 `New`。
- 把两个原始 pipeline 接到一起：显示 `Unsaved merge`。

后续完善 JSON save 逻辑时，可以基于这些 working groups 提醒用户命名、确认删除、确认拆分或合并。

### 2.4 Inspector

Inspector 根据当前选中对象自动切换：

- 选中 algorithm node：显示节点 Inspector。
- 选中 connection / buffer：显示 buffer Inspector。

节点 Inspector 支持：

- General 信息。
- Runtime parameters。
- Probe。
- Notes。

Buffer Inspector 支持：

- buffer format。
- RMS / Peak。
- frame size。
- waveform / spectrum。
- buffer dump。

### 2.5 Buffer Dump

Buffer dump 支持多 buffer 同时 dump：

- 每条 connection / buffer 有独立 dump session。
- 多个 buffer 可以同时 Start Dump。
- 每个 buffer 独立保存 WAV、独立统计 written bytes、独立 timer。
- Dump 数据必须来自后端 `/api/inspector/buffer/live` 返回的 PCM16。
- GitHub Pages 或无 backend 情况下不生成前端 fake PCM 文件。

### 2.6 Real-Time Signal Probe

`REAL-TIME SIGNAL PROBE` 是底部高清实时信号窗口，用于增强显示当前选中 buffer 的时域或频域数据。

能力包括：

- 根据当前选中 buffer format 自动识别声道数。
- 最多同时显示两个声道。
- 声道选择为上拉菜单，显示 `ch0 ~ chN`。
- Purple 通道可选择 `Off`，用于只显示一个声道。
- 支持 Time / Frequency 模式切换。
- Frequency 模式由后端执行 4096-point FFT。
- 前端只渲染后端 `/api/realtime/probe/live` 返回的数据，不自己造 fake waveform / spectrum。
- 控制信息通过 `/api/realtime/probe/config` 传给后端。

### 2.7 Per-Algorithm Cost

`PER-ALGORITHM COST` 面板展示每个算法实例的运行开销：

- CPU
- MEM
- LAT
- Core
- IDX
- Total row
- 排序和节点定位

数据由后端 `/api/algorithm/cost/live` 返回，mock 后端在 running 状态下生成合理 fake cost 数据。

### 2.8 DSP Core Loading

`DSP CORE LOADING` 面板展示 DSP core 运行状态：

- Core N load
- temperature
- power
- Total Load
- Headroom

Core 行区域可滚动，底部 Total Load / Headroom 固定显示。Core 数量跟随顶部 `Cores` 配置变化，例如 4 cores 或 8 cores。

数据由后端 `/api/dsp/core/loading` 返回。前端不自己生成 core loading fake 数据。

### 2.9 System Health / Audio I/O / Event Log

底部 dashboard 还包括：

- `SYSTEM HEALTH`
  - latency
  - buffer occupancy
  - throughput
  - XRuns / dropouts
  - memory usage
  - power usage
  - active cores

- `AUDIO I/O`
  - IN L / IN R / OUT L / OUT R 电平表

- `EVENT LOG`
  - UI 操作日志
  - backend callback 日志
  - running 状态下的周期性事件

这些面板均通过对应后端 controller 获取 mock 数据，便于后续接入真实系统状态。

---

## 3. 工程结构

```text
Audio-Studio/
├── README.md
├── GUI/frontend/
│   ├── index.html                 # standalone HTML/CSS/JS frontend
│   └── assets/
│       ├── js/
│       │   ├── configParser.js     # product JSON parser
│       │   └── utils.js
│       └── verisilicon-logo.png
├── GUI/backend/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── audio_studio.hpp        # backend interfaces and controller declarations
│   └── src/
│       ├── main.cpp                # mock server entry
│       ├── http_server.cpp         # static file server + REST API routes
│       └── mock_runtime.cpp        # fake runtime/controller implementations
├── config/
│   └── A2.json                     # product JSON example
├── scripts/
│   ├── build_backend.sh
│   └── run_tests.sh
└── tests/
    └── frontend/
        ├── config-parser.test.mjs
        ├── plain-html-integration.test.mjs
        └── standalone-features.test.mjs
```

---

## 4. 快速启动

### 4.1 构建后端

```bash
./scripts/build_backend.sh
```

### 4.2 启动本地 server

```bash
./build/audio_studio_server . 8080
```

### 4.3 打开页面

```text
http://127.0.0.1:8080
```

推荐使用本地 C++ server 打开页面。这样前端可以访问完整 `/api/*`，所有运行态数据都由 mock backend 返回。

如果直接以静态页面方式打开，或者部署到没有 C++ backend 的 GitHub Pages，部分 backend-driven 面板会显示 API 不可用 / N/A。这是预期行为。

---

## 5. 测试

完整测试：

```bash
./scripts/run_tests.sh
```

单独运行前端测试：

```bash
node tests/frontend/config-parser.test.mjs
node tests/frontend/plain-html-integration.test.mjs
node tests/frontend/standalone-features.test.mjs
```

构建后端：

```bash
./scripts/build_backend.sh
```

---

## 5.1 VSCode Debug / Profile

本工程已提供 VSCode 一键调试和剖析配置：

```text
Full Stack: Debug
Full Stack: Profile
Backend: Debug Server
Frontend: Debug Chrome
Frontend: Profile Chrome
```

详细使用方式见：

```text
docs/debug_profile_guide.md
```

---

## 6. 主要后端 API

### 6.1 Project / Config

```text
GET  /api/projects
GET  /api/config?project=A2.json
GET  /config/A2.json
```

### 6.2 Pipeline Runtime

```text
POST /api/pipeline/validate
POST /api/pipeline/build
POST /api/pipeline/edit
POST /api/pipeline/tool
POST /api/project/save
POST /api/runtime/run
POST /api/runtime/stop
```

### 6.3 Node / Parameter / Inspector

```text
POST /api/node/action
POST /api/param/update
POST /api/inspector/inspect
GET  /api/inspector/live
POST /api/inspector/buffer/inspect
GET  /api/inspector/buffer/live
```

### 6.4 Dashboard / Runtime Monitoring

```text
GET  /api/telemetry
GET  /api/algorithm/cost/live
GET  /api/dsp/core/loading
GET  /api/system/health/live
GET  /api/audio/io/live
GET  /api/event-log/live
POST /api/ui/event
```

### 6.5 Real-Time Probe

```text
POST /api/realtime/probe/config
GET  /api/realtime/probe/live
```

---

## 7. 后端扩展点

真实 DSP / VASS / simulator 接入时，主要替换或继承以下接口：

```text
IRuntimeEngine
INodeController
IParameterController
IInspectorController
IAlgorithmCostController
IDspCoreLoadingController
IEventLogController
ISystemHealthController
IAudioIoController
IRealTimeProbeController
```

当前 fake controller 的职责是：

- 让前端 UI 在没有真实 DSP 的情况下可运行、可演示、可测试。
- 保持接口形态接近真实产品集成需求。
- 对关键实时数据采用后端生成，而不是前端直接 fake。

后续接入真实 runtime 时，可以保持 API contract 不变，只替换 controller 实现。

---

## 8. Product JSON 说明

`config/A2.json` 是当前产品配置示例，描述 A2 音频 pipeline 和算法模块。前端会解析其中的：

```text
module_types
module_instances
pipelines
nodes
edges
runtime_params
static_schema
notes
performance
ui
```

常见字段用途：

- `module_types`：算法类型定义，包含端口、参数、UI 显示和性能估计。
- `module_instances`：具体模块实例。
- `pipelines`：pipeline 拓扑。
- `nodes`：pipeline 中的算法节点。
- `edges`：节点之间的 buffer connection。
- `runtime_params`：运行态可调参数。
- `static_schema`：静态配置 schema。
- `performance`：默认 CPU / latency / memory hints。
- `notes`：Inspector Notes 中显示的模块说明。

---

## 9. 前端开发说明

当前前端是 standalone HTML：

```text
GUI/frontend/index.html
```

特点：

- 不依赖 React / Vite / Webpack。
- 不需要 npm install。
- 可以直接由 C++ server 托管。
- UI、交互和 API 调用逻辑集中在 `index.html`。
- JSON 解析相关逻辑在 `GUI/frontend/assets/js/configParser.js`。

修改前端后，通常只需要刷新浏览器即可验证。若浏览器缓存旧页面，可以强制刷新。

---

## 10. 后端开发说明

后端是无第三方依赖的 C++17 mock server：

```text
GUI/backend/include/audio_studio.hpp
GUI/backend/src/http_server.cpp
GUI/backend/src/mock_runtime.cpp
GUI/backend/src/main.cpp
```

开发方式：

1. 在 `audio_studio.hpp` 中新增 interface / controller 声明。
2. 在 `mock_runtime.cpp` 中实现 fake controller。
3. 在 `http_server.cpp` 中增加 route。
4. 在 `main.cpp` 中创建并注入 controller。
5. 运行 `./scripts/build_backend.sh` 验证编译。

---

## 11. 常用开发命令

```bash
# 构建后端
./scripts/build_backend.sh

# 运行完整测试
./scripts/run_tests.sh

# 启动本地服务
./build/audio_studio_server . 8080

# 打开本地页面
open http://127.0.0.1:8080
```

---

## 12. 当前定位

Audio Studio 当前已经具备较完整的前端 demo 能力，适合作为以下工作的基础：

- A2 / VASS 产品 pipeline 配置演示。
- DSP runtime API 对接验证。
- Audio algorithm 参数调试 UI 验证。
- Buffer probe / dump / realtime probe 交互验证。
- 多 dashboard 运行态监控窗口验证。
- 后续 save-to-json、真实 DSP runtime、simulator、remote target 接入开发。
