# Node 7 IP-Core Generation Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use reasoning effort `high` for implementation workers and `xhigh` for architecture review before archive.

**Goal:** Move generator/DRC JSON export out of `Graph`, rename the external input vocabulary, and invoke generation/DRC through the selected IP core instance.

**Architecture:** `Graph` remains the editor topology store and structural guard only. `IpCoreGraphExporter` becomes the generator/DRC serialization boundary and receives the graph, selected catalog entry, selected project IP instance record, and design name. `GeneratorRunner` resolves commands from a selected IP catalog entry instead of inferring a plugin from graph modules; Ruby generators consume `finepaper-ipcore-graph-v1` documents with `ipcore` module ownership and `ipcore_graph_v1` manifest command format.

**Tech Stack:** C++23, Qt Core/Widgets, xmake Qt test targets, Ruby Minitest generator suites, Ruby `spec_generator`.

---

## Final Naming

Use these names in live code and generated runtime metadata:

- JSON document schema: `finepaper-ipcore-graph-v1`
- manifest command input format: `ipcore_graph_v1`
- module owner field in generator JSON: `ipcore`
- selected instance fields in generator JSON: top-level `ipcore`, `instance`, and `ipcore_state`

Do not keep `finepaper-plugin-graph-v1`, `generic_graph_v1`, or generator-input module field `plugin` as accepted generator/DRC input names.

---

## File Structure

- Create `qt/inc/ipcore/ipcoregraphexporter.h`: request/result structs and export API for active IP-core graph JSON.
- Create `qt/src/ipcore/ipcoregraphexporter.cpp`: artifact ID generation, module/port/parameter JSON export, active IP-core ownership validation, state attachment, and external-to-internal ID mapping.
- Create `qt/test/ipcoregraphexporter_test.cpp`: exporter tests moved from `graph_test` plus active-IP/state vocabulary checks.
- Modify `qt/inc/graph/graph.h`: remove `GraphJsonFlavor` and `Graph::toJsonDocument()`.
- Modify `qt/src/graph/graph.cpp`: remove generator JSON export helpers; keep structural graph behavior only.
- Modify `qt/test/graph_test.cpp`: remove graph export tests after equivalent exporter tests exist.
- Modify `qt/inc/plugins/plugindescriptor.h`: default command input format becomes `ipcore_graph_v1`; rename helper to `usesIpcoreGraphInput()`.
- Modify `qt/src/plugins/pluginregistry.cpp`: manifest input-format default becomes `ipcore_graph_v1`.
- Modify `qt/inc/plugins/generatorrunner.h`: replace graph-inference resolution APIs with selected-IP-core resolution APIs.
- Modify `qt/src/plugins/generatorrunner.cpp`: resolve generator/DRC commands directly from `IpCatalogEntry`.
- Modify `qt/test/plugin_test.cpp`: update manifest parsing and runner tests for `ipcore_graph_v1` and selected-IP-core command resolution.
- Modify `qt/inc/validation/drcrunner.h`: DRC validation receives an `IpCatalogEntry` and selected `ProjectIpInstanceRecord`.
- Modify `qt/src/validation/drcrunner.cpp`: export DRC input through `IpCoreGraphExporter`.
- Modify `qt/inc/validation/validationmanager.h`: store `IpCatalogService` and `ActiveWorkspaceController` pointers.
- Modify `qt/src/validation/validationmanager.cpp`: resolve the selected active IP instance before external DRC.
- Modify `qt/test/validation_test.cpp`: update DRC runner tests to pass selected IP catalog metadata and assert new schema.
- Modify `qt/src/app/mainwindow.cpp`: generate through active workspace/catalog entry and `IpCoreGraphExporter`.
- Modify `qt/inc/app/mainwindow.h`: update generation comments if needed.
- Modify `qt/src/app/generationartifacts.cpp`: keep project snapshot behavior; remove JSON-root mutation calls from generation paths.
- Modify `qt/test/projectdocument_test.cpp`: update generation boundary helper assertions to the exporter-produced JSON.
- Modify `qt/xmake.lua`: add `ipcoregraphexporter_test` and include exporter sources in dependent test targets.
- Modify `spec_generator/lib/spec_generator.rb`: emit `ipcore_graph_v1` command input formats from source specs.
- Modify `spec_generator/test/spec_generator_test.rb`: expect `ipcore_graph_v1`.
- Modify `ipcores/finepaper-noc/ipcore.yml`: set generator and DRC `input_format: ipcore_graph_v1`.
- Modify `ipcores/ravenoc/ipcore.yml`: set generator and DRC `input_format: ipcore_graph_v1`.
- Regenerate/modify `generated/ipcores/finepaper.noc/plugin.json`: command `input_format` fields become `ipcore_graph_v1`.
- Regenerate/modify `generated/ipcores/finepaper.ravenoc/plugin.json`: command `input_format` fields become `ipcore_graph_v1`.
- Modify `ipcores/finepaper-noc/generator/src/ruby/parser/json_parser.rb`: require `finepaper-ipcore-graph-v1` and read module `ipcore` fields.
- Modify `ipcores/finepaper-noc/generator/test/test_generator.rb`: update generic graph fixture and DRC test names.
- Modify `ipcores/finepaper-noc/generator/examples/simple_mesh.json`: convert to IP-core graph schema or remove the test dependency.
- Modify `ipcores/finepaper-noc/generator/examples/mesh_3x3.json`: convert to IP-core graph schema or remove the test dependency.
- Modify `ipcores/finepaper-noc/generator/examples/multi_endpoint.json`: convert to IP-core graph schema or remove the test dependency.
- Modify `ipcores/ravenoc/generator/src/ruby/ravenoc_generator.rb`: require `finepaper-ipcore-graph-v1`, read module `ipcore`, and emit manifest `ipcore`.
- Modify `ipcores/ravenoc/generator/test/test_generator.rb`: update fixtures and expected manifest field.
- Modify `ipcores/ravenoc/generator/examples/default_2x2.json`: update schema and module owner field.
- Modify `ipcores/ravenoc/generator/examples/internal_mesh_2x2.json`: update schema and module owner field.

