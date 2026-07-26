# Finepaper 运行时 NoC Package 目标架构

> 状态：目标架构讨论基线。
>
> 本文从产品目标出发描述 Finepaper 的新架构，不以现有代码、已有 ADR、旧 schema、旧插件系统或迁移兼容为设计约束。现有实现只可作为业务行为和已有生成能力的参考。

## 1. 文档目标

Finepaper 的目标不是构建一个通用 IP/SoC 组合平台，而是帮助用户理解、配置、创建并生成 NoC。

本文回答以下问题：

- 一份 NoC 设计保存什么；
- Finepaper 通用能力和 NoC Package 各自负责什么；
- 简单 Package 如何在不编译 Finepaper 的情况下运行时加载；
- CMN700 一类复杂 Package 如何附加 IP Engine；
- GUI、CLI 和第三方 API 如何共享同一个应用层；
- 验证、生成、日志和产物如何形成完整闭环；
- 未来页面和复杂能力应扩展在哪里。

本文不描述旧架构如何迁移，也不要求保留旧类型、旧目录或旧接口。

## 2. 产品目标

Finepaper 面向两类主要使用方式。

### 2.1 可视化创建 NoC

用户能够：

1. 选择一个运行时安装的 NoC Package；
2. 选择拓扑类型和 N×M 尺寸；
3. 设置 NoC 全局参数；
4. 在创建时或创建后，将 Endpoint 挂载到指定 Router；
5. 在拓扑页面理解 Router、链路和 Endpoint 的关系；
6. 验证设计；
7. 生成 RTL、配置文件、filelist、报告等产物。

### 2.2 CI 和脚本批量生成

自动化系统能够：

1. 使用 JSON 文件描述 NoC；
2. 显式指定 Package、N×M、参数和 Endpoint 挂载；
3. 以无界面方式验证和生成；
4. 读取稳定的结构化诊断和产物清单；
5. 使用退出码判断任务结果。

CLI 是首个需要完成的自动化入口。独立第三方 API 作为同一应用层上的另一个适配器，不重新实现业务逻辑。

### 2.3 成熟工具的参考方式

FlexNoC、Socrates 和 CMN 创建工具可以作为产品体验和复杂度边界的参考，重点学习：

- 从 IP/模板开始创建；
- 引导式设置拓扑和尺寸；
- 用可视化方式理解节点、链路和挂载关系；
- 通过表单和表格编辑大量参数；
- 将 DRC、生成和报告形成连续流程；
- 允许复杂产品使用自己的领域工具。

Finepaper 不要求复制这些软件的内部对象模型，也不把它们的专有语义抽象成通用规则系统。

## 3. 范围与非目标

### 3.1 初始范围

- 一份设计文件表示一个 NoC；
- 一份设计绑定一个 NoC Package；
- 第一类拓扑以 N×M Mesh 为主；
- Router 和 Router 链路由拓扑参数自动派生；
- 用户显式配置 Endpoint 及其挂载位置；
- 支持多个简单 NoC Package；
- 简单 Package 已有或新建的生成器属于正常 Package 能力；
- Package 在运行时发现和加载；
- 复杂 Package 可以额外提供进程外 IP Engine；
- GUI、CLI 和第三方 API 调用同一个 Finepaper 应用层。

### 3.2 暂不作为核心目标

- 任意 SoC/IP 组件组合；
- 任意节点和端口之间的图连接编辑；
- 通用 connection rules 规则语言；
- 通用协议栈建模；
- 内建复杂性能、功耗、物理和时序分析；
- 把 CMN、FlexNoC 或其他厂商产品语义写入 Finepaper Core；
- 运行时加载 QWidget 或 C++ ABI 插件；
- 将厂商的大型 RTL、DSL 或工具链复制进 Finepaper 本体；
- 多用户服务、远程调度和分布式构建。

复杂分析可以在未来作为 Package 附加能力、IP Engine 能力或独立扩展出现，但不进入第一版核心模型。

## 4. 已确认的架构决策

1. Finepaper 拥有一个小而明确的 NoC 通用模型。
2. 一个 Design 对应一个 NoC Package 和一个 NoC 实例。
3. 用户保存的是设计意图，不是展开后的完整 Router/Link 图。
4. N×M 拓扑负责派生 Router 和 Router 链路。
5. Endpoint 在创建时即可指定并挂载到 Router。
6. 不设计通用 connection rules；只提供固定的 Endpoint 挂载语义和简单容量约束。
7. Package 在运行时从目录中发现，不需要重新编译 Finepaper。
8. 简单 Package 使用 Finepaper 的通用页面、通用编辑和通用验证能力。
9. RTL 生成是简单 Package 的标准主流程，不属于可有可无的 extension。
10. 复杂 Package 可以附加进程外 IP Engine。
11. GUI、CLI 和第三方 API 都是 FinepaperApplication 的北向适配器。
12. Generator 和 IP Engine 是 Package Runtime 的南向执行能力。
13. GUI、CLI 和 API 不允许绕过应用层直接调用 Package。
14. 初期不提供进程内 C++ 动态插件。

## 5. 核心概念

| 概念 | 含义 | 所有者 |
|---|---|---|
| NocDesign | 用户保存的 NoC 设计意图 | Finepaper |
| PackageDefinition | Package 提供的拓扑、参数、Endpoint 和执行能力描述 | NoC Package |
| TopologyProjection | 根据 N×M 派生的 Router、Link 和显示数据 | Finepaper |
| NoC NodeEditor | NoC 工作台的核心画布和直接编辑交互 | Finepaper GUI |
| FinepaperApplication | GUI、CLI、API 共用的应用用例入口 | Finepaper |
| PackageCatalog | 运行时发现和索引 Package | Finepaper |
| Generator | 将设计转换为 RTL 和其他交付物 | NoC Package |
| IP Engine | 复杂 IP 可选的领域验证、导入导出和工具适配能力 | 复杂 NoC Package |
| ValidationResult | 通用验证和 Package 验证的统一结果 | Finepaper |
| GenerationResult | 一次生成的状态、日志和产物清单 | Finepaper |
| Workspace | GUI 当前打开设计的交互状态 | GUI |

Package 描述“这个 NoC IP 能配置什么以及如何生成”，Finepaper 描述“用户如何创建、编辑、验证和运行它”。

## 6. 总体架构

~~~mermaid
flowchart TB
    subgraph Northbound["北向入口"]
        GUI["Qt GUI"]
        CLI["CLI"]
        API["Third-party API"]
    end

    subgraph App["Finepaper Application Library"]
        Facade["FinepaperApplication"]
    end

    subgraph Core["NoC 通用核心"]
        Design["NocDesign"]
        Operations["Design Operations"]
        Projection["Topology Projection"]
        CommonValidation["Common Validation"]
    end

    subgraph Runtime["Package Runtime"]
        Catalog["Package Catalog"]
        Loader["Package Loader"]
        Runner["Package Process Runner"]
        EngineClient["Optional IP Engine Client"]
    end

    subgraph Package["Runtime NoC Package"]
        Manifest["package.json"]
        Assets["Assets / Presets / Pages"]
        Generator["Generator"]
        Engine["Optional IP Engine"]
    end

    GUI --> Facade
    CLI --> Facade
    API --> Facade

    Facade --> Design
    Facade --> Operations
    Facade --> Projection
    Facade --> CommonValidation
    Facade --> Catalog
    Facade --> Runner

    Catalog --> Loader
    Runner --> EngineClient

    Loader --> Manifest
    Loader --> Assets
    Runner --> Generator
    EngineClient --> Engine
