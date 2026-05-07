// Project document tests for .fpproj save/load behavior.
#include "app/generationartifacts.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/parameter.h"
#include "graph/port.h"
#include "modules/moduleregistry.h"
#include "project/graphprojectserializer.h"
#include "project/projectreader.h"
#include "project/projectstateservice.h"
#include "project/projectwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to write JSON fixture");
    file.write(QJsonDocument(object).toJson());
    file.close();
}

template <typename T>
T parameterValue(const Module* module, const QString& name) {
    const auto it = module->parameters().find(name);
    require(it != module->parameters().end(), "expected parameter to exist");
    const Parameter::Value storedValue = it.value().value();
    const auto* value = std::get_if<T>(&storedValue);
    require(value != nullptr, "parameter type mismatch");
    return *value;
}

ModuleType makeProjectXpType() {
    ModuleType type;
    type.name = QStringLiteral("ProjectDocXP");
    type.pluginId = QStringLiteral("finepaper.test");
    type.defaultPorts.push_back(Port(QStringLiteral("ep0"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("EP0"),
                                     QStringLiteral("Endpoint slot"),
                                     QStringLiteral("attachment"),
                                     QStringLiteral("ni_link")));
    type.defaultParameters.insert(QStringLiteral("external_id"),
                                  Parameter(QStringLiteral("external_id"), QStringLiteral("xp_0_0")));
    type.defaultParameters.insert(QStringLiteral("display_name"),
                                  Parameter(QStringLiteral("display_name"), QStringLiteral("XP 0 0")));
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("collapsed"), Parameter(QStringLiteral("collapsed"), true));
    type.defaultParameters.insert(QStringLiteral("routing_algorithm"),
                                  Parameter(QStringLiteral("routing_algorithm"), QStringLiteral("xy")));
    type.defaultParameters.insert(QStringLiteral("vc_count"), Parameter(QStringLiteral("vc_count"), 2));
    type.defaultParameters.insert(QStringLiteral("buffer_depth"), Parameter(QStringLiteral("buffer_depth"), 8));
    return type;
}

ModuleType makeProjectEndpointType() {
    ModuleType type;
    type.name = QStringLiteral("ProjectDocEndpoint");
    type.pluginId = QStringLiteral("finepaper.test");
    type.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                     Port::Direction::Input,
                                     QStringLiteral("bus"),
                                     QStringLiteral("NoC"),
                                     QStringLiteral("NoC attachment"),
                                     QStringLiteral("attachment"),
                                     QStringLiteral("ni_link")));
    type.defaultParameters.insert(QStringLiteral("external_id"),
                                  Parameter(QStringLiteral("external_id"), QStringLiteral("ep_cpu0")));
    type.defaultParameters.insert(QStringLiteral("display_name"),
                                  Parameter(QStringLiteral("display_name"), QStringLiteral("CPU 0")));
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), -160));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("type"), Parameter(QStringLiteral("type"), QStringLiteral("master")));
    type.defaultParameters.insert(QStringLiteral("protocol"), Parameter(QStringLiteral("protocol"), QStringLiteral("axi4")));
    type.defaultParameters.insert(QStringLiteral("data_width"), Parameter(QStringLiteral("data_width"), 64));
    type.defaultParameters.insert(QStringLiteral("qos_enabled"), Parameter(QStringLiteral("qos_enabled"), false));
    type.defaultParameters.insert(QStringLiteral("buffer_depth"), Parameter(QStringLiteral("buffer_depth"), 16));
    return type;
}

void registerProjectTypes() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleRegistry::instance().registerType(makeProjectXpType());
    ModuleRegistry::instance().registerType(makeProjectEndpointType());
    registered = true;
}

