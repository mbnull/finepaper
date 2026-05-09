# Node 6 Connection Semantics Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use reasoning effort `high` for implementation workers and `xhigh` for architecture review before archive.

**Goal:** Make editor-time connection checking an explicit layered service with structural, feature-plugin declarative, IP-core declarative, and final DRC boundaries.

**Architecture:** `Graph` stays structural: it only knows modules, ports, self-loops, and duplicate exact edges. `ConnectionRuleService` becomes a dispatcher over named rule layers and returns the layer that rejected or accepted a request. The first implementation remains data-driven: feature-style rules use common interface metadata such as bus, role, cardinality, topology, and autocomplete groups; IP-core rules use module IP-core ownership plus spec match/config/accepts fields; final DRC stays outside editor-time acceptance.

**Tech Stack:** C++23, Qt Core, xmake Qt test targets.

---

## File Structure

- Modify `qt/inc/connection/connectionruleservice.h`: add `ConnectionRuleLayer`, expose rejection layer in `ConnectionCheckResult`, rename semantic owner field to `ipcoreId`, and declare layer helpers.
- Modify `qt/src/connection/connectionruleservice.cpp`: split current mixed checks into structural, feature declarative, and IP-core declarative helpers.
- Modify `qt/inc/graph/graph.h`: update comments to make `Graph::isValidConnection()` explicitly structural.
- Modify `qt/src/graph/graph.cpp`: keep `isValidConnection()` structural-only and avoid metadata checks.
- Modify `qt/src/project/graphprojectserializer.cpp`: keep project-load using `ConnectionRuleService` and surface layered reason codes unchanged.
- Modify `qt/src/modules/moduleprovider.cpp`: keep parser behavior and add a focused test fixture path only if parser gaps appear.
- Modify `qt/inc/modules/moduleregistry.h`: keep existing metadata fields; no new persistent schema fields are required for this node.
- Modify `qt/test/connectionruleservice_test.cpp`: add dispatcher ordering, layer assertions, autocomplete-group, cardinality, topology, and IP-core match/scope tests.
- Modify `qt/test/projectdocument_test.cpp`: assert project-load failures expose the layered reason from `ConnectionRuleService`.
- Modify `qt/test/graph_test.cpp`: assert graph accepts structurally valid connections even when semantic metadata would reject them.
- Modify `qt/xmake.lua`: add any new source files only if the split introduces a new `.cpp`; the primary path keeps the service in the existing files.

---

## Final Layer Contract

Add this enum in `qt/inc/connection/connectionruleservice.h`:

```cpp
enum class ConnectionRuleLayer {
    Structural,
    FeaturePlugin,
    Ipcore,
    FinalDrc
};
```

Add this field to `ConnectionCheckResult`:

```cpp
ConnectionRuleLayer layer = ConnectionRuleLayer::Structural;
```

Layer meaning:

- `Structural`: graph/module/port shape, self-loop, exact duplicate.
- `FeaturePlugin`: common declarative behavior such as direction, bus, role compatibility, cardinality, autocomplete grouping, and topology side rules.
- `Ipcore`: concrete IP-core declarative constraints such as same-IP-core scope and interface `match`/`accept`/`config` values.
- `FinalDrc`: reserved boundary marker for generator-backed checks. `ConnectionRuleService` should not run final DRC in Node 6.

For allowed results, set `layer = ConnectionRuleLayer::Ipcore` because the candidate has passed through every editor-time layer. For rejected results, set the layer that produced the rejection.

Rename `PortSemanticInfo::pluginId` to:

```cpp
QString ipcoreId;
```

Populate it from `module->ipcoreId()` when present, otherwise from `ModuleType::pluginId` for hand-built test modules that predate Node 5 ownership stamping.

---

## Task 6.1: Layered Result Tests

**Files:**

- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/graph_test.cpp`

- [x] **Step 1: Add test helpers for layered semantics**

In `qt/test/connectionruleservice_test.cpp`, add these helpers inside the anonymous namespace:

```cpp
std::unique_ptr<Module> makeOwnedModule(const QString& id,
                                        const QString& type,
                                        const QString& ipcoreId) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    return module;
}

