# Finepaper 可配置多 Domain 架构基线

> 状态：Domain V2+ 与 Element Configuration V3 实现契约。
>
> 本文定义 Finepaper 对 clock、power 以及 Package 自定义 Domain 的公共表达。Router 仍由 Mesh 派生，用户不能创建、删除或任意连线 Router；可配置的是 Domain 类型、实例、归属、关系和 crossing 策略。

## 1. 边界：固定 Mesh，开放 Domain

Finepaper 当前只支持规则 `rows × columns` Mesh。Router、相邻 Router Link 和稳定 ID 都由拓扑确定：

- Router ID 为 `r-x-y`；
- Router Link ID 为 `link-r-x-y--r-x-y`；
- 用户不能增加 Router、删除 Router 或建立任意 Router Link；
- Endpoint 是设计实例，可以挂载到 Mesh Router；
- Router/Endpoint 在画布上的位置是 Workspace 布局，不改变设计语义。

Domain 不受 clock/power 两个名字限制。Package 可以声明 `clock`、`power`、`security`、`voltage`、`vendor.dtc`、`vendor.dn` 等任意 Type，Finepaper Core 不增加相应字符串分支。

## 2. Design V2+ 的五层 Domain 模型

Domain 数据属于 `NocDesign`，参与 dirty、undo、save、validate 和 generate 生命周期。从 `finepaper.noc-design` V2 开始固定包含五组顶层数组；V3 及后续受支持版本继续完整保留它们：

1. `domains`：Domain 实例；
2. `domainMemberships`：Router/Endpoint 的归属；
3. `domainRelations`：实例之间的 typed 关系；
4. `crossingPolicies`：同一 Type 两个实例之间的默认 crossing 策略；策略 `id` 全局唯一，且规范朝向 `(domainType, from, to)` 实例对只能有一个默认策略；
5. `edgeOverrides`：单条派生边的策略覆盖。

对应公共结构：

~~~cpp
struct DomainDefinition {
    QString id;
    QString type;
    QString name;
    QJsonObject properties;
};

struct ElementRef {
    ElementKind kind; // Router/Endpoint/RouterLink/EndpointAttachment
    QString id;
};

struct DomainMembership {
    ElementRef element;
    QHash<QString, QStringList> assignments; // type -> Domain ids
};

struct DomainRelation {
    QString type;
    QString from;
    QString to;
    QJsonObject properties;
};

struct DomainCrossingPolicy {
    QString id;
    QString domainType;
    QString from;
    QString to;
    QJsonObject properties;
};

struct DomainEdgeOverride {
    ElementRef edge; // router-link | endpoint-attachment
    QString domainType;
    QString policy;
    QJsonObject properties;
};
~~~

一个元素可以同时属于多种 Type。每种 Type 的归属数量由 Package 的 `cardinality` 决定，因此 assignments 的值始终是数组，不能把协议锁死为单值。

~~~json
{
  "format": "finepaper.noc-design",
  "formatVersion": 2,
  "domains": [
    {
      "id": "clk_fast",
      "type": "clock",
      "name": "Fast clock",
      "properties": { "frequencyMHz": 1200 }
    },
    {
      "id": "trusted",
      "type": "security",
      "name": "Trusted zone",
      "properties": {}
    }
  ],
  "domainMemberships": [
    {
      "element": { "kind": "router", "id": "r-0-0" },
      "assignments": {
        "clock": ["clk_fast"],
        "security": ["trusted", "debug-visible"]
      }
    }
  ],
  "domainRelations": [],
  "crossingPolicies": [],
  "edgeOverrides": []
}
~~~

Membership 是稀疏设计意图，不保存完整 Router 图。空 membership、空 assignment 数组、未知元素和未知 Domain 都是错误。

## 3. Package V2+ 声明可编辑能力

从 Package Manifest V2 开始必须显式提供 `domainTypes` 数组，空数组表示该 Package 不开放 Domain。V3 是累积扩展，不会关闭或缩减 Domain 能力。每个 Type 可以配置：

