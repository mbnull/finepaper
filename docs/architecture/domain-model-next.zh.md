# Finepaper 多 Domain 下一阶段设计基线

> 状态：下一阶段实现前的架构基线。
>
> 本文描述 clock、power 以及 Package 自定义 Domain 的公共表达方式。它不把任何厂商产品的专有 DRC 或链路实现写入 Finepaper Core。

## 1. 为什么 Domain 不能只是节点上的一个字符串

复杂 NoC 同时存在多种互相独立的 Domain。一个 Router 可以属于 `clock=clk0`，同时属于 `power=pd1`，还可能属于 Package 专有的 `vendor.dtc=dtc2`。因此：

- 每个节点需要按 Domain Type 分别归属；
- 每种 Type 下最多归属一个 Domain 实例；
- Router Link 和 Endpoint attachment 是否跨域是派生事实；
- 跨 clock domain 可能要求异步传输级；
- 跨 power domain 可能要求 isolation、level shifter 或其他 Package 能力；
- 连通性、允许的形状、边界器件和实现选择属于 Package Validator/IP Engine。

本地复杂 NoC 参考配置也体现了这一点：Router 具有多套独立 Domain 归属，跨 clock domain 的链路配置会随之变化，DRC 还会检查 Domain 连通性和特定节点覆盖。Finepaper 应抽象“定义、归属和 crossing”，而不是复制厂商节点图和规则。

## 2. 公共模型

Domain 是设计意图，必须进入 `NocDesign`，不能只保存在 GUI Workspace，也不应塞进不透明的 `packageData`。

建议在 `finepaper.noc-design` V2 中增加：

~~~cpp
struct DomainDefinition {
    QString id;          // 设计内稳定且唯一，例如 clk0、pd_cpu
    QString type;        // Package 声明的 type，例如 clock、power、vendor.dtc
    QString name;        // 用户可读名称
    QJsonObject parameters;
};

enum class ElementKind {
    Router,
    Endpoint
};

struct ElementRef {
    ElementKind kind;
    QString id;          // Router 使用稳定的 r-x-y，Endpoint 使用 Endpoint id
};

struct DomainMembership {
    ElementRef element;
    QHash<QString, QString> byType; // Domain type -> Domain id
};
~~~

对应 JSON 形态：

~~~json
{
  "formatVersion": 2,
  "domains": [
    { "id": "clk0", "type": "clock", "name": "Clock 0", "parameters": { "frequencyMHz": 1000 } },
    { "id": "pd0", "type": "power", "name": "Always on", "parameters": {} }
  ],
  "domainMemberships": [
    {
      "element": { "kind": "router", "id": "r-0-0" },
      "byType": { "clock": "clk0", "power": "pd0" }
    },
    {
      "element": { "kind": "endpoint", "id": "cpu_0" },
      "byType": { "clock": "clk0", "power": "pd0" }
    }
  ]
}
~~~

采用稀疏 membership，而不是把完整 Router 图持久化。Router 和 Router Link 仍由 Mesh 派生；只有用户指定的归属进入设计文件。

第一版不引入隐式继承。Router 与 Endpoint 的归属都应在规范化设计中明确，这样 GUI、CLI、Validator 和 Generator 不会各自猜测“Endpoint 是否继承 Router”。如果产品体验需要继承，创建/移动操作可以确定性地物化默认归属。

## 3. Package 声明 Domain Type

Domain Type 由 Package 声明，Core 不维护不断增长的字符串分支：

~~~json
{
  "domainTypes": [
    {
      "id": "clock",
      "label": "Clock domain",
      "elementKinds": ["router", "endpoint"],
      "required": true,
      "parameters": [
        { "id": "frequencyMHz", "type": "number", "default": 1000, "minimum": 0 }
      ]
    },
    {
      "id": "power",
      "label": "Power domain",
      "elementKinds": ["router", "endpoint"],
      "required": true,
      "parameters": []
    }
  ]
}
~~~

`clock` 和 `power` 是推荐的公共 Type ID；`vendor.dtc`、`vendor.dn` 等产品语义使用带命名空间的 ID。Package Manifest 只声明可编辑结构和基础约束，不发展成跨字段规则 DSL。

## 4. Crossing 是派生事实

`TopologyProjection` 应保留语义化的节点和边引用，并派生：

~~~cpp
struct DomainCrossingView {
    QString edgeId;
    QString domainType;
    QString fromDomain;
    QString toDomain;
};
~~~

边包括 Router Link 和 Endpoint attachment。两端在某个 Domain Type 下归属不同，即形成 crossing。

Core 只负责确定性识别 crossing。以下规则不进入 Core：

- clock crossing 需要多少 sync/async stage；
- power crossing 使用何种 isolation 或 level shifter；
- Domain 必须是矩形、象限或连通区域；
- 某类 Domain 必须包含何种专有节点；
- 厂商链路资源如何生成。

这些规则由 Package Validator 或 IP Engine 返回结构化诊断；Generator 根据规范化设计和 crossing 结果生成实现。

