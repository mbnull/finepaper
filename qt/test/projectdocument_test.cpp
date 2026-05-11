// Project document tests for .fpproj save/load behavior.
#include "app/generationartifacts.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/parameter.h"
#include "graph/port.h"
#include "ipcore/ipcoregraphexporter.h"
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

QJsonObject minimalProjectRoot() {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("kind"), QStringLiteral("finepaper-project")},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("minimal")},
            {QStringLiteral("version"), QStringLiteral("1.0")}
        }},
        {QStringLiteral("ipcores"), QJsonArray{}},
        {QStringLiteral("ipcore_state"), QJsonArray{}},
        {QStringLiteral("graph"), QJsonObject{
            {QStringLiteral("modules"), QJsonArray{}},
            {QStringLiteral("connections"), QJsonArray{}}
        }}
    };
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

const ProjectIpInstanceRecord& requireStateRecord(const QVector<ProjectIpInstanceRecord>& records,
                                                  const QString& ipcoreId,
                                                  const QString& instanceId) {
    for (const ProjectIpInstanceRecord& record : records) {
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            return record;
        }
    }
    throw std::runtime_error("expected IP-core state record to exist");
}

ModuleType makeProjectXpType() {
    ModuleType type;
    type.name = QStringLiteral("ProjectDocXP");
    type.ipcoreId = QStringLiteral("finepaper.test");
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
    type.ipcoreId = QStringLiteral("finepaper.test");
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
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.test"),
                                                   QStringLiteral("1.0")});
    document.ipcoreState.push_back(ProjectIpInstanceRecord{
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{
            {QStringLiteral("global_parameters"), QJsonObject{}}
        }
    });

    ProjectModuleRecord xp;
    xp.id = QStringLiteral("node_1");
    xp.ipcoreId = QStringLiteral("finepaper.test");
    xp.instanceId = QStringLiteral("test_0");
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
    endpoint.ipcoreId = QStringLiteral("finepaper.test");
    endpoint.instanceId = QStringLiteral("test_0");
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
    module->setIpcoreId(type.ipcoreId);
    module->setInstanceId(QStringLiteral("test_0"));
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
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.test");
    state.instanceId = QStringLiteral("test_0");
    state.schema = QStringLiteral("finepaper.test-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.fpproj"));

    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("roundtrip"));
    document.ipcoreState.push_back(state);
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, "project write should succeed");

    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, "project read should succeed");
    require(readResult.document.ipcoreState.size() == 1,
            "project read should restore IP-core state records");
    require(readResult.document.modules.size() == 2,
            "project read should restore graph modules");
    require(readResult.document.modules.first().instanceId == QStringLiteral("test_0"),
            "project read should preserve module instance ownership");

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
    require(restoredXp->instanceId() == QStringLiteral("test_0"),
            "restored XP should keep instance ownership");
    require(restoredEndpoint->instanceId() == QStringLiteral("test_0"),
            "restored endpoint should keep instance ownership");
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

void testProjectPreservesOpaqueIpcoreState() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64},
            {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}
        }}
    };
    document.ipcoreState.push_back(state);

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("ipcore_state.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(path, document);
    require(writeResult.success, "project with IP-core state should write");

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "project with IP-core state should read");
    const ProjectIpInstanceRecord& restored = requireStateRecord(readResult.document.ipcoreState,
                                                                 QStringLiteral("finepaper.ravenoc"),
                                                                 QStringLiteral("ravenoc_0"));
    require(restored.ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "IP-core state owner id should round-trip");
    require(restored.instanceId == QStringLiteral("ravenoc_0"),
            "IP-core state instance id should round-trip");
    require(restored.schema == QStringLiteral("ravenoc-project-state-v1"),
            "IP-core state schema should round-trip");
    require(restored.state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "opaque IP-core state JSON should round-trip");
}

