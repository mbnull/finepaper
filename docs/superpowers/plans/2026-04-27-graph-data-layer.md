# Graph Data Layer Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract NoC-specific behavior from the graph core so `Graph` becomes a generic IP graph operation layer.

**Architecture:** Add query, rule, projection, and view-policy layers around the existing `Module`, `Port`, `Connection`, and `Graph` storage classes. Move current NoC mesh and endpoint behavior into isolated policy/projection files while keeping project JSON and framework JSON compatible. Convert call sites incrementally so every step has runnable tests.

**Tech Stack:** C++23, Qt, xmake Qt test targets, Ruby `spec_generator` tests, Ruby NoC generator tests.

---

## File Structure

- Create `qt/inc/graph/graphqueries.h`: pure graph lookup and endpoint normalization helpers.
- Create `qt/src/graph/graphqueries.cpp`: implementations moved from anonymous helpers in `graph.cpp`.
- Create `qt/inc/graph/graphrules.h`: validation result, validation context, rule engine API.
- Create `qt/src/graph/graphrules.cpp`: generic connection validation and schema-backed policy hooks.
- Create `qt/inc/graph/nocgraphpolicy.h`: transitional NoC policy interface for mesh opposite-interface and local-attachment rules.
- Create `qt/src/graph/nocgraphpolicy.cpp`: NoC policy implementation isolated from `Graph`.
- Create `qt/inc/graph/nocgraphprojection.h`: NoC framework JSON import/export API.
- Create `qt/src/graph/nocgraphprojection.cpp`: framework JSON `xps`/`endpoints` conversion moved from `graph.cpp`.
- Create `qt/inc/nodeeditor/interfaceviewpolicy.h`: generic view-policy helpers for anchor lookup, fallback side, labels, and endpoint-facing behavior.
- Create `qt/src/nodeeditor/interfaceviewpolicy.cpp`: metadata-backed view-policy implementation.
- Modify `qt/inc/graph/graph.h`: keep the public API stable and update comments after projection delegation.
- Modify `qt/src/graph/graph.cpp`: delegate queries, rules, and projections to the new files.
- Modify `qt/inc/common/portlayout.h`: reduce to legacy compatibility helpers after call sites move.
- Modify `qt/src/commands/arrangecommand.cpp`: use NoC policy and metadata instead of local router/endpoint detection.
- Modify `qt/src/nodeeditor/graphnodegeometry.cpp`: use `InterfaceViewPolicy` instead of direct `PortLayout` and layout-name decisions.
- Modify `qt/src/nodeeditor/graphnodepainter.cpp`: draw labels from `InterfaceViewPolicy`.
- Modify `qt/src/nodeeditor/nodeeditorwidget.cpp`: use `InterfaceViewPolicy` for attach/flip decisions.
- Modify `qt/src/nodeeditor/straightconnectionpainter.cpp`: keep orthogonal rendering and read endpoint normals from view policy.
- Modify `qt/src/validation/drcrunner.cpp`: use graph groups and projection maps instead of hard-coded XP/Endpoint message handling.
- Modify `qt/xmake.lua`: add `graphqueries.cpp`, `graphrules.cpp`, `nocgraphpolicy.cpp`, `nocgraphprojection.cpp`, and `interfaceviewpolicy.cpp` to affected test targets explicitly.
- Modify `qt/test/graph_test.cpp`: add query, rule, and NoC projection regression tests.
- Modify `qt/test/projectdocument_test.cpp`: keep project failure atomicity and malformed JSON tests passing.
- Modify `qt/test/nodeeditor_geometry_test.cpp`: add metadata fallback and endpoint-facing tests.
- Modify `spec/noc/noc.yaml`: add legacy aliases and connection rule metadata in Task 6.
- Modify `spec_generator/lib/spec_generator.rb`: emit graph rule and alias metadata for `spec/noc/noc.yaml`.
- Modify `spec_generator/test/spec_generator_test.rb`: assert emitted rule and alias metadata.

## Task 1: Extract Graph Query Helpers

**Files:**
- Create: `qt/inc/graph/graphqueries.h`
- Create: `qt/src/graph/graphqueries.cpp`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing query tests**

Append tests to `qt/test/graph_test.cpp` that create two modules with interface ids and assert exact lookup, interface lookup, legacy id normalization, and occupancy.