- `id`、`label`；
- `appliesTo`：`router`、`endpoint` 或两者；
- `cardinality`：`single` 或 `multiple`；
- `required`：适用元素是否必须归属；
- `defaultInstance`：可选的 required Domain 自动创建脚手架；
- `properties`：Domain 实例属性 schema；
- `relations`：允许从该 Type 发出的关系 schema；
- `crossingProperties`：crossing policy 与 edge override 的属性 schema。

~~~json
{
  "format": "finepaper.noc-package",
  "formatVersion": 2,
  "domainTypes": [
    {
      "id": "clock",
      "label": "Clock domain",
      "appliesTo": ["router", "endpoint"],
      "cardinality": "single",
      "required": true,
      "defaultInstance": {
        "id": "clock-main",
        "name": "Primary fabric clock",
        "properties": {
          "frequencyMHz": 1000
        }
      },
      "properties": [
        {
          "id": "frequencyMHz",
          "label": "Frequency",
          "type": "number",
          "required": true,
          "default": 1000,
          "minimum": 0
        }
      ],
      "relations": [
        {
          "id": "derived-from",
          "label": "Derived from",
          "targetTypes": ["clock"],
          "cardinality": "single",
          "required": false,
          "properties": [
            { "id": "divider", "type": "integer", "minimum": 1 }
          ]
        }
      ],
      "crossingProperties": [
        {
          "id": "implementation",
          "type": "enum",
          "values": ["sync", "async-fifo"],
          "required": true
        }
      ]
    },
    {
      "id": "security",
      "label": "Security zone",
      "appliesTo": ["router", "endpoint"],
      "cardinality": "multiple",
      "required": false,
      "properties": [
        {
          "id": "parent",
          "type": "string",
          "referenceDomainType": "security"
        }
      ],
      "relations": [],
      "crossingProperties": []
    }
  ]
}
~~~

Domain property 沿用基础 scalar 类型、枚举和数值范围，同时增加：

- `required`：实例中必须出现该属性；
- `multiple`：值是同类型数组；
- `referenceDomainType`：字符串或字符串数组必须引用指定 Type 的 Domain 实例；
- `default`：可选；存在时由创建操作物化。

普通 Package 参数仍要求 default。Domain property 则允许没有 default，因为很多属性只能由用户或外部 Engine 决定。
`referenceDomainType` 属性不能把设计内实例 ID 写成 Manifest default；Package 尚未声明实例，创建流程也不能用魔法字符串猜测引用目标。

声明可编辑 schema 还不等于运行时真正消费这些数据。Package V2/V3 因此必须同时显式声明五个独立的运行时能力，不能用一个含糊的 `supportsDomains` 开关掩盖部分实现：

~~~json
{
  "runtimeCapabilities": {
    "domainConfiguration": {
      "domains": true,
      "memberships": true,
      "relations": true,
      "crossingPolicies": true,
      "edgeOverrides": true
    }
  }
}
~~~

五个字段都是必填 boolean，并采用严格未知字段检查。`memberships`、`relations` 和 `crossingPolicies` 依赖 `domains`；`edgeOverrides` 同时依赖 `domains`、`memberships` 与 `crossingPolicies`，因为 override 只能绑定由 assignment 推导出的 crossing。`false` 是明确的“不消费”承诺，而不是待猜测的默认值。设计中只要填充了对应数据面，执行级 Validate 与 Generate 就必须 fail closed；仅用于编辑草稿的结构校验仍可继续工作，让用户有机会修复或迁移配置。

`defaultInstance` 只允许出现在 `required: true` 的 Domain Type 上，并且只在创建
请求没有显式 `domainConfiguration` 时使用。它是一个严格对象，只允许 `id`、
`name` 和 `properties`：

- `properties` 先合并属性 schema 的 `default`，再由 `defaultInstance.properties`
  覆盖；显式声明后的结果必须满足完整 Domain property schema；