void testProjectWriterUsesIpcoreVocabulary() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    ProjectDocument document;
    document.name = QStringLiteral("ipcore_schema");
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.ravenoc"),
                                                   QStringLiteral("1.0")});

    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("finepaper.ravenoc-project-state-v1");
    state.state.insert(QStringLiteral("kind"), QStringLiteral("noc"));
    state.state.insert(QStringLiteral("global_parameters"), QJsonObject{
        {QStringLiteral("flit_data_width"), 32}
    });
    document.ipcoreState.push_back(state);

    ProjectModuleRecord module;
    module.id = QStringLiteral("tile_0");
    module.ipcoreId = QStringLiteral("finepaper.ravenoc");
    module.instanceId = QStringLiteral("ravenoc_0");
    module.type = QStringLiteral("RaveTile");
    document.modules.push_back(module);

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("schema.fpproj"));
    require(ProjectWriter::writeFile(path, document).success, "project should write");

    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project should reopen");
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    require(root.contains(QStringLiteral("ipcores")), "writer should emit ipcores");
    require(root.contains(QStringLiteral("ipcore_state")), "writer should emit ipcore_state");
    require(!root.contains(QStringLiteral("plugins")), "writer should not emit plugins");
    require(!root.contains(QStringLiteral("plugin_state")), "writer should not emit plugin_state");
    const QJsonObject firstModule = root.value(QStringLiteral("graph"))
                                        .toObject()
                                        .value(QStringLiteral("modules"))
                                        .toArray()
                                        .first()
                                        .toObject();
    require(firstModule.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "module should emit ipcore owner");
    require(firstModule.value(QStringLiteral("instance")).toString() == QStringLiteral("ravenoc_0"),
            "module should emit instance owner");
    require(!firstModule.contains(QStringLiteral("plugin")), "module should not emit plugin owner");
}

void testProjectSerializerUsesModuleIpcoreOwnership() {
    ModuleType type = makeProjectXpType();
    type.name = QStringLiteral("ProjectDocOwnedXP");
    type.ipcoreId = QStringLiteral("finepaper.owned");
    ModuleRegistry::instance().registerType(type);

    Graph graph;
    auto module = instantiate(type, QStringLiteral("owned_node"));
    module->setIpcoreId(QStringLiteral("finepaper.owned"));
    module->setInstanceId(QStringLiteral("owned_0"));
    require(graph.addModule(std::move(module)), "owned module should add");

    const ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("owned"));

    require(document.modules.size() == 1, "serializer should write one module");
    require(document.modules.first().ipcoreId == QStringLiteral("finepaper.owned"),
            "serializer should preserve module IP-core ownership");
    require(document.modules.first().instanceId == QStringLiteral("owned_0"),
            "serializer should preserve module instance ownership");
}

void testLoadRejectsModuleWithoutMatchingIpcoreState() {
    ProjectDocument document = validProjectDocument();
    document.modules.first().instanceId = QStringLiteral("missing_0");

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "module without matching IP-core state should be rejected");
    require(result.error.contains(QStringLiteral("node_1")),
            "missing instance-state error should mention module id");
    require(result.error.contains(QStringLiteral("missing_0")),
            "missing instance-state error should mention missing instance id");
}

void testProjectReaderRejectsPreV1IpInstances() {
    QJsonObject root = minimalProjectRoot();
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

void testProjectReaderRejectsOldPluginRootKeys() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    QJsonObject root = minimalProjectRoot();
    root.insert(QStringLiteral("plugins"), QJsonArray{});
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_plugins.fpproj"));
    writeJsonFile(path, root);

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old plugins key");
    require(result.error.contains(QStringLiteral("plugins")),
            "plugins rejection should mention plugins");
}

void testProjectReaderRejectsOldPluginStateKey() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    QJsonObject root = minimalProjectRoot();
    root.insert(QStringLiteral("plugin_state"), QJsonArray{});
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_plugin_state.fpproj"));
    writeJsonFile(path, root);

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old plugin_state key");
    require(result.error.contains(QStringLiteral("plugin_state")),
            "plugin_state rejection should mention plugin_state");
}

void testProjectReaderRejectsOldModulePluginKey() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    QJsonObject root = minimalProjectRoot();
    QJsonObject graph = root.value(QStringLiteral("graph")).toObject();
    graph.insert(QStringLiteral("modules"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("tile_0")},
            {QStringLiteral("plugin"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("type"), QStringLiteral("RaveTile")},
            {QStringLiteral("parameters"), QJsonObject{}}
        }
    });
    root.insert(QStringLiteral("graph"), graph);
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_module_plugin.fpproj"));
    writeJsonFile(path, root);

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old module plugin key");
    require(result.error.contains(QStringLiteral("plugin")),
            "module plugin rejection should mention plugin");
}

void testIpcoreStateWriteAddsIpcoreDependency() {
    ProjectDocument document;
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };

    ProjectDocument source;
    source.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(source);
    service.writeToDocument(document);

    require(document.ipcores.size() == 1,
            "IP-core state write should add missing IP-core dependency metadata");
    require(document.ipcores.first().id == QStringLiteral("finepaper.ravenoc"),
            "IP-core state dependency should use IP-core state owner id");
}