ProjectDocument validProjectDocument() {
    registerProjectTypes();

    ProjectDocument document;
    document.name = QStringLiteral("validation");
    document.plugins.push_back(ProjectPluginRecord{QStringLiteral("finepaper.test"),
                                                   QStringLiteral("1.0")});

    ProjectModuleRecord xp;
    xp.id = QStringLiteral("node_1");
    xp.pluginId = QStringLiteral("finepaper.test");
    xp.type = QStringLiteral("ProjectDocXP");
    xp.parameters.insert(QStringLiteral("external_id"), QStringLiteral("xp_0_0"));
    xp.parameters.insert(QStringLiteral("display_name"), QStringLiteral("XP 0 0"));
    xp.parameters.insert(QStringLiteral("x"), 0);
    xp.parameters.insert(QStringLiteral("y"), 0);
    xp.parameters.insert(QStringLiteral("collapsed"), true);
    xp.parameters.insert(QStringLiteral("routing_algorithm"), QStringLiteral("xy"));
    xp.parameters.insert(QStringLiteral("vc_count"), 2);
    xp.parameters.insert(QStringLiteral("buffer_depth"), 8);
    document.modules.push_back(xp);

    ProjectModuleRecord endpoint;
    endpoint.id = QStringLiteral("node_2");
    endpoint.pluginId = QStringLiteral("finepaper.test");
    endpoint.type = QStringLiteral("ProjectDocEndpoint");
    endpoint.parameters.insert(QStringLiteral("external_id"), QStringLiteral("ep_cpu0"));
    endpoint.parameters.insert(QStringLiteral("display_name"), QStringLiteral("CPU 0"));
    endpoint.parameters.insert(QStringLiteral("x"), -160);
    endpoint.parameters.insert(QStringLiteral("y"), 0);
    endpoint.parameters.insert(QStringLiteral("type"), QStringLiteral("master"));
    endpoint.parameters.insert(QStringLiteral("protocol"), QStringLiteral("axi4"));
    endpoint.parameters.insert(QStringLiteral("data_width"), 64);
    endpoint.parameters.insert(QStringLiteral("qos_enabled"), false);
    endpoint.parameters.insert(QStringLiteral("buffer_depth"), 16);
    document.modules.push_back(endpoint);

    document.connections.push_back(ProjectConnectionRecord{
        QStringLiteral("conn_1"),
        ProjectConnectionEndpoint{QStringLiteral("node_1"), QStringLiteral("ep0")},
        ProjectConnectionEndpoint{QStringLiteral("node_2"), QStringLiteral("noc")}
    });

    return document;
}

std::unique_ptr<Module> instantiate(const ModuleType& type, const QString& id) {
    auto module = std::make_unique<Module>(id, type.name);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    return module;
}