ModuleInterfaceMetadata interfaceMetadata(const QString& id,
                                          const QString& bus,
                                          const QString& role,
                                          const QString& connectsTo,
                                          const QString& group = {},
                                          const QString& topologyRule = {}) {
    ModuleInterfaceMetadata metadata;
    metadata.id = id;
    metadata.bus = bus;
    metadata.role = role;
    metadata.compatibleRoles = {connectsTo};
    metadata.cardinality = QStringLiteral("one");
    metadata.autocompleteGroup = group;
    metadata.topologyRule = topologyRule;
    return metadata;
}

ModuleType endpointTypeWithProtocol(const QString& typeName,
                                    const QString& ipcoreId,
                                    const QString& role,
                                    const QString& protocol) {
    ModuleType type;
    type.name = typeName;
    type.pluginId = ipcoreId;
    type.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("NoC"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("endpoint_link"),
                                     QStringLiteral("noc")));

    ModuleInterfaceMetadata metadata =
        interfaceMetadata(QStringLiteral("noc"),
                          QStringLiteral("endpoint_link"),
                          role,
                          role == QStringLiteral("initiator") ? QStringLiteral("target")
                                                              : QStringLiteral("initiator"),
                          QStringLiteral("endpoint_attachment"));
    metadata.matchFields = {QStringLiteral("protocol")};
    metadata.acceptedValues.insert(QStringLiteral("protocol"), QStringList{protocol});
    type.interfaceMetadata.insert(metadata.id, metadata);
    return type;
}
```

Use `module->setIpcoreId(...)` in existing `makeProducer()`, `makeConsumer()`, and `makeRouter()` helpers so tests exercise Node 5 ownership.

- [x] **Step 2: Add dispatcher ordering test**

Add:

```cpp
void testStructuralLayerRunsBeforeSemanticLayers() {
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
    require(result.layer == ConnectionRuleLayer::Structural,
            "missing port should be rejected by structural layer");
    require(result.reasonCode == QStringLiteral("missing_port"),
            "structural rejection should report missing_port");
}
```

Call it from `main()` after `testRejectsMissingPortWithReason()`.

- [x] **Step 3: Add structural duplicate and self-loop tests**

Add:

```cpp
void testDuplicateConnectionIsStructuralRejection() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("existing"),
        PortRef{QStringLiteral("producer"), QStringLiteral("out")},
        PortRef{QStringLiteral("consumer"), QStringLiteral("in")}));

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "duplicate connection should reject");
    require(result.layer == ConnectionRuleLayer::Structural,
            "duplicate connection should be structural");
    require(result.reasonCode == QStringLiteral("duplicate_connection"),
            "duplicate connection should report duplicate_connection");
}

void testSelfLoopIsStructuralRejection() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("router"))), "failed to add router");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("router"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("router"), QStringLiteral("west")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "self-loop should reject");
    require(result.layer == ConnectionRuleLayer::Structural,
            "self-loop should be structural");
    require(result.reasonCode == QStringLiteral("self_loop"),
            "self-loop should report self_loop");
}
```

Call both from `main()`.

- [x] **Step 4: Add layer assertions to existing feature tests**

In existing tests, add:

```cpp
require(result.layer == ConnectionRuleLayer::FeaturePlugin,
        "same-side topology should be rejected by feature layer");
```

to `testRejectsSameSideTopologyRule()`.

Add:

```cpp
require(result.layer == ConnectionRuleLayer::FeaturePlugin,
        "cardinality should be rejected by feature layer");
```

to `testRejectsOccupiedCardinalityOnePort()`.

Add:

```cpp
require(result.layer == ConnectionRuleLayer::Ipcore,
        "allowed connection should pass through IP-core layer");
