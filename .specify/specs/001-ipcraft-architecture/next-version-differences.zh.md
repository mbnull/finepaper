# 下个版本架构区别与改进说明

本文用于说明 `.specify/specs/001-ipcraft-architecture/` 中规划的下个版本，与当前 IpCraft 实现之间的主要区别和改进方向。

当前实现已经从早期 NoC 画布工具推进到 `ipcraft.project.v1` / `ipcraft.package.v1` 契约驱动的过渡架构，但 Qt 编辑器仍保留 `Graph` 作为实时画布模型，并通过 `GraphProjectSerializer`、`ProjectStateService` 等适配层把画布状态投影回项目文档。下个版本规格要求进一步 hard cutover：以 `ProjectDesign` 和 `ipcraft.*.v1` 公共契约作为系统根，删除或替换旧的 Graph-centric、NoC-specific、旧 runtime 假设。

## 1. 总体定位变化

| 维度 | 当前实现 | 下个版本目标 | 改进价值 |
|------|----------|--------------|----------|
| 产品定位 | IpCraft 过渡架构，仍保留 NoC 编辑器遗留路径 | 通用 IP creation、configuration、inspection、validation、generation 工具 | 从“图编辑器 + 生成器”升级为“IP 配置、检查、验证、生成和审计工具” |
| 项目根模型 | `ProjectDocument` 是 V1 文档根，但 Qt 仍通过 `Graph` 投影交互 | `ProjectDesign` 是唯一项目语义 aggregate root | 消除“保存画布即保存项目”的架构风险 |
| 公共契约 | `ipcraft.project.v1`、`ipcraft.package.v1`、`ipcraft.emitted-inputs.v1` | `ipcraft.project.v1`、`ipcraft.package.v1`、`ipcraft.tool.input.v1`、`ipcraft.tool.result.v1`、`ipcraft.patch.v1` 等 | 统一 IpCraft 契约命名和第三方集成边界 |
| 编辑方式 | UI 事件转成 graph command，保存时再投影文档 | UI intent 转成 `ProjectPatch`，由 `PatchApplier` 验证后应用 | 所有 durable mutation 都可审计、可撤销、可验证 |
| Package 集成 | package spec + editor manifest bridge + `ModuleRegistry` | injected `PackageRegistry`、capability registry、component/interface/connection registries | 取消全局可变 registry，支持多版本和第三方包 |
| 工具协议 | `FlowRunner` 执行 package flow，emitter 生成输入 manifest | tool 消费 `ipcraft.tool.input.v1`，返回 `ipcraft.tool.result.v1` | 工具输入、结果、诊断、产物和 patch suggestion 都成为稳定协议 |

## 2. 数据模型区别

当前模型以 `ProjectDocument` 保存项目，以 `ProjectIpInstanceRecord` 保存 IP 实例，以 `CompositionModel` 保存项目级连接，以 `GraphConfig` 保存单 IP 内部图形配置。Qt 画布中的 `Graph` 仍承载实时模块和连接。

下个版本改为 `ProjectDesign` 统一承载项目语义：

- `components` 代替当前 graph module projection，组件实例是项目语义对象。
- `interfaces` 是解析后的 interface instances，连接不再依赖 UI port index 或 Qt node。
- `semantic connections` 与 visual edge routing 分离。
- `topologies` 支持 AnyNet、NoC、显式任意拓扑、参数化拓扑展开结果。
- `views` 和 `layout documents` 保存视图和布局，不进入 semantic config。
- `extensions` 保存 package-owned extension blocks，核心不从 extension 中推导隐藏硬件语义。

主要改进是把“组件配置、接口、连接、拓扑、布局、工具输入”拆成独立语义边界。布局中的坐标、折叠状态、节点尺寸、edge waypoints、zoom、pan 不再混入 component config 或 generator input。

## 3. 架构分层区别

当前实现的核心模块主要集中在 Qt 目录下：

- `qt/inc/project/*`：项目读写和实例状态。
- `qt/inc/ipcraft/*`：package spec、config、composition、emitter、flow、artifact。
- `qt/inc/graph/*`：Qt 画布模型。
- `qt/inc/nodeeditor/*` 和 `qt/inc/panels/*`：编辑器 UI。

下个版本规格要求拆成更明确的模块边界：