void testProjectRoundTripRestoresModulesParametersAndConnections() {
    registerProjectTypes();

    Graph graph;
    auto xp = instantiate(makeProjectXpType(), QStringLiteral("node_1"));
    xp->setParameter(QStringLiteral("collapsed"), true);
    xp->setParameter(QStringLiteral("vc_count"), 4);
    auto endpoint = instantiate(makeProjectEndpointType(), QStringLiteral("node_2"));
    endpoint->setParameter(QStringLiteral("data_width"), 128);

    require(graph.addModule(std::move(xp)), "failed to add XP module");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module");
    graph.addConnection(std::make_unique<Connection>(
            QStringLiteral("conn_1"),
            PortRef{QStringLiteral("node_1"), QStringLiteral("ep0")},
            PortRef{QStringLiteral("node_2"), QStringLiteral("noc")}));
    require(graph.connections().size() == 1, "setup connection should be valid");
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.fpproj"));

    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("roundtrip"));
    require(document.ipInstances.isEmpty(), "graph project serialization should not capture IP state");
    document.pluginStates.push_back(state);
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, "project write should succeed");

    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, "project read should succeed");
    require(readResult.document.pluginStates.size() == 1,
            "project read should restore plugin state records");

    Graph restored;
    const GraphProjectLoadResult loadResult =
        GraphProjectSerializer::loadProject(readResult.document, restored);
    require(loadResult.success, "project graph restore should succeed");

    require(restored.modules().size() == 2, "project should restore both modules");
    require(restored.connections().size() == 1, "project should restore explicit connection");
    require(restored.getModule(QStringLiteral("node_1")) != nullptr,
            "stable module id should be preserved");
    require(restored.getModule(QStringLiteral("node_2")) != nullptr,
            "stable endpoint id should be preserved");

    const Module* restoredXp = restored.getModule(QStringLiteral("node_1"));
    const Module* restoredEndpoint = restored.getModule(QStringLiteral("node_2"));
    require(parameterValue<bool>(restoredXp, QStringLiteral("collapsed")) == true,
            "collapsed parameter should be restored");
    require(parameterValue<int>(restoredXp, QStringLiteral("vc_count")) == 4,
            "integer parameter should be restored");
    require(parameterValue<int>(restoredEndpoint, QStringLiteral("data_width")) == 128,
            "endpoint parameter should be restored");

    const Connection* restoredConnection = restored.connections().front().get();
    require(restoredConnection->id() == QStringLiteral("conn_1"),
            "connection id should be preserved");
    require(restoredConnection->source().moduleId == QStringLiteral("node_1") &&
                restoredConnection->source().portId == QStringLiteral("ep0"),
            "connection source should be restored");
    require(restoredConnection->target().moduleId == QStringLiteral("node_2") &&
                restoredConnection->target().portId == QStringLiteral("noc"),
            "connection target should be restored");
}

void testProjectPreservesOpaquePluginState() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64},
            {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}
        }}
    };
    document.pluginStates.push_back(state);

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("plugin_state.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "project with plugin state should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "project with plugin state should read");
    require(readResult.document.pluginStates.size() == 1,
            "plugin state record should round-trip");
    const ProjectPluginStateRecord& restored = readResult.document.pluginStates.first();
    require(restored.pluginId == QStringLiteral("finepaper.ravenoc"),
            "plugin state plugin id should round-trip");
    require(restored.instanceId == QStringLiteral("ravenoc_0"),
            "plugin state instance id should round-trip");
    require(restored.schema == QStringLiteral("ravenoc-project-state-v1"),
            "plugin state schema should round-trip");
    require(restored.state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "opaque plugin state JSON should round-trip");
}

void testLegacyIpInstanceMigratesToPluginState() {
    ProjectDocument document = validProjectDocument();
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QJsonObject{{QStringLiteral("flit_data_width"), 64}}
    });

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("legacy_ip.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "legacy ip project should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "legacy ip project should read");
    require(readResult.document.pluginStates.size() == 1,
            "legacy IP instance should migrate to plugin state");
    require(readResult.document.pluginStates.first().pluginId == QStringLiteral("finepaper.ravenoc"),
            "migrated plugin state should retain plugin id");
    require(readResult.document.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "migrated plugin state should retain parameter value");
}

void testMixedLegacyIpInstancesMergeMissingPluginState() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord existing;
    existing.pluginId = QStringLiteral("finepaper.other");
    existing.instanceId = QStringLiteral("other_0");
    existing.schema = QStringLiteral("finepaper.other-project-state-v1");
    existing.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("kept"), true}
        }}
    };
    document.pluginStates.push_back(existing);
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QJsonObject{{QStringLiteral("flit_data_width"), 64}}
    });

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("mixed_ip_state.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "mixed legacy ip project should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "mixed legacy ip project should read");
    require(readResult.document.pluginStates.size() == 2,
            "mixed project should preserve existing state and migrate missing legacy IP");

    bool foundMigrated = false;
    for (const ProjectPluginStateRecord& state : readResult.document.pluginStates) {
        if (state.pluginId == QStringLiteral("finepaper.ravenoc") &&
            state.instanceId == QStringLiteral("ravenoc_0")) {
            foundMigrated = state.state.value(QStringLiteral("global_parameters"))
                                .toObject()
                                .value(QStringLiteral("flit_data_width"))
                                .toInt() == 64;
        }
    }
    require(foundMigrated,
            "mixed project should migrate legacy IP instance not already present in plugin_state");
}

