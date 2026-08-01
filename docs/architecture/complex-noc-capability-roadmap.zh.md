# 复杂 NoC 能力差距与阶段路线

> 状态：2026-08-01 只读审计快照。
>
> 本文只回答“现在真正完成了什么、还差什么、下一步先做什么”。公共模型与完整协议分别见[多 Domain 架构基线](domain-model-next.zh.md)和[运行时 Package 目标架构](package-driven-target-architecture.zh.md)。

## 1. 判断口径

本文使用三种状态，避免把“能保存字段”说成“硬件已经实现”：

- **已闭环**：当前仓库存在可验证的编辑或输入、保存、校验、运行消费以及与声明相符的产物/证据链；
- **已有通用模型，Runtime 未完整物化**：Core、Application 或通用 UI 已能表达意图，但 Package Runtime 仍有 deferred 项，或仅生成约束/计划而未形成完整硬件行为；
- **缺失**：尚无足以承载该能力的稳定公共契约，不能靠 `packageData` 或一个字符串参数冒充完成。

参考目录 `/home/bnl/dev/some_else/ipcore` 是 CMN700 交付与测试树，只用于识别复杂 NoC 的能力形状，不作为 Finepaper Core 的对象模型。重点证据包括：

- `tests/cmn700_r2_12x12_dn.yml` 中的 global/type/instance/interface 分层参数、异构设备、固定 Mesh 端口以及 `clock_domain`、`dtc_domain`、`dn_domain`；
- `tests/cmn700/testbench/` 中的 address block、ID map、寄存器定义和验证序列；
- `tests/cmn700/ipxact/`、`mbist_files/`、生成 RTL、UPF、testbench 等多类交付物；
- RTL 中可观察到 SAM、协议适配、QoS/MPAM、debug/PMU/trace 等产品语义。

## 2. 不变边界

Router 不开放为自由图节点。当前产品边界继续固定为矩形 Mesh：

- 用户只配置 `rows × columns`，Router、相邻 Router Link、方向端口和稳定 ID 均由 Mesh 投影派生；
- 用户不能创建、删除、改 ID 或任意连线 Router；
- Endpoint 是可创建实例，可以挂载、断开、重连或移动到某个派生 Router；画布坐标只属于 Workspace；
- Package 可以为派生 Router、Link、Endpoint attachment 声明属性和 Domain 归属，但不能借此改变 Mesh 连通关系。

这里的“任意 Package-driven Domain”也有明确含义：Package 可以声明任意 Type ID、标签、属性、关系、适用元素、基数和 crossing schema；Finepaper 通用代码不按 `clock`、`power`、`security` 等名字分支。它不意味着 Finepaper 会凭字段名自动发明 CDC、隔离、供电或厂商 DRC。每种实际 lowering 必须由 Package Runtime 的有版本映射、renderer 或 IP Engine 提供，并产出完成或 deferred 证据。

## 3. 当前差距矩阵