```cpp
static std::unique_ptr<Module> makeQueryModule(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("GenericTile"));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("local0"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("Local 0"), {}, QStringLiteral("attachment"),
                         QStringLiteral("endpoint_link"), QStringLiteral("local0")));
    return module;
}

static void testGraphQueriesNormalizeLegacyIds() {
    Graph graph;
    require(graph.addModule(makeQueryModule(QStringLiteral("a"))), "add a");
    Module* module = graph.getModule(QStringLiteral("a"));

    require(GraphQueries::findPort(module, QStringLiteral("east")) != nullptr, "find exact port");
    require(GraphQueries::findPortByInterface(module, QStringLiteral("east")) != nullptr, "find interface port");
    require(GraphQueries::normalizePortId(module, QStringLiteral("east_out")) == QStringLiteral("east"),
            "legacy router output maps to interface id");
    require(GraphQueries::normalizePortId(module, QStringLiteral("ep0")) == QStringLiteral("local0"),
            "legacy endpoint slot maps to local interface id");
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: compile failure because `GraphQueries` does not exist.

- [ ] **Step 3: Add the public query API**

Create `qt/inc/graph/graphqueries.h`:

```cpp
#pragma once

#include "graph/connection.h"
#include "graph/module.h"
#include "graph/port.h"
#include <QString>

class Graph;

namespace GraphQueries {

const Port* findPort(const Module* module, const QString& portId);
const Port* findPortByInterface(const Module* module, const QString& interfaceId);
QString normalizePortId(const Module* module, const QString& requestedPortId);
const Port* findNormalizedPort(const Module* module, const QString& portId);
bool endpointUsesInterface(const Connection& connection,
                           const QString& moduleId,
                           const QString& interfaceId);
bool isEndpointOccupied(const Graph& graph,
                        const QString& moduleId,
                        const QString& portId);

} // namespace GraphQueries
```

- [ ] **Step 4: Move helper implementations**

Create `qt/src/graph/graphqueries.cpp` by moving these helpers out of the anonymous namespace in `qt/src/graph/graph.cpp`: `findPort`, `findPortByInterface`, `normalizePortId`, and `findNormalizedPort`. Add `endpointUsesInterface()` and `isEndpointOccupied()` using existing connection iteration.

```cpp
bool endpointUsesInterface(const Connection& connection,
                           const QString& moduleId,
                           const QString& interfaceId) {
    return (connection.source().moduleId == moduleId && connection.source().portId == interfaceId) ||
           (connection.target().moduleId == moduleId && connection.target().portId == interfaceId);
}
```

- [ ] **Step 5: Include the new source in tests**

In `qt/xmake.lua`, add `src/**/graphqueries.cpp` to `graph_test`, `validation_test`, and `projectdocument_test`.

- [ ] **Step 6: Run the focused test and commit**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: `graph_test passed`.

Commit:

```bash
git add qt/inc/graph/graphqueries.h qt/src/graph/graphqueries.cpp qt/src/graph/graph.cpp qt/test/graph_test.cpp qt/xmake.lua
git commit -m "refactor: extract graph query helpers"
```

## Task 2: Add Generic Graph Rule Engine

**Files:**
- Create: `qt/inc/graph/graphrules.h`
- Create: `qt/src/graph/graphrules.cpp`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing validation tests**

Add tests that validate generic behavior without naming XP or Endpoint.

```cpp
static std::unique_ptr<Module> makeBusModule(const QString& id,
                                             const QString& portId,
                                             Port::Direction direction,
                                             const QString& bus) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Generic"));
    module->addPort(Port(portId, direction, QStringLiteral("bus"), portId, {},
                         QStringLiteral("generic"), bus, portId));
    return module;
}