---

## Task 7.1: Exporter API And Tests

**Files:**

- Create: `qt/inc/ipcore/ipcoregraphexporter.h`
- Create: `qt/test/ipcoregraphexporter_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add exporter header skeleton**

Create `qt/inc/ipcore/ipcoregraphexporter.h`:

```cpp
// IpCoreGraphExporter serializes the active IP-core graph for generator/DRC tools.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/pluginstate.h"

#include <QHash>
#include <QJsonDocument>
#include <QString>

class Graph;

struct IpCoreGraphExportRequest {
    const Graph* graph = nullptr;
    IpCatalogEntry ipcore;
    ProjectIpInstanceRecord instance;
    QString designName;
    QHash<QString, QString>* externalToInternalIds = nullptr;
};

struct IpCoreGraphExportResult {
    bool success = false;
    QJsonDocument document;
    QString error;
};

class IpCoreGraphExporter {
public:
    static QString schemaName();
    static IpCoreGraphExportResult exportGraph(const IpCoreGraphExportRequest& request);
};
```

- [x] **Step 2: Add failing exporter tests**

Create `qt/test/ipcoregraphexporter_test.cpp` with these test cases:

```cpp
// IpCoreGraphExporter tests for generator/DRC boundary JSON.
#include "ipcore/ipcoregraphexporter.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ModuleType registerOwnedType(const QString& typeName, const QString& ipcoreId) {
    ModuleType type;
    type.name = typeName;
    type.pluginId = ipcoreId;
    ModuleRegistry::instance().registerType(type);
    return type;
}

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   const QString& ipcoreId,
                                   std::vector<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    for (const Port& port : ports) {
        module->addPort(port);
    }
    return module;
}

IpCatalogEntry catalogEntry(const QString& ipcoreId) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("ipcore_graph_v1");
    return entry;
}

ProjectIpInstanceRecord instanceRecord(const QString& ipcoreId, const QString& instanceId) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.schema = ipcoreId + QStringLiteral("-project-state-v1");
    record.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("global_parameters"), QJsonObject{{QStringLiteral("data_width"), 64}}}
    };
    return record;
}

IpCoreGraphExportResult exportGraph(const Graph& graph,
                                    const QString& ipcoreId,
                                    QHash<QString, QString>* externalToInternalIds = nullptr) {
    return IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        &graph,
        catalogEntry(ipcoreId),
        instanceRecord(ipcoreId, QStringLiteral("noc_0")),
        QStringLiteral("design"),
        externalToInternalIds
    });
}

void testExportsIpcoreSchemaStateAndModuleOwner() {
    registerOwnedType(QStringLiteral("RaveTile"), QStringLiteral("finepaper.ravenoc"));
    Graph graph;
    auto tile = makeModule(
        QStringLiteral("tile_runtime"),
        QStringLiteral("RaveTile"),
        QStringLiteral("finepaper.ravenoc"),
        {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))});
    tile->setParameter(QStringLiteral("external_id"), QStringLiteral("rave_0_0"));
    tile->setParameter(QStringLiteral("mesh_col"), 0);
    tile->setParameter(QStringLiteral("mesh_row"), 0);
    require(graph.addModule(std::move(tile)), "tile should add");

    const IpCoreGraphExportResult result = exportGraph(graph, QStringLiteral("finepaper.ravenoc"));
    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();

    require(root.value(QStringLiteral("schema")).toString() == QStringLiteral("finepaper-ipcore-graph-v1"),
            "export should use IP-core graph schema");
    require(root.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "export should name selected IP core");
    require(root.value(QStringLiteral("instance")).toString() == QStringLiteral("noc_0"),
            "export should name selected instance");
    require(root.value(QStringLiteral("ipcore_state")).toArray().size() == 1,
            "export should include selected IP-core state");

    const QJsonObject module = root.value(QStringLiteral("modules")).toArray().first().toObject();
    require(module.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "module owner field should be ipcore");
    require(!module.contains(QStringLiteral("plugin")),
            "module owner field should not use plugin");
}