~~~

依赖方向固定为：

~~~text
GUI / CLI / API
        ↓
FinepaperApplication
        ↓
NoC Core + Package Runtime
        ↓
Generator / IP Engine Process
~~~

Package 不能反向依赖 GUI，Generator 不能读取实时界面对象，页面也不能绕过应用层直接修改设计文件。

## 7. NoC 设计模型

### 7.1 最小模型

NocDesign 只包含用户真正指定的内容：

~~~cpp
struct PackageReference {
    QString id;
    QString version;
};

struct RouterPosition {
    int x = 0;
    int y = 0;
};

struct TopologySpec {
    QString type;      // 第一阶段固定为 mesh
    int rows = 1;
    int columns = 1;
};

struct EndpointAttachment {
    RouterPosition router;
    std::optional<QString> slot;
};

struct EndpointInstance {
    QString id;
    QString type;
    EndpointAttachment attachment;
    QJsonObject parameters;
};

struct NocDesign {
    QString format = QStringLiteral("finepaper.noc-design");
    int formatVersion = 1;
    QString id;
    QString name;
    PackageReference package;
    TopologySpec topology;
    QJsonObject parameters;
    QVector<EndpointInstance> endpoints;
    QJsonObject packageData;
};
~~~

其中：

- parameters 是 NoC 全局参数；
- endpoints 是用户创建的 Endpoint；
- attachment 指定 Endpoint 挂载的 Router；
- slot 通常为空，由 Finepaper 按确定性规则分配本地端口；
- packageData 是复杂 Package 的可选私有数据；
- 第一阶段的简单 Package 不使用 packageData。

packageData 不是新的通用对象模型。Finepaper Core 只负责保存和传递它，只有对应 Package、IP Engine 或受控的 Package 页面能够解释它。

第一阶段对 Endpoint 的产品定义是：

> Endpoint 是由当前 NoC Package 定义、挂载到某个 Router 的 NoC-facing 逻辑接口或逻辑端点。

Endpoint 不表示可以任意组合的 CPU、Memory 或其他外部 SoC IP 实例，也不建立 Endpoint-to-Endpoint 连接图。Package 可以把 NIC、NI 或 adapter 的生成行为映射为某种 Endpoint 类型，但 Finepaper 不因此引入独立 IP Package 引用。

### 7.2 设计文件示例

~~~json
{
  "format": "finepaper.noc-design",
  "formatVersion": 1,
  "id": "demo_noc",
  "name": "Demo NoC",
  "package": {
    "id": "finepaper.ravenoc",
    "version": "1.0"
  },
  "topology": {
    "type": "mesh",
    "rows": 3,
    "columns": 4
  },
  "parameters": {
    "flitDataWidth": 64,
    "virtualChannels": 4,
    "routingAlgorithm": "xy"
  },
  "endpoints": [
    {
      "id": "cpu0",
      "type": "initiator",
      "attachment": {
        "router": {
          "x": 0,
          "y": 0
        }
      },
      "parameters": {
        "dataWidth": 64
      }
    },
    {
      "id": "memory0",
      "type": "target",
      "attachment": {
        "router": {
          "x": 3,
          "y": 2
        }
      },
      "parameters": {}
    }
  ]
}
~~~

### 7.3 不保存在 NocDesign 中的内容

以下内容均为派生结果或运行状态：

- N×M 展开后的 Router 实例；
- Router 之间的 Link；
- 自动分配的 Router 本地端口；
- Router 和 Link 的通用绘制几何；
- 当前选择项、缩放、停靠窗口位置；
- 验证诊断；
- 生成日志；
- RTL 和其他生成产物；
- Generator 或 IP Engine 的进程状态。

这可以避免同一事实被多份模型重复保存和同步。

### 7.4 初始拓扑边界

第一阶段允许用户修改：

- rows 和 columns；
- 全局参数；
- Endpoint 类型、参数和挂载位置。

topology.type 字段保留在文件中，但第一阶段唯一合法值是 mesh。它用于明确文件语义和未来识别，不代表当前需要实现通用拓扑抽象。

第一阶段不允许用户直接添加、删除或连接单个 Router。Mesh 的 Router 和 Link 始终由 N×M 自动派生。

当缩小 N×M 导致 Endpoint 超出范围时，操作必须返回冲突列表，由用户移动或删除 Endpoint，不允许静默丢失。

### 7.5 Mesh V1 确定性规则

第一阶段固定以下规则，GUI、CLI、验证器和 Generator adapter 必须一致：

- 坐标从 0 开始；
- x 表示 column，向右递增；
- y 表示 row，向下递增；
- (0, 0) 是拓扑视图左上角；
- 有效范围为 0 <= x < columns、0 <= y < rows；
- Router ID 为 r-{x}-{y}，例如 r-0-0、r-3-2；
- Router 按 y、再按 x 的 row-major 顺序排列；
- 每个 Router 只向 east 和 south 生成一个邻接 Link，避免重复；
- Link ID 为 link-{from-router-id}--{to-router-id}；
- 一个 Link 表示两个相邻 Router 的逻辑邻接，具体单向/双向通道由 Package Generator 解释。

如果某个 Generator 使用不同坐标方向或命名规则，其 Package adapter 负责转换，不能改变 NocDesign 的公共坐标语义。

自动 slot 的规则为：

- 对同一个 Router 上未显式指定 slot 的 Endpoint，按 Endpoint ID 升序分配 0、1、2……；
- 自动 slot 是派生值，不写回源设计；
- Finepaper 在运行输入副本中物化该值，确保 GUI 验证和 Generator 一致；
- 如果 slot 会成为必须长期稳定的 RTL 接口身份，Package 必须使用 explicit slot 模式，并把 slot 保存到 EndpointAttachment。

## 8. 派生模型与编辑操作

### 8.1 TopologyProjection

TopologyProjection 根据 NocDesign 生成只读的可视化数据：

~~~cpp
struct RouterView {
    QString id;
    RouterPosition position;
};

struct LinkView {
    QString id;
    QString fromRouter;
    QString toRouter;
};

struct EndpointView {
    QString id;
    QString type;
    QString routerId;
};

struct TopologyProjection {
    QVector<RouterView> routers;
    QVector<LinkView> links;
    QVector<EndpointView> endpoints;
};
~~~

它可以被 GUI、预览导出和测试复用，但不是持久化事实源。

### 8.2 明确的设计操作

Finepaper 不需要通用 Action 语言或 JSON Patch。应用层提供有限的 NoC 操作：

- createDesign；
- resizeTopology；
- setGlobalParameter；
- addEndpoint；
- removeEndpoint；
- moveEndpoint；
- setEndpointParameter；
- applyPreset。