static void testGraphRulesAcceptInOutEitherDirection() {
    Graph graph;
    require(graph.addModule(makeBusModule(QStringLiteral("a"), QStringLiteral("if0"),
                                          Port::Direction::InOut, QStringLiteral("axi"))), "add a");
    require(graph.addModule(makeBusModule(QStringLiteral("b"), QStringLiteral("if0"),
                                          Port::Direction::InOut, QStringLiteral("axi"))), "add b");

    require(graph.isValidConnection({QStringLiteral("a"), QStringLiteral("if0")},
                                    {QStringLiteral("b"), QStringLiteral("if0")}),
            "inout interface should be valid in source-target order");
    require(graph.isValidConnection({QStringLiteral("b"), QStringLiteral("if0")},
                                    {QStringLiteral("a"), QStringLiteral("if0")}),
            "inout interface should be valid in reverse order");
}
```

- [ ] **Step 2: Run and verify the missing engine failure**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: compile failure until `GraphRuleEngine` exists and `Graph` includes it.

- [ ] **Step 3: Define validation types**

Create `qt/inc/graph/graphrules.h`:

```cpp
#pragma once

#include "graph/portref.h"
#include <QString>

class Graph;

struct GraphValidationResult {
    enum class Code {
        Valid,
        SameModule,
        MissingModule,
        MissingPort,
        DirectionMismatch,
        BusMismatch,
        InterfaceMismatch,
        InterfaceCapacityExceeded,
        PolicyRejected
    };

    Code code = Code::Valid;
    QString message;

    bool valid() const { return code == Code::Valid; }
    static GraphValidationResult ok();
    static GraphValidationResult reject(Code code, const QString& message);
};

class GraphRuleEngine {
public:
    static GraphValidationResult validateConnection(const Graph& graph,
                                                    const PortRef& source,
                                                    const PortRef& target);
};
```

- [ ] **Step 4: Implement generic rules**

Create `qt/src/graph/graphrules.cpp` and move generic checks from `Graph::isValidConnection()` into `GraphRuleEngine::validateConnection()`: same-module rejection, module lookup, normalized port lookup, direction, bus family, and interface compatibility. Keep interface compatibility helpers private in this file.

```cpp
GraphValidationResult GraphValidationResult::ok() {
    return {};
}

GraphValidationResult GraphValidationResult::reject(Code code, const QString& message) {
    GraphValidationResult result;
    result.code = code;
    result.message = message;
    return result;
}
```

- [ ] **Step 5: Delegate from Graph**

Replace the body of `Graph::isValidConnection()` in `qt/src/graph/graph.cpp` with:

```cpp
bool Graph::isValidConnection(const PortRef& source, const PortRef& target) const {
    return GraphRuleEngine::validateConnection(*this, source, target).valid();
}
```

- [ ] **Step 6: Run tests and commit**

Run:

```bash
xmake build graph_test validation_test projectdocument_test && xmake run graph_test && xmake run validation_test && xmake run projectdocument_test
```

Expected: all three tests pass.

Commit:

```bash
git add qt/inc/graph/graphrules.h qt/src/graph/graphrules.cpp qt/src/graph/graph.cpp qt/test/graph_test.cpp qt/xmake.lua
git commit -m "refactor: route graph validation through rule engine"
```

## Task 3: Isolate NoC Connection Rules

**Files:**
- Create: `qt/inc/graph/nocgraphpolicy.h`
- Create: `qt/src/graph/nocgraphpolicy.cpp`
- Modify: `qt/src/graph/graphrules.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing NoC policy regression tests**

Add a test proving router-link opposite interfaces and one-connection-per-interface behavior remain enforced.

```cpp
static void testNoCPolicyRejectsReusedRouterInterface() {
    Graph graph;
    require(graph.addModule(makeXp(QStringLiteral("xp_a"))), "add xp_a");
    require(graph.addModule(makeXp(QStringLiteral("xp_b"))), "add xp_b");
    require(graph.addModule(makeXp(QStringLiteral("xp_c"))), "add xp_c");

    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("c0"),
        PortRef{QStringLiteral("xp_a"), QStringLiteral("east")},
        PortRef{QStringLiteral("xp_b"), QStringLiteral("west")}));

    require(!graph.isValidConnection(PortRef{QStringLiteral("xp_a"), QStringLiteral("east")},
                                     PortRef{QStringLiteral("xp_c"), QStringLiteral("west")}),
            "router interface cannot be reused");
}
```

- [ ] **Step 2: Run and verify current behavior before extraction**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: test passes before extraction; keep it as a guard while moving code.

- [ ] **Step 3: Define the policy API**

Create `qt/inc/graph/nocgraphpolicy.h`:

```cpp
#pragma once

#include "graph/graphrules.h"
#include "graph/portref.h"

class Graph;
class Module;
class Port;

class NoCGraphPolicy {
public:
    static bool appliesTo(const Module* sourceModule,
                          const Port* sourcePort,
                          const Module* targetModule,
                          const Port* targetPort);

    static GraphValidationResult validateConnection(const Graph& graph,
                                                    const PortRef& source,
                                                    const PortRef& target);
};
```

- [ ] **Step 4: Move mesh rules into the policy**

Create `qt/src/graph/nocgraphpolicy.cpp` by moving `isMeshRouterModule()`, `isRouterLink()`, `connectionUsesRouterSide()`, and opposite-interface checks from `graph.cpp`/`graphrules.cpp`. Use `ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router")` only inside this file.

```cpp
bool NoCGraphPolicy::appliesTo(const Module* sourceModule,
                               const Port* sourcePort,
                               const Module* targetModule,
                               const Port* targetPort) {
    return sourceModule && targetModule && sourcePort && targetPort &&
           ModuleTypeMetadata::hasEditorLayout(sourceModule, u"mesh_router") &&
           ModuleTypeMetadata::hasEditorLayout(targetModule, u"mesh_router") &&
           PortLayout::isRouterPort(*sourcePort) &&
           PortLayout::isRouterPort(*targetPort);
}
```

- [ ] **Step 5: Call the policy from the rule engine**

In `GraphRuleEngine::validateConnection()`, after generic compatibility succeeds, call `NoCGraphPolicy::validateConnection()` when `NoCGraphPolicy::appliesTo()` returns true.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
xmake build graph_test validation_test && xmake run graph_test && xmake run validation_test
```

Expected: `graph_test passed` and `validation_test passed`.

Commit:

```bash
git add qt/inc/graph/nocgraphpolicy.h qt/src/graph/nocgraphpolicy.cpp qt/src/graph/graphrules.cpp qt/test/graph_test.cpp qt/xmake.lua
git commit -m "refactor: isolate noc graph connection policy"
```

## Task 4: Move NoC Framework JSON To Projection

**Files:**
- Create: `qt/inc/graph/nocgraphprojection.h`
- Create: `qt/src/graph/nocgraphprojection.cpp`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add projection round-trip tests**

Add a test that imports framework JSON with `xps`, `endpoints`, and links, then exports it again through `Graph::toJsonDocument(..., GraphJsonFlavor::Framework)`.

```cpp
static void testNoCProjectionKeepsFrameworkShape() {
    const QJsonObject project{
        {QStringLiteral("design"), QStringLiteral("mesh")},
        {QStringLiteral("xps"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("xp_0_0")},
                        {QStringLiteral("x"), 0},
                        {QStringLiteral("y"), 0},
                        {QStringLiteral("links"), QJsonArray{
                            QJsonObject{{QStringLiteral("dir"), QStringLiteral("east")},
                                        {QStringLiteral("target"), QStringLiteral("xp_1_0")}}
                        }}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("xp_1_0")},
                        {QStringLiteral("x"), 1},
                        {QStringLiteral("y"), 0}}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("ep_0")},
                        {QStringLiteral("xp"), QStringLiteral("xp_0_0")},
                        {QStringLiteral("port"), QStringLiteral("local0")}}
        }}
    };

    Graph graph;
    QString error;
    require(NoCGraphProjection::importFrameworkJson(project, graph, &error), qPrintable(error));
    QJsonDocument exported = NoCGraphProjection::exportFrameworkJson(graph, QStringLiteral("mesh"), nullptr);
    require(exported.object().contains(QStringLiteral("xps")), "framework export contains xps");
    require(exported.object().contains(QStringLiteral("endpoints")), "framework export contains endpoints");
}
```

- [ ] **Step 2: Run and verify compile failure**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: compile failure because `NoCGraphProjection` does not exist.

- [ ] **Step 3: Define projection API**

Create `qt/inc/graph/nocgraphprojection.h`:

```cpp
#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

class Graph;