void testExporterUsesArtifactIdsAndMapping() {
    registerOwnedType(QStringLiteral("Source"), QStringLiteral("finepaper.noc"));
    registerOwnedType(QStringLiteral("Target"), QStringLiteral("finepaper.noc"));
    Graph graph;
    auto source = makeModule(QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"),
                             QStringLiteral("Source"),
                             QStringLiteral("finepaper.noc"),
                             {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Out"))});
    source->setParameter(QStringLiteral("external_id"), QStringLiteral("source_0"));
    auto target = makeModule(QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe"),
                             QStringLiteral("Target"),
                             QStringLiteral("finepaper.noc"),
                             {Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("In"))});
    target->setParameter(QStringLiteral("external_id"), QStringLiteral("target_0"));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("3c357093_4961_4ac7_8302_cad7f44f909d"),
        PortRef{QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"), QStringLiteral("out")},
        PortRef{QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe"), QStringLiteral("in")}));

    QHash<QString, QString> externalToInternalIds;
    const IpCoreGraphExportResult result =
        exportGraph(graph, QStringLiteral("finepaper.noc"), &externalToInternalIds);
    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();
    const QJsonArray modules = root.value(QStringLiteral("modules")).toArray();
    const QJsonArray connections = root.value(QStringLiteral("connections")).toArray();

    require(modules.at(0).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("source_0"),
            "export should use source external_id");
    require(modules.at(1).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("target_0"),
            "export should use target external_id");
    require(connections.first().toObject().value(QStringLiteral("id")).toString() ==
                QStringLiteral("source_0_out_to_target_0_in"),
            "export should generate readable connection id");
    require(externalToInternalIds.value(QStringLiteral("source_0")) ==
                QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"),
            "export should map artifact source id to runtime source id");

    const QString text = QString::fromUtf8(result.document.toJson(QJsonDocument::Compact));
    require(!text.contains(QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4")),
            "export should not leak runtime source UUID");
    require(!text.contains(QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe")),
            "export should not leak runtime target UUID");
    require(!text.contains(QStringLiteral("3c357093_4961_4ac7_8302_cad7f44f909d")),
            "export should not leak runtime connection UUID");
}

void testExporterRejectsOtherIpcoreModules() {
    registerOwnedType(QStringLiteral("ActiveTile"), QStringLiteral("finepaper.ravenoc"));
    registerOwnedType(QStringLiteral("OtherTile"), QStringLiteral("finepaper.other"));
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("active"),
                                       QStringLiteral("ActiveTile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))})),
            "active module should add");
    require(graph.addModule(makeModule(QStringLiteral("other"),
                                       QStringLiteral("OtherTile"),
                                       QStringLiteral("finepaper.other"),
                                       {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))})),
            "other module should add");

    const IpCoreGraphExportResult result = exportGraph(graph, QStringLiteral("finepaper.ravenoc"));
    require(!result.success, "export should reject non-selected IP-core modules");
    require(result.error.contains(QStringLiteral("finepaper.other")),
            "rejection should mention the unexpected IP core");
}

void testExporterRejectsMismatchedInstance() {
    Graph graph;
    const IpCoreGraphExportResult result = IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        &graph,
        catalogEntry(QStringLiteral("finepaper.ravenoc")),
        instanceRecord(QStringLiteral("finepaper.noc"), QStringLiteral("noc_0")),
        QStringLiteral("design"),
        nullptr
    });
    require(!result.success, "mismatched instance should reject");
    require(result.error.contains(QStringLiteral("finepaper.ravenoc")),
            "mismatch error should mention requested IP core");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testExportsIpcoreSchemaStateAndModuleOwner();
        testExporterUsesArtifactIdsAndMapping();
        testExporterRejectsOtherIpcoreModules();
        testExporterRejectsMismatchedInstance();
    } catch (const std::exception& error) {
        std::cerr << "ipcoregraphexporter_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcoregraphexporter_test passed\n";
    return 0;
}
```

- [x] **Step 3: Wire the new test target**

Add to `qt/xmake.lua`:

```lua
add_qt_test_target("ipcoregraphexporter_test", "test/ipcoregraphexporter_test.cpp", {
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})
```

- [x] **Step 4: Run exporter test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcoregraphexporter_test
```

Expected: compile failure because `ipcoregraphexporter.cpp` does not exist and `IpCoreGraphExporter` has no implementation.

---

## Task 7.2: Implement Exporter And Remove Graph Export

**Files:**

- Create: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`

- [x] **Step 1: Implement exporter**

Create `qt/src/ipcore/ipcoregraphexporter.cpp`:

```cpp
// IpCoreGraphExporter implementation.
#include "ipcore/ipcoregraphexporter.h"

#include "graph/graph.h"
#include "modules/modulelabels.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <variant>

namespace {

QJsonValue parameterToJson(const Parameter::Value& value) {
    if (std::holds_alternative<QString>(value)) return QJsonValue(std::get<QString>(value));
    if (std::holds_alternative<int>(value)) return QJsonValue(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return QJsonValue(std::get<double>(value));
    if (std::holds_alternative<bool>(value)) return QJsonValue(std::get<bool>(value));
    return QJsonValue();
}

QString directionToJsonString(Port::Direction direction) {
    if (direction == Port::Direction::Input) return QStringLiteral("input");
    if (direction == Port::Direction::Output) return QStringLiteral("output");
    return QStringLiteral("inout");
}

QJsonObject portToIpcoreJson(const Port& port) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), port.id());
    object.insert(QStringLiteral("direction"), directionToJsonString(port.direction()));
    object.insert(QStringLiteral("type"), port.type());
    object.insert(QStringLiteral("name"), port.name());
    if (!port.description().isEmpty()) object.insert(QStringLiteral("description"), port.description());
    if (!port.role().isEmpty()) object.insert(QStringLiteral("role"), port.role());
    if (!port.busType().isEmpty()) object.insert(QStringLiteral("bus_type"), port.busType());
    if (!port.interfaceId().isEmpty()) object.insert(QStringLiteral("interface"), port.interfaceId());
    return object;
}

QJsonObject parametersToIpcoreJson(const Module* module) {
    QJsonObject parameters;
    if (!module) return parameters;
    for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
        parameters.insert(it.key(), parameterToJson(it.value().value()));
    }
    return parameters;
}

QJsonObject ipcoreStateObject(const ProjectIpInstanceRecord& record) {
    QJsonObject object;
    object.insert(QStringLiteral("ipcore"), record.ipcoreId);
    object.insert(QStringLiteral("instance"), record.instanceId);
    object.insert(QStringLiteral("schema"), record.schema);
    object.insert(QStringLiteral("state"), record.state);
    return object;
}

QString safeArtifactToken(QString token, const QString& defaultToken) {
    token = token.trimmed();
    if (token.isEmpty()) token = defaultToken.trimmed();
    token.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_$]+")), QStringLiteral("_"));
    token.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    token = token.trimmed();
    while (token.startsWith(QStringLiteral("_"))) token.remove(0, 1);
    while (token.endsWith(QStringLiteral("_"))) token.chop(1);
    if (token.isEmpty()) token = QStringLiteral("module");
    if (!token.front().isLetter() && token.front() != QLatin1Char('_')) {
        token.prepend(QStringLiteral("m_"));
    }
    return token;
}

QString uniqueArtifactToken(const QString& token, QSet<QString>& usedTokens) {
    QString candidate = token;
    int suffix = 1;
    while (usedTokens.contains(candidate)) {
        candidate = QStringLiteral("%1_%2").arg(token).arg(suffix++);
    }
    usedTokens.insert(candidate);
    return candidate;
}