每个操作：

1. 接收当前 NocDesign 和明确的参数；
2. 执行结构检查；
3. 返回新的设计或失败诊断；
4. 不直接写文件；
5. 不直接调用 GUI。

GUI 可以在操作外维护 undo/redo。CLI 和 API 使用相同操作，但通常不需要编辑会话。

## 9. FinepaperApplication

FinepaperApplication 是所有北向入口的统一门面。

~~~cpp
class FinepaperApplication {
public:
    PackageList listPackages(const PackageQuery&);
    PackageInfo describePackage(const PackageReference&);
    PackageReloadResult reloadPackages();

    CreateDesignResult createDesign(const CreateNocRequest&);
    LoadDesignResult loadDesign(const QString& path);
    SaveDesignResult saveDesign(const NocDesign&, const QString& path);
    DesignOperationResult resizeTopology(
        const NocDesign&, int rows, int columns);
    DesignOperationResult setGlobalParameter(
        const NocDesign&, const QString& id, const QJsonValue& value);
    DesignOperationResult addEndpoint(
        const NocDesign&, const EndpointInstance&);
    DesignOperationResult moveEndpoint(
        const NocDesign&, const QString& endpointId, RouterPosition);
    DesignOperationResult removeEndpoint(
        const NocDesign&, const QString& endpointId);
    DesignOperationResult setEndpointParameter(
        const NocDesign&,
        const QString& endpointId,
        const QString& parameterId,
        const QJsonValue& value);
    DesignOperationResult applyPreset(
        const NocDesign&, const QString& presetId);

    ValidationResult validate(
        const NocDesign&,
        OperationContext&);

    GenerationResult generate(
        const NocDesign&,
        const GenerationOptions&,
        OperationContext&);
};
~~~

以上 C++ 只是职责示意，不是需要冻结的公共 ABI。第一阶段需要刻意保持稳定的只有跨文件或跨进程边界，例如 .fpnoc、package.json、Generator 输入/result 和 CLI JSON 输出；内部对象可以随实现演进。

FinepaperApplication 表示共享业务入口，不要求第一阶段机械拆出一组一一对应的 Service、DTO 和转发层。初始实现可以由一个普通协调类、NoC 纯函数、小型 PackageLoader 和 ProcessRunner 组成；只有出现真实的独立变化原因时再拆分。

上述编辑接口是有限、明确的 NoC 操作，不是开放式 Action/Command 协议。共用应用层的判断标准是 GUI、CLI 和 API 不复制业务逻辑，而不是所有代码必须经过一个大型 God Object。

### 9.1 OperationContext

长时间运行的验证和生成通过 OperationContext 获得：

- 取消信号；
- 进度回调；
- 日志接收器；
- 操作 ID；
- 超时设置。

OperationContext 不包含 QWidget、窗口指针或前端状态。

GUI 可以在线程池中执行应用调用；CLI 可以同步等待；API 适配器可以将其映射成任务状态。

### 9.2 结果类型

所有入口共享同一种诊断和结果表达：

~~~cpp
struct Diagnostic {
    QString severity;  // info, warning, error
    QString code;
    QString message;
    QString path;
    QString source;    // finepaper, package, generator, engine
};

struct Artifact {
    QString id;
    QString type;
    QString path;
    bool primary = false;
};

struct ExecutionTool {
    QString kind;      // generator or engine
    QString name;
    QString version;
};

struct ValidationResult {
    bool success = false;
    QVector<Diagnostic> diagnostics;
};

struct GenerationResult {
    bool success = false;
    PackageReference package;
    std::optional<ExecutionTool> tool;
    QVector<Diagnostic> diagnostics;
    QVector<Artifact> artifacts;
    QString outputDirectory;
    QString stdoutLog;
    QString stderrLog;
    int exitCode = -1;
};
~~~

取消和超时不引入复杂运行状态机。它们使用 success=false，并分别返回 operation.cancelled 或 operation.timed_out 诊断码；exitCode 在进程未正常退出时保持 -1。

第三方入口不暴露内部 QObject、Package 指针或进程对象。

## 10. Package Runtime

Package Runtime 负责运行时发现、加载和执行 NoC Package。

~~~text
PackageRuntime
├── PackageCatalog
│   ├── scan
│   ├── list
│   ├── resolve
│   └── reload
├── PackageLoader
│   ├── parse package.json
│   ├── validate descriptor
│   └── resolve package-relative paths
├── PackageProcessRunner
│   ├── validate
│   ├── generate
│   ├── timeout/cancel
│   └── log capture
└── IpEngineClient
    └── optional complex validation / vendor adapter
~~~

### 10.1 Package 搜索路径

Package 可以来自：

1. 当前命令显式传入的 package roots；
2. Finepaper 用户配置；
3. FINEPAPER_PACKAGE_PATH；
4. 随应用安装的默认 Package 目录。

Package 以 id 和 version 索引。同一个 id/version 如果在多个搜索路径中产生歧义，必须报告错误，CI 不依赖隐式的随机优先级。

### 10.2 加载和刷新

加载流程为：

1. 扫描 Package 根目录；
2. 找到 package.json；
3. 解析并执行基本完整性检查；
4. 解析 Package 内相对路径；
5. 形成不可变 LoadedPackage；
6. 发布新的 PackageCatalog 快照。

正在进行的验证或生成固定使用启动时解析出的 LoadedPackage。运行时刷新不会改变已经启动的操作。

Package 不通过静态注册表编译进 Finepaper，也不要求在应用启动时链接其代码。

## 11. Package 目录结构

简单 Package 的推荐结构：

~~~text
simple-noc/
├── package.json
├── presets/
├── assets/
├── examples/
└── generator/
    └── bin/
        └── generate
~~~

复杂 Package 可以增加：

~~~text
complex-noc/
├── package.json
├── presets/
├── assets/
├── examples/
├── pages/                 # 可选，未来的声明式页面
├── generator/
│   └── bin/
│       └── generate
└── engine/                # 可选
    └── bin/
        └── ip-engine
~~~

不设置通用 editor 目录。通用编辑能力属于 Finepaper GUI，Package 只提供描述、资源和可选执行能力。

大型厂商 RTL、安装包和受许可证约束的工具可以保留在外部安装位置。Package 中只放轻量适配器，并在运行时检查所需路径和环境变量。

## 12. Package 描述

package.json 的职责是告诉 Finepaper：

- 这个 Package 是谁；
- 支持的 Mesh 尺寸范围；
- 有哪些全局参数；
- 有哪些 Endpoint 类型；
- Endpoint 如何挂载；
- 使用哪个 Generator；
- 是否存在可选 IP Engine；
- 如何显示名称、图标和颜色。

示例：