- reference property 可以在这里引用另一个 required Type 的 resolved
  `defaultInstance.id`，因为这些实例由同一次原子创建共同物化；
- 未声明属性、错误类型/范围/枚举、未知或类型不匹配的引用、重复 scaffold ID
  都会让 Package 加载失败；
- optional Domain Type 声明 `defaultInstance` 会被拒绝，避免存在永远不执行的配置。

为兼容现有 Package，required Type 省略 `defaultInstance` 时，Package parser 会把
旧约定解析成 canonical scaffold：ID 为 `<type>-default`，名称来自 Type label，
属性来自 schema defaults。该兼容规则只存在于 Package 解析层；Application 只消费
resolved scaffold，不再拼接 ID、猜测名称或自行收集默认属性。

## 4. Core 与 Package Validation 的职责

Core 只保证与具体产品无关的结构不变量：

- Design/Package 版本严格匹配，旧版本不能携带未来字段；
- Domain ID 非空且全局唯一；
- membership 只引用 Mesh 派生 Router 或已有 Endpoint；
- relation、policy 和 override 的引用存在且类型一致；
- override 只引用派生 Router Link 或 Endpoint attachment；
- Router/Link 不成为可编辑持久实体。

Finepaper Application 根据当前 Package 保证声明式约束：

- Domain Type 存在；
- properties、relation properties 和 crossing properties 满足 schema；
- `appliesTo`、`cardinality`、`required` 得到满足；
- reference property 指向正确 Type；
- relation type、target type、数量和 required 约束有效；
- add/remove/resize 等操作不会留下悬空设计意图。

下列产品规则继续留在 Package Validator 或 IP Engine：

- CDC 需要多少同步级或采用何种 FIFO；
- power crossing 的 isolation、level shifter 和时序规则；
- Domain 是否必须矩形、连续或覆盖特定专有节点；
- 厂商资源、credit、地址、QoS 和 RTL 映射。

Application 在调用 Package 进程前核对 `runtimeCapabilities.domainConfiguration`。如果运行时没有声明能力，或某个已填充的数据面被明确声明为 `false`，Package Validate/Generate 都不会被调用，并返回定位到相应 Design/Manifest 路径的结构化诊断。这样 GUI 能展示完整可编辑模型，同时不会出现“界面保存成功、生成器静默忽略”的伪支持。

## 5. Crossing 是派生事实，策略是设计意图

Router Link 和 Endpoint attachment 都由 Mesh 与挂载关系派生。对某一 Domain Type，如果边两端的 assignment 集合不同，则产生 crossing view。多归属 Type 比较规范化后的集合，而不是假设只有一个 Domain。

`crossingPolicies` 表示两个 Domain 实例之间的默认实现意图；`edgeOverrides` 只在某条边需要不同处理时使用。策略 `id` 是全局稳定引用，同一规范朝向 `(domainType, from, to)` 只能声明一个默认策略。这里的 `from/to` 不是单向 traffic channel，而是派生物理边的稳定朝向：Router Link 使用 west→east 或 north→south，Endpoint Attachment 使用 Router→Endpoint。一个 policy/override 必须描述这条双向物理边界的完整处理；Package 若需要不同的正反向参数，应在自己的 crossing property schema 中显式声明两组属性。反向实例对只用于另一条规范朝向上 Domain 分布相反的物理边，不能被误解为同一条链路还必须再绑定第二个 policy。

当前 `edgeOverrides` 只支持 singleton crossing：规范化后的 `fromDomains` 与 `toDomains` 必须各自恰好包含一个实例，所引用 policy 的 `from/to` 必须与这两个实例精确、同向匹配。`multiple` cardinality 仍可以产生并展示 set-valued crossing，但只要任一侧为空或包含多个实例，绑定 override 会以 `domain_edge_override.unsupported_set_crossing` 明确失败，Core/Application 不会从集合中猜选一个实例对。