QString moduleArtifactId(const Module* module, QSet<QString>& usedModuleIds) {
    const QString defaultToken = module ? module->type().toLower() : QStringLiteral("module");
    return uniqueArtifactToken(safeArtifactToken(ModuleLabels::externalId(module), defaultToken), usedModuleIds);
}

QString connectionArtifactId(const QString& sourceModuleId,
                             const QString& sourcePortId,
                             const QString& targetModuleId,
                             const QString& targetPortId,
                             QSet<QString>& usedConnectionIds) {
    const QString raw = QStringLiteral("%1_%2_to_%3_%4")
                            .arg(sourceModuleId, sourcePortId, targetModuleId, targetPortId);
    return uniqueArtifactToken(safeArtifactToken(raw, QStringLiteral("connection")), usedConnectionIds);
}

} // namespace

QString IpCoreGraphExporter::schemaName() {
    return QStringLiteral("finepaper-ipcore-graph-v1");
}

IpCoreGraphExportResult IpCoreGraphExporter::exportGraph(const IpCoreGraphExportRequest& request) {
    if (!request.graph) {
        return {false, {}, QStringLiteral("Graph is not available.")};
    }
    if (request.ipcore.id.trimmed().isEmpty()) {
        return {false, {}, QStringLiteral("Selected IP core is required.")};
    }
    if (request.instance.ipcoreId != request.ipcore.id || request.instance.instanceId.trimmed().isEmpty()) {
        return {false, {},
                QStringLiteral("Selected IP instance does not match IP core '%1'.").arg(request.ipcore.id)};
    }
    if (request.externalToInternalIds) {
        request.externalToInternalIds->clear();
    }

    QJsonArray modules;
    QJsonArray connections;
    QHash<QString, QString> runtimeToArtifactIds;
    QSet<QString> usedModuleIds;

    for (const auto& module : request.graph->modules()) {
        if (module->ipcoreId() != request.ipcore.id) {
            return {false, {},
                    QStringLiteral("Module '%1' belongs to IP core '%2', not selected IP core '%3'.")
                        .arg(module->id(), module->ipcoreId(), request.ipcore.id)};
        }

        const QString artifactId = moduleArtifactId(module.get(), usedModuleIds);
        runtimeToArtifactIds.insert(module->id(), artifactId);
        if (request.externalToInternalIds) {
            request.externalToInternalIds->insert(artifactId, module->id());
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), artifactId);
        object.insert(QStringLiteral("ipcore"), request.ipcore.id);
        object.insert(QStringLiteral("instance"), request.instance.instanceId);
        object.insert(QStringLiteral("type"), module->type());
        object.insert(QStringLiteral("parameters"), parametersToIpcoreJson(module.get()));

        QJsonArray ports;
        for (const Port& port : module->ports()) {
            ports.append(portToIpcoreJson(port));
        }
        object.insert(QStringLiteral("ports"), ports);
        modules.append(object);
    }

    QSet<QString> usedConnectionIds;
    for (const auto& connection : request.graph->connections()) {
        const QString sourceModuleId = runtimeToArtifactIds.value(connection->source().moduleId);
        const QString targetModuleId = runtimeToArtifactIds.value(connection->target().moduleId);
        if (sourceModuleId.isEmpty() || targetModuleId.isEmpty()) {
            continue;
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"),
                      connectionArtifactId(sourceModuleId,
                                           connection->source().portId,
                                           targetModuleId,
                                           connection->target().portId,
                                           usedConnectionIds));
        object.insert(QStringLiteral("source"), QJsonObject{
            {QStringLiteral("module"), sourceModuleId},
            {QStringLiteral("port"), connection->source().portId}
        });
        object.insert(QStringLiteral("target"), QJsonObject{
            {QStringLiteral("module"), targetModuleId},
            {QStringLiteral("port"), connection->target().portId}
        });
        connections.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("name"), request.designName.trimmed().isEmpty()
                                       ? QStringLiteral("design")
                                       : request.designName);
    root.insert(QStringLiteral("ipcore"), request.ipcore.id);
    root.insert(QStringLiteral("instance"), request.instance.instanceId);
    QJsonArray ipcoreState;
    ipcoreState.append(ipcoreStateObject(request.instance));
    root.insert(QStringLiteral("ipcore_state"), ipcoreState);
    root.insert(QStringLiteral("modules"), modules);
    root.insert(QStringLiteral("connections"), connections);
    return {true, QJsonDocument(root), {}};
}
```

- [x] **Step 2: Remove Graph export API**

In `qt/inc/graph/graph.h`, remove:

```cpp
#include <QJsonDocument>

enum class GraphJsonFlavor {
    Plugin
};

QJsonDocument toJsonDocument(const QString& designName,
                             GraphJsonFlavor flavor = GraphJsonFlavor::Plugin,
                             QHash<QString, QString>* externalToInternalIds = nullptr) const;
```

Keep the graph header focused on structural graph operations.

- [x] **Step 3: Remove Graph export implementation**

In `qt/src/graph/graph.cpp`, remove generator-export-only helpers:

- `parameterToJson`
- `directionToJsonString`
- `portToGenericJson`
- `parametersToGenericJson`
- `safeArtifactToken`
- `uniqueArtifactToken`
- `pluginModuleArtifactId`
- `pluginConnectionArtifactId`
- `Graph::toJsonDocument(...)`

Keep `findPort(...)` because `Graph::isValidConnection()` uses it.

- [x] **Step 4: Move graph export tests out of graph_test**

Remove these tests from `qt/test/graph_test.cpp`:

- `testGenericPluginExportKeepsNonNocModules`
- `testPluginExportUsesArtifactIdsInsteadOfRuntimeIds`

Remove their calls from `main()`. The new `ipcoregraphexporter_test` covers the behavior with IP-core vocabulary.

- [x] **Step 5: Run exporter and graph tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcoregraphexporter_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
```

Expected: both pass.

---

## Task 7.3: Selected IP-Core Command Resolution