| 能力 | 状态 | 当前证据与准确边界 | 下一闭环条件 |
|---|---|---|---|
| 固定 Mesh、派生 Router/Link、Endpoint 挂载 | **已闭环** | Design 保存尺寸与 Endpoint 意图；Core 派生稳定拓扑；Application 覆盖 resize、add/move/remove；Router 不进入自由编辑模型 | 继续以回归测试保护，不扩成任意图 |
| Package 发现、版本绑定、Validate/Generate、结构化诊断与 artifact 清单 | **已闭环** | Catalog、进程协议、隔离 run 目录、日志、结果解析和 artifact 路径约束已存在 | 增加复杂 Package conformance 套件，而不是改协议捷径 |
| 全局参数与 Endpoint 类型参数 | **已闭环（扁平 schema）** | 默认值、类型/范围/枚举、通用表单、实例参数与 Runtime 消费均存在 | 保持基础 scalar schema 小而稳定 |
| 任意 Domain Type 的实例、membership、relation、policy、override | **已闭环（通用意图层）** | Design V2/V3、Package `domainTypes`、Application 原子校验、五面 Workspace、crossing 投影和 fail-closed capability 检查均不依赖产品 Type 名 | 用第三种自定义 Type 做独立 conformance fixture，持续证明无名称分支 |
| Clock Domain RTL | **已闭环（当前 V3 Package 范围）** | Package mapping、逐 Domain clock、本地 reset release、双向 async ready/valid FIFO、结构/仿真及 implementation evidence 已存在 | 扩展 recipe 时仍要求逐项 hierarchy evidence |
| Power Domain / Power Intent | **已有通用模型，Runtime 未完整物化** | 已有 Power Type、Package-owned extension/compiler、UPF/plan/evidence；组合 CDC+Power 边、基础设施供电归属、安全关断和 power-aware routing 仍明确 deferred | 补充基础设施 ownership、组合边 lowering、关断/连通性证明和 EDA 语义验证回执 |
| DTC、DN、security、voltage 等自定义 Domain | **已有通用模型，Runtime 未物化** | 通用模型和 UI 可承载任意名称；当前 bundled Runtime 没有这些产品的完整 mapping/renderer | 每个 Package 显式声明 realization 与 Engine DRC；未映射必须失败，不能静默忽略 |
| Router/Link 局部实现参数 | **已有通用模型，Runtime 未完整物化** | V3 `elementPropertySets` 和 sparse override 已有；当前 Router forwarding shell 会读取部分配置，但不构成完整 routing/VC NoC | 建立资源、pipeline、VC、route 的真实 RTL 与证据闭环 |
| global → node type → node instance → interface 的分层参数 | **缺失** | 当前全局、Endpoint 和 element schema 分属不同平面，尚无继承、override 来源、依赖条件与批量矩阵的统一契约 | 先定义层级、优先级、effective value 与来源追踪，再做通用表格编辑器 |
| 异构 node type、固定端口与嵌套 interface/adapter | **缺失** | Endpoint type 只能覆盖基础端点实例；尚不能表达 CMN 一类 HNF/RND/HND/CCG 及其 p0/p1、ADB/CAL 等组合 | Package 声明 node/interface taxonomy；实例只能挂到 Mesh 固定 attachment 位，不开放 Router rewiring |
| 地址区域、SAM、目标 ID 与路由表 | **缺失** | `addrWidth` 或 `routingAlgorithm` 只是参数，不是地址/路由语义模型；当前 RTL 也明确不是完整 routing 实现 | 建立 typed region/target/map、重叠与覆盖 DRC、确定性 route/SAM artifact 和可追溯 ID map |
| 协议适配与接口协商 | **缺失** | 当前 `protocol=axi4` 和 NI shell 不足以表达 CHI/AXI/CXL 等接口层级、能力协商和 adapter 参数 | 由 Package 声明 interface profile/adapter，Engine 负责兼容性 DRC 和 RTL lowering |
| QoS/MPAM 与 Security 策略 | **已有通用模型，Runtime 未完整物化（仅载体）** | Endpoint QoS 开关、Domain/extension 可保存部分意图，但没有端到端资源分配、策略冲突 DRC 或完整硬件证据 | 建立 Package-owned typed policy、跨 node 约束、寄存器/RTL 映射与场景验证 |
| Debug、PMU、Trace | **缺失** | 仓库中存在未启用 trace stub，不等于可配置、可路由、可验证的观测子系统 | 定义事件源、计数器/过滤器、trace route、寄存器和软件可见 artifact |
| RTL 与 Domain/Power 证据 artifact | **已闭环（已声明范围）** | 当前 V3 可输出 RTL、constraints、implementation plan/evidence、Power artifacts，且 deferred 不冒充完成 | 把同一 receipt 纪律推广到后续复杂能力 |
| 配置/寄存器、IP-XACT、filelist、ID/address 文档、testbench/UVM/MBIST 等交付 | **缺失** | 参考 IP 展示了完整 collateral 族；Finepaper 当前 artifact 框架能承载，但 bundled Package 未形成相应生成契约 | 先版本化 artifact type/manifest，再由 Package/Engine 分阶段生成与验收 |

## 4. Application “全是硬编码”的实情

`src/application/application.cpp` 当前约 1600 行，观感上的主要问题是**职责集中**，并不是大量厂商语义已经写进 Application。

已经 Package-driven 的内容包括 Mesh 尺寸范围与默认值、全局参数、Endpoint 类型与参数、attachment 容量/slot、Domain schema、element property set、design extension schema、Runtime capability 以及 Generator/Engine 元数据。只读搜索没有发现 Application 根据 `clock`、`power` 或某个 Package ID 改变行为的产品分支。

