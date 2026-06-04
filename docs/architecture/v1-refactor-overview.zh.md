# Ipcraft V1 架构重构总览

本文说明 Finepaper 当前 V1 架构重构的背景、目标、已经落地的边界和仍在过渡的部分。它面向汇报和团队对齐，不替代更正式的公开契约文档
`v1-core-architecture.md`。

## 1. 重构背景

旧 Finepaper 以 Qt 画布 `Graph` 为中心：项目文件、UI 模块、连接、参数、NoC generator 输入和验证逻辑都围绕画布模型组织。这种设计适合早期 NoC 编辑器，但不适合扩展成通用 IP 制作平台。

主要问题是：

- 项目语义和画布表现耦合，布局坐标容易进入生成配置。
- Core 容易硬编码 NoC、router、endpoint、方向端口等实现细节。
- 第三方 IP 接入需要理解旧 Graph / Module / Connection 结构。
- generator / validator 容易读取项目文件或 UI 结构，缺少稳定工具输入边界。
- 黑盒审计难以只通过公开 schema、CLI 和诊断规则判断行为。

V1 重构的目标是把系统从“图编辑器”改成“IP 制作与组合平台”。

## 2. 新架构核心

| 方向 | 当前设计 |
|------|----------|
| 项目根 | `ProjectDocument` / `ipcraft.project.v1` |
| Package 契约 | `PackageSpec` / `ipcraft.package.v1` |
| 实例配置 | `ConfigSchema` 声明，`ConfigBundle` 保存值 |
| 项目连接 | `CompositionModel`，支持 endpoint 数组和 n-ary connection |
| 内部结构 | `GraphConfig`，仅属于单个 IP 实例 |
| 可视化状态 | `layout` / `views[].layout`，不进入硬件语义 |
| 生成输入 | package emitters 产生 `ipcraft.emitted-inputs.v1` |
| 外部执行 | `FlowRunner` 显式执行 package-declared flow |
| 诊断 | `DiagnosticStore` 和稳定 rule id |

## 3. 重构前后对照

| 维度 | 旧架构 | 当前 V1 架构 |
|------|--------|--------------|
| 项目事实源 | Qt `Graph` | `ProjectDocument` / `ProjectDesign` |
| Package 入口 | 旧 manifest / `ipcore.yml` 运行时依赖 | normalized `ipcraft.package.v1` |
| 连接表达 | 二元 `source` / `target` port ref | `CompositionModel.endpoints[]` |
| 实例配置 | 模块参数、旧 `ipcore_state` 混用 | `ConfigBundle` |
| 内部图 | 项目根 graph | 实例局部 `GraphConfig` |
| 布局状态 | 易混入 module parameters | `layout` / `views[].layout` |
| 验证 | 内置验证和外部 DRC 边界不清 | 静态验证默认无副作用，外部工具走 flow |
| 生成 | 旧 NoC project / graph 输入 | package emitter / tool input projection |
| UI | 画布就是 domain | 画布是 projection 和交互 adapter |

## 4. 当前已落地部分

当前代码已经具备这些 V1 基础：

- `ProjectDocument`、`ProjectReader`、`ProjectWriter` 保存和读取 V1 项目结构。
- `ProjectIpInstanceRecord` 保存 IP 实例、package 引用、config、graphConfig、lastRuns、artifacts。
- `PackageSpecReader` 读取 package-local `ipcraft.json`，解析 extension、config、interfaces、flows、artifacts。
- `ConfigSchema` / `ConfigBundle` 支持参数、表格、文档、文件输入和结构验证。
- `CompositionModel` 支持项目级连接和 package connection rules。
- `PackageInputBuilder` 发射生成输入并记录 `ipcraft.emitted-inputs.v1` manifest。
- `FlowRunner` 负责 flow 执行、stdout/stderr 捕获、timeout 和受控环境。
- `ArtifactCollector` 根据 package artifact spec 收集产物。
- CLI/headless 路径不依赖 Qt Widgets 或 NodeEditor。
- Qt Generate 路径已经通过 project/package/flow 边界执行生成。

## 5. 当前仍在过渡部分

| 过渡点 | 当前状态 | 收敛方向 |
|--------|----------|----------|
| Qt 画布 | `Graph` 仍是实时交互模型 | 逐步改为 ProjectDesign / GraphConfig projection |
| 命令系统 | command 仍多为 graph mutation | durable mutation 进入 design service / patch boundary |
| ModuleRegistry | Qt 桥接仍使用 registry 形态 | 迁移到注入式 package/component/interface/rule registry |
| generator 输入 | 部分 first-party generator 仍接受 graph-shaped compatibility input | 全部改成 package emitter 产生的 tool input |
| view 描述 | 仍有 XML/view bridge | 收敛为 `ipcraft.view.v1` / `ipcraft.view.descriptor.v1` |
| Foundation core | `ProjectDesign` / `ProjectPatch` 已存在基础 API | 接入更多运行时读写、UI mutation 和工具协议 |

## 6. 为什么保留适配层

适配层存在是为了降低迁移风险，不是为了保留旧架构中心：

- Qt UI 需要继续可用，因此 `Graph` 暂时保留为画布交互模型。
- first-party NoC / OpenNoC / RaveNoC generator 已有成熟模板和测试，因此先在边界 normalize 输入。
- 已有项目和测试需要迁移路径，因此旧 schema 只允许出现在 explicit migration 或 compatibility fixture 中。

适配层的规则是：只能投影、转换、兼容，不能重新成为事实源。

## 7. 后续收敛重点

1. 让 Qt 编辑器的持久化 mutation 从 graph command 转为 design service / project patch。
2. 让 `NodeEditorWidget` 成为 view projection，而不是 domain owner。
3. 把 package registry、component registry、interface registry、connection rule registry 从全局状态中抽离。
4. 把 first-party generator 输入收敛到标准 tool input projection。
5. 完善 `parse_diagnostics` 和 `plugin_hook` 的执行边界。
6. 用 public schemas、CLI examples、diagnostic rule id 继续扩大黑盒审计覆盖。

## 8. 汇报结论

V1 重构已经完成了最关键的架构转向：项目语义进入文档模型，package 能力进入契约模型，验证和执行进入可审计边界。当前剩余工作不是重新设计方向，而是移除旧 Graph-centric 实现惯性，把 Qt 编辑器和 generator 彻底收敛到新的 project/package/tool-input 架构。
