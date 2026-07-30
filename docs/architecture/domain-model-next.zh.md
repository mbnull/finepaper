# Finepaper 可配置多 Domain 架构基线

> 状态：Domain V2 实现契约。
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

## 2. Design V2 的五层 Domain 模型

Domain 数据属于 `NocDesign`，参与 dirty、undo、save、validate 和 generate 生命周期。`finepaper.noc-design` V2 固定包含五组顶层数组：

1. `domains`：Domain 实例；
2. `domainMemberships`：Router/Endpoint 的归属；
3. `domainRelations`：实例之间的 typed 关系；
4. `crossingPolicies`：同一 Type 两个实例之间的默认 crossing 策略；策略 `id` 全局唯一，且有向 `(domainType, from, to)` 实例对只能有一个默认策略；
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

## 3. Package V2 声明可编辑能力

Package Manifest V2 必须显式提供 `domainTypes` 数组，空数组表示该 Package 不开放 Domain。每个 Type 可以配置：

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

## 5. Crossing 是派生事实，策略是设计意图

Router Link 和 Endpoint attachment 都由 Mesh 与挂载关系派生。对某一 Domain Type，如果边两端的 assignment 集合不同，则产生 crossing view。多归属 Type 比较规范化后的集合，而不是假设只有一个 Domain。

`crossingPolicies` 表示两个 Domain 实例之间的默认实现意图；`edgeOverrides` 只在某条边需要不同处理时使用。策略 `id` 是全局稳定引用，同一有向 `(domainType, from, to)` 只能声明一个默认策略，反向实例对是另一个独立策略。这样无需给每条普通边持久化重复配置，同时仍能精确表达例外，也不会出现多个“默认值”之间的隐式优先级。

当前 `edgeOverrides` 只支持 singleton crossing：规范化后的 `fromDomains` 与 `toDomains` 必须各自恰好包含一个实例，所引用 policy 的 `from/to` 必须与这两个实例精确、同向匹配。`multiple` cardinality 仍可以产生并展示 set-valued crossing，但只要任一侧为空或包含多个实例，绑定 override 会以 `domain_edge_override.unsupported_set_crossing` 明确失败，Core/Application 不会从集合中猜选一个实例对。

未来若要让 set-valued crossing 可配置，需要先定义无歧义的集合策略模型，例如显式的实例对矩阵、集合级 policy 或确定的组合/优先级规则，并通过相应的 Design 格式演进开放；不能复用当前单个 policy 引用来隐式代表整个集合关系。

Generator/Engine 接收完整 Design V2。Core 不把 clock、power 或厂商 Type 转成内部特例字段。

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

`DomainConfiguration` 只是五组既有数组的内存 aggregate，不是 Design V2 的第六个持久化字段；保存时仍展开为 `domains`、`domainMemberships`、`domainRelations`、`crossingPolicies` 和 `edgeOverrides`。

`replaceDomainConfiguration` 是 aggregate transaction：先在候选 Design 上整体替换五组数据，再执行 Core 与 Package 全量校验；任何结构、引用、schema 或 required 约束失败时返回原 Design，不暴露半更新状态。Router 和 Router Link 仍由 Mesh 派生，配置只能通过稳定引用选择它们，不能携带或创建 Router/Link 实体。

GUI 的完整编辑器持有 `DomainConfigurationDraft`。每条草稿记录使用只存在于编辑会话的单调 token 定位，因此损坏旧设计中的重复 relation/policy/override 也能逐条修复；token 不是持久化字段。草稿 reducer 不逐条运行权威校验，允许 required relation、self/cyclic reference 等互相依赖的对象按任意顺序组装。只有最终 Apply 调用 `replaceDomainConfiguration`，所以 Design 永远不会暴露半套配置。

Package V2 的 `createDesign` 请求可以显式携带完整配置。`domainConfiguration` 必须是 object，并显式包含全部五个数组；解析复用 Design V2 的严格 JSON codec：

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

required Type 可以在 Package V2 中用 `defaultInstance` 显式声明脚手架的 ID、名称和属性。Application 只消费 Package parser 解析后的 canonical scaffold，不再拼接 ID、猜名称或收集默认属性。为兼容旧 Package，省略该字段时仍由 Package parser（而不是 Application）解析为 `<type>-default`、Type label 和属性 schema defaults；这是受控的 manifest 兼容规则，不是 clock/power 特例。

批量 assignment 必须原子执行：任一元素或 Domain 不合法时，整次操作不修改设计。修改 Domain 的稳定 `id/type` 不应通过普通 update 偷偷重写引用；需要独立 rename/migrate 操作时再增加显式 API。

创建 Package V2 设计时，Application 可以按 Package schema 物化 required Type 的默认实例与归属，但不能按 `clock`、`power` 名称分支。Endpoint 删除应清理其 membership 和 attachment override。Mesh 缩小若会令 Endpoint 悬空必须作为硬阻塞；若只会删除被裁剪 Router 的 membership 或被裁剪 RouterLink 的 override，则预览必须完整列出原记录，并要求调用方原样回传精确确认。缺失、额外或已陈旧的确认都应原子失败，不能使用宽泛的 `allowDataLoss` 开关，也不能静默丢数据。

## 7. GUI 交互目标

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

## 8. 版本与迁移

- Design V1 不允许出现任何五组 V2 Domain 字段，即使值是空数组；
- Design V2 必须显式提供全部五组数组；
- Package V1 不允许出现 `domainTypes`；
- Package V2 必须显式提供 `domainTypes` 数组；
- aggregate `replaceDomainConfiguration` API 与 create request 的 `domainConfiguration` 仅支持 Design V2；create request 同时要求 Package V2，且五个数组必须全部显式存在；
- 未知更高版本必须失败关闭，不能读入后以旧版本覆盖保存。

V1 -> V2 迁移是显式应用操作：根据 Package required Type 创建默认实例、为所有适用 Router/Endpoint 物化归属、运行 Core 与 Package validation，最后由用户确认保存。

## 9. 实施顺序

1. Design V2、Package V2、严格 JSON/Manifest 与模型测试；
2. Application schema validation、默认物化、typed 单个/批量操作；
3. crossing/resolved projection 与 Generator/Engine 输入测试；
4. Domain Manager、图层、Legend、批量编辑和 crossing Inspector；
5. clock/power 示例 Package 的 create -> validate -> generate 纵向闭环；
6. 再接复杂 Package/IP Engine，验证厂商 Domain DRC 和实现映射。

## 10. 复杂 NoC 参考带来的能力要求

对 `/home/bnl/dev/some_else/ipcore` 的只读检查表明，复杂交付会同时记录 Router 的 clock、DTC、DN 等归属，在跨 clock Domain 的 Link 上配置同步/异步实现，并提供独立图层与分领域 DRC。Finepaper 因此需要通用的多 Domain、分层可视化、结构化诊断和进程外 Engine 边界。

参考实现直接证明的是 clock、DTC、DN 等模型，并不等于 Finepaper 只能支持这些名称。power、security、voltage 以及其他 Package Type 必须由同一 schema 表达，也不能假称为对参考交付的原样复制。