```

to `testAllowsSimplePortToPortConnection()`.

- [x] **Step 5: Add node-body autocomplete-group test**

Add:

```cpp
void testNodeBodyAutocompleteUsesMatchingGroup() {
    ModuleType host;
    host.name = QStringLiteral("AutocompleteHost");
    host.pluginId = QStringLiteral("finepaper.test");
    host.defaultPorts.push_back(Port(QStringLiteral("router"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Router"),
                                     {},
                                     QStringLiteral("router"),
                                     QStringLiteral("shared_bus"),
                                     QStringLiteral("router")));
    host.defaultPorts.push_back(Port(QStringLiteral("endpoint"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Endpoint"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("shared_bus"),
                                     QStringLiteral("endpoint")));
    host.interfaceMetadata.insert(QStringLiteral("router"),
        interfaceMetadata(QStringLiteral("router"),
                          QStringLiteral("shared_bus"),
                          QStringLiteral("target"),
                          QStringLiteral("initiator"),
                          QStringLiteral("router_side")));
    host.interfaceMetadata.insert(QStringLiteral("endpoint"),
        interfaceMetadata(QStringLiteral("endpoint"),
                          QStringLiteral("shared_bus"),
                          QStringLiteral("target"),
                          QStringLiteral("initiator"),
                          QStringLiteral("endpoint_attachment")));
    ModuleRegistry::instance().registerType(host);

    ModuleType endpoint = endpointTypeWithProtocol(QStringLiteral("AutocompleteEndpoint"),
                                                   QStringLiteral("finepaper.test"),
                                                   QStringLiteral("initiator"),
                                                   QStringLiteral("axi4"));
    endpoint.defaultPorts.front() = Port(QStringLiteral("noc"),
                                         Port::Direction::InOut,
                                         QStringLiteral("bus"),
                                         QStringLiteral("NoC"),
                                         {},
                                         QStringLiteral("attachment"),
                                         QStringLiteral("shared_bus"),
                                         QStringLiteral("noc"));
    endpoint.interfaceMetadata[QStringLiteral("noc")].bus = QStringLiteral("shared_bus");
    ModuleRegistry::instance().registerType(endpoint);

    Graph graph;
    auto source = makeOwnedModule(QStringLiteral("endpoint"), QStringLiteral("AutocompleteEndpoint"), QStringLiteral("finepaper.test"));
    source->addPort(endpoint.defaultPorts.front());
    auto target = makeOwnedModule(QStringLiteral("host"), QStringLiteral("AutocompleteHost"), QStringLiteral("finepaper.test"));
    target->addPort(host.defaultPorts.at(0));
    target->addPort(host.defaultPorts.at(1));
    require(graph.addModule(std::move(source)), "endpoint should add");
    require(graph.addModule(std::move(target)), "host should add");

    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.start.moduleId = QStringLiteral("endpoint");
    request.start.portId = QStringLiteral("noc");
    request.end.moduleId = QStringLiteral("host");
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(request);

    require(result.status == ConnectionCheckStatus::Allowed,
            "node-body autocomplete should find one matching group option");
    require(result.options.size() == 1,
            "autocomplete group should suppress same-bus nonmatching hidden ports");
    require(result.options.first().target.portId == QStringLiteral("endpoint"),
            "node-body autocomplete should choose endpoint_attachment hidden port");
}
```

Call it from `main()`.

- [x] **Step 6: Add IP-core constraint tests**

Add:

```cpp
void testRejectsCrossIpcoreConnectionAtIpcoreLayer() {
    Graph graph;
    auto producer = makeProducer(QStringLiteral("producer"));
    producer->setIpcoreId(QStringLiteral("finepaper.left"));
    auto consumer = makeConsumer(QStringLiteral("consumer"));
    consumer->setIpcoreId(QStringLiteral("finepaper.right"));
    require(graph.addModule(std::move(producer)), "producer should add");
    require(graph.addModule(std::move(consumer)), "consumer should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "cross-IP-core connection should reject");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "cross-IP-core connection should be rejected by IP-core layer");
    require(result.reasonCode == QStringLiteral("ipcore_mismatch"),
            "cross-IP-core rejection should report ipcore_mismatch");
}

void testRejectsInterfaceFieldMismatchAtIpcoreLayer() {
    ModuleRegistry::instance().registerType(endpointTypeWithProtocol(QStringLiteral("AxiEndpoint"),
                                                                     QStringLiteral("finepaper.test"),
                                                                     QStringLiteral("initiator"),
                                                                     QStringLiteral("axi4")));
    ModuleRegistry::instance().registerType(endpointTypeWithProtocol(QStringLiteral("ApbTarget"),
                                                                     QStringLiteral("finepaper.test"),
                                                                     QStringLiteral("target"),
                                                                     QStringLiteral("apb")));

    Graph graph;
    auto source = makeOwnedModule(QStringLiteral("source"), QStringLiteral("AxiEndpoint"), QStringLiteral("finepaper.test"));
    source->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("endpoint_link"), QStringLiteral("noc")));
    auto target = makeOwnedModule(QStringLiteral("target"), QStringLiteral("ApbTarget"), QStringLiteral("finepaper.test"));
    target->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("endpoint_link"), QStringLiteral("noc")));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("source"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("target"), QStringLiteral("noc")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "IP-core interface match mismatch should reject");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "interface field mismatch should be rejected by IP-core layer");
    require(result.reasonCode == QStringLiteral("interface_field_mismatch"),
            "IP-core constraint should report interface_field_mismatch");
}
```

Call both from `main()`.

- [x] **Step 7: Add project-load layered rejection test**

In `qt/test/projectdocument_test.cpp`, add:

```cpp
void testProjectLoadReportsIpcoreConnectionRuleFailure() {
    ModuleType sourceType = makeProjectEndpointType();
    sourceType.name = QStringLiteral("ProjectDocAxiSource");
    sourceType.pluginId = QStringLiteral("finepaper.test");
    sourceType.defaultPorts.clear();
    sourceType.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                           Port::Direction::InOut,
                                           QStringLiteral("bus"),
                                           QStringLiteral("NoC"),
                                           {},
                                           QStringLiteral("attachment"),
                                           QStringLiteral("endpoint_link"),
                                           QStringLiteral("noc")));
    ModuleInterfaceMetadata sourceMeta;
    sourceMeta.id = QStringLiteral("noc");
    sourceMeta.bus = QStringLiteral("endpoint_link");
    sourceMeta.role = QStringLiteral("initiator");
    sourceMeta.compatibleRoles = {QStringLiteral("target")};
    sourceMeta.matchFields = {QStringLiteral("protocol")};
    sourceMeta.acceptedValues.insert(QStringLiteral("protocol"), QStringList{QStringLiteral("axi4")});
    sourceType.interfaceMetadata.insert(sourceMeta.id, sourceMeta);
    ModuleRegistry::instance().registerType(sourceType);

    ModuleType targetType = sourceType;
    targetType.name = QStringLiteral("ProjectDocApbTarget");
    targetType.interfaceMetadata[QStringLiteral("noc")].role = QStringLiteral("target");
    targetType.interfaceMetadata[QStringLiteral("noc")].compatibleRoles = {QStringLiteral("initiator")};
    targetType.interfaceMetadata[QStringLiteral("noc")].acceptedValues.insert(QStringLiteral("protocol"), QStringList{QStringLiteral("apb")});
    ModuleRegistry::instance().registerType(targetType);

    ProjectDocument document;
    document.name = QStringLiteral("ipcore_rule_failure");
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.test"), QStringLiteral("1.0")});
    document.modules.push_back(ProjectModuleRecord{QStringLiteral("source"), QStringLiteral("finepaper.test"), sourceType.name, {}});
    document.modules.push_back(ProjectModuleRecord{QStringLiteral("target"), QStringLiteral("finepaper.test"), targetType.name, {}});
    document.connections.push_back(ProjectConnectionRecord{
        QStringLiteral("bad_connection"),
        ProjectConnectionEndpoint{QStringLiteral("source"), QStringLiteral("noc")},
        ProjectConnectionEndpoint{QStringLiteral("target"), QStringLiteral("noc")}
    });

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "project load should reject IP-core rule mismatch");
    require(result.error.contains(QStringLiteral("interface_field_mismatch")),
            "project load should surface IP-core layer reason code");
    require(graph.modules().empty(), "failed load should not mutate graph");
}
```

Call it from `main()`.

- [x] **Step 8: Add graph structural-only test**

In `qt/test/graph_test.cpp`, add a test that registers two module types with incompatible interface metadata, creates modules with ports, and then calls `graph.isValidConnection(...)` directly.

Use this assertion:

```cpp
require(graph.isValidConnection(PortRef{QStringLiteral("source"), QStringLiteral("out")},
                                PortRef{QStringLiteral("target"), QStringLiteral("in")}),
        "Graph structural validation should not reject semantic bus/role mismatches");