void testProjectStateServiceUpdatesIpcoreStateWithoutGraph() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                 QStringLiteral("ravenoc_0"),
                                 QStringLiteral("global_parameters"),
                                 QStringLiteral("flit_data_width"),
                                 64),
            "IP-core state parameter update should succeed");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(requireStateRecord(saved.ipcoreState,
                               QStringLiteral("finepaper.ravenoc"),
                               QStringLiteral("ravenoc_0"))
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "service should write updated IP-core parameter");
}

void testProjectStateServiceDoesNotCreateMissingSection() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{};
    document.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(!service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                  QStringLiteral("ravenoc_0"),
                                  QStringLiteral("global_parameters"),
                                  QStringLiteral("flit_data_width"),
                                  64),
            "missing IP-core state section should not be created");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(!requireStateRecord(saved.ipcoreState,
                                QStringLiteral("finepaper.ravenoc"),
                                QStringLiteral("ravenoc_0"))
                 .state.contains(QStringLiteral("global_parameters")),
            "missing section should remain absent after failed update");
}

void testProjectStateServiceDoesNotOverwriteNonObjectSection() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QStringLiteral("opaque")}
    };
    document.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(!service.setParameter(QStringLiteral("finepaper.ravenoc"),
                                  QStringLiteral("ravenoc_0"),
                                  QStringLiteral("global_parameters"),
                                  QStringLiteral("flit_data_width"),
                                  64),
            "non-object IP-core state section should not be overwritten");

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(requireStateRecord(saved.ipcoreState,
                               QStringLiteral("finepaper.ravenoc"),
                               QStringLiteral("ravenoc_0"))
                .state.value(QStringLiteral("global_parameters"))
                .toString() == QStringLiteral("opaque"),
            "non-object section should remain unchanged after failed update");
}

void testProjectStateServiceParameterReturnsUndefinedForMissingValues() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{}}
    };
    document.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    require(service.parameter(QStringLiteral("finepaper.missing"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("global_parameters"),
                              QStringLiteral("flit_data_width"))
                .isUndefined(),
            "missing IP-core state record should return undefined");
    require(service.parameter(QStringLiteral("finepaper.ravenoc"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("missing_section"),
                              QStringLiteral("flit_data_width"))
                .isUndefined(),
            "missing IP-core state section should return undefined");
    require(service.parameter(QStringLiteral("finepaper.ravenoc"),
                              QStringLiteral("ravenoc_0"),
                              QStringLiteral("global_parameters"),
                              QStringLiteral("missing_name"))
                .isUndefined(),
            "missing IP-core state parameter should return undefined");
}

void testProjectStateServiceParameterPreservesExplicitNull() {
    ProjectDocument document = validProjectDocument();
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("optional_value"), QJsonValue(QJsonValue::Null)}
        }}
    };
    document.ipcoreState.push_back(state);

    ProjectStateService service;
    service.loadFromDocument(document);
    const QJsonValue value = service.parameter(QStringLiteral("finepaper.ravenoc"),
                                               QStringLiteral("ravenoc_0"),
                                               QStringLiteral("global_parameters"),
                                               QStringLiteral("optional_value"));
    require(value.isNull(), "explicit null IP-core state parameter should be preserved");
    require(!value.isUndefined(), "explicit null IP-core state parameter should not be undefined");
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

void testProjectLoadRejectsConnectionRuleFailure() {
    ProjectDocument document = validProjectDocument();
    document.modules.clear();
    document.connections.clear();
    document.ipcoreState.push_back(ProjectIpInstanceRecord{
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}
    });

    ProjectModuleRecord left;
    left.id = QStringLiteral("left");
    left.ipcoreId = QStringLiteral("finepaper.ravenoc");
    left.instanceId = QStringLiteral("ravenoc_0");
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

