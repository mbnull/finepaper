# V1 Connection Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. When dispatching subagents, set reasoning effort to `high`; use `xhigh` for review or architecture-critical tasks.

**Goal:** Replace pre-v1 compatibility and scattered connection rules with a v1 project boundary and a single metadata-driven `ConnectionRuleService`.

**Architecture:** `Graph` remains the topology store and only guards structural integrity. `ConnectionRuleService` resolves editor/programmatic connection requests into allowed options or rejection reasons using module/interface metadata and plugin-declared lightweight rules. UI, commands, and project load all ask the same service; Ruby/plugin DRC remains the final validate/generate authority.

**Tech Stack:** C++23, Qt 6, QtNodes, xmake Qt tests, existing module XML metadata, existing `.fpproj` project reader/writer.

---

## Spec Validation

Source spec: `docs/superpowers/specs/2026-05-08-v1-connection-rules-design.md`.

Coverage map:

- V1 project-only format and no pre-v1 compatibility: Task 1 and Task 2.
- Declarative plugin connection rules: Task 3.
- Structured connection request/result model: Task 4.
- Metadata, direction, cardinality, topology-rule checks: Task 5.
- Commands and project load share the service: Task 6.
- UI delegates connection completion to the service: Task 7.
- Ruby/plugin DRC remains final authority: Task 8 verification keeps `validation_test` and generator paths.
- Generated output snapshot remains `.fpproj`: Task 1 preserves `writeGeneratedProjectSnapshot()`.

## File Structure

- Create `qt/inc/connection/connectionruleservice.h`: request/result structs, semantic port snapshot, and service API.
- Create `qt/src/connection/connectionruleservice.cpp`: resolution, core metadata checks, cardinality, and option ranking.
- Create `qt/test/connectionruleservice_test.cpp`: focused unit tests for service behavior.
- Modify `qt/inc/modules/moduleregistry.h`: add declarative rule fields to `ModuleInterfaceMetadata`.
- Modify `qt/src/modules/moduleprovider.cpp`: parse interface `cardinality`, `autocomplete_group`, and `topology_rule`.
- Modify `plugins/noc/modules.xml` and `plugins/ravenoc/modules.xml`: declare v1 connection metadata.
- Modify `qt/inc/project/projectdocument.h`: remove `ProjectIpInstanceRecord` and `ipInstances`.
- Modify `qt/src/project/projectreader.cpp`: reject pre-v1 `ip_instances`; remove migration.
- Modify `qt/src/project/projectwriter.cpp`: stop writing `ip_instances`.
- Modify `qt/inc/app/generationartifacts.h` and `qt/src/app/generationartifacts.cpp`: remove legacy `ip_instance` compatibility helpers.
- Modify `qt/src/app/mainwindow.cpp`: stop importing legacy JSON and pass `ProjectStateService` to the node editor.
- Modify `qt/inc/graph/graph.h` and `qt/src/graph/graph.cpp`: remove legacy JSON load/save APIs and domain-specific connection rules.
- Delete `qt/inc/commands/loadgraphcommand.h` and `qt/src/commands/loadgraphcommand.cpp`.
- Modify `qt/inc/commands/addconnectioncommand.h` and `qt/src/commands/addconnectioncommand.cpp`: validate through `ConnectionRuleService`.
- Modify `qt/inc/nodeeditor/nodeeditorwidget.h`, `qt/src/nodeeditor/nodeeditorwidget.cpp`, and `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`: use the service for port-to-port and port-to-node completion.
- Modify tests: `qt/test/projectdocument_test.cpp`, `qt/test/graph_test.cpp`, `qt/test/plugin_test.cpp`, `qt/test/validation_test.cpp`, `qt/test/topology_preset_test.cpp`, `qt/test/nodeeditor_geometry_test.cpp`.
- Modify `qt/xmake.lua`: add `connectionruleservice_test` and include connection service sources in affected targets.

---

### Task 1: Remove Legacy IP Instance Compatibility

**Files:**
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/inc/app/generationartifacts.h`
- Modify: `qt/src/app/generationartifacts.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Add failing v1 rejection and no-compatibility tests**

In `qt/test/projectdocument_test.cpp`, replace tests that expect `ip_instances` migration with these tests:

```cpp
void testProjectReaderRejectsPreV1IpInstances() {
    QJsonObject root = validProjectRoot();
    root.insert(QStringLiteral("ip_instances"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("ravenoc_0")},
            {QStringLiteral("plugin"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("kind"), QStringLiteral("noc")},
            {QStringLiteral("type"), QStringLiteral("RaveNoC")},
            {QStringLiteral("parameters"), QJsonObject{}}
        }
    });

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("pre_v1_ip_instances.fpproj"));
    writeJsonFile(path, root);

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject pre-v1 ip_instances");
    require(result.error.contains(QStringLiteral("ip_instances")),
            "ip_instances rejection should mention ip_instances");
}

void testGeneratedArtifactsDoNotEmitLegacyIpInstance() {
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("finepaper.ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("type"), QStringLiteral("RaveNoC")},
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };

    QJsonObject root;
    attachPluginState(root, QVector<ProjectPluginStateRecord>{state});

    require(root.contains(QStringLiteral("plugin_state")),
            "generated input should include plugin_state");
    require(!root.contains(QStringLiteral("ip_instance")),
            "generated input should not include legacy ip_instance");
}
```

Call both tests from `main()`.

In `qt/test/validation_test.cpp`, remove the assertion that DRC input must not report missing `ip_instance` and add:

```cpp
require(!messages.join('\n').contains(QStringLiteral("missing plugin_state")),
        "RaveNoC DRC should receive plugin_state");
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
```

Expected: `projectdocument_test` fails because the reader still accepts/migrates `ip_instances`; compilation may also fail after removing old test references until the production code is updated.

- [ ] **Step 3: Remove project IP instance record shape**

In `qt/inc/project/projectdocument.h`, delete:

```cpp
struct ProjectIpInstanceRecord {
    QString id;
    QString pluginId;
    QString kind;
    QString type;
    QJsonObject parameters;
};
```

Delete this field from `ProjectDocument`:

```cpp
QVector<ProjectIpInstanceRecord> ipInstances;
```

- [ ] **Step 4: Reject ip_instances in the reader**

In `qt/src/project/projectreader.cpp`, replace the entire `ip_instances` read and migration block with:

```cpp
    if (root.contains(QStringLiteral("ip_instances"))) {
        return failure(QStringLiteral("Project ip_instances is a pre-v1 field and is not supported"));
    }
```

Remove `#include <QSet>` if it becomes unused.

- [ ] **Step 5: Stop writing ip_instances**

In `qt/src/project/projectwriter.cpp`, delete the block that builds and inserts `ip_instances`:

```cpp
QJsonArray ipInstances;
for (const ProjectIpInstanceRecord& ipInstance : document.ipInstances) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), ipInstance.id);
    object.insert(QStringLiteral("plugin"), ipInstance.pluginId);
    object.insert(QStringLiteral("kind"), ipInstance.kind);
    object.insert(QStringLiteral("type"), ipInstance.type);
    object.insert(QStringLiteral("parameters"), sortedObject(ipInstance.parameters));
    ipInstances.append(object);
}
root.insert(QStringLiteral("ip_instances"), ipInstances);
```