```

Call it from `main()`.

- [x] **Step 9: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
```

Expected:

- `connectionruleservice_test` fails to compile because `ConnectionRuleLayer` and `ConnectionCheckResult::layer` do not exist.
- `projectdocument_test` fails until layered reason behavior is implemented.
- `graph_test` should pass if `Graph` is already structural; keep it as a regression guard.

---

## Task 6.2: Split ConnectionRuleService Internals

**Files:**

- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`

- [x] **Step 1: Add public layer type and result field**

In `qt/inc/connection/connectionruleservice.h`, add the enum from the Final Layer Contract before `ConnectionCheckStatus`.

Update `ConnectionCheckResult`:

```cpp
ConnectionRuleLayer layer = ConnectionRuleLayer::Structural;
```

Update `PortSemanticInfo`:

```cpp
QString ipcoreId;
```

Remove or rename `pluginId`.

- [x] **Step 2: Replace reject helper with layered reject helper**

Update declaration:

```cpp
ConnectionCheckResult reject(ConnectionRuleLayer layer,
                             QString reasonCode,
                             QString message) const;
```

Implementation:

```cpp
ConnectionCheckResult ConnectionRuleService::reject(ConnectionRuleLayer layer,
                                                    QString reasonCode,
                                                    QString message) const {
    ConnectionCheckResult result;
    result.status = ConnectionCheckStatus::Rejected;
    result.layer = layer;
    result.reasonCode = std::move(reasonCode);
    result.message = std::move(message);
    return result;
}
```

- [x] **Step 3: Add structural evaluation helpers**

Add these helpers:

```cpp
struct StructuralCheckResult {
    bool passed = false;
    QString reasonCode;
    QString message;
};