未来若要让 set-valued crossing 可配置，需要先定义无歧义的集合策略模型，例如显式的实例对矩阵、集合级 policy 或确定的组合/优先级规则，并通过相应的 Design 格式演进开放；不能复用当前单个 policy 引用来隐式代表整个集合关系。

Generator/Engine 接收当前版本的完整 Design。Core 不把 clock、power 或厂商 Type 转成内部特例字段。bundled V3 runtime 会把五组数据编译为确定排序的 `*_domain_constraints.json`：其中包含 Domain 实例、成员、关系、策略、单条边覆盖以及由固定 Mesh/Endpoint attachment 推导出的双向物理 crossing 与最终生效属性。Validate 与 Generate 复用同一编译路径；缺失策略、规范朝向不匹配、无效或未使用的 override 都必须在执行前失败，不能退化成 Design JSON 的原样复制。

`runtimeCapabilities` 的 `true` 表示 Package 进程会校验并消费该数据面，消费目标可以是实现本身，也可以是 Package 明确声明的下游约束 artifact；它不自动等价于“主 RTL 已经插入所有 CDC/isolation 单元”。当前 bundled V3 已将 timing-domain 归属落实为逐 Domain clock、本地同步释放 reset，以及每条异步物理边两个定向 ready/valid FIFO，并通过生成后结构、lint 与双时钟双向仿真验证。Generator 同时输出 `*_domain_implementation_evidence.json`，把实际层级与 typed plan 逐项关联；Power isolation、level shifter 与 derived-clock relation 尚未物化时必须列为 deferred，且 `completePlan=false`，不能静默省略或生成占位逻辑。

## 6. 应用层操作

GUI、CLI 和未来 API 都只能通过 `FinepaperApplication` 修改 Domain：

- `replaceDomainConfiguration`；
- `addDomain`；
- `updateDomain`；
- `removeDomain`；
- `patchDomainAssignments`（同时表达 exact replacement、ensure-present 和 ensure-absent）；
- `assignDomainsToElements` / `clearDomainAssignment` 作为现有兼容入口，内部仍复用同一 Application 校验边界。

五组 Domain 数据可以通过公共 `DomainConfiguration` 一次性提交：

~~~cpp
struct DomainConfiguration {
    QVector<DomainDefinition> domains;
    QVector<DomainMembership> domainMemberships;
    QVector<DomainRelation> domainRelations;
    QVector<DomainCrossingPolicy> crossingPolicies;
    QVector<DomainEdgeOverride> edgeOverrides;
};
~~~

`DomainConfiguration` 只是五组既有数组的内存 aggregate，不是额外的持久化字段；保存时仍展开为 `domains`、`domainMemberships`、`domainRelations`、`crossingPolicies` 和 `edgeOverrides`。

`replaceDomainConfiguration` 是 aggregate transaction：先在候选 Design 上整体替换五组数据，再执行 Core 与 Package 全量校验；任何结构、引用、schema 或 required 约束失败时返回原 Design，不暴露半更新状态。Router 和 Router Link 仍由 Mesh 派生，配置只能通过稳定引用选择它们，不能携带或创建 Router/Link 实体。

GUI 的完整编辑器持有 `DomainConfigurationDraft`。每条草稿记录使用只存在于编辑会话的单调 token 定位，因此损坏旧设计中的重复 relation/policy/override 也能逐条修复；token 不是持久化字段。草稿 reducer 不逐条运行权威校验，允许 required relation、self/cyclic reference 等互相依赖的对象按任意顺序组装。只有最终 Apply 调用 `replaceDomainConfiguration`，所以 Design 永远不会暴露半套配置。

支持 Domain 的 Package（V2+）在 `createDesign` 请求中可以显式携带完整配置。`domainConfiguration` 必须是 object，并显式包含全部五个数组；解析复用当前 Design 版本的严格 JSON codec：