- [ ] **Step 6: Remove legacy ip_instance generation helpers**

In `qt/inc/app/generationartifacts.h`, delete `legacyIpInstanceObject()` and change `attachPluginState()` to:

```cpp
void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records);
```

In `qt/src/app/generationartifacts.cpp`, delete `legacyIpInstanceObject()` and replace `attachPluginState()` with:

```cpp
void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records) {
    root.insert(QStringLiteral("plugin_state"), pluginStateArray(records));
}
```

Update call sites:

```cpp
attachPluginState(root, m_projectStateService->pluginStates());
attachPluginState(root, pluginStates);
```

- [ ] **Step 7: Remove old tests and references**

Delete test cases in `qt/test/projectdocument_test.cpp` that assert:

- legacy `ip_instances` migrate into `plugin_state`.
- mixed `ip_instances` and `plugin_state` are merged.
- `legacyIpInstanceObject()` returns compatibility JSON.
- malformed legacy state suppresses `ip_instance`.
- read results preserve `document.ipInstances`.

Run:

```bash
rg -n "ProjectIpInstanceRecord|ipInstances|legacyIpInstanceObject|ip_instance|ip_instances" qt/inc qt/src qt/test
```

Expected: no matches except intentional user-facing rejection text containing `ip_instances` in `projectreader.cpp` and the new test.