~~~json
{
  "format": "finepaper.noc-package",
  "formatVersion": 1,
  "id": "finepaper.ravenoc",
  "name": "RaveNoC",
  "version": "1.0",
  "mesh": {
    "rows": {
      "min": 1,
      "max": 16,
      "default": 2
    },
    "columns": {
      "min": 1,
      "max": 16,
      "default": 2
    }
  },
  "parameters": [
    {
      "id": "flitDataWidth",
      "type": "integer",
      "default": 64,
      "minimum": 8,
      "maximum": 512,
      "label": "Flit data width"
    },
    {
      "id": "routingAlgorithm",
      "type": "enum",
      "values": ["xy", "yx"],
      "default": "xy",
      "label": "Routing algorithm"
    }
  ],
  "endpointTypes": [
    {
      "id": "initiator",
      "label": "Initiator",
      "icon": "assets/initiator.svg",
      "parameters": []
    },
    {
      "id": "target",
      "label": "Target",
      "icon": "assets/target.svg",
      "parameters": []
    }
  ],
  "attachment": {
    "target": "router",
    "maxPerRouter": 1,
    "slotMode": "automatic"
  },
  "generator": {
    "name": "ravenoc-generator",
    "version": "1.0",
    "executable": "generator/bin/generate",
    "supportsValidate": true,
    "timeoutSeconds": 300
  },
  "presentation": {
    "color": "#4F7CAC",
    "logo": "assets/logo.svg"
  }
}
~~~

第一阶段不应继续向 package.json 加入条件表达式、工作流 DAG、任意 action、UI 布局语言或跨字段规则 DSL。一个字段如果没有当前 GUI、CLI、验证或生成流程的实际消费者，就不进入 Package 格式。

### 12.1 不提供 connectionRules

Finepaper Core 只理解固定的挂载语义：

- Endpoint 挂载目标是 Router；
- Router 坐标必须存在；
- Endpoint ID 必须唯一；
- 同一个显式 slot 不能冲突；
- maxPerRouter 不能被超过；
- slotMode 只允许 automatic 或 explicit；
- explicit 模式要求每个 Endpoint 保存非空且唯一的 slot；
- 参数必须满足声明的类型和范围。

其他产品特定限制由 Package 的 validate 或 IP Engine 判断，不再发明通用连接规则语言。

## 13. 简单 NoC Package

简单 Package 使用以下 Finepaper 通用能力：

- Package 选择和创建向导；
- N×M 拓扑展开；
- Router/Link 可视化；
- Endpoint Palette；
- Endpoint 挂载和移动；
- 参数表单；
- 通用结构验证；
- 设计文件读写；
- Generator 调用；
- 日志和产物展示；
- CLI 和 API 自动化。

简单 Package 需要提供：

- package.json；
- 可选 preset、示例和资源；
- 可以生成 RTL 的 Generator；
- 可选的 Package 级 validate。

Generator 是 Package 的标准能力，不称为 extension，也不要求 Package 实现 IP Engine。

### 13.1 简单 Package 制作流程

一个简单 NoC 接入 Finepaper 的最短流程是：

1. 创建 package.json；
2. 声明 N×M 范围、全局参数和 Endpoint 类型；
3. 将现有 RTL 生成入口适配为标准 generate 命令；
4. 如有需要，增加 validate 命令；
5. 提供一个最小 .fpnoc 示例；
6. 执行 finepaper package check；
7. 把 Package 目录加入运行时 package roots；
8. 在不重新编译 Finepaper 的情况下完成 create → validate → generate。

Package 作者不需要编写 GUI 页面即可获得完整的通用工作台。

~~~mermaid
sequenceDiagram
    participant User as GUI/CLI/API
    participant App as FinepaperApplication
    participant Core as Common NoC Core
    participant Package as Simple Package
    participant Gen as Generator

    User->>App: create/validate/generate
    App->>Core: common validation
    Core-->>App: diagnostics
    App->>Package: resolve generator
    App->>Gen: validate or generate
    Gen-->>App: result.json + artifacts
    App-->>User: unified result
~~~

## 14. 复杂 Package 与 IP Engine

IP Engine 是复杂 Package 的可选南向能力。它不是 Finepaper 的第二套应用层，也不是 GUI 插件。

适合放入 IP Engine 的内容包括：

- 大量产品特定参数和组合限制；
- CMN700 一类复杂节点、域和接口语义；
- 厂商 DSL、YAML 或数据库导入导出；
- 产品专用 DRC；
- 地址、时钟、电源域等复杂检查；
- 对厂商生成器的适配；
- 复杂设计的派生报告；
- 必须使用 Ruby、Python 或厂商二进制的流程。

IP Engine 不负责：

- Package 扫描；
- NocDesign 文件所有权；
- GUI 主窗口；
- CLI 参数解析；
- 第三方 API 服务；
- Finepaper 通用页面；
- 任务和产物的最终管理。

### 14.1 进程外边界

初始架构规定 IP Engine 以进程外可执行程序接入，而不是共享库：

- 避免 Qt/C++ ABI 绑定；
- 允许使用任意实现语言；
- 可以复用已有厂商工具；
- Engine 崩溃不会直接破坏 Finepaper 进程；
- 可以独立安装和更新；
- 更适合包含许可证环境和外部依赖的工具。

Package 可以声明：

~~~json
{
  "engine": {
    "executable": "engine/bin/cmn700-engine",
    "providesValidation": true,
    "timeoutSeconds": 1800
  }
}
~~~

第一阶段使用一次操作启动一次进程的模型。只有真实复杂 Package 证明启动成本不可接受时，才考虑长驻 Engine Session。

第一阶段不建立通用 capability 协商或 operation 路由表。执行规则固定为：

- generate 始终调用 Package 的 Generator；
- 复杂 Generator 可以在内部转调 IP Engine 或厂商工具；
- validate 优先调用声明 providesValidation 的 IP Engine；
- 没有 Engine validation 时，调用 supportsValidate 的 Generator；
- 两者都没有时，只执行 Finepaper 通用验证；
- Finepaper 不会对同一次 validate 重复调用 Generator 和 Engine。

import、export 和其他 Engine 操作只有在真实复杂 Package 需要时再加入，不预先扩展 package.json。

### 14.2 packageData

复杂 Package 可以把 Finepaper 通用模型无法表达的内容放入 NocDesign.packageData。

约束如下：

- 必须是 JSON；
- 由当前 Package 独占解释；
- Generator 和 IP Engine 接收完整 NocDesign；
- Finepaper 保存但不推断其领域含义；
- 通用页面不能任意写入；
- Package 版本变化时不得静默重解释。

packageData 是受限逃生口，不应被简单 Package 用来重复保存 topology、parameters 或 endpoints。

阶段一至阶段三中，packageData 只允许为空或被动透传，不提供通用编辑接口。阶段四由首个真实复杂 Package 决定是否需要编辑，以及最小的应用层写入操作。

### 14.3 CMN700 参考检查

对本地 CMN700 参考交付中的 `cmn700_r2_12x12_dn.yml` 和创建脚本的只读检查确认，复杂 IP 的实际配置同时包含：

- 12×12 Mesh 的 MXP 实例和逐边链路；
- 每个 MXP 的端口、设备链、设备实例参数；
- clock、DTC、DN 等多个域；
- MCS/异步链路等传输细节；
- 全局、MXP 和不同设备类别的大量参数；
- 厂商 Ruby `cmn_create` API、DRC 与 YAML/HTML 导出。