~~~json
{
  "package": { "id": "vendor.noc", "version": "2.0.0" },
  "name": "configured-noc",
  "domainConfiguration": {
    "domains": [],
    "domainMemberships": [],
    "domainRelations": [],
    "crossingPolicies": [],
    "edgeOverrides": []
  }
}
~~~

显式配置存在时，Application 不再补 required 默认实例，也不合并 Domain property default；该配置是权威的完整快照，必须自行满足 Package 约束。未提供时继续执行 Package-driven required 默认物化。Package V1 请求出现 `domainConfiguration` 时必须明确失败。

简单 required Type 可以继续使用默认物化作为脚手架；若 required reference/relation 令脚手架无法独立成立，GUI 应把尚未成功创建的候选 Design 交给同一个完整编辑器，并把最终五数组作为显式 `domainConfiguration` 重新提交。Application 不应为某个 clock/power 关系编造隐式对象或顺序规则。

required Type 可以在支持 Domain 的 Package 中用 `defaultInstance` 显式声明脚手架的 ID、名称和属性。Application 只消费 Package parser 解析后的 canonical scaffold，不再拼接 ID、猜名称或收集默认属性。为兼容旧 Package，省略该字段时仍由 Package parser（而不是 Application）解析为 `<type>-default`、Type label 和属性 schema defaults；这是受控的 manifest 兼容规则，不是 clock/power 特例。

批量 assignment 必须原子执行：任一元素或 Domain 不合法时，整次操作不修改设计。修改 Domain 的稳定 `id/type` 不应通过普通 update 偷偷重写引用；需要独立 rename/migrate 操作时再增加显式 API。

创建支持 Domain 的设计时，Application 可以按 Package schema 物化 required Type 的默认实例与归属，但不能按 `clock`、`power` 名称分支。Endpoint 删除应清理其 membership、attachment override 和 attachment element configuration。Mesh 缩小若会令 Endpoint 悬空必须作为硬阻塞；若会删除被裁剪 Router/RouterLink 的 membership、override 或 element configuration，则预览必须完整列出原记录，并要求调用方原样回传精确确认。缺失、额外或已陈旧的确认都应原子失败，不能使用宽泛的 `allowDataLoss` 开关，也不能静默丢数据。

## 7. GUI 交互目标

Domain GUI 采用 feature slice，而不是继续堆入通用 `src/gui`：

- `src/features/domain/` 保存完整 Workspace、Domain Manager、五页编辑器、schema 表单以及 presentation/projection；
- `src/ui/common/` 保存跨 feature 与 GUI shell 复用、但不包含业务语义的 Qt 控件；
- `src/gui/` 只保存 MainWindow、Workbench 注册、画布、主题和功能装配；
- `src/application/` 保存不依赖 Qt Widgets 的草稿事务、操作和权威校验；
- `src/noc/` 保存持久化模型与 Mesh 派生不变量。

MainWindow 只负责注册和切换 Domain Workspace，不拥有五页编辑状态。后续 Endpoint、Topology、Package 等复杂能力可以沿用 `src/features/<name>`，避免重新形成一个巨型 GUI 目录。