StructuralCheckResult passedStructuralCheck() {
    StructuralCheckResult result;
    result.passed = true;
    return result;
}

StructuralCheckResult failedStructuralCheck(QString reasonCode, QString message) {
    StructuralCheckResult result;
    result.reasonCode = std::move(reasonCode);
    result.message = std::move(message);
    return result;
}
```

Add a private helper in the service:

```cpp
StructuralCheckResult checkStructuralRules(const ConnectionRequest& request) const;
```

This helper owns:

- missing graph -> `missing_graph`;
- missing module -> `missing_module`;
- missing explicit port or empty node-body candidate set -> `missing_port`;
- self-loop candidate -> `self_loop`;
- exact duplicate source-target candidate -> `duplicate_connection`.

Every rejection from this helper must use `ConnectionRuleLayer::Structural`.

- [x] **Step 4: Add candidate evaluation type**

In `connectionruleservice.cpp` anonymous namespace, add:

```cpp
struct CandidateEvaluation {
    bool accepted = false;
    ConnectionRuleLayer layer = ConnectionRuleLayer::FeaturePlugin;
    QString reasonCode;
    QString message;
};

CandidateEvaluation acceptedCandidate() {
    CandidateEvaluation evaluation;
    evaluation.accepted = true;
    evaluation.layer = ConnectionRuleLayer::Ipcore;
    return evaluation;
}