## 5. Core 必须保证的不变量

Finepaper Core 负责：

- Domain ID 非空且唯一；
- Domain Type 已由当前 Package 声明；
- Domain 参数符合 Package 定义；
- membership 引用存在的 Router/Endpoint 和 Domain；
- 同一元素对同一 Type 最多一个归属；
- required Type 在规范化设计中没有缺失；
- resize Mesh 或 remove Endpoint 不会静默留下悬空 membership；
- JSON round-trip 完整保留 domains 和 memberships；
- crossing 投影确定且与 GUI/CLI 共用。

应用层提供有限 typed 操作：

- `addDomain` / `updateDomain` / `removeDomain`；
- `assignDomain` / `clearDomainAssignment`；
- `assignDomainToElements` 批量操作；
- 带 Domain 冲突检查的 `resizeMesh`、`moveEndpoint` 和 `removeEndpoint`。

GUI 不直接改 JSON，也不在画布层维护另一份 Domain 真相。

## 6. GUI 交互

- 画布提供 `Color by: None / <Domain Type>`，一次只激活一种 Type；
- 节点填充表示 Domain，Router/Endpoint 身份继续由形状和标题表达；
- 选择状态使用独立外轮廓，不能与 Domain 颜色混用；
- Legend 同时显示颜色、名称、缩写和成员数，避免只依赖颜色；
- crossing 边使用独立线型或 badge，并能在 Inspector 查看两端归属；
- Router/Endpoint 多选后可批量分配，混合值显示 `Mixed`；
- 当前着色 Type、颜色偏好和 Legend 状态属于 Workspace；
- Domain 定义和 membership 属于 `NocDesign`，纳入 dirty/undo/save 生命周期。

NodeEditor 需要先把“拓扑数据、Workspace 布局、presentation 样式”拆开。切换 Domain Type 只刷新 presentation，不应清空并重建整张图。

## 7. 版本与迁移

Domain 顶层字段必须伴随 `formatVersion=2` 和显式 V1 -> V2 migration。不能让旧版本打开未来文件后在保存时静默删除 Domain 数据。

迁移可以由 Package 提供默认 Domain：

1. 创建每个 required Type 的默认 Domain；
2. 将全部 Router 和 Endpoint 归入默认 Domain；
3. 运行 Core 与 Package validation；
4. 用户确认后保存 V2。

## 8. 实施顺序

1. 先收敛当前字符串协议、dirty 状态、严格 Manifest/JSON 解析和语义化 edge metadata；
2. 增加 V2 Domain 模型、序列化、迁移和 Core 验证；
3. 增加应用层单个/批量归属操作及 resize/remove 冲突处理；
4. 增加 GUI 的 active Domain Type、Legend、批量编辑和 crossing 展示；
5. 由简单测试 Package 验证 clock/power 主链路；
6. 再接入复杂 Package/IP Engine，验证 CDC、power crossing 和厂商 Domain DRC。

整个阶段继续维持 create -> validate -> generate 的完整闭环。

## 9. 参考能力与后续路线图

对 `/home/bnl/dev/some_else/ipcore` 的只读检查给出了三个直接证据：测试 YAML 和创建脚本为 Router 同时记录 `clock_domain`、`dtc_domain`、`dn_domain`，并在跨 clock domain 的链路上插入 sync/async MCS；HTML 视图可以独立切换 clock、DTC、DN 图层；交付中的 DRC 又按 clock domain、DN、Mesh、设备、credit、RN-SAM 等领域拆分。复杂 NoC 工具因此不只是“画拓扑并生成 RTL”，还需要分层可视化、结构化 DRC、资源与地址意图，以及可演进的厂商执行边界。

参考交付直接证明的是 clock、DTC、DN 等模型，不包含名为 `power_domain` 的一等模型。power domain 是 Finepaper 面向多类 NoC/IP 的公共需求，不能表述成对该参考实现的照搬。

建议按以下优先级推进：

| 优先级 | 能力闭环 | 归属 |
| --- | --- | --- |
| P0 | Design V2/migration、typed Domain 与 membership、crossing 投影、Manifest `domainTypes`、应用层批量归属、GUI 图层/Legend/多选，以及 clock/power 测试 Package | Core + Application + 通用 GUI + Package 描述 |
| P1 | 扩展进程协议以支持复杂 Engine 的 validate/import/export/analyze、结构化日志与产物；先用 mock Engine 验证，再接入 CMN 一类 adapter | Package Runtime + IP Engine |
| P2 | 地址区域与协议绑定、QoS/traffic/performance intent、credit/resource 约束、分析报告和更丰富的诊断定位 | 公共 intent 模型 + Package/IP Engine 语义 |

P0 必须先做出可保存、可编辑、可验证、可生成的完整纵向切片，不先建立一个只有 GUI 颜色、没有模型和验证语义的 Domain 演示层。P1/P2 的产品规则不得回流到 `FinepaperApplication` 的字符串判断或节点类型分支中。