- [ ] **Step 8: Verify and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
git diff --check
```

Expected:

```text
projectdocument_test passed
validation_test passed
```

Commit:

```bash
git add qt/inc/project/projectdocument.h qt/src/project/projectreader.cpp qt/src/project/projectwriter.cpp qt/inc/app/generationartifacts.h qt/src/app/generationartifacts.cpp qt/src/app/mainwindow.cpp qt/src/validation/drcrunner.cpp qt/test/projectdocument_test.cpp qt/test/validation_test.cpp
git commit -m "refactor: remove legacy ip instance compatibility"
```

---

### Task 2: Remove Legacy Graph JSON Import And Framework Export

**Files:**
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/project/projectreader.h`
- Modify: `qt/src/project/projectreader.cpp`
- Delete: `qt/inc/commands/loadgraphcommand.h`
- Delete: `qt/src/commands/loadgraphcommand.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Add failing unsupported legacy format test**

In `qt/test/projectdocument_test.cpp`, add:

```cpp
void testProjectReaderDetectsOnlyFinepaperProjects() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const QString legacyPath = QDir(tempDir.path()).filePath(QStringLiteral("legacy.json"));
    writeJsonFile(legacyPath, QJsonObject{
        {QStringLiteral("xps"), QJsonArray{}},
        {QStringLiteral("endpoints"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });

    require(ProjectReader::detectKind(legacyPath) == ProjectFileKind::Unknown,
            "pre-v1 legacy graph JSON should not be a supported import kind");
}
```

Call it from `main()`.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: FAIL because `ProjectReader::detectKind()` still returns `LegacyJson`.

- [ ] **Step 3: Remove LegacyJson kind and app import path**

In `qt/inc/project/projectreader.h`, change `ProjectFileKind` to:

```cpp
enum class ProjectFileKind {
    Unknown,
    Project
};
```

In `qt/src/project/projectreader.cpp`, remove the branch that returns `ProjectFileKind::LegacyJson` for `xps`, `endpoints`, or `connections`.

In `qt/src/app/mainwindow.cpp`, delete the entire `if (kind == ProjectFileKind::LegacyJson)` block in `MainWindow::loadDocument()`.

- [ ] **Step 4: Remove legacy Graph file APIs**

In `qt/inc/graph/graph.h`, remove:

```cpp
bool loadFromJson(const QString& jsonPath);
bool saveToJson(const QString& jsonPath) const;
bool saveToXml(const QString& xmlPath) const;
```

Change `GraphJsonFlavor` to:

```cpp
enum class GraphJsonFlavor {
    Plugin
};
```

In `qt/src/graph/graph.cpp`, delete:

- `normalizePortId()`
- `findPortByInterface()`
- `findNormalizedPort()`
- `firstAvailablePort()` used only by legacy import
- `Graph::loadFromJson()`
- `Graph::saveToJson()`
- `Graph::saveToXml()`
- the framework branch of `Graph::toJsonDocument()`
- XML writer helpers used only by `saveToXml()`

Keep the plugin graph JSON branch of `Graph::toJsonDocument()` and simplify its signature implementation to always produce plugin graph JSON.

- [ ] **Step 5: Remove LoadGraphCommand**

Delete:

```text
qt/inc/commands/loadgraphcommand.h
qt/src/commands/loadgraphcommand.cpp
```

The app target uses broad `src/commands/*.cpp`, so deleting the file is enough. Run:

```bash
rg -n "LoadGraphCommand|loadFromJson|saveToJson|saveToXml|GraphJsonFlavor::Framework|GraphJsonFlavor::Editor" qt/inc qt/src qt/test
```

Expected: no matches.

- [ ] **Step 6: Update tests to v1 expectations**

In `qt/test/graph_test.cpp`, delete tests that call `graph.loadFromJson()` or assert legacy framework export/import. Keep plugin graph JSON tests that assert `modules`, `connections`, and artifact IDs.

In `qt/test/plugin_test.cpp`, update generator command tests so every `PluginCommandDescriptor` uses:

```cpp
plugin.generator.inputFormat = QStringLiteral("generic_graph_v1");
plugin.drc.inputFormat = QStringLiteral("generic_graph_v1");
```

In `qt/src/app/mainwindow.cpp` and `qt/src/validation/drcrunner.cpp`, replace conditional flavor selection with:

```cpp
QJsonObject root = m_graph->toJsonDocument(designName, GraphJsonFlavor::Plugin).object();
```

and:

```cpp
QJsonObject root = graph->toJsonDocument(QStringLiteral("design"),
                                         GraphJsonFlavor::Plugin,
                                         &m_externalToInternalIds).object();
```

- [ ] **Step 7: Verify and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
git diff --check
```

Expected:

```text
graph_test passed
projectdocument_test passed
plugin_test passed
validation_test passed
```

Commit:

```bash
git add qt/inc/graph/graph.h qt/src/graph/graph.cpp qt/src/app/mainwindow.cpp qt/inc/project/projectreader.h qt/src/project/projectreader.cpp qt/test/graph_test.cpp qt/test/projectdocument_test.cpp qt/test/plugin_test.cpp qt/test/validation_test.cpp
git add -u qt/inc/commands/loadgraphcommand.h qt/src/commands/loadgraphcommand.cpp
git commit -m "refactor: remove legacy graph json compatibility"
```

---

### Task 3: Parse Declarative Connection Rule Metadata

**Files:**
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `plugins/noc/modules.xml`
- Modify: `plugins/ravenoc/modules.xml`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Write failing metadata parse test**

In `qt/test/plugin_test.cpp`, extend `testRaveNoCPluginLoadsModulesAndMetadata()` after checking topology presets:

```cpp
    const ModuleType* tileType = registry.getType(QStringLiteral("RaveTile"));
    require(tileType != nullptr, "RaveTile module type should load");
    const ModuleInterfaceMetadata eastInterface =
        tileType->interfaceMetadata.value(QStringLiteral("east"));
    require(eastInterface.cardinality == QStringLiteral("one"),
            "RaveTile east interface should declare one connection");
    require(eastInterface.autocompleteGroup == QStringLiteral("router_side"),
            "RaveTile east interface should declare router_side autocomplete group");
    require(eastInterface.topologyRule == QStringLiteral("opposite_side"),
            "RaveTile east interface should declare opposite_side topology rule");

    const ModuleInterfaceMetadata localInterface =
        tileType->interfaceMetadata.value(QStringLiteral("local"));
    require(localInterface.cardinality == QStringLiteral("one"),
            "RaveTile local interface should declare one endpoint attachment");
    require(localInterface.autocompleteGroup == QStringLiteral("endpoint_attachment"),
            "RaveTile local interface should declare endpoint attachment autocomplete group");
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: compile fails because `ModuleInterfaceMetadata` has no `cardinality`, `autocompleteGroup`, or `topologyRule`.

- [ ] **Step 3: Add metadata fields**

In `qt/inc/modules/moduleregistry.h`, add fields to `ModuleInterfaceMetadata`:

```cpp
QString cardinality = QStringLiteral("one");
QString autocompleteGroup;
QString topologyRule;
```

- [ ] **Step 4: Parse interface attributes**

In `qt/src/modules/moduleprovider.cpp`, where interface metadata is parsed, add:

```cpp
metadata.cardinality = attributeValue(attrs, u"cardinality");
if (metadata.cardinality.isEmpty()) {
    metadata.cardinality = QStringLiteral("one");
}
metadata.autocompleteGroup = attributeValue(attrs, u"autocomplete_group");
metadata.topologyRule = attributeValue(attrs, u"topology_rule");
```

- [ ] **Step 5: Update bundled plugin XML**

In `plugins/ravenoc/modules.xml`, update interfaces:

```xml
<interface id="north" label="North" bus="ravenoc_router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">
</interface>
<interface id="east" label="East" bus="ravenoc_router_link" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">
</interface>
<interface id="south" label="South" bus="ravenoc_router_link" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">
</interface>
<interface id="west" label="West" bus="ravenoc_router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">
</interface>
<interface id="local" label="Local" bus="ravenoc_endpoint_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="endpoint_attachment">
</interface>
```

For `RaveEndpoint`:

```xml
<interface id="noc" label="NoC" bus="ravenoc_endpoint_link" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="endpoint_attachment">
</interface>
```

In `plugins/noc/modules.xml`, add matching fields to `local0..local3`, `north/east/south/west`, and endpoint `noc` interfaces. Keep existing `match="protocol,data_width"` on endpoint attachment interfaces.

- [ ] **Step 6: Verify and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
git diff --check
```

Expected:

```text
plugin_test passed
```

Commit:

```bash
git add qt/inc/modules/moduleregistry.h qt/src/modules/moduleprovider.cpp plugins/noc/modules.xml plugins/ravenoc/modules.xml qt/test/plugin_test.cpp
git commit -m "feat: parse connection rule metadata"
```

---

### Task 4: Add ConnectionRuleService Core API

**Files:**
- Create: `qt/inc/connection/connectionruleservice.h`
- Create: `qt/src/connection/connectionruleservice.cpp`
- Create: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing service tests**

Create `qt/test/connectionruleservice_test.cpp`:

```cpp
// ConnectionRuleService tests for v1 editor-time connection decisions.
#include "connection/connectionruleservice.h"
#include "graph/graph.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<Module> makeProducer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Producer"));
    module->addPort(Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("Out"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeConsumer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Consumer"));
    module->addPort(Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("In"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

void testAllowsSimplePortToPortConnection() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Allowed,
            "matching output/input bus ports should be allowed");
    require(result.options.size() == 1, "simple port-to-port should produce one option");
    require(result.options.first().source.moduleId == QStringLiteral("producer"),
            "source module should be producer");
    require(result.options.first().target.moduleId == QStringLiteral("consumer"),
            "target module should be consumer");
}

void testRejectsMissingPortWithReason() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("missing")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "missing port should reject");
    require(result.reasonCode == QStringLiteral("missing_port"),
            "missing port rejection should have reason code");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testAllowsSimplePortToPortConnection();
        testRejectsMissingPortWithReason();
    } catch (const std::exception& error) {
        std::cerr << "connectionruleservice_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "connectionruleservice_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add xmake test target and verify failure**

In `qt/xmake.lua`, add:

```lua
add_qt_test_target("connectionruleservice_test", "test/connectionruleservice_test.cpp", {
    "src/connection/connectionruleservice.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
```

Expected: compile fails because `connectionruleservice.h` does not exist.

- [ ] **Step 3: Add service header**

Create `qt/inc/connection/connectionruleservice.h`:

```cpp
// ConnectionRuleService resolves v1 editor-time connection requests.
#pragma once

#include "graph/connection.h"
#include "graph/parameter.h"
#include "project/pluginstate.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class Graph;
class Module;
class Port;

enum class ConnectionRequestKind {
    PortToPort,
    PortToNode,
    NodeToPort,
    Programmatic,
    ProjectLoad
};

enum class ConnectionVisualSide {
    None,
    Input,
    Output
};

struct ConnectionEndpointRequest {
    QString moduleId;
    std::optional<QString> portId;
    QPointF scenePos;
    ConnectionVisualSide visualSide = ConnectionVisualSide::None;
    bool fromNodeBody = false;
    bool hiddenPortsAllowed = false;
};

struct ConnectionRequest {
    ConnectionRequestKind kind = ConnectionRequestKind::Programmatic;
    ConnectionEndpointRequest start;
    ConnectionEndpointRequest end;
    bool interactive = true;
    bool allowAutoComplete = true;
    bool allowAlternatives = true;

    static ConnectionRequest portToPort(const PortRef& start,
                                        const PortRef& end,
                                        ConnectionRequestKind kind);
};

struct PortSemanticInfo {
    PortRef ref;
    QString moduleType;
    QString pluginId;
    QString graphGroup;
    QString editorLayout;
    QString portName;
    QString direction;
    QString busType;
    QString portRole;
    QString interfaceId;
    QString interfaceBus;
    QString interfaceRole;
    QStringList compatibleRoles;
    QHash<QString, QStringList> matchFieldValues;
    QString cardinality = QStringLiteral("one");
    QString autocompleteGroup;
    QString topologyRule;
    bool supportsInput = false;
    bool supportsOutput = false;
    bool occupiedAsSource = false;
    bool occupiedAsTarget = false;
    bool visibleInUi = true;
};

enum class ConnectionCheckStatus {
    Allowed,
    NeedsSelection,
    Rejected
};

struct ConnectionResolvedOption {
    PortRef source;
    PortRef target;
    QString label;
    int priority = 0;
};

struct ConnectionCheckResult {
    ConnectionCheckStatus status = ConnectionCheckStatus::Rejected;
    QVector<ConnectionResolvedOption> options;
    QString reasonCode;
    QString message;

    bool hasSingleOption() const {
        return status == ConnectionCheckStatus::Allowed && options.size() == 1;
    }
};

class ConnectionRuleService {
public:
    ConnectionRuleService(const Graph* graph,
                          QVector<ProjectPluginStateRecord> pluginStates);

    ConnectionCheckResult check(const ConnectionRequest& request) const;

private:
    ConnectionCheckResult reject(QString reasonCode, QString message) const;
    QVector<PortSemanticInfo> resolveEndpointPorts(const ConnectionEndpointRequest& endpoint) const;
    std::optional<PortSemanticInfo> resolvePort(const QString& moduleId,
                                                const QString& portId,
                                                bool visibleInUi) const;
    QVector<ConnectionResolvedOption> buildOptions(const QVector<PortSemanticInfo>& startPorts,
                                                   const QVector<PortSemanticInfo>& endPorts,
                                                   const ConnectionRequest& request,
                                                   QString* rejectionReason,
                                                   QString* rejectionMessage) const;

    const Graph* m_graph = nullptr;
    QVector<ProjectPluginStateRecord> m_pluginStates;
};
```

- [ ] **Step 4: Add minimal implementation**

Create `qt/src/connection/connectionruleservice.cpp`:

```cpp
#include "connection/connectionruleservice.h"

#include "common/portlayout.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"

#include <algorithm>

namespace {

QString directionName(Port::Direction direction) {
    if (direction == Port::Direction::Input) {
        return QStringLiteral("input");
    }
    if (direction == Port::Direction::Output) {
        return QStringLiteral("output");
    }
    return QStringLiteral("inout");
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }
    for (const Port& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }
    return nullptr;
}

bool connectionExists(const Graph* graph, const PortRef& source, const PortRef& target) {
    if (!graph) {
        return false;
    }
    return std::any_of(graph->connections().begin(), graph->connections().end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return connection->source().moduleId == source.moduleId &&
                   connection->source().portId == source.portId &&
                   connection->target().moduleId == target.moduleId &&
                   connection->target().portId == target.portId;
        });
}

} // namespace

ConnectionRequest ConnectionRequest::portToPort(const PortRef& start,
                                                const PortRef& end,
                                                ConnectionRequestKind requestKind) {
    ConnectionRequest request;
    request.kind = requestKind;
    request.interactive = requestKind != ConnectionRequestKind::ProjectLoad;
    request.start.moduleId = start.moduleId;
    request.start.portId = start.portId;
    request.end.moduleId = end.moduleId;
    request.end.portId = end.portId;
    return request;
}

ConnectionRuleService::ConnectionRuleService(const Graph* graph,
                                             QVector<ProjectPluginStateRecord> pluginStates)
    : m_graph(graph),
      m_pluginStates(std::move(pluginStates)) {}

ConnectionCheckResult ConnectionRuleService::reject(QString reasonCode, QString message) const {
    ConnectionCheckResult result;
    result.status = ConnectionCheckStatus::Rejected;
    result.reasonCode = std::move(reasonCode);
    result.message = std::move(message);
    return result;
}

std::optional<PortSemanticInfo> ConnectionRuleService::resolvePort(const QString& moduleId,
                                                                   const QString& portId,
                                                                   bool visibleInUi) const {
    const Module* module = m_graph ? m_graph->getModule(moduleId) : nullptr;
    const Port* port = findPort(module, portId);
    if (!module || !port) {
        return std::nullopt;
    }

    const ModuleType* moduleType = ModuleTypeMetadata::type(module);
    PortSemanticInfo info;
    info.ref = PortRef{moduleId, portId};
    info.moduleType = module->type();
    info.pluginId = moduleType ? moduleType->pluginId : QString();
    info.graphGroup = moduleType ? moduleType->graphGroup : QString();
    info.editorLayout = ModuleTypeMetadata::editorLayout(module);
    info.portName = port->name();
    info.direction = directionName(port->direction());
    info.busType = port->busType();
    info.portRole = port->role();
    info.interfaceId = port->interfaceId();
    info.supportsInput = PortLayout::supportsInput(*port);
    info.supportsOutput = PortLayout::supportsOutput(*port);
    info.visibleInUi = visibleInUi;
    return info;
}

QVector<PortSemanticInfo> ConnectionRuleService::resolveEndpointPorts(
    const ConnectionEndpointRequest& endpoint) const {
    QVector<PortSemanticInfo> ports;
    const Module* module = m_graph ? m_graph->getModule(endpoint.moduleId) : nullptr;
    if (!module) {
        return ports;
    }

    if (endpoint.portId.has_value()) {
        if (auto info = resolvePort(endpoint.moduleId, *endpoint.portId, !endpoint.fromNodeBody)) {
            ports.push_back(*info);
        }
        return ports;
    }

    if (!endpoint.fromNodeBody || !endpoint.hiddenPortsAllowed) {
        return ports;
    }

    for (const Port& port : module->ports()) {
        if (auto info = resolvePort(endpoint.moduleId, port.id(), false)) {
            ports.push_back(*info);
        }
    }
    return ports;
}

QVector<ConnectionResolvedOption> ConnectionRuleService::buildOptions(
    const QVector<PortSemanticInfo>& startPorts,
    const QVector<PortSemanticInfo>& endPorts,
    const ConnectionRequest&,
    QString* rejectionReason,
    QString* rejectionMessage) const {
    QVector<ConnectionResolvedOption> options;
    for (const PortSemanticInfo& start : startPorts) {
        for (const PortSemanticInfo& end : endPorts) {
            if (start.ref.moduleId == end.ref.moduleId) {
                if (rejectionReason) *rejectionReason = QStringLiteral("self_loop");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Cannot connect a module to itself");
                continue;
            }

            if (start.supportsOutput && end.supportsInput &&
                start.busType == end.busType &&
                !connectionExists(m_graph, start.ref, end.ref)) {
                options.push_back(ConnectionResolvedOption{
                    start.ref,
                    end.ref,
                    QStringLiteral("%1.%2 -> %3.%4")
                        .arg(start.ref.moduleId, start.ref.portId, end.ref.moduleId, end.ref.portId),
                    0
                });
            }
        }
    }
    if (options.isEmpty() && rejectionReason && rejectionReason->isEmpty()) {
        *rejectionReason = QStringLiteral("direction_mismatch");
        if (rejectionMessage) *rejectionMessage = QStringLiteral("No direction-compatible connection option");
    }
    return options;
}

ConnectionCheckResult ConnectionRuleService::check(const ConnectionRequest& request) const {
    if (!m_graph) {
        return reject(QStringLiteral("missing_graph"), QStringLiteral("Connection graph is not available"));
    }
    if (!m_graph->getModule(request.start.moduleId) || !m_graph->getModule(request.end.moduleId)) {
        return reject(QStringLiteral("missing_module"), QStringLiteral("Connection references a missing module"));
    }

    const QVector<PortSemanticInfo> startPorts = resolveEndpointPorts(request.start);
    const QVector<PortSemanticInfo> endPorts = resolveEndpointPorts(request.end);
    if (startPorts.isEmpty() || endPorts.isEmpty()) {
        return reject(QStringLiteral("missing_port"), QStringLiteral("Connection references a missing port"));
    }

    QString reason;
    QString message;
    QVector<ConnectionResolvedOption> options = buildOptions(startPorts, endPorts, request, &reason, &message);
    if (options.isEmpty()) {
        return reject(reason.isEmpty() ? QStringLiteral("no_connection_option") : reason,
                      message.isEmpty() ? QStringLiteral("No legal connection option") : message);
    }

    std::sort(options.begin(), options.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority < rhs.priority || (lhs.priority == rhs.priority && lhs.label < rhs.label);
    });

    ConnectionCheckResult result;
    result.status = options.size() == 1 ? ConnectionCheckStatus::Allowed
                                        : ConnectionCheckStatus::NeedsSelection;
    result.options = std::move(options);
    return result;
}
```

- [ ] **Step 5: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
git diff --check
```

Expected:

```text
connectionruleservice_test passed
```

Commit:

```bash
git add qt/inc/connection/connectionruleservice.h qt/src/connection/connectionruleservice.cpp qt/test/connectionruleservice_test.cpp qt/xmake.lua
git commit -m "feat: add connection rule service"
```

---

### Task 5: Implement Metadata Rules In ConnectionRuleService

**Files:**
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing metadata rule tests**

In `qt/test/connectionruleservice_test.cpp`, add helpers:

```cpp
std::unique_ptr<Module> makeRouter(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Router"));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    return module;
}

void registerRouterType() {
    ModuleType router;
    router.name = QStringLiteral("Router");
    router.pluginId = QStringLiteral("finepaper.test");
    router.graphGroup = QStringLiteral("routers");
    router.defaultPorts = {
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("East"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("east")),
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("West"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("west"))
    };

    ModuleInterfaceMetadata east;
    east.id = QStringLiteral("east");
    east.bus = QStringLiteral("router_link");
    east.role = QStringLiteral("initiator");
    east.compatibleRoles = {QStringLiteral("target")};
    east.cardinality = QStringLiteral("one");
    east.autocompleteGroup = QStringLiteral("router_side");
    east.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(east.id, east);

    ModuleInterfaceMetadata west;
    west.id = QStringLiteral("west");
    west.bus = QStringLiteral("router_link");
    west.role = QStringLiteral("target");
    west.compatibleRoles = {QStringLiteral("initiator")};
    west.cardinality = QStringLiteral("one");
    west.autocompleteGroup = QStringLiteral("router_side");
    west.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(west.id, west);

    ModuleRegistry::instance().registerType(router);
}
```

Add tests:

```cpp
void testRejectsSameSideTopologyRule() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("left"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("right"), QStringLiteral("east")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "same-side router connection should reject");
    require(result.reasonCode == QStringLiteral("topology_rule_mismatch"),
            "same-side rejection should report topology rule mismatch");
}

void testRejectsOccupiedCardinalityOnePort() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");
    require(graph.addModule(makeRouter(QStringLiteral("extra"))), "failed to add extra router");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("existing"),
        PortRef{QStringLiteral("left"), QStringLiteral("east")},
        PortRef{QStringLiteral("right"), QStringLiteral("west")}));

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("left"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("extra"), QStringLiteral("west")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "occupied cardinality-one port should reject");
    require(result.reasonCode == QStringLiteral("port_occupied"),
            "occupied rejection should report port_occupied");
}
```

Call both tests from `main()`.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
```

Expected: at least one new test fails because the service has not implemented interface roles, cardinality, or topology rules.

- [ ] **Step 3: Populate interface metadata in PortSemanticInfo**

In `qt/src/connection/connectionruleservice.cpp`, add helpers adapted from current `graph.cpp`:

```cpp
QString parameterValueString(const Module* module, const QString& parameterName);
QString canonicalInterfaceFieldValue(const QString& field, const QString& value);
QStringList interfaceFieldValues(const ModuleInterfaceMetadata& metadata,
                                 const Module* module,
                                 const QString& field);
bool valuesOverlap(const QStringList& lhs, const QStringList& rhs);
```

Inside `ConnectionRuleService::resolvePort()`, after basic port fields, add:

```cpp
    if (moduleType && !port->interfaceId().isEmpty()) {
        const auto metadataIt = moduleType->interfaceMetadata.find(port->interfaceId());
        if (metadataIt != moduleType->interfaceMetadata.end()) {
            const ModuleInterfaceMetadata& metadata = metadataIt.value();
            info.interfaceBus = metadata.bus;
            info.interfaceRole = metadata.role;
            info.compatibleRoles = metadata.compatibleRoles;
            info.cardinality = metadata.cardinality.isEmpty() ? QStringLiteral("one")
                                                              : metadata.cardinality;
            info.autocompleteGroup = metadata.autocompleteGroup;
            info.topologyRule = metadata.topologyRule;
            QStringList fields = metadata.matchFields;
            fields.sort();
            for (const QString& field : fields) {
                info.matchFieldValues.insert(field, interfaceFieldValues(metadata, module, field));
            }
        }
    }
```

- [ ] **Step 4: Enforce metadata compatibility**

Replace the simple bus comparison in `buildOptions()` with a call to:

```cpp
bool metadataCompatible(const PortSemanticInfo& source,
                        const PortSemanticInfo& target,
                        QString* reason,
                        QString* message);
```

Implement it:

```cpp
bool metadataCompatible(const PortSemanticInfo& source,
                        const PortSemanticInfo& target,
                        QString* reason,
                        QString* message) {
    const QString sourceBus = source.interfaceBus.isEmpty() ? source.busType : source.interfaceBus;
    const QString targetBus = target.interfaceBus.isEmpty() ? target.busType : target.interfaceBus;
    if (sourceBus != targetBus) {
        if (reason) *reason = QStringLiteral("bus_mismatch");
        if (message) *message = QStringLiteral("Connection bus types do not match");
        return false;
    }

    if (!source.interfaceRole.isEmpty() || !target.interfaceRole.isEmpty()) {
        if (!source.compatibleRoles.contains(target.interfaceRole) ||
            !target.compatibleRoles.contains(source.interfaceRole)) {
            if (reason) *reason = QStringLiteral("interface_role_mismatch");
            if (message) *message = QStringLiteral("Connection interface roles are not compatible");
            return false;
        }
    }

    QStringList fields = source.matchFieldValues.keys();
    for (const QString& field : target.matchFieldValues.keys()) {
        if (!fields.contains(field)) {
            fields.append(field);
        }
    }
    for (const QString& field : fields) {
        if (!valuesOverlap(source.matchFieldValues.value(field),
                           target.matchFieldValues.value(field))) {
            if (reason) *reason = QStringLiteral("interface_field_mismatch");
            if (message) *message = QStringLiteral("Connection interface field values do not overlap");
            return false;
        }
    }

    return true;
}
```

- [ ] **Step 5: Enforce cardinality and topology_rule**

Add:

```cpp
bool portOccupiedForCardinalityOne(const Graph* graph, const PortRef& ref) {
    if (!graph) {
        return false;
    }
    return std::any_of(graph->connections().begin(), graph->connections().end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return (connection->source().moduleId == ref.moduleId &&
                    connection->source().portId == ref.portId) ||
                   (connection->target().moduleId == ref.moduleId &&
                    connection->target().portId == ref.portId);
        });
}

bool oppositeSideRulePasses(const PortSemanticInfo& source,
                            const PortSemanticInfo& target) {
    if (source.topologyRule != QStringLiteral("opposite_side") &&
        target.topologyRule != QStringLiteral("opposite_side")) {
        return true;
    }

    const QString sourceSide = PortLayout::routerSideId(source.ref.portId);
    const QString targetSide = PortLayout::routerSideId(target.ref.portId);
    return !sourceSide.isEmpty() &&
           PortLayout::oppositeRouterSide(sourceSide) == targetSide;
}
```

In `buildOptions()`, before appending an option:

```cpp
            if ((start.cardinality == QStringLiteral("one") &&
                 portOccupiedForCardinalityOne(m_graph, start.ref)) ||
                (end.cardinality == QStringLiteral("one") &&
                 portOccupiedForCardinalityOne(m_graph, end.ref))) {
                if (rejectionReason) *rejectionReason = QStringLiteral("port_occupied");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection port is already occupied");
                continue;
            }

            if (!oppositeSideRulePasses(start, end)) {
                if (rejectionReason) *rejectionReason = QStringLiteral("topology_rule_mismatch");
                if (rejectionMessage) *rejectionMessage = QStringLiteral("Connection does not satisfy topology rule");
                continue;
            }
```

- [ ] **Step 6: Run focused tests and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
git diff --check
```

Expected:

```text
connectionruleservice_test passed
```

Commit:

```bash
git add qt/src/connection/connectionruleservice.cpp qt/test/connectionruleservice_test.cpp qt/xmake.lua
git commit -m "feat: enforce metadata connection rules"
```

---

### Task 6: Route Commands And Project Load Through ConnectionRuleService

**Files:**
- Modify: `qt/inc/commands/addconnectioncommand.h`
- Modify: `qt/src/commands/addconnectioncommand.cpp`
- Modify: `qt/inc/project/graphprojectserializer.h`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing project-load semantic rejection test**

In `qt/test/projectdocument_test.cpp`, add a v1 project with an invalid same-side router connection:

```cpp
void testProjectLoadRejectsConnectionRuleFailure() {
    ProjectDocument document = validProjectDocument();
    document.modules.clear();
    document.connections.clear();

    ProjectModuleRecord left;
    left.id = QStringLiteral("left");
    left.pluginId = QStringLiteral("finepaper.ravenoc");
    left.type = QStringLiteral("RaveTile");
    left.parameters = QJsonObject{
        {QStringLiteral("x"), 0},
        {QStringLiteral("y"), 0},
        {QStringLiteral("mesh_col"), 0},
        {QStringLiteral("mesh_row"), 0}
    };
    document.modules.push_back(left);

    ProjectModuleRecord right = left;
    right.id = QStringLiteral("right");
    right.parameters.insert(QStringLiteral("x"), 220);
    right.parameters.insert(QStringLiteral("mesh_col"), 1);
    document.modules.push_back(right);

    document.connections.push_back(ProjectConnectionRecord{
        QStringLiteral("bad_same_side"),
        ProjectConnectionEndpoint{QStringLiteral("left"), QStringLiteral("east")},
        ProjectConnectionEndpoint{QStringLiteral("right"), QStringLiteral("east")}
    });

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "project load should reject connection rule failure");
    require(result.error.contains(QStringLiteral("bad_same_side")),
            "project load failure should include connection id");
    require(result.error.contains(QStringLiteral("topology_rule_mismatch")),
            "project load failure should include connection rule reason");
}
```

Call it from `main()`.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: FAIL if `Graph::isValidConnection()` still owns the old reasonless rejection, or PASS for the wrong reason without `topology_rule_mismatch`.

- [ ] **Step 3: Change AddConnectionCommand to require the service**

In `qt/inc/commands/addconnectioncommand.h`, include the service and change constructor/member:

```cpp
#include "connection/connectionruleservice.h"

class AddConnectionCommand : public Command {
public:
    AddConnectionCommand(Graph* graph,
                         ConnectionRuleService* ruleService,
                         std::unique_ptr<Connection> connection);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph = nullptr;
    ConnectionRuleService* m_ruleService = nullptr;
    std::unique_ptr<Connection> m_connection;
    QString m_connectionId;
    bool m_executed = false;
};
```

In `qt/src/commands/addconnectioncommand.cpp`, update:

```cpp
AddConnectionCommand::AddConnectionCommand(Graph* graph,
                                           ConnectionRuleService* ruleService,
                                           std::unique_ptr<Connection> connection)
    : m_graph(graph),
      m_ruleService(ruleService),
      m_connection(std::move(connection)) {
    m_connectionId = m_connection->id();
}

void AddConnectionCommand::execute() {
    if (!m_graph || !m_connection) {
        return;
    }

    if (m_ruleService) {
        const ConnectionCheckResult result = m_ruleService->check(
            ConnectionRequest::portToPort(m_connection->source(),
                                          m_connection->target(),
                                          ConnectionRequestKind::Programmatic));
        if (!result.hasSingleOption()) {
            return;
        }
    }

    if (!m_graph->isValidConnection(m_connection->source(), m_connection->target())) {
        return;
    }
    m_graph->insertConnection(std::move(m_connection));
    m_executed = true;
}
```

- [ ] **Step 4: Use service in project load**

In `qt/inc/project/graphprojectserializer.h`, keep public API unchanged:

```cpp
static GraphProjectLoadResult loadProject(const ProjectDocument& document, Graph& graph);
```

In `qt/src/project/graphprojectserializer.cpp`, include:

```cpp
#include "connection/connectionruleservice.h"
```

In `populateGraph()`, after adding modules and before adding each connection:

```cpp
        const ConnectionRuleService ruleService(&graph, document.pluginStates);
        const ConnectionCheckResult check = ruleService.check(
            ConnectionRequest::portToPort(source, target, ConnectionRequestKind::ProjectLoad));
        if (!check.hasSingleOption()) {
            return failure(QStringLiteral("Invalid connection %1: %2")
                               .arg(record.id,
                                    check.reasonCode.isEmpty() ? check.message : check.reasonCode));
        }
```

Then add through `graph.addConnection()` after the check.

- [ ] **Step 5: Simplify Graph connection validity to structural guard**

In `qt/src/graph/graph.cpp`, replace `Graph::isValidConnection()` with:

```cpp
bool Graph::isValidConnection(const PortRef& source, const PortRef& target) const {
    if (source.moduleId == target.moduleId) {
        return false;
    }

    const Module* sourceModule = getModule(source.moduleId);
    const Module* targetModule = getModule(target.moduleId);
    if (!sourceModule || !targetModule) {
        return false;
    }

    if (!findPort(sourceModule, source.portId) || !findPort(targetModule, target.portId)) {
        return false;
    }

    return std::none_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return connection->source().moduleId == source.moduleId &&
                   connection->source().portId == source.portId &&
                   connection->target().moduleId == target.moduleId &&
                   connection->target().portId == target.portId;
        });
}
```

Delete now-unused helpers from `graph.cpp`:

- `portSupportsDirection`
- `oppositeDirection`
- `isRouterLink`
- `parameterValueString`
- `canonicalInterfaceFieldValue`
- `interfaceMetadataFor`
- `interfaceFieldValues`
- `valuesOverlap`
- `interfaceMetadataCompatible`
- `interfacesCompatible`
- `connectionUsesRouterSide`

- [ ] **Step 6: Update graph tests to structural expectations**

In `qt/test/graph_test.cpp`, change tests that currently expect `Graph::isValidConnection()` to reject bus/interface/router semantic mismatches so they instead use `ConnectionRuleService`.

Example replacement:

```cpp
ConnectionRuleService service(&graph, {});
const ConnectionCheckResult result = service.check(
    ConnectionRequest::portToPort(PortRef{QStringLiteral("endpoint"), QStringLiteral("noc")},
                                  PortRef{QStringLiteral("xp"), QStringLiteral("local0")},
                                  ConnectionRequestKind::Programmatic));
require(result.status == ConnectionCheckStatus::Rejected,
        "connection service should reject mismatched data_width");
```

Keep `Graph` tests for missing module, missing port, self-loop, and duplicate exact edge.

- [ ] **Step 7: Update xmake target dependencies**

Add connection service source/header to targets that now use it:

```lua
"src/connection/connectionruleservice.cpp",
"inc/**/connectionruleservice.h",
```

Targets:

- `projectdocument_test`
- `graph_test`
- `arrangecommand_test` if it links `Graph` tests that include service helpers
- app target already includes `src/**.cpp` and `inc/**.h`

- [ ] **Step 8: Verify and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
git diff --check
```

Expected:

```text
connectionruleservice_test passed
graph_test passed
projectdocument_test passed
```

Commit:

```bash
git add qt/inc/commands/addconnectioncommand.h qt/src/commands/addconnectioncommand.cpp qt/inc/project/graphprojectserializer.h qt/src/project/graphprojectserializer.cpp qt/inc/graph/graph.h qt/src/graph/graph.cpp qt/test/graph_test.cpp qt/test/projectdocument_test.cpp qt/xmake.lua
git commit -m "refactor: route connection validation through service"
```

---

### Task 7: Replace NodeEditor Connection Completion With Rule Service

**Files:**
- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/nodeeditor_geometry_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add compile-level service ownership test**

In `qt/test/nodeeditor_geometry_test.cpp`, update widget construction to pass a `ProjectStateService`:

```cpp
ProjectStateService stateService;
CommandManager commandManager;
NodeEditorWidget widget(&graph, &stateService, &commandManager);
```

Add includes:

```cpp
#include "project/projectstateservice.h"
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
```

Expected: compile fails because `NodeEditorWidget` constructor does not accept `ProjectStateService*`.

- [ ] **Step 2: Add service dependencies to NodeEditorWidget**

In `qt/inc/nodeeditor/nodeeditorwidget.h`, forward declare and add constructor/member:

```cpp
class ProjectStateService;
class ConnectionRuleService;

NodeEditorWidget(Graph* graph,
                 ProjectStateService* projectStateService,
                 CommandManager* commandManager,
                 QWidget* parent = nullptr);

ProjectStateService* m_projectStateService = nullptr;
std::unique_ptr<ConnectionRuleService> m_connectionRuleService;
```

Include `<memory>`.

In `qt/src/nodeeditor/nodeeditorwidget.cpp`, include:

```cpp
#include "connection/connectionruleservice.h"
#include "project/projectstateservice.h"
```

Update constructor:

```cpp
NodeEditorWidget::NodeEditorWidget(Graph* graph,
                                   ProjectStateService* projectStateService,
                                   CommandManager* commandManager,
                                   QWidget* parent)
    : QWidget(parent),
      m_graph(graph),
      m_projectStateService(projectStateService),
      m_commandManager(commandManager),
      m_canvasRect(kCanvasRect) {
    m_connectionRuleService = std::make_unique<ConnectionRuleService>(
        m_graph,
        m_projectStateService ? m_projectStateService->pluginStates()
                              : QVector<ProjectPluginStateRecord>{});
```

Add helper:

```cpp
void NodeEditorWidget::refreshConnectionRuleService() {
    m_connectionRuleService = std::make_unique<ConnectionRuleService>(
        m_graph,
        m_projectStateService ? m_projectStateService->pluginStates()
                              : QVector<ProjectPluginStateRecord>{});
}
```

Declare it private in the header.

- [ ] **Step 3: Update MainWindow construction**

In `qt/src/app/mainwindow.cpp`, change:

```cpp
m_nodeEditor = new NodeEditorWidget(m_graph, m_commandManager.get(), this);
```

to:

```cpp
m_nodeEditor = new NodeEditorWidget(m_graph,
                                    m_projectStateService.get(),
                                    m_commandManager.get(),
                                    this);
```

- [ ] **Step 4: Replace port-to-port creation**

In `NodeEditorWidget::onConnectionCreated()`, after resolving source/target, use service:

```cpp
    refreshConnectionRuleService();
    const ConnectionCheckResult result = m_connectionRuleService->check(
        ConnectionRequest::portToPort(source, target, ConnectionRequestKind::PortToPort));
    if (!result.hasSingleOption()) {
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->deleteConnection(connectionId);
        return;
    }

    executeAddConnection(result.options.first().source, result.options.first().target);
```

Do not inspect bus, router, endpoint, or interface metadata in this method.

- [ ] **Step 5: Replace router/endpoint draft completion**

Delete from `qt/inc/nodeeditor/nodeeditorwidget.h`:

```cpp
bool resolveRouterDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                  QtNodes::NodeId targetNodeId,
                                  PortRef& source,
                                  PortRef& target) const;
bool resolveEndpointDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                    QtNodes::NodeId targetNodeId,
                                    PortRef& source,
                                    PortRef& target) const;
bool tryCompleteRouterDraftConnection(const QPoint& viewportPos);
bool tryCompleteEndpointDraftConnection(const QPoint& viewportPos);
bool tryCompleteDraftConnection(const QPoint& viewportPos,
                                const std::function<bool(const QtNodes::ConnectionGraphicsObject&,
                                                         QtNodes::NodeId,
                                                         PortRef&,
                                                         PortRef&)>& resolver);
```

Add:

```cpp
bool tryCompleteDraftConnection(const QPoint& viewportPos);
ConnectionRequest draftConnectionRequest(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                         QtNodes::NodeId targetNodeId,
                                         const QPointF& scenePos) const;
void showConnectionOptionsMenu(const QPoint& viewportPos,
                               const QVector<ConnectionResolvedOption>& options);
```

In `handleViewportMouseRelease()`, replace the two calls with:

```cpp
    if (tryCompleteDraftConnection(event->position().toPoint())) {
        return true;
    }
```

Implement `draftConnectionRequest()`:

```cpp
ConnectionRequest NodeEditorWidget::draftConnectionRequest(
    const QtNodes::ConnectionGraphicsObject& draftConnection,
    QtNodes::NodeId targetNodeId,
    const QPointF& scenePos) const {
    const auto start = resolveDraftConnectionStart(draftConnection);
    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.interactive = true;
    request.allowAutoComplete = true;
    request.allowAlternatives = true;

    if (!start) {
        return request;
    }

    request.start.moduleId = m_nodeToModuleId.value(start->nodeId);
    request.start.portId = getPortId(start->nodeId, start->portType, start->portIndex);
    request.start.visualSide = start->startFromOutput ? ConnectionVisualSide::Output
                                                       : ConnectionVisualSide::Input;
    request.start.scenePos = m_graphModel->nodeData(start->nodeId, QtNodes::NodeRole::Position).value<QPointF>();

    request.end.moduleId = m_nodeToModuleId.value(targetNodeId);
    request.end.scenePos = scenePos;
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;
    request.end.visualSide = start->startFromOutput ? ConnectionVisualSide::Input
                                                     : ConnectionVisualSide::Output;
    return request;
}
```

Implement `tryCompleteDraftConnection()`:

```cpp
bool NodeEditorWidget::tryCompleteDraftConnection(const QPoint& viewportPos) {
    auto* draftConnection = findDraftConnection();
    if (!draftConnection) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* targetNode = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!targetNode) {
        return false;
    }

    refreshConnectionRuleService();
    const ConnectionCheckResult result =
        m_connectionRuleService->check(draftConnectionRequest(*draftConnection,
                                                              targetNode->nodeId(),
                                                              scenePos));

    m_scene->resetDraftConnection();

    if (result.hasSingleOption()) {
        const ConnectionResolvedOption& option = result.options.first();
        executeAddConnection(option.source, option.target);
        return true;
    }

    if (result.status == ConnectionCheckStatus::NeedsSelection && !result.options.isEmpty()) {
        showConnectionOptionsMenu(viewportPos, result.options);
        return true;
    }

    return true;
}
```

Implement `showConnectionOptionsMenu()`:

```cpp
void NodeEditorWidget::showConnectionOptionsMenu(
    const QPoint& viewportPos,
    const QVector<ConnectionResolvedOption>& options) {
    QMenu menu(this);
    for (const ConnectionResolvedOption& option : options) {
        QAction* action = menu.addAction(option.label);
        connect(action, &QAction::triggered, this, [this, option]() {
            executeAddConnection(option.source, option.target);
        });
    }
    menu.exec(m_view->viewport()->mapToGlobal(viewportPos));
}
```

- [ ] **Step 6: Update executeAddConnection**

In `NodeEditorWidget::executeAddConnection()`:

```cpp
void NodeEditorWidget::executeAddConnection(const PortRef& source, const PortRef& target) {
    refreshConnectionRuleService();
    auto connection = std::make_unique<Connection>(NodeEditorEntityFactory::generateEntityId(), source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph,
                                                          m_connectionRuleService.get(),
                                                          std::move(connection));
    m_commandManager->executeCommand(std::move(command));
}
```

- [ ] **Step 7: Remove domain-specific helpers from NodeEditorWidget**

Delete local helpers from `qt/src/nodeeditor/nodeeditorwidget.cpp` if no longer used:

- `isMeshRouterModule()` if used only for connection completion.
- `isEndpointModule()` if used only for connection completion and presentation replacement is complete.
- `firstAvailablePort()`.
- `isEndpointAttachmentConnection()` only after presentation code is updated to rely on metadata or no longer collapses endpoint attachments.

For collapsed endpoint presentation, if a helper remains necessary, move it behind metadata checks:

```cpp
bool isAttachmentConnection(const Graph* graph, const Connection& connection) {
    ConnectionRuleService service(graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(connection.source(),
                                      connection.target(),
                                      ConnectionRequestKind::Programmatic));
    return result.hasSingleOption();
}
```

Keep this helper local only for presentation; it must not decide connection legality.

- [ ] **Step 8: Update xmake and verify**

Add to `nodeeditor_geometry_test` target:

```lua
add_files("src/connection/connectionruleservice.cpp")
add_files("src/project/projectstateservice.cpp")
add_files("inc/**/connectionruleservice.h")
add_files("inc/**/projectstateservice.h")
add_files("inc/**/pluginstate.h")
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake -P qt -r qt
git diff --check
```

Expected:

```text
nodeeditor_geometry_test passed
connectionruleservice_test passed
```

App target builds.

Commit:

```bash
git add qt/inc/nodeeditor/nodeeditorwidget.h qt/src/nodeeditor/nodeeditorwidget.cpp qt/src/nodeeditor/events/nodeeditorwidget_events.cpp qt/src/app/mainwindow.cpp qt/test/nodeeditor_geometry_test.cpp qt/xmake.lua
git commit -m "refactor: drive node editor connections through rule service"
```

---

### Task 8: Final Cleanup And Verification

**Files:**
- Modify: `qt/test/topology_preset_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Remove stale compatibility assertions**

Run:

```bash
rg -n "legacy|ip_instance|ip_instances|LegacyJson|loadFromJson|saveToJson|saveToXml|GraphJsonFlavor::Framework|GraphJsonFlavor::Editor|configureIpInstance|isMeshRouterModule|resolveRouterDraftConnection|resolveEndpointDraftConnection" qt/inc qt/src qt/test
```

Expected allowed matches:

- User-facing rejection text for `ip_instances` in `projectreader.cpp`.
- Test name/assertion for rejecting `ip_instances`.
- Historical comments only if they describe v1 rejection rather than compatibility.

Delete or rewrite all other matches.

- [ ] **Step 2: Run focused tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt topology_preset_test
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
```

Expected:

```text
connectionruleservice_test passed
graph_test passed
projectdocument_test passed
plugin_test passed
validation_test passed
topology_preset_test passed
nodeeditor_geometry_test passed
```

- [ ] **Step 3: Run full test suite**

Run:

```bash
CCACHE_DISABLE=1 xmake test -P qt
```

Expected: all Qt tests pass.

- [ ] **Step 4: Build app**

Run:

```bash
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: app target builds successfully.

- [ ] **Step 5: Check diff hygiene**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` prints no output. `git status --short` shows only intended files.

- [ ] **Step 6: Commit cleanup**

Commit the cleanup files changed by Step 1:

```bash
git add qt docs
git commit -m "test: clean up v1 connection rule coverage"
```

When Step 1 produces no source changes, record the verification in the final implementation report without creating a commit.

## Self-Review

- Spec coverage: tasks remove pre-v1 compatibility, add declarative metadata, introduce `ConnectionRuleService`, route project/command/UI paths through it, keep generated project snapshots, and leave Ruby DRC as final authority.
- Placeholder scan: every task has concrete files, test snippets, implementation snippets, commands, expected failures, expected passes, and commit commands.
- Type consistency: `ConnectionRequest`, `ConnectionEndpointRequest`, `PortSemanticInfo`, `ConnectionCheckResult`, and `ConnectionRuleService` are defined once in Task 4 and used consistently by later tasks.
- Sequencing check: legacy compatibility is removed before rule-service integration; metadata is parsed before semantic rules consume it; UI migration happens after commands and project load can use the service.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-08-v1-connection-rules.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh high-reasoning subagent per task, review between tasks, use xhigh for review.
2. **Inline Execution** - Execute tasks in this session using executing-plans, with checkpoints after each task.