因此 CMN700 不能被压缩为通用 Endpoint 表格或通用 connection rule，也不应将 YAML 的节点图复制到 Finepaper Core。未来的 `cmn700` Package 应保留通用的 Package 引用、Mesh 尺寸和少量公共摘要；厂商 YAML/DSL、专有 DRC、导入导出及 RTL 调用由其进程外 Engine 负责。实际厂商交付不进入普通 CI，Core 只用 mock Engine 验证该进程边界。

## 15. Generator 与 IP Engine 调用

### 15.1 规范化运行输入

Finepaper 在启动 Package 进程前写一份临时运行输入。它仍使用 finepaper.noc-design 的结构，不建立另一套持久化模型。

运行输入与源设计相比只做确定性补齐：

- 保留源设计的 Package、Mesh、参数、Endpoint 和 packageData；
- 使用 createDesign 时已经物化的 Package 默认参数；
- 为 automatic slot 模式补齐派生 slot；
- 使用固定 Mesh 坐标和 ID 规则进行结构检查；
- 不展开并保存完整 Router/Link 图；
- 不写回源 .fpnoc 文件。

Generator 和 IP Engine 都接收这份规范化副本，从而避免各自重新猜测默认值和自动 slot。

### 15.2 标准命令

简单 Generator 至少支持 generate；推荐同时支持 validate：

~~~bash
generator/bin/generate validate \
  --design /absolute/run/input/design.json \
  --result /absolute/run/result.json

generator/bin/generate generate \
  --design /absolute/run/input/design.json \
  --output /absolute/run/artifacts \
  --result /absolute/run/result.json
~~~

复杂 Package 的 generator 脚本可以转调 IP Engine。FinepaperApplication 对调用方始终返回相同的 ValidationResult 或 GenerationResult。

每个能够生成 RTL 的 Package 必须提供 Generator。validate 可以由 Generator 或 Engine 提供；如果 Package 没有额外 validate，Finepaper 仍执行通用验证。

### 15.3 进程执行规则

PackageProcessRunner 负责：

- 使用绝对路径解析可执行程序；
- 创建独立运行目录；
- 写入只读输入副本；
- 设置工作目录；
- 捕获 stdout 和 stderr；
- 支持超时和取消；
- 记录退出码；
- 读取 result.json；
- 检查产物路径没有逃出输出目录；
- 将进程错误转换成统一诊断。

Generator 和 Engine 不允许直接修改当前打开的设计文件。

### 15.4 Generator 成功条件

一次生成只有同时满足以下条件才成功：

1. 进程成功启动，且未被取消或超时；
2. 进程正常退出，退出码为 0；
3. result.json 存在并且可以解析；
4. result.json 中 success 为 true；
5. 所有声明的 artifact 都存在；
6. 所有 artifact 路径都位于本次运行的 artifacts 目录中；
7. result 中不存在 error 级诊断。

任一条件不满足时，GenerationResult.success 为 false。已有文件可以保留用于诊断，但不能作为成功产物返回。

Generator 最少只需写 success、artifacts 和 diagnostics。Finepaper 在读取并验证后，为最终 GenerationResult 补充 Package、执行工具、outputDirectory、日志路径和 exitCode，不建立额外的 ToolResult/HostResult 协议层。

### 15.5 环境和外部工具

为兼容 EDA 和厂商环境，执行进程默认可以继承启动 Finepaper 时的环境。用户或 CI 可以显式覆盖需要传递的环境变量；Package 只声明必需变量的名称，不保存变量值。许可证密钥或其他敏感值不得写进设计、结果或普通日志。

未来如需运行不受信任的第三方 Package，应另行引入沙箱和信任策略。初始模型将本地安装的 Package 视为受信任工具软件。

## 16. 验证架构

验证按固定顺序执行：

~~~text
1. 设计文件格式检查
2. Package 解析
3. 通用拓扑检查
4. 通用参数检查
5. Endpoint 和挂载检查
6. 按固定优先级调用一个 Package validate 执行方
7. 汇总诊断
~~~

### 16.1 Finepaper 通用验证

Finepaper 负责：

- formatVersion 是否支持；
- format 是否为 finepaper.noc-design；
- Package id/version 是否完整且可解析；
- topology.type 是否为第一阶段唯一支持的 mesh；
- rows/columns 是否在范围内；
- 参数是否存在、类型正确且满足范围；
- Endpoint 类型是否存在；
- Endpoint ID 是否唯一；
- Router 坐标是否有效；
- Router 挂载数量和 slot 是否冲突；
- 第一阶段简单 Package 的 packageData 是否为空；
- 输出目录和运行参数是否合法。

### 16.2 Package 验证

Package 负责：

- 参数之间的组合约束；
- Endpoint 数量和角色限制；
- 特定路由算法限制；
- RTL Generator 自身限制；
- IP 专有 DRC；
- packageData 的结构和语义。

验证不修改设计。修复建议可以作为 Diagnostic 的附加数据返回，但 Finepaper 不自动应用未确认的修改。

## 17. 生成与产物

生成是核心主流程：

~~~text
NocDesign
    ↓
Resolve Package
    ↓
Common Validation
    ↓
Selected Package Validation
    ↓
Create Run Directory
    ↓
Generator
    ↓
Collect result.json and artifacts
    ↓
GenerationResult
~~~

### 17.1 运行目录

每次生成必须使用新的 operation 目录。传给 Generator 的 output 是本次 operation 下的 artifacts 目录，而不是一个可能包含旧文件的共享目录。

推荐布局：

~~~text
output-root/
└── runs/
    └── <operation-id>/
        ├── input/
        │   └── design.json
        ├── artifacts/
        │   ├── rtl/
        │   ├── reports/
        │   ├── scripts/
        │   └── filelists/
        ├── stdout.log
        ├── stderr.log
        └── result.json
~~~

具体 artifact 子目录由 Package 决定。Finepaper 不扫描 output-root 中的既有文件，只验证本次 result.json 明确声明的产物。这样不需要隐式清理，也不会把旧文件误认为本次结果。

### 17.2 Generation Result 示例

~~~json
{
  "success": true,
  "outputDirectory": "/work/build/runs/op-001/artifacts",
  "package": {
    "id": "finepaper.ravenoc",
    "version": "1.0"
  },
  "tool": {
    "kind": "generator",
    "name": "ravenoc-generator",
    "version": "1.0"
  },
  "exitCode": 0,
  "stdoutLog": "/work/build/runs/op-001/stdout.log",
  "stderrLog": "/work/build/runs/op-001/stderr.log",
  "artifacts": [
    {
      "id": "top",
      "type": "rtl",
      "path": "rtl/ravenoc_top.sv",
      "primary": true
    },
    {
      "id": "filelist",
      "type": "filelist",
      "path": "filelists/ravenoc.f",
      "primary": false
    }
  ],
  "diagnostics": []
}
~~~

生成结果记录 Package 和实际执行工具的版本，便于 CI 追踪。可以附加输入摘要，但不需要引入 Revision Graph、对象仓库或 Snapshot Store。

## 18. GUI 架构