| 新模块 | 主要职责 | 与当前实现的区别 |
|--------|----------|------------------|
| `ipcraft-core` | `ProjectDesign`、公共文档、diagnostic、artifact、patch、layout document | 不依赖 Qt Widgets，不包含 NoC implementation package 特例 |
| `ipcraft-package` | package manifest、registry、capability、component/interface/connection rule registry、package check | 替代当前 editor manifest bridge 和全局 `ModuleRegistry` |
| `ipcraft-domain` | `ProjectSession`、`DesignEditingService`、`ResolutionService`、validation/generation pipeline | 把 UI 交互和核心模型之间的业务服务独立出来 |
| `ipcraft-topology` | topology graph、参数化拓扑、显式图拓扑、layout provider | 任意拓扑成为一等模型，不再围绕 mesh/router 特例组织 |
| `ipcraft-ui` | ViewHost、ProjectOverview、BlockDiagram、TopologyGraph、InterfaceTable、ConfigInspector | UI 通过 intent/patch 工作，不直接改 domain object |
| `ipcraft-capability-noc` | NoC capability schemas、NoC 拓扑、attachment、NoC 验证和视图集成 | NoC 语义从 core 移出 |
| `packages/vendor-meshnoc` | 示例 NoC 实现 package、tools、views、examples | 具体 NoC 实现作为普通 package 加载，core 不识别 package id |

## 4. UI 和交互改进

当前 Qt UI 以 NodeEditor 为中心，主要工作流是 IP Catalog、Workspace Modules、PropertyPanel、LogPanel 和画布交互。

下个版本 UI 以 inspection-first 为核心：

- `ProjectOverviewView` 展示 project、packages、components、topologies、diagnostics、artifacts 的总体状态。
- `ConfigInspector` 展示 authored config、resolved config、derived config、tool input config、runtime output config。
- `InspectorPanel` 使用 package schema 渲染参数、单位、枚举、约束、说明，不写 package-specific UI。
- `InterfaceTableView` 支持不用图画布也能查看和编辑接口连接。
- `TopologyGraphViewProvider` 支持任意 topology graph，而不是只服务 NoC mesh。
- `FlowArtifactsPanel` 展示工具运行结果、产物、日志和 diagnostics。

主要改进是从“先画图再生成”变成“先能检查、解释、对比、预览，再编辑和执行”。用户可以在不手动编辑、不运行 generator 的情况下查看 resolved config、provenance、semantic diff 和 tool input preview。

## 5. 配置解析和检查改进

当前实现已有 `ConfigSchema` / `ConfigBundle`，可以校验 parameters、tables、documents、files，并处理 unknown key、类型、范围、文件扩展和 path confinement。

下个版本新增 `ResolutionService`，目标不仅是验证输入是否合法，还要解释配置从哪里来：

- 值是 explicit、default、inherited、derived、generated 还是 overridden。
- 值来自哪个 package、schema、组件、接口或 extension block。
- 值的类型、单位、合法枚举、范围、文档说明是什么。
- 哪些 validator、generator、view 或 topology provider 会消费这个值。
- 两个项目或两次配置之间的 semantic diff 与 layout-only diff 分离。

这会让 IpCraft 更适合 IP 集成评审和审计，而不是只做生成前参数填写。

## 6. 验证和生成改进

当前 CLI 的 `validate-project` 是静态验证；Qt Generate 会先跑 built-in validation，再执行 package 的 `generate` flow。工具输入通过 emitter 生成 `ipcraft.emitted-inputs.v1` manifest。

下个版本把验证和生成统一到 tool protocol：

- `ProjectionService` 生成确定性的 `ipcraft.tool.input.v1`。
- UI 可以在执行前展示 Tool Input Preview。
- validator/generator 返回 `ipcraft.tool.result.v1`。
- tool result 可以包含 diagnostics、artifacts、metrics、logs 和 patch suggestions。
- host 对 tool result 和 patch suggestions 做验证，用户确认后才通过 patch 流程应用。

主要改进是工具不再读 `.fpproj`、Qt graph、UI 坐标或旧 NoC generator input，而是只消费稳定投影。这样 generator、validator、CLI、UI 和测试都围绕同一份公共协议工作。

## 7. NoC 和具体 NoC 实现包边界改进

当前实现中已经有 `noc.v1` extension、package manifest 和 NoC implementation package 适配，但 Qt editor 中仍能看到 mesh/router/endpoint、attachment、方向、IP-XACT 映射等 NoC 相关逻辑。

下个版本要求：