- Domain Manager 按 Package schema 创建、编辑和删除任意 Type 实例；
- 完整配置工作台同时提供 Instances、Memberships、Relations、Crossing Policies、Edge Overrides 五个 section，并持续显示整份草稿的 DRC；
- 行级 Dialog 只负责局部字段与 schema 形状，完整草稿可以暂时无效；最终 Apply 必须是最新一次整体校验成功的快照；
- 缺失 Application validator 时必须 fail closed；DRC 不得静默截断，关闭、Cancel 或 Revert 未 Apply 的五页草稿前必须显式确认；
- Domain reference 在完整工作区允许输入 future/self/mutual ID，最终由 Package 校验引用存在性与 Type；普通快速编辑器仍使用严格候选列表；
- policy properties 使用 Complete schema，edge override properties 使用 Partial schema；Partial 只持久化差异，不复制默认 policy 值；
- `Color by: None / <Domain Type>` 切换画布图层；
- Legend 显示颜色、名称、缩写和成员数，不能只靠颜色；
- Router/Endpoint 多选后可以批量赋值，混合值显示 `Mixed`；
- 新建或断开后重连 Endpoint 时，先按 Package 中所有 `appliesTo: endpoint` 的 Type 收集 membership；required 多实例必须显式选择，optional/multiple 原值必须在重连时保留；
- Mesh resize 只改变 topology spec，Router/RouterLink 始终由 Mesh 投影；新增 Router 逐个收集 Package-driven assignment，缩容则展示并精确确认会删除的 Router membership 与 RouterLink override；
- crossing 使用独立线型或 badge，Inspector 显示两端集合、默认 policy 和 edge override；
- Domain 颜色、当前图层和面板状态属于 Workspace presentation；
- Domain 实例、归属、关系、policy 和 override 属于 `NocDesign`。

切换 Domain 图层或提交 DomainConfiguration 只刷新 Domain presentation，不应清空重建整个 NodeEditor。Router 仍然是不可创建/删除/改 ID/任意连线的 Mesh 投影；Membership 只能选择这些派生 Router 和现有 Endpoint，Override 只能选择派生 RouterLink/EndpointAttachment，Domain UI 不改变这一边界。

## 8. Design/Package V3 的任意元素配置

Domain membership 回答“这个 Router/Endpoint 属于哪个 clock、power、security 等逻辑区域”；普通 element configuration 回答“这个具体 Router、Mesh Link 或 Endpoint Attachment 使用什么实现参数”。两者不能复用同一记录，否则 buffer、pipeline、routing 等局部参数会被错误解释成 Domain crossing 策略。

Package V3 在完整保留 `domainTypes` 的同时，必须显式提供 `elementPropertySets` 数组。Set ID 和 property ID 都由 Package 定义，Core 不认识 `bufferDepth`、`pipelineStages`、`virtualChannels` 等产品字符串：

~~~json
{
  "format": "finepaper.noc-package",
  "formatVersion": 3,
  "domainTypes": [],
  "elementPropertySets": [
    {
      "id": "fabric.microarchitecture",
      "label": "Fabric microarchitecture",
      "appliesTo": ["router", "router-link"],
      "properties": [
        {
          "id": "pipelineStages",
          "type": "integer",
          "default": 2,
          "minimum": 0,
          "maximum": 8
        }
      ]
    },
    {
      "id": "initiator.attachment",
      "appliesTo": ["endpoint-attachment"],
      "endpointTypes": ["initiator"],
      "properties": [
        {
          "id": "implementation",
          "type": "enum",
          "default": "registered",
          "values": ["combinational", "registered"]
        }
      ]
    }
  ]
}
~~~

Property 支持 integer、number、boolean、string、enum、数值范围和 `multiple` 数组。每个 property 必须声明 default，因此 Mesh 扩容可以直接继承 Package baseline，而不需要为每个新 Router/Link 物化重复记录。Endpoint 自身的可配置参数继续归 `EndpointInstance.parameters` 所有；`elementPropertySets` 不允许直接应用到 Endpoint，避免出现两个互相竞争的参数 owner。

### Endpoint 参数与类型迁移

Endpoint 配置继续使用 `endpointTypes[].parameters` 作为唯一 schema，Application 在创建时物化 Package 默认值，创建后以原子操作替换完整的 `EndpointInstance.parameters`。通用表单只解释 `ParameterDefinition`，不能识别 `dataWidth`、`protocol`、`bufferDepth` 等具体 ID。Parameter schema 可提供 `description`、`unit`、`category` 和 `advanced` 展示元数据；这些字段只影响通用编辑器的说明与分组，不参与硬件语义或 Core 条件判断。

Endpoint 类型切换不是简单修改 `type` 字符串，而是显式的 preview/confirm/apply 生命周期：