**Files:**

- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/inc/plugins/generatorrunner.h`
- Modify: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/test/plugin_test.cpp`

- [x] **Step 1: Update command format defaults**

In `qt/inc/plugins/plugindescriptor.h`, update:

```cpp
struct PluginCommandDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList args;

    bool hasCommand() const { return !command.trimmed().isEmpty(); }
    bool hasGenerator() const { return hasCommand(); }
    bool usesIpcoreGraphInput() const {
        return inputFormat == QStringLiteral("ipcore_graph_v1");
    }
    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const;
};
```

Keep `using PluginGeneratorDescriptor = PluginCommandDescriptor;` unchanged.

In `qt/src/plugins/pluginregistry.cpp`, update `commandFromJson(...)`:

```cpp
command.inputFormat =
    object.value(QStringLiteral("input_format")).toString(QStringLiteral("ipcore_graph_v1")).trimmed();
if (command.inputFormat.isEmpty()) {
    command.inputFormat = QStringLiteral("ipcore_graph_v1");
}
```

- [x] **Step 2: Replace GeneratorRunner APIs**

In `qt/inc/plugins/generatorrunner.h`, replace graph-based APIs with selected-IP APIs:

```cpp
// GeneratorRunner selects and builds IP-core generator process commands.
#pragma once

#include "ipcore/ipcatalogservice.h"

#include <QString>
#include <QStringList>

struct GeneratorCommand {
    bool valid = false;
    QString errorMessage;
    QString ipcoreId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList arguments;
};

class GeneratorRunner {
public:
    static GeneratorCommand resolveForIpcore(const IpCatalogEntry& entry,
                                             const QString& inputPath,
                                             const QString& outputDirectory);
    static GeneratorCommand resolveDrcForIpcore(const IpCatalogEntry& entry,
                                                const QString& inputPath,
                                                const QString& outputDirectory);
};
```

- [x] **Step 3: Implement selected-IP command resolution**

In `qt/src/plugins/generatorrunner.cpp`, replace graph/module-owner inference with:

```cpp
// GeneratorRunner resolves selected IP-core command descriptors.
#include "plugins/generatorrunner.h"

namespace {

GeneratorCommand failure(const QString& message) {
    GeneratorCommand command;
    command.errorMessage = message;
    return command;
}

GeneratorCommand resolveIpcoreCommand(const IpCatalogEntry& entry,
                                      const QString& inputPath,
                                      const QString& outputDirectory,
                                      const QString& missingCommandMessage,
                                      const PluginCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Select an IP core instance before running this action."));
    }

    const PluginCommandDescriptor& descriptor = entry.*commandMember;
    if (!descriptor.hasCommand()) {
        return failure(missingCommandMessage.arg(entry.id));
    }

    GeneratorCommand command;
    command.valid = true;
    command.ipcoreId = entry.id;
    command.workingDirectory = entry.sourceRootPath;
    command.command = descriptor.command;
    command.inputFormat = descriptor.inputFormat;
    command.arguments = descriptor.arguments(inputPath, outputDirectory);
    return command;
}

} // namespace

GeneratorCommand GeneratorRunner::resolveForIpcore(const IpCatalogEntry& entry,
                                                   const QString& inputPath,
                                                   const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a generator."),
                                &IpCatalogEntry::generator);
}

GeneratorCommand GeneratorRunner::resolveDrcForIpcore(const IpCatalogEntry& entry,
                                                      const QString& inputPath,
                                                      const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a DRC command."),
                                &IpCatalogEntry::drc);
}
```

- [x] **Step 4: Update plugin tests**

In `qt/test/plugin_test.cpp`:

- replace expected `generic_graph_v1` with `ipcore_graph_v1`;
- replace `GeneratorRunner::resolveForGraph(...)` with `GeneratorRunner::resolveForIpcore(...)`;
- replace `GeneratorRunner::resolveDrcForGraph(...)` with `GeneratorRunner::resolveDrcForIpcore(...)`;
- build an `IpCatalogEntry` directly in runner tests.

Use this helper inside the anonymous namespace:

```cpp
IpCatalogEntry commandEntry(const QString& ipcoreId) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.sourceRootPath = QStringLiteral("/tmp/") + ipcoreId;
    return entry;
}
```

Update the command assertion:

```cpp
require(command.ipcoreId == QStringLiteral("finepaper.format"),
        "resolved command should carry IP-core id");
require(command.inputFormat == QStringLiteral("ipcore_graph_v1"),
        "resolved command should carry IP-core graph input format");
```

- [x] **Step 5: Run plugin test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: pass.

---

## Task 7.4: DRC And Validation Use Active IP Instance

**Files:**

- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Update DRCRunner API**

In `qt/inc/validation/drcrunner.h`, use:

```cpp
#include "ipcore/ipcatalogservice.h"
#include "project/pluginstate.h"

class DRCRunner {
public:
    QList<ValidationResult> validate(const Graph* graph,
                                     const IpCatalogEntry& ipcore,
                                     const ProjectIpInstanceRecord& instance);
private:
    QList<ValidationResult> parseErrors(const QString& stderr);

    QHash<QString, QString> m_externalToInternalIds;
};
```

- [x] **Step 2: Export DRC input through IpCoreGraphExporter**

In `qt/src/validation/drcrunner.cpp`, replace `Graph::toJsonDocument(...)` and `attachIpcoreState(...)` with:

```cpp
const GeneratorCommand generatorCommand =
    GeneratorRunner::resolveDrcForIpcore(ipcore, tmpFile.fileName(), outputDir.path());
if (!generatorCommand.valid) {
    return {ValidationResult(ValidationSeverity::Error, generatorCommand.errorMessage, "", "DRC")};
}

const IpCoreGraphExportResult exportResult =
    IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        graph,
        ipcore,
        instance,
        QStringLiteral("design"),
        &m_externalToInternalIds
    });
if (!exportResult.success) {
    return {ValidationResult(ValidationSeverity::Error, exportResult.error, "", "DRC")};
}

const QByteArray jsonBytes = exportResult.document.toJson();
```