- core 不出现 NoC、具体 NoC 实现包、mesh、router、endpoint、north/east/south/west 等特例判断。
- NoC 语义放入 `ipcraft-capability-noc`。
- 具体 NoC 实现包放入普通 package 路径，例如 `packages/vendor-meshnoc`，通过 package registry 加载。
- payload IP、NIC/adapter、NoC attachment slot 都是普通 component 和 topology/interface 关系。
- NoC-aware payload IP 可以直接声明 NoC endpoint interface，但这由 package/capability 决定，不由 core 特判。

改进结果是：具体 NoC 实现包不再是内置类型，NoC 不再等于 mesh，任意拓扑和普通 IP 组合都能共享同一套能力。

## 8. 测试和验收改进

当前仓库已有大量 Qt/xmake 测试、contract examples、architecture readiness 文档和 CLI contract tests。下个版本规格进一步要求测试先行和多角色验收：

- 每个公共 schema 都要有 parser、writer、roundtrip、negative、golden tests。
- 每个阶段先写 public contract tests，再实现。
- 增加 architecture boundary scans，防止 Graph-as-root、NoC hardcode、global registry、UI-domain coupling 等回流。
- hidden acceptance tests 由独立 Test/QA Agent 维护，Implementation Agent 不读取隐藏期望。
- Acceptance Agent 按 review checklist、隐藏测试、架构扫描和手工语义审查给最终结论。

这会把“测试通过”升级为“公共契约、架构边界、隐藏验收和评审清单共同通过”。

## 9. 迁移策略区别

当前实现仍保留部分过渡字段和桥接层，例如：

- `ProjectDocument` 中的 transitional graph 字段。
- `ProjectStateService` 维护 canonical `instances[].config` 和 legacy alias。
- `GraphProjectSerializer` 在保存/加载时桥接 `Graph` 和 V1 文档。
- `ModuleRegistry` 仍是全局 singleton。

下个版本规格要求 hard cutover：

- 不把旧项目文档兼容当作 runtime requirement。
- 旧 schema 只允许通过显式 import/conversion command 处理。
- 删除 graph source-of-truth 假设。
- 删除 `ipcraft.noc.project.v1` generator path。
- 删除全局可变 `ModuleRegistry`。
- 删除 NoC implementation package hardcoded UI/core checks。

这意味着下个版本不是简单兼容升级，而是架构边界收敛：保留必要转换入口，但运行时只接受新契约。

## 10. 建议优先落地的改进

按 `.specify` 的计划，下个版本第一阶段不应直接重写 UI 或迁移某个具体 NoC implementation generator。更合理的优先级是：

1. 建立 `ipcraft.project.v1`、`ipcraft.package.v1`、`ipcraft.component.v1`、`ipcraft.tool.input.v1`、`ipcraft.patch.v1` 的公共 schema 和 contract tests。
2. 实现最小 `ProjectDesign` value model 和 deterministic roundtrip。
3. 建立 `ProjectPatch` / `PatchApplier` 接口，先覆盖 add component、set config、add connection、set layout 等基础操作。
4. 建立 `ResolutionService` 接口和 config provenance 数据结构。
5. 增加 architecture scan，阻止 core 引入 Qt Widgets、NoC implementation package id 特判、Graph-as-root。
6. 添加最小 public examples：UART、CPU -> NIC -> NoC、AnyNet explicit graph、blackbox Verilog。
7. 在 UI 层只做 adapter，不立即重写整个 NodeEditor；让旧画布逐步变成 `ProjectDesign` 的 projection。

## 11. 汇报结论

当前版本已经完成了从旧 NoC 图编辑器到契约驱动架构的第一步：项目、package、flow、emitter、artifact、diagnostics 已经具备 V1 雏形，CLI/headless core 和 Qt UI 也已经开始分离。

`.specify` 里的下个版本是在此基础上的第二次架构收敛：把项目根彻底切到 `ProjectDesign`，把 mutation 统一到 `ProjectPatch`，把工具统一到 `ipcraft.tool.input/result`，把配置检查和 provenance 做成产品能力，并把具体 NoC implementation package 从 core 中完全移出。

因此，下个版本的关键改进不是增加几个功能，而是建立可长期扩展的 IpCraft 架构边界：项目语义独立于画布，package 能力独立于 Qt 插件，工具协议独立于 `.fpproj` 和 UI，具体 NoC implementation package 独立于 core，验收独立于实现细节。