- `ResetToDefaults` 从目标类型默认值开始；
- `PreserveCompatible` 只保留目标 schema 中同 ID 且仍满足类型、范围和枚举约束的旧值，其余值回落到目标默认值；
- 调用者可以在迁移结果上提供稀疏 parameter patch，但最终仍必须得到满足目标类型 schema 的完整参数对象；
- 以 `endpointTypes` 过滤且不再适用于目标类型的 Endpoint Attachment configuration 必须进入精确 impact preview，只有调用者回传完全一致的记录后才允许删除；
- Domain membership 当前按 `ElementKind::Endpoint` 适用，因此类型切换保留 membership，并由完整设计验证再次确认；Application 不根据 Endpoint 类型名字猜测 clock、power 或其他 Domain；
- Endpoint Attachment 的 crossing override 与 Endpoint 参数属于不同 owner，类型切换不会静默重写 crossing state。

创建 Endpoint 的 UI 应在一次清晰的配置流程中展示 ID、Package 声明的类型、Endpoint 参数和需要用户决策的 Domain assignment。若 required/single Domain 只有唯一可用实例，则 Application/UI 可直接采用 Package 驱动的确定结果，不应弹出没有选择价值的对话框。

Design V3 必须显式包含 `elementConfigurations`。持久化值是相对 Package default 的稀疏 delta，而不是完整参数副本：

~~~cpp
struct ElementConfiguration {
    ElementRef element;      // Router | RouterLink | EndpointAttachment
    QString propertySet;     // Package-defined stable id
    QJsonObject properties;  // only values different from defaults
};
~~~

唯一键为 `(ElementRef, propertySet)`。设置为 default 会移除对应 key；最后一个 override 被清除时移除整条记录。读取 effective values 时使用 `Package defaults + sparse delta`。所有 set/clear 操作先在候选 Design 上完成 Core 与 Package 全量校验，失败必须返回原 Design。

Router 和 RouterLink 仍不是持久化实体。它们的 ID 来自 Mesh 投影，element configuration 只能引用这些稳定 ID，不能创建 Router、删除 Router、改变 Router ID 或自由 rewiring。生命周期规则如下：

- Mesh 扩容：新 Router/Link 继承 Package default，不新增记录；
- Mesh 缩容：准确预览会删除的 Router/Link configuration，并要求 exact confirmation；
- Endpoint 移动或断开后重连：Attachment stable ID 不变，configuration 随草稿保存并恢复；
- Endpoint 永久删除：同时删除其 EndpointAttachment configuration；
- Domain crossing 引起的 synchronizer/isolation 等策略继续属于 `crossingPolicies`/`edgeOverrides`；普通链路 pipeline/buffer 等属于 `elementConfigurations`。

## 9. 版本与迁移

- Design V1 不允许出现任何五组 Domain 字段或 `elementConfigurations`，即使值是空数组；
- Design V2 必须显式提供全部五组 Domain 数组，并拒绝 `elementConfigurations`；
- Design V3 必须显式提供全部五组 Domain 数组和 `elementConfigurations`；
- Package V1 不允许出现 `domainTypes` 或 `elementPropertySets`；
- Package V2 必须显式提供 `domainTypes` 与五项 `runtimeCapabilities.domainConfiguration`，并拒绝 `elementPropertySets`；
- Package V3 必须显式提供 `domainTypes`、五项 `runtimeCapabilities.domainConfiguration` 与 `elementPropertySets`；
- `replaceDomainConfiguration` 与 create request 的 `domainConfiguration` 支持所有具备 Domain capability 的 Design/Package（当前为 V2/V3），且五个数组必须全部显式存在；
- 未知更高版本必须失败关闭，不能读入后以旧版本覆盖保存。

V1 -> V2 迁移是显式应用操作：根据 Package required Type 创建默认实例、为所有适用 Router/Endpoint 物化归属、运行 Core 与 Package validation，最后由用户确认保存。