class NoCGraphProjection {
public:
    static bool importFrameworkJson(const QJsonObject& object,
                                    Graph& graph,
                                    QString* errorMessage);
    static QJsonDocument exportFrameworkJson(const Graph& graph,
                                             const QString& designName,
                                             QHash<QString, QString>* externalToInternalIds);
};
```

- [ ] **Step 4: Move framework import/export code**

Create `qt/src/graph/nocgraphprojection.cpp` by moving the framework branch from `Graph::loadFromJson()` and the framework branch from `Graph::toJsonDocument()`. Keep helper functions for module instantiation, external id generation, endpoint list projection, and router link projection in this file.

- [ ] **Step 5: Delegate from Graph**

In `qt/src/graph/graph.cpp`, keep the existing public API and delegate:

```cpp
if (root.contains(QStringLiteral("xps")) || root.contains(QStringLiteral("endpoints"))) {
    QString errorMessage;
    if (!NoCGraphProjection::importFrameworkJson(root, *this, &errorMessage)) {
        qWarning() << errorMessage;
        return false;
    }
    return true;
}
```

For `GraphJsonFlavor::Framework`, return `NoCGraphProjection::exportFrameworkJson(*this, designName, externalToInternalIds)`.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
xmake build graph_test projectdocument_test && xmake run graph_test && xmake run projectdocument_test
```

Expected: both tests pass.

Commit:

```bash
git add qt/inc/graph/nocgraphprojection.h qt/src/graph/nocgraphprojection.cpp qt/src/graph/graph.cpp qt/test/graph_test.cpp qt/xmake.lua
git commit -m "refactor: move noc framework json to projection"
```

## Task 5: Make Graph Loading Atomic For Projection Imports

**Files:**
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/src/graph/nocgraphprojection.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Add failed-import preservation tests**

Extend `qt/test/graph_test.cpp` to create a graph with one module, import framework JSON with duplicate local port reuse, and assert the original graph remains intact.

```cpp
static void testFailedFrameworkImportDoesNotClearGraph() {
    Graph graph;
    require(graph.addModule(makeBusModule(QStringLiteral("existing"), QStringLiteral("if0"),
                                          Port::Direction::InOut, QStringLiteral("axi"))), "add existing");

    const QJsonObject malformed{
        {QStringLiteral("xps"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("xp_0_0")}}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("ep_a")},
                        {QStringLiteral("xp"), QStringLiteral("xp_0_0")},
                        {QStringLiteral("port"), QStringLiteral("local0")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("ep_b")},
                        {QStringLiteral("xp"), QStringLiteral("xp_0_0")},
                        {QStringLiteral("port"), QStringLiteral("local0")}}
        }}
    };

    QString error;
    require(!NoCGraphProjection::importFrameworkJson(malformed, graph, &error),
            "duplicate endpoint attachment should fail");
    require(graph.getModule(QStringLiteral("existing")) != nullptr,
            "failed import keeps original graph");
}
```

- [ ] **Step 2: Run and verify failure**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: FAIL if the projection clears the live graph before full validation.

- [ ] **Step 3: Validate through a temporary graph**

In `NoCGraphProjection::importFrameworkJson()`, build into a local `Graph staged;`, add all modules and connections to `staged`, and only swap into the live graph after every connection has passed validation.

Use existing public operations to apply the staged result:

```cpp
target.clear();
for (const auto& module : staged.modules()) {
    target.addModule(module->clone());
}
for (const auto& connection : staged.connections()) {
    target.addConnection(std::make_unique<Connection>(
        connection->id(), connection->source(), connection->target()));
}
```

- [ ] **Step 4: Run tests and commit**

Run:

```bash
xmake build graph_test projectdocument_test && xmake run graph_test && xmake run projectdocument_test
```

Expected: both tests pass.

Commit:

```bash
git add qt/src/graph/graph.cpp qt/src/graph/nocgraphprojection.cpp qt/test/graph_test.cpp qt/test/projectdocument_test.cpp
git commit -m "fix: stage graph imports before replacing live graph"
```

## Task 6: Replace PortLayout Semantics With Metadata Queries

**Files:**
- Modify: `qt/inc/common/portlayout.h`
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/inc/modules/moduletypemetadata.h`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `qt/src/graph/nocgraphpolicy.cpp`
- Modify: `qt/src/graph/graphqueries.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `spec/noc/noc.yaml`
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add alias metadata tests**

In `spec_generator/test/spec_generator_test.rb`, assert generated modules carry aliases for legacy ids.

```ruby
assert_includes modules_xml, '<alias port="east_out" interface="east" />'
assert_includes modules_xml, '<alias port="east_in" interface="east" />'
assert_includes modules_xml, '<alias port="ep0" interface="local0" />'
```