void testProjectLoadReportsIpcoreConnectionRuleFailure() {
    ModuleType sourceType = makeProjectEndpointType();
    sourceType.name = QStringLiteral("ProjectDocAxiSource");
    sourceType.ipcoreId = QStringLiteral("finepaper.test");
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
    targetType.interfaceMetadata[QStringLiteral("noc")].acceptedValues.insert(
        QStringLiteral("protocol"),
        QStringList{QStringLiteral("apb")});
    ModuleRegistry::instance().registerType(targetType);

    ProjectDocument document;
    document.name = QStringLiteral("ipcore_rule_failure");
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.test"), QStringLiteral("1.0")});
    document.ipcoreState.push_back(ProjectIpInstanceRecord{
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}
    });
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("source"),
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        sourceType.name,
        {}
    });
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("target"),
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        targetType.name,
        {}
    });
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
            {QStringLiteral("ipcores"), QJsonArray{}}
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

    QJsonObject wrongIpcoreState = baseProject();
    wrongIpcoreState.insert(QStringLiteral("ipcore_state"), QJsonObject{});
    wrongIpcoreState.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_ipcore_state.fpproj"));
    writeJsonFile(path, wrongIpcoreState);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-array ipcore_state should be rejected");
    require(result.error.contains(QStringLiteral("ipcore_state")),
            "ipcore_state error should mention ipcore_state");

    QJsonObject wrongIpcoreStateItem = baseProject();
    wrongIpcoreStateItem.insert(QStringLiteral("ipcore_state"), QJsonArray{
        QStringLiteral("not-an-object")
    });
    wrongIpcoreStateItem.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_ipcore_state_item.fpproj"));
    writeJsonFile(path, wrongIpcoreStateItem);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-object ipcore_state item should be rejected");
    require(result.error.contains(QStringLiteral("ipcore_state")),
            "ipcore_state item error should mention ipcore_state");

    QJsonObject missingIpcoreStateObject = baseProject();
    missingIpcoreStateObject.insert(QStringLiteral("ipcore_state"), QJsonArray{
        QJsonObject{
            {QStringLiteral("ipcore"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("instance"), QStringLiteral("ravenoc_0")},
            {QStringLiteral("schema"), QStringLiteral("ravenoc-project-state-v1")}
        }
    });
    missingIpcoreStateObject.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("missing_ipcore_state_object.fpproj"));
    writeJsonFile(path, missingIpcoreStateObject);
    result = ProjectReader::readFile(path);
    require(!result.success, "missing ipcore_state state should be rejected");
    require(result.error.contains(QStringLiteral("ipcore_state")) &&
                result.error.contains(QStringLiteral("state")),
            "missing ipcore_state state error should mention ipcore_state and state");

    QJsonObject wrongIpcoreStateObject = baseProject();
    wrongIpcoreStateObject.insert(QStringLiteral("ipcore_state"), QJsonArray{
        QJsonObject{
            {QStringLiteral("ipcore"), QStringLiteral("finepaper.ravenoc")},
            {QStringLiteral("instance"), QStringLiteral("ravenoc_0")},
            {QStringLiteral("schema"), QStringLiteral("ravenoc-project-state-v1")},
            {QStringLiteral("state"), QStringLiteral("not-an-object")}
        }
    });
    wrongIpcoreStateObject.insert(QStringLiteral("graph"), QJsonObject{
        {QStringLiteral("modules"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });
    path = QDir(tempDir.path()).filePath(QStringLiteral("wrong_ipcore_state_object.fpproj"));
    writeJsonFile(path, wrongIpcoreStateObject);
    result = ProjectReader::readFile(path);
    require(!result.success, "non-object ipcore_state state should be rejected");
    require(result.error.contains(QStringLiteral("ipcore_state")) &&
                result.error.contains(QStringLiteral("state")),
            "non-object ipcore_state state error should mention ipcore_state and state");
}

void testProjectReaderDetectsOnlyFinepaperProjects() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("design.fpproj"));
    const QString preV1Path = QDir(tempDir.path()).filePath(QStringLiteral("pre_v1_graph.json"));

    const ProjectWriteResult writeResult =
        ProjectWriter::writeFile(projectPath, validProjectDocument());
    require(writeResult.success, "failed to write project fixture");

    writeJsonFile(preV1Path, QJsonObject{
        {QStringLiteral("name"), QStringLiteral("pre_v1_graph")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("xps"), QJsonArray{}},
        {QStringLiteral("endpoints"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}}
    });

    require(ProjectReader::detectKind(projectPath) == ProjectFileKind::Project,
            "project file should be detected as Finepaper project");
    require(ProjectReader::detectKind(preV1Path) == ProjectFileKind::Unknown,
            "pre-v1 graph JSON should not be a supported import kind");
}