GUI 是 FinepaperApplication 的一个适配器，不拥有独立业务模型；但 **NoC NodeEditor 是 GUI 的核心组件**，不能退化为只读的 Topology 页面或表单附属图。

GUI 使用可停靠的 NoC 工作台，而不是把创建、编辑、检查和生成割裂为六个页面：

~~~text
NoC Workbench
├── Toolbar: New / Open / Save / Undo / Redo / Validate / Generate
├── Left Dock: Package 安装、已加载 Package、Endpoint Palette
├── Center View Tabs
│   ├── NoC NodeEditor（默认且常驻的核心编辑页）
│   ├── Performance Analysis
│   ├── Problem Report
│   └── Package 提供的受控分析/报告页
├── Right Dock: Design / Router / Endpoint Inspector
└── Bottom Dock Tabs
    ├── DRC Problems
    ├── Activity Log
    └── Generation Outputs
~~~

用户可以移动、浮动、关闭和重新打开 Dock；布局、中央当前页和底部当前页只写入本机 Workspace 设置。NodeEditor 缩放与选择属于会话状态，不进入 `NocDesign`；是否跨会话恢复由实际使用体验决定。`NocDesign` 仍是唯一持久化设计事实。

中央视图由一个小型 `WorkbenchViewRegistry` 集中管理。NodeEditor 是不可移除的默认视图；性能分析、问题报告和未来 Package 视图按实际能力注册。第一阶段 Registry 只管理页面身份、标题、实例和顺序；图标、延迟创建或可见性只有出现真实需求时再加入，不发展为通用插件框架。

底部区域使用固定的结果分类：DRC Problems 展示结构化诊断并可定位到 NodeEditor 元素；Activity Log 展示运行过程和用户操作；Generation Outputs 展示本次运行目录、stdout/stderr 与 artifact 清单。三者不混成一个纯文本日志框。

### 18.1 NoC NodeEditor

NodeEditor 负责直接操作 NoC，而不是承载通用 IP 图编辑器：

- Mesh Router 和 Router Link 从 `TopologyProjection` 派生并在画布上稳定显示；
- Router 与 Router Link 不可任意创建、删除或连接；
- Endpoint Palette 中的类型可拖到 Router，映射为 `FinepaperApplication::addEndpoint`；
- 已有 Endpoint 可从一个 Router 拖到另一个 Router，映射为 `moveEndpoint`；
- 选择 Endpoint 或 Router 会驱动右侧 Inspector；
- 右键菜单仅提供当前 NoC 语义允许的动作，例如添加、移除、移动 Endpoint；
- 画布支持缩放、平移、框选、自动布局/聚焦和多 Dock 工作流；
- NodeEditor 不保存第二份 Graph，也不允许 Endpoint-to-Endpoint 任意连线。

可复用已有 NodeEditor/QtNodes 的画布、拖拽、缩放、选择和布局能力；不复用其旧的通用 `Graph`、`Module`、任意 Port 连线或 connection-rule 业务绑定。新 NodeEditor 是 `NocDesign` 的专用投影和手势适配器。

### 18.2 创建流程

创建从一个轻量对话框开始：选择或安装 Package、选择 preset 或空白设计、设置 N×M 和少量初始参数。创建完成后立即进入 NodeEditor 工作台；Endpoint 的主要配置路径是 Palette 到 Router 的直接拖放，表格/导入仅作为批量操作补充。

### 18.3 Workspace

GUI Workspace 保存：

- 当前 NocDesign 和文件路径；
- dirty 状态与 undo/redo；
- 当前选择项；
- NodeEditor 缩放、平移和临时选中状态；
- Dock 布局和可见性；
- 正在运行的操作。

保存文件时只写 NocDesign。窗口布局和本机偏好写入用户设置，不进入设计语义。

### 18.4 页面扩展

核心页面由 Finepaper 提供。阶段一至阶段三不实现 Package 特殊页面，也不提供 packageData 编辑器。

只有真实复杂 Package 证明通用页面不足后，阶段五才考虑把 Package 特殊页面注册到中央 View Tabs，并由 GUI 的 PackagePageHost 运行时加载。优先考虑的声明式页面类型是：

- form；
- table；
- report；
- read-only visualization。

这类页面可以读取：

- NocDesign.parameters；
- NocDesign.endpoints；
- NocDesign.packageData；
- 验证结果；
- 生成结果。

如果未来需要写操作，必须先根据真实 Package 定义有限的 FinepaperApplication 操作。页面不能直接写设计文件，也不能直接调用 Generator 或 IP Engine。

初期不加载任意 QWidget。HTML/Web 页面只有在声明式页面无法满足真实需求时再评估，并需要受限桥接接口。

## 19. CLI 架构

CLI 与 GUI 链接同一个 Finepaper Application Library。

推荐命令：

~~~bash
finepaper package list --json
finepaper package describe finepaper.ravenoc --json
finepaper package check /path/to/package --json

finepaper design create \
  --input create-request.json \
  --output demo.fpnoc

finepaper design validate demo.fpnoc --json

finepaper design generate demo.fpnoc \
  --output build \
  --result build/result.json

finepaper run create-request.json \
  --output build \
  --result build/result.json
~~~

--output 指定 output-root，Finepaper 在其下创建唯一 operation 目录；实际 artifacts 路径由 GenerationResult.outputDirectory 返回。

--result 指定一份便于 CI 查找的最终结果副本，不改变 operation 目录内的原始日志和结果。

run 是 CI 便捷入口，顺序执行：

~~~text
create in memory
    ↓
validate
    ↓
generate
    ↓
write result
~~~

### 19.1 CLI 输出纪律

- 不显示窗口；
- 不询问交互式问题；
- --json 时 stdout 只输出 JSON；
- 人类日志写 stderr；
- 完整进程日志写文件；
- 所有路径可以显式指定；
- 失败返回非零退出码；
- result 文件即使失败也尽量写出。

建议退出码：

| 退出码 | 含义 |
|---:|---|
| 0 | 成功 |
| 2 | 参数或输入格式错误 |
| 3 | Package 未找到或加载失败 |
| 4 | 通用设计验证失败 |
| 5 | Package/IP Engine 验证失败 |
| 6 | Generator 执行失败 |
| 7 | 文件系统或产物收集失败 |
| 8 | Finepaper 内部错误 |

## 20. 第三方 API

第三方 API 是另一个北向适配器：

~~~text
Third-party Protocol
        ↓
API Adapter
        ↓
FinepaperApplication
~~~

它不单独实现 Package 解析、验证或生成。

可以提供的操作与 CLI 对齐：

- listPackages；
- describePackage；
- createDesign；
- validateDesign；
- generateDesign；
- queryOperation；
- cancelOperation。

API 的请求和响应语义应与 CLI 对齐，但第一阶段不冻结一套跨 HTTP、C API 和 FFI 的统一 DTO。具体传输格式由首个真实 API 集成场景决定：

- 本地 HTTP 服务；
- Python SDK 调用本地服务；
- C API/FFI；
- 其他进程间通信。

CI 第一阶段可以直接使用 CLI。独立 API 服务只有在第三方系统确实需要长驻服务、并发任务或远程调用时再实现。