- [ ] **Step 2: Add Qt alias lookup tests**

In `qt/test/graph_test.cpp`, assert `GraphQueries::normalizePortId()` uses metadata aliases before the legacy fallback table.

```cpp
require(GraphQueries::normalizePortId(module, QStringLiteral("east_out")) == QStringLiteral("east"),
        "alias east_out resolves to east");
require(GraphQueries::normalizePortId(module, QStringLiteral("ep0")) == QStringLiteral("local0"),
        "alias ep0 resolves to local0");
```

- [ ] **Step 3: Run and verify missing alias support**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
xmake build graph_test && xmake run graph_test
```

Expected: generator assertions fail until aliases are emitted and Qt alias parsing exists.

- [ ] **Step 4: Emit and parse aliases**

Add `aliases` under `interfaces` in `spec/noc/noc.yaml`. Extend `spec_generator/lib/spec_generator.rb` to emit alias XML next to interface metadata. Add this field to `ModuleType` in `qt/inc/modules/moduleregistry.h`, expose lookup through `ModuleTypeMetadata`, and parse it in `qt/src/modules/moduleprovider.cpp`.

```cpp
QHash<QString, QString> legacyPortAliases;
```

- [ ] **Step 5: Reduce `PortLayout`**

Keep these helpers in `qt/inc/common/portlayout.h` because they are generic:

```cpp
bool supportsInput(const Port& port);
bool supportsOutput(const Port& port);
QString directionLabel(const Port& port);
bool sameBusFamily(const Port& lhs, const Port& rhs);
```

Move router side, endpoint slot, and opposite side logic to `NoCGraphPolicy` or metadata alias handling.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
xmake build graph_test validation_test && xmake run graph_test && xmake run validation_test
```

Expected: all listed tests pass.

Commit:

```bash
git add qt/inc/common/portlayout.h qt/inc/modules/moduleregistry.h qt/inc/modules/moduletypemetadata.h qt/src/modules/moduleprovider.cpp qt/src/graph/nocgraphpolicy.cpp qt/src/graph/graphqueries.cpp qt/test/graph_test.cpp spec/noc/noc.yaml spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb
git commit -m "refactor: move legacy port names into metadata aliases"
```

## Task 7: Extract Interface View Policy

**Files:**
- Create: `qt/inc/nodeeditor/interfaceviewpolicy.h`
- Create: `qt/src/nodeeditor/interfaceviewpolicy.cpp`
- Modify: `qt/src/nodeeditor/graphnodegeometry.cpp`
- Modify: `qt/src/nodeeditor/graphnodepainter.cpp`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/src/nodeeditor/straightconnectionpainter.cpp`
- Modify: `qt/test/nodeeditor_geometry_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing metadata fallback tests**

In `qt/test/nodeeditor_geometry_test.cpp`, add a module with no authored anchors and three interfaces. Assert fallback positions are distributed on one edge and labels come from interface metadata.

```cpp
static void testFallbackLayoutUsesInterfaceCount() {
    ModuleType type;
    type.name = QStringLiteral("FallbackNode");
    type.defaultPorts.push_back(Port(QStringLiteral("if0"), Port::Direction::InOut,
                                     QStringLiteral("bus"), QStringLiteral("Iface 0"), {},
                                     QStringLiteral("generic"), QStringLiteral("axi"),
                                     QStringLiteral("if0")));
    type.defaultPorts.push_back(Port(QStringLiteral("if1"), Port::Direction::InOut,
                                     QStringLiteral("bus"), QStringLiteral("Iface 1"), {},
                                     QStringLiteral("generic"), QStringLiteral("axi"),
                                     QStringLiteral("if1")));
    type.defaultPorts.push_back(Port(QStringLiteral("if2"), Port::Direction::InOut,
                                     QStringLiteral("bus"), QStringLiteral("Iface 2"), {},
                                     QStringLiteral("generic"), QStringLiteral("axi"),
                                     QStringLiteral("if2")));
    ModuleRegistry::instance().registerType(type);

    Module module(QStringLiteral("n"), QStringLiteral("FallbackNode"));
    for (const Port& port : type.defaultPorts) module.addPort(port);

    const QVector<QPointF> positions = InterfaceViewPolicy::fallbackInterfacePositions(
        &module, QSizeF(120.0, 90.0));
    require(positions.size() == 3, "fallback exposes all interfaces");
    require(positions.at(0).y() < positions.at(1).y(), "fallback positions are ordered");
}
```