Update comments from "plugin" to "IP core" in this file.

- [x] **Step 3: Make ValidationManager resolve active target**

In `qt/inc/validation/validationmanager.h`, add forward declarations and members:

```cpp
class IpCatalogService;
class ActiveWorkspaceController;

ValidationManager(Graph* graph,
                  ProjectStateService* projectStateService,
                  const IpCatalogService* catalogService,
                  const ActiveWorkspaceController* activeWorkspaceController,
                  LogPanel* logPanel,
                  QObject* parent = nullptr);

const IpCatalogService* m_catalogService;
const ActiveWorkspaceController* m_activeWorkspaceController;
```

In `qt/src/validation/validationmanager.cpp`, add a helper:

```cpp
std::optional<ProjectIpInstanceRecord> selectedRecord(
    const ProjectStateService* stateService,
    const QString& ipcoreId,
    const QString& instanceId) {
    if (!stateService) return std::nullopt;
    for (const ProjectIpInstanceRecord& record : stateService->ipInstanceRecords()) {
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            return record;
        }
    }
    return std::nullopt;
}
```

In `runValidation()`, after local validation:

```cpp
if (m_activeWorkspaceController && m_activeWorkspaceController->state().hasActiveIp && m_catalogService) {
    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    const std::optional<IpCatalogEntry> entry = m_catalogService->entry(workspace.ipcoreId);
    const std::optional<ProjectIpInstanceRecord> record =
        selectedRecord(m_projectStateService, workspace.ipcoreId, workspace.instanceId);
    if (entry.has_value() && record.has_value()) {
        results.append(m_drcRunner->validate(m_graph, *entry, *record));
    } else {
        results.append(ValidationResult(ValidationSeverity::Error,
                                        QStringLiteral("Active IP instance is not available for DRC."),
                                        "",
                                        "DRC"));
    }
}
```

- [x] **Step 4: Update MainWindow ValidationManager construction**

In `qt/src/app/mainwindow.cpp`, construct:

```cpp
m_validationManager = new ValidationManager(m_graph,
                                            m_projectStateService.get(),
                                            m_ipCatalogService.get(),
                                            m_activeWorkspaceController.get(),
                                            m_logPanel,
                                            this);
```

- [x] **Step 5: Update validation tests**

In `qt/test/validation_test.cpp`, update direct DRC calls:

```cpp
IpCatalogEntry ravenocCatalogEntry() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.ravenoc");
    entry.name = QStringLiteral("RaveNoC");
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.sourceRootPath = QFileInfo(QStringLiteral("ipcores/ravenoc")).absoluteFilePath();
    entry.drc.command = QStringLiteral("ruby");
    entry.drc.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.drc.args = {
        QStringLiteral("generator/bin/drc"),
        QStringLiteral("-i"),
        QStringLiteral("{input}")
    };
    return entry;
}

ProjectIpInstanceRecord ravenocIpcoreStateRecord() {
    return ravenocIpcoreState().first();
}
```

Replace:

```cpp
runner.validate(&graph, ravenocIpcoreState());
```

with:

```cpp
runner.validate(&graph, ravenocCatalogEntry(), ravenocIpcoreStateRecord());
```

Update the schema assertion text:

```cpp
require(!messages.join('\n').contains(QStringLiteral("expected schema finepaper-ipcore-graph-v1")),
        "RaveNoC DRC should receive IP-core graph JSON");
```

- [x] **Step 6: Update xmake test sources**

In `qt/xmake.lua`, add `src/ipcore/ipcoregraphexporter.cpp` and `src/ipcore/ipcatalogservice.cpp` to `validation_test` if missing, and add `inc/ipcore/ipcoregraphexporter.h`.

- [x] **Step 7: Run validation tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt validation_test
```

Expected: pass.

---

## Task 7.5: MainWindow Generation Uses Active IP Instance

**Files:**

- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/generationartifacts.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add MainWindow generation helpers**

In `qt/src/app/mainwindow.cpp`, include:

```cpp
#include "ipcore/ipcoregraphexporter.h"
```

Add an anonymous namespace helper:

```cpp
std::optional<ProjectIpInstanceRecord> selectedRecord(
    const ProjectStateService* stateService,
    const QString& ipcoreId,
    const QString& instanceId) {
    if (!stateService) return std::nullopt;
    for (const ProjectIpInstanceRecord& record : stateService->ipInstanceRecords()) {
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            return record;
        }
    }
    return std::nullopt;
}
```

- [x] **Step 2: Replace generation command resolution**

In `MainWindow::generateVerilog()`, before writing JSON:

```cpp
if (!m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
    const QString error = QStringLiteral("Select an IP core instance before generating.");
    m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
    QMessageBox::warning(this, "Generator Not Available", error);
    return;
}

const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
const std::optional<IpCatalogEntry> entry = m_ipCatalogService->entry(workspace.ipcoreId);
const std::optional<ProjectIpInstanceRecord> record =
    selectedRecord(m_projectStateService.get(), workspace.ipcoreId, workspace.instanceId);
if (!entry.has_value() || !record.has_value()) {
    const QString error = QStringLiteral("Active IP instance is not available for generation.");
    m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
    QMessageBox::warning(this, "Generator Not Available", error);
    return;
}

const GeneratorCommand generatorCommand =
    GeneratorRunner::resolveForIpcore(*entry, jsonPath, outputDirectory);
```

Replace JSON writing with:

```cpp
const IpCoreGraphExportResult exportResult =
    IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        m_graph,
        *entry,
        *record,
        designName,
        nullptr
    });
if (!exportResult.success) {
    m_logPanel->appendMessage("[Generate] " + exportResult.error, QColor(220, 50, 50));
    QMessageBox::warning(this, "JSON Export Failed", exportResult.error);
    return;
}