void testGenerationHelpersShapeIpcoreStateForGeneratorBoundary() {
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("type"), QStringLiteral("RaveNoC")},
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };
    const QVector<ProjectIpInstanceRecord> records{state};

    const QJsonArray states = ipcoreStateArray(records);
    require(states.size() == 1, "IP-core state helper should serialize one record");
    require(states.first().toObject().value(QStringLiteral("ipcore")).toString() ==
                QStringLiteral("finepaper.ravenoc"),
            "IP-core state helper should serialize ipcore owner");
    require(states.first().toObject()
                .value(QStringLiteral("state"))
                .toObject()
                .value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "IP-core state helper should preserve global parameters");

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.ravenoc");
    entry.name = QStringLiteral("RaveNoC");
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");

    Graph graph;
    auto module = instantiate(makeProjectXpType(), QStringLiteral("node_1"));
    module->setIpcoreId(QStringLiteral("finepaper.ravenoc"));
    module->setInstanceId(QStringLiteral("ravenoc_0"));
    require(graph.addModule(std::move(module)), "failed to add generator boundary module");

    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            entry,
            state,
            QStringLiteral("generated_design"),
            nullptr
        });
    require(exportResult.success, exportResult.error.toLocal8Bit().constData());
    const QJsonObject root = exportResult.document.object();

    require(root.value(QStringLiteral("schema")).toString() == QStringLiteral("finepaper-ipcore-graph-v1"),
            "generated input should use IP-core graph schema");
    require(root.contains(QStringLiteral("ipcore_state")),
            "generated input should include ipcore_state");
    require(!root.contains(QStringLiteral("plugin_state")),
            "generated input should not include old plugin_state");
    require(!root.value(QStringLiteral("modules")).toArray().first().toObject()
                 .contains(QStringLiteral("plug") + QStringLiteral("in")),
            "generated input module should not include plugin owner");
    const QString preV1IpInstanceField = QStringLiteral("ip_") + QStringLiteral("instance");
    require(!root.contains(preV1IpInstanceField),
            "generated input should not include pre-v1 IP instance field");
}

void testGenerationWritesProjectSnapshot() {
    Graph graph;
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
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
                                      QVector<ProjectIpInstanceRecord>{state});
    require(result.success, "generation project snapshot should be written");
    require(QFileInfo::exists(result.path), "generation project snapshot should exist");
    require(QFileInfo(result.path).fileName() == QStringLiteral("generated_design.fpproj"),
            "generation project snapshot should use design name");

    const ProjectReadResult readResult = ProjectReader::readFile(result.path);
    require(readResult.success, "generation project snapshot should be readable");
    require(readResult.document.name == QStringLiteral("generated_design"),
            "generation project snapshot should use design name as project name");
    require(readResult.document.ipcoreState.size() == 1,
            "generation project snapshot should include IP-core state");
    require(readResult.document.ipcores.size() == 1,
            "generation project snapshot should include IP-core state dependency");
    require(readResult.document.ipcores.first().id == QStringLiteral("finepaper.ravenoc"),
            "generation project snapshot dependency should use IP-core state owner id");
    require(readResult.document.ipcoreState.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "generation project snapshot should preserve IP-core parameters");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testProjectRoundTripRestoresModulesParametersAndConnections();
        testProjectPreservesOpaqueIpcoreState();
        testProjectWriterUsesIpcoreVocabulary();
        testProjectSerializerUsesModuleIpcoreOwnership();
        testLoadRejectsModuleWithoutMatchingIpcoreState();
        testProjectReaderRejectsPreV1IpInstances();
        testProjectReaderRejectsOldPluginRootKeys();
        testProjectReaderRejectsOldPluginStateKey();
        testProjectReaderRejectsOldModulePluginKey();
        testIpcoreStateWriteAddsIpcoreDependency();
        testProjectStateServiceUpdatesIpcoreStateWithoutGraph();
        testProjectStateServiceDoesNotCreateMissingSection();
        testProjectStateServiceDoesNotOverwriteNonObjectSection();
        testProjectStateServiceParameterReturnsUndefinedForMissingValues();
        testProjectStateServiceParameterPreservesExplicitNull();
        testReaderRejectsWrongKind();
        testLoadRejectsDuplicateModuleIds();
        testLoadRejectsMissingModuleType();
        testLoadRejectsInvalidParameterType();
        testLoadRejectsInvalidConnectionReference();
        testProjectLoadRejectsConnectionRuleFailure();
        testProjectLoadReportsIpcoreConnectionRuleFailure();
        testLoadRejectsConnectionInvalidatedByEarlierConnectionWithoutChangingGraph();
        testReaderRejectsMalformedProjectGraphArrays();
        testProjectReaderDetectsOnlyFinepaperProjects();
        testGenerationHelpersShapeIpcoreStateForGeneratorBoundary();
        testGenerationWritesProjectSnapshot();
    } catch (const std::exception& error) {
        std::cerr << "projectdocument_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectdocument_test passed\n";
    return 0;
}