void testPluginStateWriteAddsPluginDependency() {
    ProjectDocument document;
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };

    ProjectDocument source;
    source.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(source);
    service.writeToDocument(document);

    require(document.plugins.size() == 1,
            "plugin state write should add missing plugin dependency metadata");
    require(document.plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "plugin state dependency should use plugin state plugin id");
}

void testProjectStateServiceUpdatesPluginStateWithoutGraph() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                 QStringLiteral("ravenoc_0"),
                                 QStringLiteral("global_parameters"),
                                 QStringLiteral("flit_data_width"),
                                 64),
            "plugin state parameter update should succeed");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(saved.pluginStates.size() == 1,
            "service should write one plugin state record");
    require(saved.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "service should write updated plugin parameter");
}

void testProjectStateServiceDoesNotCreateMissingSection() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{};
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(!service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                  QStringLiteral("ravenoc_0"),
                                  QStringLiteral("global_parameters"),
                                  QStringLiteral("flit_data_width"),
                                  64),
            "missing plugin state section should not be created");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(!saved.pluginStates.first().state.contains(QStringLiteral("global_parameters")),
            "missing section should remain absent after failed update");
}

void testProjectStateServiceDoesNotOverwriteNonObjectSection() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QStringLiteral("opaque")}
    };
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(!service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                  QStringLiteral("ravenoc_0"),
                                  QStringLiteral("global_parameters"),
                                  QStringLiteral("flit_data_width"),
                                  64),
            "non-object plugin state section should not be overwritten");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(saved.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toString() == QStringLiteral("opaque"),
            "non-object section should remain unchanged after failed update");
}

void testProjectStateServiceParameterReturnsUndefinedForMissingValues() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{}}
    };
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(service.parameter(QStringLiteral("finepaper.missing"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("global_parameters"),
                              QStringLiteral("flit_data_width"))
                .isUndefined(),
            "missing plugin state record should return undefined");
    require(service.parameter(QStringLiteral("finepaper.ravenoc"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("missing_section"),
                              QStringLiteral("flit_data_width"))
                .isUndefined(),
            "missing plugin state section should return undefined");
    require(service.parameter(QStringLiteral("finepaper.ravenoc"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("global_parameters"),
                              QStringLiteral("missing_name"))
                .isUndefined(),
            "missing plugin state parameter should return undefined");
}

void testProjectStateServiceParameterPreservesExplicitNull() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("optional_value"), QJsonValue(QJsonValue::Null)}
        }}
    };
    document.pluginStates.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    const QJsonValue value = service.parameter(QStringLiteral("finepaper.ravenoc"),
                                               QStringLiteral("ravenoc_0"),
                                               QStringLiteral("global_parameters"),
                                               QStringLiteral("optional_value"));
    require(value.isNull(), "explicit null plugin state parameter should be preserved");
    require(!value.isUndefined(), "explicit null plugin state parameter should not be undefined");
}

void testReaderRejectsWrongKind() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("wrong_kind.fpproj"));

    QFile file(projectPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to write wrong-kind project");
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("kind"), QStringLiteral("not-a-project")}
    }).toJson());
    file.close();

    const ProjectReadResult result = ProjectReader::readFile(projectPath);
    require(!result.success, "wrong project kind should be rejected");
    require(result.error.contains(QStringLiteral("kind")),
            "wrong-kind error should mention kind");
}

void testLoadRejectsDuplicateModuleIds() {
    ProjectDocument document = validProjectDocument();
    ProjectModuleRecord duplicate = document.modules.first();
    duplicate.type = QStringLiteral("ProjectDocEndpoint");
    document.modules.push_back(duplicate);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "duplicate module id should be rejected");
    require(result.error.contains(QStringLiteral("node_1")),
            "duplicate module id error should mention the duplicated id");
}