V2 -> V3 迁移同样是显式操作：保留五组 Domain 数据，加入空 `elementConfigurations`，并绑定同 ID/version 或用户明确选择的 V3 Package。不能把 V2 Package 猜测升级成某个 V3 property schema。

## 10. 实施顺序

1. Design/Package V2 Domain 与严格 JSON/Manifest；
2. Domain Application validation、默认物化、typed 单个/批量操作；
3. crossing/resolved projection 与 Generator/Engine 输入；
4. Design/Package V3 element property schema 与 sparse configuration；
5. Application 原子编辑、Mesh/Endpoint 生命周期和 Inspector；
6. clock/power 示例 Package 的 create -> validate -> generate 纵向闭环；
7. 再接复杂 Package/IP Engine，验证厂商 Domain DRC 和实现映射。

其中“纵向闭环”分成两个可验证阶段：先把 normalized Design 与派生 Mesh crossing 编译为 Package-owned typed implementation plan，再由 RTL/约束 renderer 逐项物化。前一阶段的输出必须包含 Domain/实体绑定、关系、每条物理边的有序 stage、正反向参数与策略来源；后一阶段必须回报每个 stage 的实际 artifact/hierarchy evidence。仅复制 Design 或只输出 crossing constraints 都不能声称已经实现 CDC/电源意图。

## 11. 复杂 NoC 参考带来的能力要求

对 `/home/bnl/dev/some_else/ipcore` 的只读检查表明，复杂交付会同时记录 Router 的 clock、DTC、DN 等归属，在跨 clock Domain 的 Link 上配置同步/异步实现，并提供独立图层与分领域 DRC。Finepaper 因此需要通用的多 Domain、分层可视化、结构化诊断和进程外 Engine 边界。

参考实现直接证明的是 clock、DTC、DN 等模型，并不等于 Finepaper 只能支持这些名称。power、security、voltage 以及其他 Package Type 必须由同一 schema 表达，也不能假称为对参考交付的原样复制。

## 12. 硬编码边界

减少硬编码不等于删除所有稳定协议字段。以下内容是 Finepaper 公共格式和当前产品边界，可以固定并由测试保护：

- 五组 Domain 数据面的 JSON 字段名；
- `router`、`endpoint`、`router-link`、`endpoint-attachment` 四种公共引用 kind；
- 矩形 Mesh、Router/Link 稳定 ID 和派生规则；
- Package process 的 validate/generate 输入输出协议。

以下内容不得出现在 Core、Application 或通用 GUI 的条件分支中：

- `clock`、`power`、`voltage`、`security` 等 Domain Type ID；
- `frequencyMHz`、`isolation`、`levelShift` 等属性 ID；
- 某个厂商节点、资源、crossing 实现或 DRC 的魔法字符串；
- 根据特定 Package ID 改变通用编辑行为的例外路径。

这些产品语义只能由 Package schema、Package Validator/Generator 或 IP Engine 拥有。bundled `finepaper-noc` runtime 中的参数映射可以是该 Package 的显式适配代码，因为它已经离开公共 Application 边界；如果多个 Package 反复出现同一映射，再把它提升为有版本的公共 schema，而不是先在 Application 中加临时分支。

bundled V3 Package 使用有版本的 `runtime/domain-realization.json` 声明这种适配。通用 realizer 只理解 role、typed binding、recipe kind、selector、stage order 和来源核对，不包含 `clock`、`power` 或具体属性 ID 分支。Package 新增自定义 Domain Type 时可以任意扩展编辑 schema，但若未同时给出完整 realization mapping，Validate/Generate 必须失败关闭；不能静默忽略为“只影响 UI”。生成的 `*_domain_implementation.json` 是确定性实现计划，不等同于已经生成的 CDC、UPF 或 SDC；实际完成范围以 renderer 输出的 `*_domain_implementation_evidence.json` 为准。当前 clock async FIFO 已有 hierarchy evidence，Power/derived-clock 项仍明确 deferred。