jsonFile.write(exportResult.document.toJson());
```

Update log text:

```cpp
m_logPanel->appendMessage(QString("[Generate] IP core=%1").arg(generatorCommand.ipcoreId),
                          QColor(70, 110, 190));
```

Update user-facing process failure text from "plugin generator" to "IP core generator".

- [x] **Step 3: Keep project snapshot scoped to current project**

Keep:

```cpp
writeGeneratedProjectSnapshot(*m_graph,
                              outputDirectory,
                              designName,
                              m_projectStateService->ipInstanceRecords());
```

This snapshot remains an editor-loadable `.fpproj`, not the generator input JSON.

- [x] **Step 4: Update generation boundary tests**

In `qt/test/projectdocument_test.cpp`, update `testGenerationHelpersShapeIpcoreStateForGeneratorBoundary()` so it calls `IpCoreGraphExporter::exportGraph(...)` and asserts:

```cpp
require(root.value(QStringLiteral("schema")).toString() == QStringLiteral("finepaper-ipcore-graph-v1"),
        "generated input should use IP-core graph schema");
require(root.contains(QStringLiteral("ipcore_state")),
        "generated input should include ipcore_state");
require(!root.contains(QStringLiteral("plugin_state")),
        "generated input should not include old plugin_state");
require(!root.value(QStringLiteral("modules")).toArray().first().toObject().contains(QStringLiteral("plugin")),
        "generated input module should not include plugin owner");
```

Add exporter sources to `projectdocument_test` in `qt/xmake.lua`.

- [x] **Step 5: Run project document test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: pass.

---

## Task 7.6: Spec And Runtime Manifest Input Format

**Files:**

- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Modify: `generated/ipcores/finepaper.noc/plugin.json`
- Modify: `generated/ipcores/finepaper.ravenoc/plugin.json`

- [x] **Step 1: Update source specs**

In both `ipcores/finepaper-noc/ipcore.yml` and `ipcores/ravenoc/ipcore.yml`, replace each:

```yaml
input_format: generic_graph_v1
```

with:

```yaml
input_format: ipcore_graph_v1
```

- [x] **Step 2: Update spec generator tests**

In `spec_generator/test/spec_generator_test.rb`, replace expected generated input format:

```ruby
assert_equal 'ipcore_graph_v1', plugin_json.fetch('generator').fetch('input_format')
```

Update inline YAML test fixtures from:

```yaml
input_format: generic_graph_v1
```

to:

```yaml
input_format: ipcore_graph_v1
```

- [x] **Step 3: Keep spec generator emitter direct**

`spec_generator/lib/spec_generator.rb` already emits the `input_format` declared by `ipcore.yml`. No mapping layer is needed. Confirm that `plugin_json` uses:

```ruby
input_format: generator.fetch('input_format')
```

and:

```ruby
input_format: drc.fetch('input_format')
```

- [x] **Step 4: Regenerate runtime bundles**

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/finepaper-noc/ipcore.yml \
  --views ipcores/finepaper-noc/views \
  --runtime-bundle generated/ipcores/finepaper.noc \
  --ruby-model ipcores/finepaper-noc/generator/src/ruby/model

ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/ravenoc/ipcore.yml \
  --views ipcores/ravenoc/views \
  --runtime-bundle generated/ipcores/finepaper.ravenoc
```

Expected: generated `plugin.json` files contain `ipcore_graph_v1`.

- [x] **Step 5: Run spec generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: pass.

---

## Task 7.7: Ruby Generator Input Migration

**Files:**

- Modify: `ipcores/finepaper-noc/generator/src/ruby/parser/json_parser.rb`
- Modify: `ipcores/finepaper-noc/generator/test/test_generator.rb`
- Modify: `ipcores/ravenoc/generator/src/ruby/ravenoc_generator.rb`
- Modify: `ipcores/ravenoc/generator/test/test_generator.rb`
- Modify: `ipcores/ravenoc/generator/examples/default_2x2.json`
- Modify: `ipcores/ravenoc/generator/examples/internal_mesh_2x2.json`

- [x] **Step 1: Update Finepaper NoC parser schema and owner field**

In `ipcores/finepaper-noc/generator/src/ruby/parser/json_parser.rb`, replace:

```ruby
GENERIC_GRAPH_SCHEMA = 'finepaper-plugin-graph-v1'.freeze
```

with:

```ruby
IPCORE_GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
```

Update parse dispatch:

```ruby
raise "expected schema #{IPCORE_GRAPH_SCHEMA}" unless data['schema'] == IPCORE_GRAPH_SCHEMA
parse_ipcore_graph(data, path)
```

Remove the legacy standalone parse path:

- delete `parse_standalone_config`;
- delete `validate_required!`;
- update tests that used `simple_mesh.json`, `mesh_3x3.json`, or `multi_endpoint.json` so they pass IP-core graph JSON.

Rename methods:

- `parse_generic_graph` -> `parse_ipcore_graph`
- `parse_generic_connections` -> `parse_ipcore_connections`
- `generic_router_connection` can keep its name if it is not schema vocabulary, but prefer `ipcore_router_connection`
- `generic_parameters` -> `ipcore_parameters`

Update module owner check:

```ruby
def self.noc_module_type?(mod, type)
  mod.is_a?(Hash) && mod['ipcore'] == 'finepaper.noc' && mod['type'] == type
end
```

- [x] **Step 2: Update Finepaper NoC tests**

In `ipcores/finepaper-noc/generator/test/test_generator.rb`:

- rename `GENERIC_GRAPH` to `IPCORE_GRAPH`;
- set fixture schema to `finepaper-ipcore-graph-v1`;
- replace each module field `'plugin' => 'finepaper.noc'` with `'ipcore' => 'finepaper.noc'`;
- rename `test_parses_generic_plugin_graph` to `test_parses_ipcore_graph`;
- rename `test_drc_script_accepts_generic_plugin_graph` to `test_drc_script_accepts_ipcore_graph`;
- assert stdout includes `DRC passed for generic_noc` only if the fixture name remains `generic_noc`.
- add `test_rejects_standalone_legacy_config` that writes `{"name":"legacy","version":"1.0"}` and expects `expected schema finepaper-ipcore-graph-v1`;
- add `test_rejects_old_plugin_graph_schema` that writes `{"schema":"finepaper-plugin-graph-v1","name":"legacy"}` and expects `expected schema finepaper-ipcore-graph-v1`;
- convert `EXAMPLE`, `MESH_3X3`, and `MULTI_EP` fixtures to IP-core graph documents, or replace those constants with in-test IP-core graph helper methods.

- [x] **Step 3: Update RaveNoC parser schema and owner field**

In `ipcores/ravenoc/generator/src/ruby/ravenoc_generator.rb`, replace `read_graph` schema validation with:

```ruby
IPCORE_GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze

def read_graph
  data = JSON.parse(File.read(input_path))
  raise GenerationError, "expected schema #{IPCORE_GRAPH_SCHEMA}" unless data['schema'] == IPCORE_GRAPH_SCHEMA
  data
rescue Errno::ENOENT
  raise GenerationError, "input graph not found: #{input_path}"
rescue JSON::ParserError => error
  raise GenerationError, "invalid JSON input: #{error.message}"
end
```

Replace RaveNoC module owner checks:

```ruby
mod['ipcore'] == 'finepaper.ravenoc' && mod['type'] == 'RaveNoC'
mod['ipcore'] == 'finepaper.ravenoc' && mod['type'] == 'RaveTile'
mod['ipcore'] == 'finepaper.ravenoc' && mod['type'] == 'RaveEndpoint'
```

Update `write_manifest`:

```ruby
manifest = {
  ipcore: 'finepaper.ravenoc',
  source: {
    repository: 'https://github.com/aignacio/ravenoc.git',
    commit: source_commit
  },
  module: {
    id: module_record['id'],
    type: module_record['type'],
    tiles: module_record.fetch('tiles', []).size,
    endpoints: module_record.fetch('endpoints', [])
  },
  parameters: parameters,
  verification: {
    command: 'bash verify.sh'
  }
}
```

- [x] **Step 4: Update RaveNoC tests and examples**

In `ipcores/ravenoc/generator/test/test_generator.rb`:

- replace fixture schema `finepaper-plugin-graph-v1` with `finepaper-ipcore-graph-v1`;
- replace module field `plugin` with `ipcore`;
- update expected manifest key:

```ruby
assert_equal 'finepaper.ravenoc', manifest.fetch('ipcore')
refute manifest.key?('plugin')
```

In `ipcores/ravenoc/generator/examples/default_2x2.json` and `ipcores/ravenoc/generator/examples/internal_mesh_2x2.json`:

- set `"schema": "finepaper-ipcore-graph-v1"`;
- replace module `"plugin": "finepaper.ravenoc"` with `"ipcore": "finepaper.ravenoc"`.

- [x] **Step 5: Run Ruby generator tests**

Run:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected: all pass.

---

## Task 7.8: Integration Verification And Stale Scan

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-7-ipcore-generation-boundary.md`

- [x] **Step 1: Run required verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcoregraphexporter_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
ruby spec_generator/test/spec_generator_test.rb
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
git diff --check
```

Expected: all commands pass.

- [x] **Step 2: Run focused stale scan**

Run:

```bash
rg -n "finepaper-plugin-graph-v1|generic_graph_v1|GraphJsonFlavor|toJsonDocument\\(|\\\"plugin\\\"|fetch\\('plugin'\\)|manifest\\.fetch\\('plugin'\\)" \
  qt/inc qt/src qt/test spec_generator ipcores/finepaper-noc ipcores/ravenoc generated/ipcores \
  -g '!**/*.svg'
```

Expected:

- no `finepaper-plugin-graph-v1` hits;
- no `generic_graph_v1` hits;
- no `GraphJsonFlavor` or `toJsonDocument(` hits;
- no generator-input module owner `"plugin"` hits;
- `ProjectReader` and project tests may still contain old project `plugin` rejection tests; those are outside generator input and can remain until Node 8 stale-term cleanup.

- [x] **Step 3: Architecture review**

Ask an `xhigh` reviewer/supervisor to inspect:

- `Graph` no longer exports generator JSON;
- `IpCoreGraphExporter` owns schema, state attachment, selected IP-core filtering, and ID mapping;
- generation and DRC resolve commands from the active catalog entry;
- `finepaper-ipcore-graph-v1` and `ipcore_graph_v1` are the only live generator schema/input names;
- Ruby generator tests consume `ipcore` owner fields;
- generated project snapshots remain `.fpproj` and are separate from generator input JSON.

- [x] **Step 4: Supervisor preflight**

Send standing supervisor:

- verification command results;
- stale scan result;
- `git diff --stat`;
- confirmation that `.codex/`, `.superpowers/`, and `image.png` are not staged.

- [ ] **Step 5: Archive Node 7**

Run:

```bash
git status --short
git add qt spec_generator ipcores generated docs/superpowers/plans/2026-05-09-node-7-ipcore-generation-boundary.md
git add -f qt/inc/ipcore/ipcoregraphexporter.h qt/src/ipcore/ipcoregraphexporter.cpp
git commit -m "archive: complete node-7 ipcore generation boundary"
```

Expected: archive commit succeeds and helper artifacts remain uncommitted.

---

## Self-Review

- Spec coverage: The plan moves export out of `Graph`, renames the generator schema/input format, targets active IP-core instances for generation and DRC, updates Ruby inputs, and keeps project snapshots separate from generator JSON.
- Scope control: The plan does not introduce native plugin loading, marketplace trust, multi-IP orchestration, or old-schema compatibility.
- Test coverage: The plan adds exporter tests, updates runner and DRC tests, updates project generation helper tests, and runs Ruby generator/DRC suites plus a focused stale scan before archive.