void testLoadRejectsMissingModuleType() {
    ProjectDocument document = validProjectDocument();
    document.modules.first().type = QStringLiteral("MissingProjectType");

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "missing module type should be rejected");
    require(result.error.contains(QStringLiteral("MissingProjectType")),
            "missing type error should mention the type");
}

void testLoadRejectsInvalidParameterType() {
    ProjectDocument document = validProjectDocument();
    document.modules.last().parameters.insert(QStringLiteral("data_width"), QStringLiteral("wide"));

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "invalid parameter type should be rejected");
    require(result.error.contains(QStringLiteral("data_width")),
            "invalid parameter error should mention the parameter");
}

void testLoadRejectsInvalidConnectionReference() {
    ProjectDocument document = validProjectDocument();
    document.connections.first().target.moduleId = QStringLiteral("missing_node");

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "connection referencing missing module should be rejected");
    require(result.error.contains(QStringLiteral("conn_1")),
            "invalid connection error should mention the connection id");
}

void testLoadRejectsConnectionInvalidatedByEarlierConnectionWithoutChangingGraph() {
    registerProjectTypes();

    Graph graph;
    require(graph.addModule(instantiate(makeProjectEndpointType(), QStringLiteral("existing_node"))),
            "failed to add existing module");

    ProjectDocument document = validProjectDocument();

    ProjectModuleRecord secondEndpoint = document.modules.last();
    secondEndpoint.id = QStringLiteral("node_3");
    document.modules.push_back(secondEndpoint);
    document.connections.push_back(ProjectConnectionRecord{
        QStringLiteral("conn_2"),
        ProjectConnectionEndpoint{QStringLiteral("node_1"), QStringLiteral("ep0")},
        ProjectConnectionEndpoint{QStringLiteral("node_3"), QStringLiteral("noc")}
    });

    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "duplicate source-port connection should be rejected");
    require(result.error.contains(QStringLiteral("conn_2")),
            "invalid connection error should mention the rejected connection id");
    require(graph.modules().size() == 1, "failed project load should preserve existing modules");
    require(graph.connections().empty(), "failed project load should preserve existing connections");
    require(graph.getModule(QStringLiteral("existing_node")) != nullptr,
            "failed project load should keep the previous graph");
    require(graph.getModule(QStringLiteral("node_1")) == nullptr,
            "failed project load should not partially install new modules");
}