- [ ] **Step 2: Run and verify compile failure**

Run:

```bash
xmake build nodeeditor_geometry_test && QT_QPA_PLATFORM=offscreen xmake run nodeeditor_geometry_test
```

Expected: compile failure because `InterfaceViewPolicy` does not exist.

- [ ] **Step 3: Define the policy API**

Create `qt/inc/nodeeditor/interfaceviewpolicy.h`:

```cpp
#pragma once

#include "graph/module.h"
#include "graph/port.h"
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace InterfaceViewPolicy {

QPointF portPosition(const Module* module,
                     const Port& port,
                     const QSizeF& nodeSize,
                     bool collapsed);
QRectF labelRect(const Module* module,
                 const Port& port,
                 const QSizeF& nodeSize,
                 bool collapsed);
QString label(const Module* module, const Port& port);
QVector<QPointF> fallbackInterfacePositions(const Module* module,
                                            const QSizeF& nodeSize);
QPointF preferredNormal(const Module* module,
                        const Port& port,
                        const QSizeF& nodeSize,
                        const QPointF& connectedNodeCenter);

} // namespace InterfaceViewPolicy
```

- [ ] **Step 4: Implement metadata-first view behavior**

Create `qt/src/nodeeditor/interfaceviewpolicy.cpp`. It should check `ModuleTypeMetadata::interfaceAnchor(module, port)` first, scale `x/y` to current node size, use `label_x/label_y` when present, and fall back to evenly spaced positions on the right edge for `InOut`/output interfaces and left edge for input-only interfaces.

- [ ] **Step 5: Replace direct geometry decisions**