CandidateEvaluation rejectedCandidate(ConnectionRuleLayer layer,
                                      QString reasonCode,
                                      QString message) {
    CandidateEvaluation evaluation;
    evaluation.layer = layer;
    evaluation.reasonCode = std::move(reasonCode);
    evaluation.message = std::move(message);
    return evaluation;
}
```

- [x] **Step 5: Split feature declarative rules**

Move these checks out of `tryAppendOption()` into:

```cpp
CandidateEvaluation checkFeatureDeclarativeRules(const Graph* graph,
                                                 const PortSemanticInfo& source,
                                                 const PortSemanticInfo& target,
                                                 const ConnectionEndpointRequest& sourceEndpoint,
                                                 const ConnectionEndpointRequest& targetEndpoint);
```

The function owns these existing checks and reason codes:

- visual side / source-target support -> `direction_mismatch`;
- cardinality-one occupation -> `port_occupied`;
- topology side rule -> `topology_rule_mismatch`;
- bus mismatch -> `bus_mismatch`;
- interface role mismatch -> `interface_role_mismatch`.

Return `ConnectionRuleLayer::FeaturePlugin` for every rejection in this function.

- [x] **Step 6: Split IP-core declarative constraints**

Move match/config/accepts value overlap out of `metadataCompatible()` into:

```cpp
CandidateEvaluation checkIpcoreDeclarativeConstraints(const PortSemanticInfo& source,
                                                      const PortSemanticInfo& target);
```

This function first checks same IP-core scope:

```cpp
    if (!source.ipcoreId.isEmpty() &&
        !target.ipcoreId.isEmpty() &&
        source.ipcoreId != target.ipcoreId) {
        return rejectedCandidate(ConnectionRuleLayer::Ipcore,
                                 QStringLiteral("ipcore_mismatch"),
                                 QStringLiteral("Connection endpoints belong to different IP cores"));
    }
```

Then it checks `matchFieldValues` overlap and returns:

```cpp
return rejectedCandidate(ConnectionRuleLayer::Ipcore,
                         QStringLiteral("interface_field_mismatch"),
                         QStringLiteral("Connection interface field values do not overlap"));
```

When accepted, return `acceptedCandidate()`.

- [x] **Step 7: Add autocomplete-group filtering**

In `resolveEndpointPorts()`, after collecting hidden node-body ports, if the opposite endpoint has a visible port with a non-empty `autocompleteGroup`, filter hidden candidates to matching group.

Implement a helper in `buildOptions()` before candidate loops:

```cpp
QVector<PortSemanticInfo> filterAutocompleteCandidates(const QVector<PortSemanticInfo>& fixedPorts,
                                                       const QVector<PortSemanticInfo>& hiddenPorts) {
    QStringList groups;
    for (const PortSemanticInfo& port : fixedPorts) {
        if (!port.autocompleteGroup.isEmpty() && !groups.contains(port.autocompleteGroup)) {
            groups.append(port.autocompleteGroup);
        }
    }
    if (groups.isEmpty()) {
        return hiddenPorts;
    }

    QVector<PortSemanticInfo> filtered;
    for (const PortSemanticInfo& port : hiddenPorts) {
        if (groups.contains(port.autocompleteGroup)) {
            filtered.push_back(port);
        }
    }
    return filtered.isEmpty() ? hiddenPorts : filtered;
}
```

At the start of `buildOptions()`:

```cpp
const QVector<PortSemanticInfo> effectiveStartPorts =
    request.start.fromNodeBody ? filterAutocompleteCandidates(endPorts, startPorts) : startPorts;
const QVector<PortSemanticInfo> effectiveEndPorts =
    request.end.fromNodeBody ? filterAutocompleteCandidates(startPorts, endPorts) : endPorts;
```

Iterate over `effectiveStartPorts` and `effectiveEndPorts`.

- [x] **Step 8: Update tryAppendOption**

Inside `tryAppendOption`, run layers in order:

```cpp
const CandidateEvaluation feature =
    checkFeatureDeclarativeRules(m_graph, source, target, sourceEndpoint, targetEndpoint);