无论选择哪种传输方式，API Adapter 必须调用 FinepaperApplication，不能直接执行 Package。

## 21. 设计文件、Preset 与创建请求

### 21.1 .fpnoc

建议使用单一 JSON 文件扩展名 .fpnoc：

- 易于版本控制；
- 易于脚本生成；
- 易于查看和调试；
- GUI、CLI 和 API 可以共享；
- Generator 可以直接接收。

一份 .fpnoc 对应一个 NoC，不嵌入生成产物。

### 21.2 Create Request

CI 可以使用接近 NocDesign 的创建请求：

~~~json
{
  "package": {
    "id": "finepaper.ravenoc",
    "version": "1.0"
  },
  "name": "ci_noc",
  "topology": {
    "type": "mesh",
    "rows": 4,
    "columns": 4
  },
  "parameters": {
    "flitDataWidth": 64
  },
  "endpoints": [
    {
      "id": "cpu0",
      "type": "initiator",
      "router": [0, 0]
    },
    {
      "id": "memory0",
      "type": "target",
      "router": [3, 3]
    }
  ]
}
~~~

CreateDesign 和 applyPreset 会把当前 Package 声明的全局参数及 Endpoint 参数默认值物化到 NocDesign，使新建文件自包含。CLI 的 run 可以不落盘设计文件，也可以通过选项保留生成后的 .fpnoc。

loadDesign 保留文件中的原始值，不因 Package 当前默认值而静默改写。缺失的必需参数在验证时报告；需要补齐或升级时必须由用户显式执行相应操作。

### 21.3 Preset

Preset 是 Package 提供的创建模板，可以包含：

- topology；
- 参数默认值；
- 预设 Endpoint；
- Endpoint 挂载位置；
- 可选 packageData。

Preset 只在创建或用户明确应用时展开，不会持续成为另一个事实源。

## 22. Package 版本与可重复性

NocDesign 显式记录 Package id 和 version。

解析规则：

- 默认要求精确版本；
- CI 必须能够传入明确 package roots；
- 同 id/version 多份安装产生歧义时失败；
- GenerationResult 记录实际 Package 和执行工具版本；
- 不自动把旧 Package 设计解释为新版本。

如果 Package 未安装：

- 文件仍可作为 JSON 打开和查看；
- GUI 可以进入受限只读模式；
- 不能进行 Package 相关编辑、验证和生成；
- 必须给出明确的缺失 Package 诊断。

设计文件格式升级由 Finepaper 提供小范围、显式的格式迁移。Package 私有 packageData 的迁移只有在真实需求出现时由对应 IP Engine 提供，不预先建立复杂迁移框架。

## 23. 生命周期与并发

### 23.1 应用实例

每个 GUI、CLI 或 API 服务创建自己的 FinepaperApplication 实例。应用内部不依赖全局单例，便于测试和未来并发执行。

### 23.2 设计编辑

单个 GUI Workspace 中的 NocDesign 在 UI 线程串行编辑。长时间验证和生成使用工作线程或子进程，输入为启动操作时的设计副本。

编辑中的新变化不会修改已经运行的生成输入。

### 23.3 Package Reload

PackageCatalog 使用不可变快照：

- reload 创建新快照；
- 新操作使用新快照；
- 已运行操作继续使用旧 LoadedPackage；
- 删除 Package 不会使正在执行的进程失去上下文。

### 23.4 取消和超时

取消生成时：

1. 请求子进程正常终止；
2. 超时后终止整个进程树；
3. 保留 stdout、stderr 和已有 result；
4. GenerationResult 使用 success=false 和 operation.cancelled 诊断；
5. 不把部分输出声明为成功产物。

## 24. 错误处理与安全边界

必须处理：

- Package 描述损坏；
- Generator 或 Engine 不存在；
- 外部依赖缺失；
- 进程启动失败；
- 超时、取消和崩溃；
- result.json 缺失或格式错误；
- 产物路径逃出输出目录；
- 输出目录不可写；
- 同版本 Package 冲突；
- 不支持的设计格式；
- Package 版本不匹配。

初始安全模型：

- Package 是本地安装且受信任的软件；
- Package 代码始终在进程外运行；
- Finepaper 校验可执行路径和输出路径；
- 日志中不得主动输出环境秘密；
- API 若提供网络访问，认证和授权必须由 API Adapter 负责；
- Finepaper Core 不加载未知共享库。

## 25. 内部模块边界

推荐新代码结构：

~~~text
src/
├── noc/
│   ├── noc_design
│   ├── topology_projection
│   ├── design_operations
│   ├── common_validation
│   └── diagnostics
├── application/
│   └── finepaper_application
├── package/
│   ├── package_definition
│   ├── package_catalog
│   ├── package_loader
│   └── package_paths
├── execution/
│   ├── package_process_runner
│   ├── ip_engine_client
│   ├── operation_context
│   └── artifact_collector
├── storage/
│   ├── noc_design_json
│   ├── package_json
│   └── result_json
├── gui/
│   ├── workspace
│   ├── pages
│   ├── topology
│   └── main_window
├── cli/
│   └── main
└── api/
    └── adapter                # 未来
~~~

这是一张职责图，不是必须创建同名文件、类或库的 contract。阶段一可以把紧密相关的小结构放在同一模块中；只有代码出现真实的独立变化原因时才拆分。package_page_host 在阶段五出现真实需求前不创建。

建议构建产物：

~~~text
finepaper-application     # 可复用应用库
finepaper-gui             # Qt GUI
finepaper                 # CLI
finepaper-api             # 可选，未来第三方 API 服务
~~~

目录名称可以调整，但依赖方向必须保持：

- noc 不依赖 GUI、CLI、API 或具体 Package；
- application 不依赖 QWidget；
- package 不执行 GUI 操作；
- execution 不解释 NoC 领域；
- GUI、CLI、API 只调用 application；
- Generator 和 Engine 只通过 execution 访问。

## 26. 关键运行流程

### 26.1 启动与 Package 发现

~~~text
Frontend starts
    ↓
Create FinepaperApplication
    ↓
Resolve package roots
    ↓
PackageLoader scans package.json
    ↓
Build PackageCatalog snapshot
    ↓
Frontend lists available NoC Packages
~~~

### 26.2 GUI 创建与生成

~~~text
Select Package
    ↓
Create N×M Design
    ↓
Attach Endpoints
    ↓
Edit Parameters
    ↓
Common + Package Validation
    ↓
Generate
    ↓
Show Logs and Artifacts
~~~

### 26.3 CI 批量生成

~~~mermaid
sequenceDiagram
    participant CI
    participant CLI
    participant App as FinepaperApplication
    participant Catalog as PackageCatalog
    participant Runner as PackageProcessRunner
    participant Tool as Generator/IP Engine

    CI->>CLI: finepaper run request.json
    CLI->>App: createDesign(request)
    App->>Catalog: resolve(id, version)
    Catalog-->>App: LoadedPackage
    App->>App: common validation
    App->>Runner: package validate
    Runner->>Tool: validate
    Tool-->>Runner: diagnostics
    App->>Runner: generate
    Runner->>Tool: generate
    Tool-->>Runner: result.json + artifacts
    Runner-->>App: GenerationResult
    App-->>CLI: structured result
    CLI-->>CI: result.json + exit code
