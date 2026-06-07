# Audio Studio Product Demo — React + C++ Mock Backend

Audio Studio 是 VASS（VeriSilicon Advanced Sound System）的一部分。本工程是一个产品级 Demo，用于展示基于产品 JSON 配置的音频 Pipeline 设计、运行态监控、静态/动态参数配置和 C++ Mock 后端扩展接口。

## 本版更新

- 前端切换为 React 组件化实现。
- UI 风格恢复为暗色科技感 Audio Studio 仪表盘布局。
- 左上角增加 Audio Studio logo，并显示 VASS 简写。
- 顶部左侧恢复为 Project 选择；A2.json 作为 Project 示例。
- Pipeline/Scene 切换放在 Pipeline Header 区域。
- 算法库每个算法自动显示小图标。
- Pipeline 节点保持原方框比例。
- 支持手动 output port 拖拽到 input port。
- 支持点击 in-out 连线后删除。
- UI 手动编辑限制一个 output 只能连接一个 input，同时一个 input 只能有一个上游。
- 恢复子窗口显示/隐藏：Library / Inspector / Dashboard。

## 快速启动

```bash
cd audio_studio_product
./scripts/build_backend.sh
./build/audio_studio_server . 8080
```

浏览器打开：

```text
http://127.0.0.1:8080
```

当前 `index.html` 默认通过 CDN 加载 React 18。若要内网离线部署，请参考 `docs/frontend_development.md` 改成 Vite 构建并将 React 打包进前端产物。

## 工程结构

```text
frontend/              React static frontend + CSS
backend/               C++17 mock backend，无第三方依赖
config/A2.json         产品配置 JSON 示例
scripts/               构建和测试脚本
tests/frontend/        前端纯逻辑单元测试
docs/                  二次开发文档
```

## 测试

```bash
./scripts/run_tests.sh
```

测试覆盖：

- 配置解析
- 自动布局最小距离
- 静态/动态参数 running 策略
- 单 output / 单 input 连线策略
- C++ mock backend

## 后端扩展点

真实 DSP / VASS / simulator 接入时，可以替换：

```text
IRuntimeEngine
INodeController
IParameterController
```

当前 `MockRuntimeEngine` 仍返回合理随机数，用于维持 UI demo 的实时显示。