if (!feature.accepted) {
    if (rejectionLayer) *rejectionLayer = feature.layer;
    if (rejectionReason) *rejectionReason = feature.reasonCode;
    if (rejectionMessage) *rejectionMessage = feature.message;
    return;
}

const CandidateEvaluation ipcore = checkIpcoreDeclarativeConstraints(source, target);
if (!ipcore.accepted) {
    if (rejectionLayer) *rejectionLayer = ipcore.layer;
    if (rejectionReason) *rejectionReason = ipcore.reasonCode;
    if (rejectionMessage) *rejectionMessage = ipcore.message;
    return;
}
```

Add `ConnectionRuleLayer* rejectionLayer` parameter to `buildOptions()`.

- [x] **Step 9: Set allowed result layer**

In `check()` after sorting options:

```cpp
result.layer = ConnectionRuleLayer::Ipcore;
```

For structural failures:

```cpp
return reject(ConnectionRuleLayer::Structural,
              QStringLiteral("missing_module"),
              QStringLiteral("Connection references a missing module"));
```

Before building options, call `checkStructuralRules(request)`. If it fails, return its reason with layer `Structural`.

For empty options:

```cpp
return reject(layer,
              reason.isEmpty() ? QStringLiteral("no_connection_option") : reason,
              message.isEmpty() ? QStringLiteral("No legal connection option") : message);
```

Default `layer` should be `ConnectionRuleLayer::FeaturePlugin`.

- [x] **Step 10: Keep Graph comments structural**

In `qt/inc/graph/graph.h`, update the comment for `isValidConnection()`:

```cpp
// Validates structural integrity only: endpoints exist, no self-loop, no exact duplicate.
// Semantic/editor rules live in ConnectionRuleService.
```

Do not add metadata, IP-core, or DRC checks to `Graph`.

- [x] **Step 11: Run layered service tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: all pass.

---

## Task 6.3: Verification And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-6-connection-semantics-split.md`

- [x] **Step 1: Run required verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake build -P qt qt
git diff --check
```

Expected: all commands pass.

- [x] **Step 2: Run stale mixed-semantics scan**

Run:

```bash
rg -n "PortSemanticInfo.*pluginId|info\\.pluginId|metadataCompatible|TopologyPresetBuilder::apply\\(m_graph|application/x-moduletype" qt/inc/connection qt/src/connection qt/test/connectionruleservice_test.cpp
```

Expected:

- no stale `PortSemanticInfo::pluginId` owner field or assignment in connection service files;
- `ModuleType::pluginId` test fixture ownership values are allowed;
- no `metadataCompatible` helper remains;
- no unrelated stale topology or legacy MIME hits.

- [x] **Step 3: Architecture review**

Ask an `xhigh` reviewer/supervisor to inspect:

- `Graph::isValidConnection()` remains structural-only;
- `ConnectionRuleService` returns meaningful `ConnectionRuleLayer`;
- feature-plugin rules and IP-core constraints are explicit helpers;
- no final DRC/generator calls were added to editor-time checking;
- tests cover dispatcher ordering, cardinality, topology, autocomplete, project-load reason, and IP-core constraints.

- [x] **Step 4: Supervisor preflight**

Send standing supervisor:

- verification command results;
- stale scan result;
- `git diff --stat`;
- confirmation that helper artifacts `.codex/`, `.superpowers/`, and `image.png` are not staged.

- [x] **Step 5: Archive Node 6**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-6-connection-semantics-split.md
git commit -m "archive: complete node-6 connection semantics split"
```

Expected: archive commit succeeds and helper artifacts remain uncommitted.

---

## Self-Review

- Spec coverage: The plan defines the four layers, keeps `Graph` structural, makes `ConnectionRuleService` a named-layer dispatcher, and leaves final DRC as a boundary instead of adding editor-time command execution.
- Scope control: No native plugin runtime or trust mechanism is introduced. No legacy compatibility path is added.
- Test coverage: The plan adds direct tests for dispatcher ordering, feature-layer cardinality/topology/autocomplete, IP-core scope and match constraints, project-load rejection reasons, and graph structural-only behavior.