~~~

### 26.4 复杂 IP

~~~text
NocDesign common fields
        +
optional packageData
        ↓
Finepaper common validation
        ↓
IpEngineClient
        ↓
CMN/FlexNoC/vendor adapter
        ↓
Vendor DRC / generator / reports
        ↓
Unified Finepaper result
~~~

## 27. 测试策略

### 27.1 NoC Core

- N×M Router/Link 派生；
- Endpoint 挂载；
- resize 冲突；
- Mesh 坐标、Router ID、Link 顺序和自动 slot；
- 参数类型和范围；
- create 物化默认值、load 不静默改写；
- 设计 JSON round-trip；
- 设计操作确定性。

### 27.2 Package Runtime

- 多搜索路径扫描；
- descriptor 错误；
- id/version 冲突；
- runtime reload；
- 相对路径解析；
- 缺失 Generator；
- operation 固定 Package 快照。

### 27.3 Process Runner

- 成功、失败、超时和取消；
- stdout/stderr 捕获；
- result.json 校验；
- 输出路径越界；
- 旧 output-root 文件不会被收集为本次产物；
- Engine 崩溃；
- 外部依赖缺失。

### 27.4 Package Conformance

提供命令：

~~~bash
finepaper package check /path/to/package --json
~~~

检查：

- package.json；
- 参数和 Endpoint 描述；
- 资源路径；
- Generator 可执行性；
- 最小设计 validate；
- 可选 smoke generation；
- result.json 和 artifact 路径。

阶段二增加同一设计通过 GUI 和 CLI 得到相同验证结果的行为测试。测试重点是事实源和可见行为，不固定内部类数量或调用层级。

### 27.5 参考场景

至少维护：

- 两个简单、能够实际生成 RTL 的 NoC Package；
- 一个支持不同参数或 Endpoint 类型的简单 Package；
- 一个 mock IP Engine Package；
- CMN700 的可选集成测试，不要求普通 CI 携带大型厂商交付。

最早的端到端门槛必须是：

~~~text
runtime load package
    ↓
create 2×2 or 3×4 NoC
    ↓
attach endpoints
    ↓
validate
    ↓
generate real RTL
~~~

## 28. 实施顺序

### 阶段 1：Headless 端到端

- NocDesign；
- 确定性的 Mesh 坐标、Router ID、Link 和 slot；
- 创建时物化全局及 Endpoint 参数默认值；
- PackageCatalog 和 PackageLoader；
- 至少一个现有简单 Package 适配；
- create/validate/generate；
- CLI；
- 真实 RTL 和 result.json。

这一阶段首先证明 CI/脚本能够批量创建并生成 NoC。

### 阶段 2：NodeEditor 工作台

- 保留原工作台式布局和可停靠的 Package Palette、Inspector、底部结果区；
- NodeEditor 作为中央交互组件；
- 中央视图可切换 NodeEditor、性能分析和问题报告；
- 底部结果区可切换 DRC Problems、Activity Log 和 Generation Outputs；
- N×M 创建和 TopologyProjection；
- Endpoint Palette → Router 的拖放和 Router 间移动；
- Inspector 中的参数编辑；
- Validate 和 Generate 的工具栏与结果 Dock；
- GUI 调用同一个 FinepaperApplication。

### 阶段 3：多个运行时 Package

- Package roots 配置；
- reload；
- presets；
- Package conformance；
- 多个简单 NoC 的一致体验。

### 阶段 4：复杂 IP Engine

- IpEngineClient；
- packageData 只读透传；
- mock engine；
- CMN700 轻量适配器验证；
- 厂商工具和外部安装位置处理。

只有真实复杂 Package 无法通过参数和 Endpoint 表达时，才在本阶段增加最小 packageData 编辑操作。

### 阶段 5：受需求驱动的扩展

- 声明式 Package 页面；
- 独立第三方 API Adapter；
- import/export；
- 复杂报告和分析；
- 必要时评估长驻 Engine。

每个阶段都必须维持 create → validate → generate 的完整主链路。

### 阶段评审准则

提出任何新抽象时必须回答：

1. 当前由哪个真实 Package 或用户流程使用？
2. 删除它会破坏哪一项当前阶段验收？

如果两个问题都没有具体答案，默认延后。普通函数、简单结构或直接调用能够完成当前闭环时，不先建立通用框架。

## 29. 架构不变量

后续实现必须保持：

1. GUI、CLI 和第三方 API 只调用 FinepaperApplication。
2. Package 在运行时发现，不通过重新编译应用接入。
3. 简单 Package 不需要 IP Engine。
4. RTL 生成是主流程，不是可选 extension。
5. 一个 NocDesign 对应一个 NoC Package。
6. NocDesign 保存 N×M、参数和 Endpoint 挂载，不保存派生 Router/Link。
7. 第一阶段 Core 只实现确定性的 Mesh V1，不预建通用拓扑系统。
8. Endpoint 是 NoC-facing 逻辑端点，不是任意外部 IP 实例。
9. 简单 Package 不使用 packageData 复制 Core 已有信息。
10. 不引入通用 connection rules 语言。
11. Generator 和 IP Engine 不直接读取 GUI 状态。
12. Package 代码不以 QWidget/C++ ABI 加载进 Finepaper。
13. 复杂产品语义留在 Package 或 IP Engine。
14. 验证和生成不修改源设计文件。
15. 每次生成使用独立运行目录，产物和日志不嵌入 NocDesign。
16. 运行中的操作固定使用启动时解析的 Package。
17. Package 页面不能绕过应用层修改设计或启动工具。
18. 新能力必须服务于理解、配置、验证或生成 NoC，而不是把 Finepaper 扩张成通用平台。

## 30. 尚可延后决定的问题

以下问题不阻塞前几个阶段：

- 独立第三方 API 最终使用 HTTP、C API 还是其他 IPC；
- 声明式 Package 页面的具体描述格式；
- packageData 的首个真实复杂模型；
- 是否需要 Package 签名和不受信任 Package 沙箱；
- IP Engine 是否在未来支持长驻会话；
- 是否增加更丰富的拓扑类型。

这些问题必须由真实 Package 或集成场景推动，不提前建立通用框架。

## 31. 总结

Finepaper 的目标架构可以归纳为：

~~~text
一个小型 NoC 设计模型
        +
一个共享的应用层
        +
运行时 NoC Package
        +
Package 自带 Generator
        +
复杂 Package 可选的进程外 IP Engine
~~~

Finepaper 通用能力负责 N×M 创建、Endpoint 挂载、可视化、参数编辑、通用验证、任务运行和产物管理。

NoC Package 负责描述具体 IP 的可配置内容并实际生成 RTL。

复杂 IP Engine 负责 Finepaper 不应内建的产品语义和厂商工具适配。

GUI、CLI 和第三方 API 从同一个 FinepaperApplication 进入，确保交互式创建与 CI 批量生成得到一致结果。