In `GraphNodeGeometry`, `GraphNodePainter`, `NodeEditorWidget`, and `StraightConnectionPainter`, call `InterfaceViewPolicy` for port positions, text rectangles, labels, and preferred normals. Keep existing layout-specific functions private until their callers are removed, then delete unused code.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
xmake build nodeeditor_geometry_test && QT_QPA_PLATFORM=offscreen xmake run nodeeditor_geometry_test
```

Expected: `nodeeditor_geometry_test passed`.

Commit:

```bash
git add qt/inc/nodeeditor/interfaceviewpolicy.h qt/src/nodeeditor/interfaceviewpolicy.cpp qt/src/nodeeditor/graphnodegeometry.cpp qt/src/nodeeditor/graphnodepainter.cpp qt/src/nodeeditor/nodeeditorwidget.cpp qt/src/nodeeditor/straightconnectionpainter.cpp qt/test/nodeeditor_geometry_test.cpp qt/xmake.lua
git commit -m "refactor: drive node editor interfaces from view policy"
```

## Task 8: Move Arrange Command To Policy Queries

**Files:**
- Modify: `qt/src/commands/arrangecommand.cpp`
- Modify: `qt/inc/commands/arrangecommand.h`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Add arrangement graph-group regression**

In `qt/test/graph_test.cpp`, register renamed NoC-like module types with `graph_group: xps` and `graph_group: endpoints`, then assert arrangement helpers identify them through metadata rather than type names.

```cpp
static void testArrangementUsesGraphGroups() {
    ModuleType router;
    router.name = QStringLiteral("RouterTile");
    router.graphGroup = QStringLiteral("xps");
    router.editorLayout = QStringLiteral("mesh_router");
    ModuleRegistry::instance().registerType(router);

    Module module(QStringLiteral("r0"), QStringLiteral("RouterTile"));
    require(ModuleTypeMetadata::isInGraphGroup(&module, u"xps"), "router group metadata is used");
}
```

- [ ] **Step 2: Move NoC helpers out of local lambdas**

Replace local helpers such as `isEndpointModule()`, `endpointSlotForModule()`, `xpForEndpoint()`, side delta logic, and endpoint slot counting with calls to `NoCGraphPolicy` and `GraphQueries`.

- [ ] **Step 3: Run command-adjacent tests**

Run:

```bash
xmake build graph_test validation_test && xmake run graph_test && xmake run validation_test
```

Expected: both tests pass.

- [ ] **Step 4: Commit**

```bash
git add qt/src/commands/arrangecommand.cpp qt/inc/commands/arrangecommand.h qt/test/graph_test.cpp
git commit -m "refactor: arrange noc graphs through policy queries"
```

## Task 9: Clean DRC And External ID Mapping

**Files:**
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/src/graph/nocgraphprojection.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Add renamed-module validation test**

In `qt/test/validation_test.cpp`, construct modules with renamed types and NoC graph groups, run DRC mapping, and assert messages identify the correct internal module ids without relying on `XP` or `Endpoint`.

```cpp
static void testValidationUsesProjectionIdMap() {
    Graph graph;
    require(graph.addModule(makeXp(QStringLiteral("router_a"))), "add router");
    QHash<QString, QString> externalToInternal;
    NoCGraphProjection::exportFrameworkJson(graph, QStringLiteral("design"), &externalToInternal);
    require(externalToInternal.contains(QStringLiteral("router_a")), "projection records external id");
}
```

- [ ] **Step 2: Move external id logic into projection**

Keep `externalToInternalIds` population inside `NoCGraphProjection::exportFrameworkJson()`. In `drcrunner.cpp`, consume that map and remove parsing that searches for XP/Endpoint text in messages when a structured id is available.

- [ ] **Step 3: Run validation tests and commit**

Run:

```bash
xmake build validation_test && xmake run validation_test
```

Expected: `validation_test passed`.

Commit:

```bash
git add qt/src/validation/drcrunner.cpp qt/src/graph/nocgraphprojection.cpp qt/test/validation_test.cpp
git commit -m "refactor: map drc ids through graph projection"
```

## Task 10: Remove NoC Terms From Graph Core

**Files:**
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Scan graph core for NoC terms**

Run:

```bash
rg -n "mesh_router|Endpoint|XP|endpoint|router|east|west|north|south|xps|endpoints|ep[0-9]" qt/src/graph/graph.cpp qt/inc/graph/graph.h
```

Expected: no matches in `graph.cpp` or `graph.h` except comments that describe delegated projections. Remove those comments if they imply graph-owned NoC semantics.

- [ ] **Step 2: Remove unused includes and helpers**

Delete unused includes from `qt/src/graph/graph.cpp`, including `common/portlayout.h` if no generic graph code needs it. Delete anonymous namespace helpers that moved to `GraphQueries`, `GraphRuleEngine`, `NoCGraphPolicy`, and `NoCGraphProjection`.

- [ ] **Step 3: Run graph tests**

Run:

```bash
xmake build graph_test projectdocument_test validation_test && xmake run graph_test && xmake run projectdocument_test && xmake run validation_test
```

Expected: all three tests pass.

- [ ] **Step 4: Commit**

```bash
git add qt/src/graph/graph.cpp qt/inc/graph/graph.h qt/test/graph_test.cpp
git commit -m "refactor: keep graph core generic"
```

## Task 11: Full Regression And Formatting

**Files:**
- Verify all modified files.

- [ ] **Step 1: Run Qt build**

Run:

```bash
xmake build qt
```

Expected: build succeeds.

- [ ] **Step 2: Run focused Qt tests**

Run:

```bash
xmake build graph_test projectdocument_test validation_test nodeeditor_geometry_test
xmake run graph_test
xmake run projectdocument_test
xmake run validation_test
QT_QPA_PLATFORM=offscreen xmake run nodeeditor_geometry_test
```

Expected: every test prints its `passed` marker.

- [ ] **Step 3: Run generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby plugins/noc/generator/test/test_generator.rb
```

Expected: both Ruby suites pass.

- [ ] **Step 4: Check whitespace**

Run:

```bash
git diff --check
```

Expected: no output.

- [ ] **Step 5: Final commit**

```bash
git add qt spec plugins docs
git commit -m "refactor: abstract graph data operations"
```

## Self Review

- Spec coverage: the plan covers graph core extraction, query helpers, rules, NoC projection, Qt view policy, arrange behavior, DRC mapping, project import atomicity, and generator metadata aliases.
- Placeholder scan: the plan contains concrete files, APIs, tests, commands, and expected results.
- Type consistency: `GraphQueries`, `GraphRuleEngine`, `NoCGraphPolicy`, `NoCGraphProjection`, and `InterfaceViewPolicy` are defined before use in later tasks.