void testReaderRejectsMalformedProjectGraphArrays() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    auto baseProject = [] {
        return QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("v1")},
            {QStringLiteral("kind"), QStringLiteral("finepaper-project")},
            {QStringLiteral("project"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("malformed")},
                {QStringLiteral("version"), QStringLiteral("1.0")}
            }},
            {QStringLiteral("plugins"), QJsonArray{}}
        };
    };

    QJsonObject missingGraph = baseProject();
    QString path = QDir(tempDir.path()).filePath(QStringLiteral("missing_graph.fpproj"));
    writeJsonFile(path, missingGraph);
    ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "missing graph object should be rejected");
    require(result.error.contains(QStringLiteral("graph")),
            "missing graph error should mention graph");

    QJsonObject wrongModules = baseProject();
    wrongModules.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonObject{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_modules.fpproj"));
    writeJsonFile(path, wrongModules);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-array modules should be rejected");
    require(result.error.contains(QStringLiteral("graph.modules")),
            "modules error should mention graph.modules");

    QJsonObject wrongConnections = baseProject();
    wrongConnections.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonObject{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_connections.fpproj"));
    writeJsonFile(path, wrongConnections);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-array connections should be rejected");
    require(result.error.contains(QStringLiteral("graph.connections")),
            "connections error should mention graph.connections");

    QJsonObject wrongPluginState = baseProject();
    wrongPluginState.insert(QStringLiteral("plugin_state"), QJsonObject{});
    wrongPluginState.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_plugin_state.fpproj"));
    writeJsonFile(path, wrongPluginState);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-array plugin_state should be rejected");
    require(result.error.contains(QStringLiteral("plugin_state")),
            "plugin_state error should mention plugin_state");

    QJsonObject wrongPluginStateItem = baseProject();
    wrongPluginStateItem.insert(QStringLiteral("plugin_state"), QJsonArray{
        QStringLiteral("not-an-object")
    });
    wrongPluginStateItem.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_plugin_state_item.fpproj"));
    writeJsonFile(path, wrongPluginStateItem);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-object plugin_state item should be rejected");
    require(result.error.contains(QStringLiteral("plugin_state")),
            "plugin_state item error should mention plugin_state");

    QJsonObject missingPluginStateObject = baseProject();
    missingPluginStateObject.insert(QStringLiteral("plugin_state"), QJsonArray{
        QJsonObject{
            {QStringLiteral("plugin"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("instance"), QStringLiteral("ravenoc_0")},
            {QStringLiteral("schema"), QStringLiteral("ravenoc-project-state-v1")}
        }
    });
    missingPluginStateObject.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("missing_plugin_state_object.fpproj"));
    writeJsonFile(path, missingPluginStateObject);
    result = ProjectReader::readFile(path);
    require(!result.success, "missing plugin_state state should be rejected");
    require(result.error.contains(QStringLiteral("plugin_state")) &&
                result.error.contains(QStringLiteral("state")),
            "missing plugin_state state error should mention plugin_state and state");

    QJsonObject wrongPluginStateObject = baseProject();
    wrongPluginStateObject.insert(QStringLiteral("plugin_state"), QJsonArray{
        QJsonObject{
            {QStringLiteral("plugin"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("instance"), QStringLiteral("ravenoc_0")},
            {QStringLiteral("schema"), QStringLiteral("ravenoc-project-state-v1")},
            {QStringLiteral("state"), QStringLiteral("not-an-object")}
        }
    });
    wrongPluginStateObject.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_plugin_state_object.fpproj"));
    writeJsonFile(path, wrongPluginStateObject);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-object plugin_state state should be rejected");
    require(result.error.contains(QStringLiteral("plugin_state")) &&
                result.error.contains(QStringLiteral("state")),
            "non-object plugin_state state error should mention plugin_state and state");
}

void testReaderDetectsProjectAndLegacyJsonFiles() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("design.fpproj"));
    const QString legacyPath = QDir(tempDir.path()).filePath(QStringLiteral("legacy.json"));

    const ProjectWriteResult writeResult =
        ProjectWriter::writeFile(projectPath, validProjectDocument());
    require(writeResult.success, "failed to write project fixture");

    writeJsonFile(legacyPath, QJsonObject{
        {QStringLiteral("name"), QStringLiteral("legacy")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("xps"), QJsonArray{}},
        {QStringLiteral("endpoints"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });

    require(ProjectReader::detectKind(projectPath) == ProjectFileKind::Project,
            "project file should be detected as Finepaper project");
    require(ProjectReader::detectKind(legacyPath) == ProjectFileKind::LegacyJson,
            "legacy NoC JSON should be detected as legacy JSON");
}

void testGenerationHelpersShapePluginStateForGeneratorBoundary() {
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("type"), QStringLiteral("RaveNoC")},
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };
    const QVector<ProjectPluginStateRecord> records{state};

    const QJsonArray states = pluginStateArray(records);
    require(states.size() == 1, "plugin state helper should serialize one record");
    require(states.first().toObject()
                .value(QStringLiteral("state"))
                .toObject()
                .value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "plugin state helper should preserve global parameters");

    const QJsonObject ipInstance =
        legacyIpInstanceObject(records, QStringLiteral("finepaper.ravenoc"));
    require(ipInstance.value(QStringLiteral("id")).toString() == QStringLiteral("ravenoc_0"),
            "legacy IP object should use plugin state instance id");
    require(ipInstance.value(QStringLiteral("plugin")).toString() == QStringLiteral("finepaper.ravenoc"),
            "legacy IP object should use plugin id");
    require(ipInstance.value(QStringLiteral("kind")).toString() == QStringLiteral("noc"),
            "legacy IP object should preserve kind when present");
    require(ipInstance.value(QStringLiteral("type")).toString() == QStringLiteral("RaveNoC"),
            "legacy IP object should preserve type when present");
    require(ipInstance.value(QStringLiteral("parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "legacy IP object should derive parameters from global plugin state");

    ProjectPluginStateRecord duplicate = state;
    duplicate.instanceId = QStringLiteral("ravenoc_1");
    require(legacyIpInstanceObject({state, duplicate}, QStringLiteral("finepaper.ravenoc")).isEmpty(),
            "legacy IP helper should not choose ambiguously between multiple same-plugin states");

    ProjectPluginStateRecord malformed = state;
    malformed.state.insert(QStringLiteral("global_parameters"), QStringLiteral("opaque"));
    require(legacyIpInstanceObject({malformed}, QStringLiteral("finepaper.ravenoc")).isEmpty(),
            "legacy IP helper should not produce empty parameters for malformed plugin state");
}

void testGenerationWritesProjectSnapshot() {
    Graph graph;
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const GeneratedProjectSnapshotResult result =
        writeGeneratedProjectSnapshot(graph,
                                      tempDir.path(),
                                      QStringLiteral("generated_design"),
                                      QVector<ProjectPluginStateRecord>{state});
    require(result.success, "generation project snapshot should be written");
    require(QFileInfo::exists(result.path), "generation project snapshot should exist");
    require(QFileInfo(result.path).fileName() == QStringLiteral("generated_design.fpproj"),
            "generation project snapshot should use design name");

    const ProjectReadResult readResult = ProjectReader::readFile(result.path);
    require(readResult.success, "generation project snapshot should be readable");
    require(readResult.document.name == QStringLiteral("generated_design"),
            "generation project snapshot should use design name as project name");
    require(readResult.document.ipInstances.isEmpty(),
            "generation project snapshot should not write graph IP state");
    require(readResult.document.pluginStates.size() == 1,
            "generation project snapshot should include plugin state");
    require(readResult.document.plugins.size() == 1,
            "generation project snapshot should include plugin state dependency");
    require(readResult.document.plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "generation project snapshot dependency should use plugin state plugin id");
    require(readResult.document.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "generation project snapshot should preserve plugin parameters");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testProjectRoundTripRestoresModulesParametersAndConnections();
        testProjectPreservesOpaquePluginState();
        testLegacyIpInstanceMigratesToPluginState();
        testMixedLegacyIpInstancesMergeMissingPluginState();
        testPluginStateWriteAddsPluginDependency();
        testProjectStateServiceUpdatesPluginStateWithoutGraph();
        testProjectStateServiceDoesNotCreateMissingSection();
        testProjectStateServiceDoesNotOverwriteNonObjectSection();
        testProjectStateServiceParameterReturnsUndefinedForMissingValues();
        testProjectStateServiceParameterPreservesExplicitNull();
        testReaderRejectsWrongKind();
        testLoadRejectsDuplicateModuleIds();
        testLoadRejectsMissingModuleType();
        testLoadRejectsInvalidParameterType();
        testLoadRejectsInvalidConnectionReference();
        testLoadRejectsConnectionInvalidatedByEarlierConnectionWithoutChangingGraph();
        testReaderRejectsMalformedProjectGraphArrays();
        testReaderDetectsProjectAndLegacyJsonFiles();
        testGenerationHelpersShapePluginStateForGeneratorBoundary();
        testGenerationWritesProjectSnapshot();
    } catch (const std::exception& error) {
        std::cerr << "projectdocument_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectdocument_test passed\n";
    return 0;
}