仍可固定在 Finepaper 的内容是版本化 Design/Create JSON 字段、诊断路径、`mesh` 产品边界、稳定 Router/Link ID、validate/generate 进程协议和运行目录安全规则。这些是公共协议与不变量，不应为了“零字符串”改成动态配置。

真正需要整理的是同一文件同时承担了：

- Create Request JSON 解析与默认物化；
- Core/Package 校验编排；
- Endpoint、Mesh、Domain、element、extension 用例入口；
- Package validate/generate 进程执行、日志和 artifact 收据。

下一步应做等价拆分，而不是重写行为或制造通用规则语言：

```text
src/application/design_creation/     Create Request codec + materialization
src/application/design_validation/   Core/Package validation orchestration
src/application/design_operations/   Mesh/Endpoint/Domain/element use cases
src/execution/package_operations/     validate/generate runner + receipt
```

`FinepaperApplication` 保留稳定 facade；GUI、CLI 与未来 API 继续只调用它。产品字段映射放在 Package Runtime，只有被多个 Package 证明稳定的概念才提升为公共 schema。

## 5. 阶段优先顺序

### P0：守住真实能力与当前交互

1. 固化固定 Mesh/可移动 Endpoint/可删除 attachment 的交互回归；Router 操作明确显示为只读派生项。
2. 增加不叫 clock/power 的自定义 Domain Package fixture，覆盖创建、五面编辑、保存重载、Validate、Generate 或“未映射失败”全链路。
3. 所有结果页区分 intent、plan、materialized、deferred、not-performed；旧成功产物不能覆盖新失败结果。
4. 按上述目录做无行为变化的 Application 拆分，每一小步单独测试、review、commit。

### P1：完成 Domain Runtime 纵向闭环

1. 先补 Power 基础设施 ownership、CDC+Power 组合边和 switchable Router 的安全关断/连通性证据。
2. 将 realization conformance 做成 Package 安装时或 CI 可运行的契约测试：schema 中每个 Type/property/relation/crossing value 都必须 mapped、explicitly deferred 或 rejected。
3. 用 DTC/DN 或 security fixture 验证 Package/Engine 可以增加新 Domain，而 Finepaper Core、Application 和通用 UI 无需改代码。

### P2：复杂节点与分层参数

1. 定义 global/type/instance/interface 四层 parameter override 与 effective-value provenance。
2. 引入 Package-owned node/interface taxonomy、固定 attachment port 与 adapter 子树；Router 仍由 Mesh 派生且不可 rewiring。
3. UI 先提供可搜索表格、批量编辑、默认值/override 来源和 DRC 定位，再考虑更丰富的图形表达。

### P3：地址、SAM 与真实路由

1. 建立 address region、target、ID map、SAM/routing policy 的 typed schema。
2. 形成覆盖/重叠/不可达/容量 DRC，以及确定性 route/SAM/ID artifacts。
3. 只有真实 RTL、配置和验证证据齐全后，才把 Router routing/VC 标记为完成。

### P4：协议、QoS/Security 与可观测性

按实际 Package 需求依次补 interface adapter、QoS/MPAM、Security、Debug/PMU/Trace；复杂算法和厂商规则进入进程外 Engine，不进入 Application 条件分支。

### P5：交付与验证矩阵

扩充 artifact contract，覆盖 RTL/filelist、配置与寄存器、IP-XACT、地址/ID 文档、testbench/UVM、lint/仿真/形式检查和可选 MBIST。每类产物都带 Package 版本、输入摘要、工具版本和完成/deferred 回执。

## 6. 文字优先 UI 原则

- 主操作默认使用清晰动词文字：`创建 Endpoint`、`断开连接`、`应用 Domain 配置`、`验证`、`生成`；不提供只有图标、必须猜含义的按钮。
- 图标最多作为文字旁的冗余提示，不作为唯一语义、状态或错误载体。
- 状态直接写成 `已应用`、`有未保存修改`、`Runtime 未物化`、`生成失败`，不能只依靠颜色或符号。
- 危险操作写明对象和影响数量；Domain/参数密集编辑优先使用搜索、表格、分组和批量操作。
- 画布只承担 Mesh、attachment、Domain 图层和 crossing 的空间理解；大批量配置与 DRC 细节使用文字面板，不把所有能力塞进节点图标。

阶段完成的判据不是“页面上出现了字段”，而是相应的 Package conformance、Application 回归、Runtime artifact/evidence 和 UI 可用性检查全部通过，并形成一个可回滚的阶段提交。
