// Project document tests for .fpproj save/load behavior.
#include "app/appsettings.h"
#include "app/generationartifacts.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/parameter.h"
#include "graph/port.h"
#include "ipcore/ipcoregraphexporter.h"
#include "modules/moduleregistry.h"
#include "project/graphprojectserializer.h"
#include "project/projectdesignserializer.h"
#include "project/projectreader.h"
#include "project/projectstateservice.h"
#include "project/projectwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
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

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePath));
            if (info.exists()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePath)).absoluteFilePath();
}

void configureDefaultPackageRootsForTest() {
    static QTemporaryDir settingsRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("projectdocument_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("projectdocument_test_app"));
    AppSettings().setIpcorePaths(QStringList{repositoryPath(QStringLiteral("ipcores"))});
}

QJsonObject minimalProjectRoot() {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("ipcraft.project.v1")},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("project_0")},
            {QStringLiteral("name"), QStringLiteral("minimal")}
        }},
        {QStringLiteral("instances"), QJsonArray{}},
        {QStringLiteral("composition"), QJsonObject{
            {QStringLiteral("connections"), QJsonArray{}},
            {QStringLiteral("external_ports"), QJsonArray{}}
        }},
        {QStringLiteral("layout"), QJsonObject{{QStringLiteral("views"), QJsonArray{}}}},
        {QStringLiteral("diagnostics"), QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("ipcraft.diagnostics.v1")},
            {QStringLiteral("records"), QJsonArray{}}
        }},
        {QStringLiteral("artifacts"), QJsonObject{}},
        {QStringLiteral("migration"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
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

ModuleInterfaceMetadata makeProjectConnectionInterface(const QString& id,
                                                       const QString& role,
                                                       const QString& connectionClassId) {
    ModuleInterfaceMetadata metadata;
    metadata.id = id;
    metadata.label = id.toUpper();
    metadata.bus = connectionClassId;
    metadata.role = role;
    metadata.compatibleRoles = role == QStringLiteral("node")
        ? QStringList{QStringLiteral("interconnect")}
        : QStringList{QStringLiteral("node")};
    metadata.acceptRules.push_back(IpcraftInterfaceAcceptRule{connectionClassId, role});
    return metadata;
}

ModuleType makeProjectChiRnfType() {
    ModuleType type;
    type.name = QStringLiteral("ProjectDocChiRnf");
    type.ipcoreId = QStringLiteral("finepaper.chi");
    type.defaultPorts.push_back(Port(QStringLiteral("chi"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("CHI"),
                                     QStringLiteral("CHI requester interface"),
                                     QStringLiteral("attachment"),
                                     QStringLiteral("chi_node_interface"),
                                     QStringLiteral("chi")));
    type.interfaceMetadata.insert(QStringLiteral("chi"),
                                  makeProjectConnectionInterface(QStringLiteral("chi"),
                                                                 QStringLiteral("node"),
                                                                 QStringLiteral("chi_node_interface")));
    return type;
}

ModuleType makeProjectChiXpType() {
    ModuleType type;
    type.name = QStringLiteral("ProjectDocChiXp");
    type.ipcoreId = QStringLiteral("finepaper.chi");
    type.defaultPorts.push_back(Port(QStringLiteral("rnf0"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("RNF0"),
                                     QStringLiteral("CHI interconnect interface"),
                                     QStringLiteral("attachment"),
                                     QStringLiteral("chi_node_interface"),
                                     QStringLiteral("rnf0")));
    type.interfaceMetadata.insert(QStringLiteral("rnf0"),
                                  makeProjectConnectionInterface(QStringLiteral("rnf0"),
                                                                 QStringLiteral("interconnect"),
                                                                 QStringLiteral("chi_node_interface")));
    return type;
}

IpcraftInterfaceAcceptRule projectAcceptRule(const QString& connectionClassId,
                                             const QString& role) {
    IpcraftInterfaceAcceptRule rule;
    rule.connectionClassId = connectionClassId;
    rule.role = role;
    return rule;
}

IpcraftInterfaceDescriptor projectInterfaceDescriptor(
    const QString& id,
    QVector<IpcraftInterfaceAcceptRule> accepts) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.accepts = std::move(accepts);
    return descriptor;
}

IpcraftModuleDescriptor projectManifestModule(
    const QString& id,
    QVector<IpcraftInterfaceDescriptor> interfaces) {
    IpcraftModuleDescriptor descriptor;
    descriptor.id = id;
    descriptor.interfaces = std::move(interfaces);
    return descriptor;
}

IpcraftPackageManifest projectLoadManifest() {
    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("finepaper.project_load_manifest");
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("project_load_link"),
        QStringList{QStringLiteral("endpoint"), QStringLiteral("interconnect")},
        false
    });
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("project_load_unaccepted"),
        QStringList{QStringLiteral("endpoint"), QStringLiteral("interconnect")},
        false
    });
    manifest.modules.push_back(projectManifestModule(
        QStringLiteral("ProjectLoadEndpoint"),
        {projectInterfaceDescriptor(
            QStringLiteral("noc"),
            {projectAcceptRule(QStringLiteral("project_load_link"),
                               QStringLiteral("endpoint"))})}));
    manifest.modules.push_back(projectManifestModule(
        QStringLiteral("ProjectLoadXp"),
        {projectInterfaceDescriptor(
            QStringLiteral("local0"),
            {projectAcceptRule(QStringLiteral("project_load_link"),
                               QStringLiteral("interconnect"))})}));
    return manifest;
}

void ensureProjectLoadManifestRegistered(const IpcraftPackageManifest& manifest) {
    ModuleRegistry::instance().loadIpcraftPackages({manifest});
    require(ModuleRegistry::instance().packageManifest(manifest.id) != nullptr,
            "project-load manifest fixture should be registered");
    require(ModuleRegistry::instance().getType(QStringLiteral("ProjectLoadEndpoint")) != nullptr,
            "project-load endpoint type should be registered");
    require(ModuleRegistry::instance().getType(QStringLiteral("ProjectLoadXp")) != nullptr,
            "project-load XP type should be registered");
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

void registerProjectConnectionTypes() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleRegistry::instance().registerType(makeProjectChiRnfType());
    ModuleRegistry::instance().registerType(makeProjectChiXpType());
    registered = true;
}

void registerScopedProjectType(const QString& packageId, const QString& moduleId) {
    ModuleType type;
    type.name = ModuleRegistry::scopedTypeName(packageId, moduleId);
    type.packageId = packageId;
    type.moduleId = moduleId;
    type.ipcoreId = packageId;
    ModuleRegistry::instance().registerType(type);
}

ProjectIpInstanceRecord projectInstanceRecord(const QString& ipcoreId,
                                              const QString& instanceId,
                                              const QString& schema,
                                              const QJsonObject& state) {
    ProjectIpInstanceRecord record;
    record.id = instanceId;
    record.package = ProjectPackageRef{ipcoreId, QStringLiteral("1.0")};
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.schema = schema;
    record.state = state;
    const QJsonValue globalParameters = state.value(QStringLiteral("global_parameters"));
    if (globalParameters.isObject()) {
        record.config.insert(QStringLiteral("parameters"), globalParameters.toObject());
    }
    return record;
}

ProjectDocument validProjectDocument() {
    registerProjectTypes();

    ProjectDocument document;
    document.name = QStringLiteral("validation");
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.test"),
                                                   QStringLiteral("1.0")});
    document.ipcoreState.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));

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

    ProjectConnectionRecord connection;
    connection.id = QStringLiteral("conn_1");
    connection.source = ProjectConnectionEndpoint{QStringLiteral("node_1"), QStringLiteral("ep0")};
    connection.target = ProjectConnectionEndpoint{QStringLiteral("node_2"), QStringLiteral("noc")};
    document.connections.push_back(connection);

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

void testProjectWritesInterfaceConnectionsWithoutFromTo() {
    registerProjectConnectionTypes();

    Graph graph;
    auto rnf = instantiate(makeProjectChiRnfType(), QStringLiteral("rnf_0"));
    rnf->setInstanceId(QStringLiteral("opennoc_0"));
    auto xp = instantiate(makeProjectChiXpType(), QStringLiteral("xp_0"));
    xp->setInstanceId(QStringLiteral("opennoc_0"));

    require(graph.addModule(std::move(rnf)), "failed to add RNF module");
    require(graph.addModule(std::move(xp)), "failed to add XP module");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("conn_0"),
        PortRef{QStringLiteral("rnf_0"), QStringLiteral("chi")},
        PortRef{QStringLiteral("xp_0"), QStringLiteral("rnf0")}));
    require(graph.connections().size() == 1, "setup connection should be valid");

    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("interfaces"));
    document.ipcoreState.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.chi"),
        QStringLiteral("opennoc_0"),
        QStringLiteral("finepaper.chi-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    ProjectStateService stateService;
    stateService.loadFromDocument(document);
    stateService.writeToDocument(document);

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("interfaces.fpproj"));
    require(ProjectWriter::writeFile(path, document).success, "project should write");

    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project should reopen");
    const QJsonObject project = QJsonDocument::fromJson(file.readAll()).object();
    require(!project.contains(QStringLiteral("graph")),
            "V1 project should not write legacy root graph");
    const QJsonObject connection = project.value(QStringLiteral("instances"))
                                       .toArray()
                                       .first()
                                       .toObject()
                                       .value(QStringLiteral("graph_config"))
                                       .toObject()
                                       .value(QStringLiteral("relationships"))
                                       .toArray()
                                       .first()
                                       .toObject();

    require(connection.value(QStringLiteral("id")).toString() == QStringLiteral("conn_0"),
            "connection id should be written");
    require(connection.value(QStringLiteral("type")).toString() == QStringLiteral("chi_node_interface"),
            "connection type should be written");
    require(connection.value(QStringLiteral("properties")).toObject().value(QStringLiteral("status")).toString() == QStringLiteral("valid"),
            "connection status should be written");
    require(!connection.contains(QStringLiteral("source")),
            "new project connection should not write source endpoint");
    require(!connection.contains(QStringLiteral("target")),
            "new project connection should not write target endpoint");
    require(!connection.contains(QStringLiteral("from")),
            "new project connection should not write from endpoint");
    require(!connection.contains(QStringLiteral("to")),
            "new project connection should not write to endpoint");

    const QJsonArray interfaces = connection.value(QStringLiteral("endpoints")).toArray();
    require(interfaces.size() == 2, "connection should write two interface participants");
    require(interfaces.at(0).toObject().value(QStringLiteral("object")).toString() ==
                QStringLiteral("rnf_0"),
            "first participant instance should be written");
    require(interfaces.at(0).toObject().value(QStringLiteral("role")).toString() ==
                QStringLiteral("chi"),
            "first participant interface should be written");
    require(interfaces.at(1).toObject().value(QStringLiteral("object")).toString() ==
                QStringLiteral("xp_0"),
            "second participant instance should be written");
    require(interfaces.at(1).toObject().value(QStringLiteral("role")).toString() ==
                QStringLiteral("rnf0"),
            "second participant interface should be written");
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
    ProjectStateService stateService;
    stateService.loadFromDocument(document);
    stateService.writeToDocument(document);
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

void testProjectLoadUsesLoadedPackageManifestsForInterfaceRules() {
    const IpcraftPackageManifest manifest = projectLoadManifest();
    ensureProjectLoadManifestRegistered(manifest);

    ProjectDocument document;
    document.name = QStringLiteral("manifest_connection_load");
    document.ipcores.push_back(ProjectIpcoreRecord{manifest.id, QStringLiteral("1.0")});
    document.ipcoreState.push_back(projectInstanceRecord(
        manifest.id,
        QStringLiteral("manifest_0"),
        manifest.id + QStringLiteral("-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("endpoint"),
        manifest.id,
        QStringLiteral("manifest_0"),
        QStringLiteral("ProjectLoadEndpoint"),
        {}
    });
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("xp"),
        manifest.id,
        QStringLiteral("manifest_0"),
        QStringLiteral("ProjectLoadXp"),
        {}
    });
    ProjectConnectionRecord manifestConnection;
    manifestConnection.id = QStringLiteral("conn_manifest");
    manifestConnection.source = ProjectConnectionEndpoint{QStringLiteral("endpoint"), QStringLiteral("noc")};
    manifestConnection.target = ProjectConnectionEndpoint{QStringLiteral("xp"), QStringLiteral("local0")};
    document.connections.push_back(manifestConnection);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "project load should restore the manifest-backed connection");
    const Connection* connection = graph.connections().front().get();
    require(connection->connectionClassId() == QStringLiteral("project_load_link"),
            "project load should validate and store the manifest connection class");
    require(connection->status() == QStringLiteral("valid"),
            "manifest-backed project connection should load as valid");
}

void testProjectLoadRejectsSavedConnectionClassNotAcceptedByInterfaces() {
    const IpcraftPackageManifest manifest = projectLoadManifest();
    ensureProjectLoadManifestRegistered(manifest);

    ProjectDocument document;
    document.name = QStringLiteral("manifest_connection_bad_class");
    document.ipcores.push_back(ProjectIpcoreRecord{manifest.id, QStringLiteral("1.0")});
    document.ipcoreState.push_back(projectInstanceRecord(
        manifest.id,
        QStringLiteral("manifest_bad_class_0"),
        manifest.id + QStringLiteral("-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("endpoint_bad_class"),
        manifest.id,
        QStringLiteral("manifest_bad_class_0"),
        QStringLiteral("ProjectLoadEndpoint"),
        {}
    });
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("xp_bad_class"),
        manifest.id,
        QStringLiteral("manifest_bad_class_0"),
        QStringLiteral("ProjectLoadXp"),
        {}
    });

    ProjectConnectionRecord connection;
    connection.id = QStringLiteral("conn_bad_class");
    connection.source = ProjectConnectionEndpoint{QStringLiteral("endpoint_bad_class"), QStringLiteral("noc")};
    connection.target = ProjectConnectionEndpoint{QStringLiteral("xp_bad_class"), QStringLiteral("local0")};
    connection.connectionClassId = QStringLiteral("project_load_unaccepted");
    document.connections.push_back(connection);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success,
            "project load should reject a saved connection class not accepted by its interfaces");
    require(result.error.contains(QStringLiteral("conn_bad_class")),
            "saved class rejection should mention the connection id");
    require(result.error.contains(QStringLiteral("interface_class_mismatch")),
            "saved class rejection should surface the interface class validation reason");
    require(graph.modules().empty(),
            "failed saved class validation should not mutate the graph");
    require(graph.connections().empty(),
            "failed saved class validation should not restore the bad connection");
}

void testProjectLoadRejectsInterfaceConnectionMissingSavedClass() {
    const IpcraftPackageManifest manifest = projectLoadManifest();
    ensureProjectLoadManifestRegistered(manifest);

    ProjectDocument document;
    document.name = QStringLiteral("manifest_connection_missing_class");
    document.ipcores.push_back(ProjectIpcoreRecord{manifest.id, QStringLiteral("1.0")});
    document.ipcoreState.push_back(projectInstanceRecord(
        manifest.id,
        QStringLiteral("manifest_missing_class_0"),
        manifest.id + QStringLiteral("-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("endpoint_missing_class"),
        manifest.id,
        QStringLiteral("manifest_missing_class_0"),
        QStringLiteral("ProjectLoadEndpoint"),
        {}
    });
    document.modules.push_back(ProjectModuleRecord{
        QStringLiteral("xp_missing_class"),
        manifest.id,
        QStringLiteral("manifest_missing_class_0"),
        QStringLiteral("ProjectLoadXp"),
        {}
    });

    ProjectConnectionRecord connection;
    connection.id = QStringLiteral("conn_missing_class");
    connection.interfaces = {
        ProjectConnectionInterfaceRef{QStringLiteral("endpoint_missing_class"), QStringLiteral("noc")},
        ProjectConnectionInterfaceRef{QStringLiteral("xp_missing_class"), QStringLiteral("local0")}
    };
    document.connections.push_back(connection);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success,
            "interface-based project load should reject missing saved connection class");
    require(result.error.contains(QStringLiteral("conn_missing_class")),
            "missing saved class rejection should mention the connection id");
    require(result.error.contains(QStringLiteral("class")),
            "missing saved class rejection should explain the missing class");
    require(graph.modules().empty(),
            "failed missing class validation should not mutate the graph");
    require(graph.connections().empty(),
            "failed missing class validation should not restore the bad connection");
}

void testProjectWriterUsesCanonicalProjectDocumentVocabulary() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    Graph graph;
    auto module = instantiate(makeProjectXpType(), QStringLiteral("tile_0"));
    module->setIpcoreId(QStringLiteral("finepaper.ravenoc"));
    module->setInstanceId(QStringLiteral("ravenoc_0"));
    module->setParameter(QStringLiteral("x"), 12);
    module->setParameter(QStringLiteral("y"), 34);
    module->setParameter(QStringLiteral("collapsed"), true);
    module->setParameter(QStringLiteral("vc_count"), 4);
    require(graph.addModule(std::move(module)), "canonical writer module should add");

    ProjectIpInstanceRecord state = projectInstanceRecord(
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}});

    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("ipcore_schema"));
    document.ipcoreState.push_back(state);
    ProjectStateService stateService;
    stateService.loadFromDocument(document);
    stateService.writeToDocument(document);

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("schema.fpproj"));
    require(ProjectWriter::writeFile(path, document).success, "project should write");

    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project should reopen");
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    require(root.value(QStringLiteral("schema")).toString() == QStringLiteral("ipcraft.project.v1"),
            "writer should emit ProjectDocument schema");
    require(!root.contains(QStringLiteral("ipcores")), "writer should not emit old ipcores");
    require(!root.contains(QStringLiteral("ipcore_state")), "writer should not emit old ipcore_state");
    require(!root.contains(QStringLiteral("graph")), "writer should not emit old root graph");
    require(!root.contains(QStringLiteral("plugins")), "writer should not emit plugins");
    require(!root.contains(QStringLiteral("plugin_state")), "writer should not emit plugin_state");

    const QJsonObject instance = root.value(QStringLiteral("instances")).toArray().first().toObject();
    require(instance.value(QStringLiteral("id")).toString() == QStringLiteral("ravenoc_0"),
            "writer should emit canonical instance id");
    require(instance.value(QStringLiteral("package")).toObject().value(QStringLiteral("id")).toString() ==
                QStringLiteral("finepaper.ravenoc"),
            "writer should emit canonical package id");
    require(instance.value(QStringLiteral("config")).toObject()
                .value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("flit_data_width")).toInt() == 32,
            "writer should emit config parameters");

    const QJsonObject graphObject = instance.value(QStringLiteral("graph_config")).toObject()
                                        .value(QStringLiteral("objects")).toArray().first().toObject();
    require(graphObject.value(QStringLiteral("id")).toString() == QStringLiteral("tile_0"),
            "writer should emit graph-config object id");
    const QJsonObject graphProperties = graphObject.value(QStringLiteral("properties")).toObject();
    require(graphProperties.value(QStringLiteral("vc_count")).toInt() == 4,
            "writer should keep non-layout graph object properties");
    require(!graphProperties.contains(QStringLiteral("x")) &&
                !graphProperties.contains(QStringLiteral("y")) &&
                !graphProperties.contains(QStringLiteral("collapsed")),
            "writer should move editor layout out of graph-config object properties");

    const QJsonObject layoutNode = root.value(QStringLiteral("layout")).toObject()
                                       .value(QStringLiteral("views")).toArray().first().toObject()
                                       .value(QStringLiteral("canvas")).toObject()
                                       .value(QStringLiteral("nodes")).toObject()
                                       .value(QStringLiteral("tile_0")).toObject();
    require(layoutNode.value(QStringLiteral("x")).toInt() == 12 &&
                layoutNode.value(QStringLiteral("y")).toInt() == 34 &&
                layoutNode.value(QStringLiteral("collapsed")).toBool(),
            "writer should emit editor layout in LayoutModel");
}

void testProjectGraphConfigLayoutBridgePreservesNativeData() {
    QJsonObject root = minimalProjectRoot();
    QJsonObject graphConfig{
        {QStringLiteral("schema"), QStringLiteral("ipcraft.graph-config.v1")},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("node_1")},
                {QStringLiteral("type"), QStringLiteral("ProjectDocXP")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("vc_count"), 4},
                    {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("node_2")},
                {QStringLiteral("type"), QStringLiteral("ProjectDocEndpoint")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("data_width"), 128}
                }}
            }
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("conn_1")},
                {QStringLiteral("type"), QStringLiteral("chi_node_interface")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("object"), QStringLiteral("node_1")},
                        {QStringLiteral("role"), QStringLiteral("ep0")},
                        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("lane"), 0}}}
                    },
                    QJsonObject{
                        {QStringLiteral("object"), QStringLiteral("node_2")},
                        {QStringLiteral("role"), QStringLiteral("noc")}
                    }
                }},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("ambiguous")},
                    {QStringLiteral("alternatives"), QJsonArray{
                        QStringLiteral("chi_node_interface"),
                        QStringLiteral("monitor_tap")
                    }},
                    {QStringLiteral("custom_property"), QStringLiteral("keep")}
                }}
            }
        }},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("editor")}}},
        {QStringLiteral("native"), QJsonObject{{QStringLiteral("vendor.example"), QJsonObject{
            {QStringLiteral("token"), QStringLiteral("keep")}
        }}}}
    };
    root.insert(QStringLiteral("instances"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test_0")},
            {QStringLiteral("package"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("finepaper.test")},
                {QStringLiteral("version"), QStringLiteral("1.0")}
            }},
            {QStringLiteral("config"), QJsonObject{
                {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("flit_data_width"), 64}}}
            }},
            {QStringLiteral("graph_config"), graphConfig}
        }
    });
    root.insert(QStringLiteral("layout"), QJsonObject{{QStringLiteral("views"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("graph")},
            {QStringLiteral("kind"), QStringLiteral("canvas")},
            {QStringLiteral("native"), QJsonObject{{QStringLiteral("view_token"), QStringLiteral("keep")}}},
            {QStringLiteral("canvas"), QJsonObject{
                {QStringLiteral("pan"), QJsonObject{
                    {QStringLiteral("x"), 1},
                    {QStringLiteral("y"), 2}
                }},
                {QStringLiteral("zoom"), 1.5},
                {QStringLiteral("connections"), QJsonObject{
                    {QStringLiteral("conn_1"), QJsonObject{{QStringLiteral("route"), QStringLiteral("keep")}}}
                }},
                {QStringLiteral("nodes"), QJsonObject{
                    {QStringLiteral("node_1"), QJsonObject{
                        {QStringLiteral("x"), 11},
                        {QStringLiteral("y"), 22},
                        {QStringLiteral("collapsed"), true},
                        {QStringLiteral("note"), QStringLiteral("keep")}
                    }},
                    {QStringLiteral("node_2"), QJsonObject{
                        {QStringLiteral("x"), 33},
                        {QStringLiteral("y"), 44},
                        {QStringLiteral("collapsed"), false}
                    }}
                }}
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("table_view")},
            {QStringLiteral("kind"), QStringLiteral("table")},
            {QStringLiteral("native"), QJsonObject{{QStringLiteral("keep"), true}}}
        }
    }}});

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("bridge_preserve.fpproj"));
    writeJsonFile(path, root);

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "canonical project with graph-config should read");
    const auto moduleIt = std::find_if(readResult.document.modules.constBegin(),
                                       readResult.document.modules.constEnd(),
                                       [](const ProjectModuleRecord& module) {
                                           return module.id == QStringLiteral("node_1");
                                       });
    require(moduleIt != readResult.document.modules.constEnd(), "graph-config object should project to module");
    require(!moduleIt->parameters.contains(QStringLiteral("note")),
            "layout-only metadata should not become a module parameter");

    ProjectDocument saved = readResult.document;
    ProjectStateService service;
    service.loadFromDocument(readResult.document);
    service.writeToDocument(saved);
    const QString outputPath = QDir(tempDir.path()).filePath(QStringLiteral("bridge_preserve_saved.fpproj"));
    require(ProjectWriter::writeFile(outputPath, saved).success,
            "bridge-preserved project should write");

    const QJsonObject savedGraphConfig = saved.instances.first().graphConfig;
    require(savedGraphConfig.value(QStringLiteral("properties")).toObject()
                .value(QStringLiteral("owner")).toString() == QStringLiteral("editor"),
            "graph-config root properties should be preserved");
    require(savedGraphConfig.value(QStringLiteral("native")).toObject()
                .value(QStringLiteral("vendor.example")).toObject()
                .value(QStringLiteral("token")).toString() == QStringLiteral("keep"),
            "graph-config native namespace should be preserved");

    const QJsonObject savedRelationship = savedGraphConfig.value(QStringLiteral("relationships"))
                                              .toArray().first().toObject();
    const QJsonObject savedRelationshipProperties =
        savedRelationship.value(QStringLiteral("properties")).toObject();
    require(savedRelationshipProperties.value(QStringLiteral("custom_property")).toString() ==
                QStringLiteral("keep"),
            "relationship custom properties should be preserved");
    require(savedRelationshipProperties.value(QStringLiteral("status")).toString() ==
                QStringLiteral("ambiguous"),
            "relationship status should be preserved");
    require(savedRelationship.value(QStringLiteral("endpoints")).toArray().first().toObject()
                .value(QStringLiteral("properties")).toObject()
                .value(QStringLiteral("lane")).toInt() == 0,
            "relationship endpoint properties should be preserved");

    const QJsonObject savedGraphView = saved.layout.value(QStringLiteral("views"))
                                          .toArray().first().toObject();
    require(saved.layout.value(QStringLiteral("views")).toArray().size() == 2,
            "layout bridge should preserve non-graph views");
    require(savedGraphView.value(QStringLiteral("native")).toObject()
                .value(QStringLiteral("view_token")).toString() == QStringLiteral("keep"),
            "layout view native data should be preserved");
    const QJsonObject savedCanvas = savedGraphView.value(QStringLiteral("canvas")).toObject();
    require(savedCanvas.value(QStringLiteral("pan")).toObject().value(QStringLiteral("x")).toInt() == 1,
            "layout canvas pan should be preserved");
    require(savedCanvas.value(QStringLiteral("connections")).toObject()
                .value(QStringLiteral("conn_1")).toObject()
                .value(QStringLiteral("route")).toString() == QStringLiteral("keep"),
            "layout connection metadata should be preserved");
    require(savedCanvas.value(QStringLiteral("nodes")).toObject()
                .value(QStringLiteral("node_1")).toObject()
                .value(QStringLiteral("note")).toString() == QStringLiteral("keep"),
            "layout node metadata should be preserved");
}

void testProjectStateServiceClearsStaleGraphConfigObjects() {
    ProjectDocument document;
    document.instances.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    ProjectIpInstanceRecord& instance = document.instances.first();
    instance.hasGraphConfig = true;
    instance.graphConfig = QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("ipcraft.graph-config.v1")},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("stale_node")},
                {QStringLiteral("type"), QStringLiteral("ProjectDocXP")}
            }
        }},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("editor")}}},
        {QStringLiteral("native"), QJsonObject{{QStringLiteral("vendor.example"), QJsonObject{
            {QStringLiteral("token"), QStringLiteral("keep")}
        }}}}
    };

    ProjectStateService service;
    service.loadFromDocument(document);
    ProjectDocument saved = document;
    saved.modules.clear();
    saved.connections.clear();
    service.writeToDocument(saved);

    const QJsonObject savedGraphConfig = saved.instances.first().graphConfig;
    require(savedGraphConfig.value(QStringLiteral("objects")).toArray().isEmpty(),
            "state service should clear stale graph-config objects when editor graph is empty");
    require(savedGraphConfig.value(QStringLiteral("relationships")).toArray().isEmpty(),
            "state service should clear stale graph-config relationships when editor graph is empty");
    require(savedGraphConfig.value(QStringLiteral("native")).toObject()
                .value(QStringLiteral("vendor.example")).toObject()
                .value(QStringLiteral("token")).toString() == QStringLiteral("keep"),
            "state service should preserve graph-config native data while clearing editor arrays");
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

void testProjectDesignSerializerRoundTripsDesignWithoutGraphProjection() {
    ipcraft::core::ProjectDesign design;
    design.schema = QStringLiteral("ipcraft.project.v1");
    design.id = QStringLiteral("serializer_project");
    design.name = QStringLiteral("Serializer Project");
    design.packages.append(ipcraft::core::PackageRef{QStringLiteral("vendor.serializer"),
                                                     QStringLiteral("2.1.0")});

    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("serializer_component");
    component.type = QStringLiteral("SerializerBlock");
    component.packageRef = QStringLiteral("vendor.serializer@2.1.0");
    component.config = QJsonObject{
        {QStringLiteral("width"), 64},
        {QStringLiteral("mode"), QStringLiteral("fast")},
        {QStringLiteral("tables"), QJsonObject{
            {QStringLiteral("address_map"), QJsonObject{{QStringLiteral("base"), 4096}}}
        }}
    };
    design.components.append(component);

    const ProjectDocument document = ProjectDesignSerializer::toDocument(design);

    require(document.schema == QStringLiteral("ipcraft.project.v1"),
            "design serializer should preserve schema");
    require(document.projectId == QStringLiteral("serializer_project"),
            "design serializer should preserve project id");
    require(document.projectName == QStringLiteral("Serializer Project") &&
                document.name == QStringLiteral("Serializer Project"),
            "design serializer should preserve project name");
    require(document.ipcores.size() == 1 &&
                document.ipcores.first().id == QStringLiteral("vendor.serializer") &&
                document.ipcores.first().version == QStringLiteral("2.1.0"),
            "design serializer should preserve package refs");
    require(document.instances.size() == 1,
            "design serializer should write one project instance");
    require(document.modules.isEmpty() && document.connections.isEmpty(),
            "design serializer should not project components into graph records");

    const ProjectIpInstanceRecord& instance = document.instances.first();
    require(instance.id == QStringLiteral("serializer_component") &&
                instance.instanceId == QStringLiteral("serializer_component"),
            "design serializer should preserve component id");
    require(instance.native.value(QStringLiteral("componentType")).toString() ==
                QStringLiteral("SerializerBlock"),
            "design serializer should preserve component type");
    require(instance.package.id == QStringLiteral("vendor.serializer") &&
                instance.package.version == QStringLiteral("2.1.0"),
            "design serializer should preserve component package ref");
    require(instance.config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 64,
            "design serializer should store flat config under parameters");
    require(instance.config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("mode")).toString() == QStringLiteral("fast"),
            "design serializer should keep all flat parameter keys");
    require(instance.config.value(QStringLiteral("tables")).toObject()
                .value(QStringLiteral("address_map")).toObject()
                .value(QStringLiteral("base")).toInt() == 4096,
            "design serializer should keep object config sections outside parameters");

    const ipcraft::core::ProjectDesign restored =
        ProjectDesignSerializer::fromDocument(document);

    require(restored.schema == QStringLiteral("ipcraft.project.v1"),
            "design serializer should reload schema");
    require(restored.id == QStringLiteral("serializer_project"),
            "design serializer should reload project id");
    require(restored.name == QStringLiteral("Serializer Project"),
            "design serializer should reload project name");
    require(restored.packages.size() == 1 &&
                restored.packages.first().id == QStringLiteral("vendor.serializer") &&
                restored.packages.first().version == QStringLiteral("2.1.0"),
            "design serializer should reload package refs");
    require(restored.components.size() == 1,
            "design serializer should reload one component");
    const ipcraft::core::ComponentInstance& restoredComponent = restored.components.first();
    require(restoredComponent.id == QStringLiteral("serializer_component"),
            "design serializer should reload component id");
    require(restoredComponent.type == QStringLiteral("SerializerBlock"),
            "design serializer should reload component type");
    require(restoredComponent.packageRef == QStringLiteral("vendor.serializer@2.1.0"),
            "design serializer should reload component package ref");
    require(restoredComponent.config.value(QStringLiteral("width")).toInt() == 64,
            "design serializer should reload flat parameter keys");
    require(restoredComponent.config.value(QStringLiteral("mode")).toString() ==
                QStringLiteral("fast"),
            "design serializer should reload every flat parameter key");
    require(restoredComponent.config.value(QStringLiteral("tables")).toObject()
                .value(QStringLiteral("address_map")).toObject()
                .value(QStringLiteral("base")).toInt() == 4096,
            "design serializer should reload object config sections");
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

void testLoadRejectsDuplicateIpcoreStateScope() {
    ProjectDocument document = validProjectDocument();
    document.ipcoreState.push_back(document.ipcoreState.first());

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "duplicate ipcore_state scope should be rejected");
    require(result.error.contains(QStringLiteral("Duplicate")),
            "duplicate ipcore_state error should mention duplication");
    require(result.error.contains(QStringLiteral("test_0")),
            "duplicate ipcore_state error should mention the duplicated instance id");
}

void testLoadRejectsIpcoreStateMissingOwnerFields() {
    {
        ProjectDocument document = validProjectDocument();
        document.ipcoreState.first().ipcoreId.clear();

        Graph graph;
        const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

        require(!result.success, "ipcore_state without ipcore should be rejected");
        require(result.error.contains(QStringLiteral("ipcore_state")),
                "missing ipcore_state ipcore error should mention ipcore_state");
        require(result.error.contains(QStringLiteral("ipcore")),
                "missing ipcore_state ipcore error should mention ipcore");
    }

    {
        ProjectDocument document = validProjectDocument();
        document.ipcoreState.first().instanceId.clear();

        Graph graph;
        const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

        require(!result.success, "ipcore_state without instance should be rejected");
        require(result.error.contains(QStringLiteral("ipcore_state")),
                "missing ipcore_state instance error should mention ipcore_state");
        require(result.error.contains(QStringLiteral("instance")),
                "missing ipcore_state instance error should mention instance");
    }
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
    require(!result.success, "old project schema should be rejected");
    require(result.error.contains(QStringLiteral("schema")),
            "old project rejection should mention schema");
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

void testLoadDisambiguatesBareModuleTypeByIpcoreId() {
    const QString moduleId = QStringLiteral("ProjectDocSharedTile");
    const QString alphaPackage = QStringLiteral("finepaper.project_doc_alpha");
    const QString betaPackage = QStringLiteral("finepaper.project_doc_beta");
    registerScopedProjectType(alphaPackage, moduleId);
    registerScopedProjectType(betaPackage, moduleId);

    ProjectDocument document;
    ProjectIpInstanceRecord state;
    state.ipcoreId = betaPackage;
    state.instanceId = QStringLiteral("beta_0");
    state.schema = QStringLiteral("ipcraft.noc.instance-state.v1");
    state.state = QJsonObject{};
    document.ipcoreState.push_back(state);

    ProjectModuleRecord module;
    module.id = QStringLiteral("node_0");
    module.type = moduleId;
    module.ipcoreId = betaPackage;
    module.instanceId = state.instanceId;
    document.modules.push_back(module);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(result.success, result.error.toLocal8Bit().constData());
    const Module* restored = graph.getModule(QStringLiteral("node_0"));
    require(restored != nullptr, "bare typed module should load");
    require(restored->type() == ModuleRegistry::scopedTypeName(betaPackage, moduleId),
            "bare project module type should resolve through the owning ipcore id");
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

void testLoadRejectsEmptyConnectionIdWithoutChangingGraph() {
    registerProjectTypes();

    Graph graph;
    require(graph.addModule(instantiate(makeProjectEndpointType(), QStringLiteral("existing_node"))),
            "failed to add existing module");

    ProjectDocument document = validProjectDocument();
    document.connections.first().id.clear();

    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "empty connection id should be rejected");
    require(result.error.contains(QStringLiteral("Connection is missing id")),
            "empty connection id error should mention missing id");
    require(graph.modules().size() == 1, "failed project load should preserve existing modules");
    require(graph.connections().empty(), "failed project load should preserve existing connections");
    require(graph.getModule(QStringLiteral("existing_node")) != nullptr,
            "failed project load should keep the previous graph");
}

void testLoadRejectsDuplicateConnectionIdsWithoutChangingGraph() {
    registerProjectTypes();

    Graph graph;
    require(graph.addModule(instantiate(makeProjectEndpointType(), QStringLiteral("existing_node"))),
            "failed to add existing module");

    ProjectDocument document = validProjectDocument();
    document.connections.push_back(document.connections.first());

    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "duplicate connection id should be rejected");
    require(result.error.contains(QStringLiteral("Duplicate connection id: conn_1")),
            "duplicate connection id error should mention the duplicated id");
    require(graph.modules().size() == 1, "failed project load should preserve existing modules");
    require(graph.connections().empty(), "failed project load should preserve existing connections");
    require(graph.getModule(QStringLiteral("existing_node")) != nullptr,
            "failed project load should keep the previous graph");
}

void testProjectLoadRejectsConnectionRuleFailure() {
    ProjectDocument document = validProjectDocument();
    document.modules.clear();
    document.connections.clear();
    document.ipcoreState.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));

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

    ProjectConnectionRecord badSameSide;
    badSameSide.id = QStringLiteral("bad_same_side");
    badSameSide.source = ProjectConnectionEndpoint{QStringLiteral("left"), QStringLiteral("east")};
    badSameSide.target = ProjectConnectionEndpoint{QStringLiteral("right"), QStringLiteral("east")};
    document.connections.push_back(badSameSide);

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
    document.ipcoreState.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_0"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
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
    ProjectConnectionRecord badConnection;
    badConnection.id = QStringLiteral("bad_connection");
    badConnection.source = ProjectConnectionEndpoint{QStringLiteral("source"), QStringLiteral("noc")};
    badConnection.target = ProjectConnectionEndpoint{QStringLiteral("target"), QStringLiteral("noc")};
    document.connections.push_back(badConnection);

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "project load should reject IP-core rule mismatch");
    require(result.error.contains(QStringLiteral("interface_field_mismatch")),
            "project load should surface IP-core layer reason code");
    require(graph.modules().empty(), "failed load should not mutate graph");
}

void testProjectLoadRejectsCrossInstanceConnectionWithinSameIpcore() {
    ProjectDocument document = validProjectDocument();
    document.ipcoreState.push_back(projectInstanceRecord(
        QStringLiteral("finepaper.test"),
        QStringLiteral("test_1"),
        QStringLiteral("finepaper.test-project-state-v1"),
        QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}));
    document.modules.last().instanceId = QStringLiteral("test_1");
    document.connections.first().id = QStringLiteral("cross_instance_conn");

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);

    require(!result.success, "project load should reject same-IP cross-instance connections");
    require(result.error.contains(QStringLiteral("cross_instance_conn")),
            "cross-instance load failure should mention the connection id");
    require(result.error.contains(QStringLiteral("ip_instance_mismatch")),
            "cross-instance load failure should surface the instance mismatch reason code");
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
    ProjectConnectionRecord secondConnection;
    secondConnection.id = QStringLiteral("conn_2");
    secondConnection.source = ProjectConnectionEndpoint{QStringLiteral("node_1"), QStringLiteral("ep0")};
    secondConnection.target = ProjectConnectionEndpoint{QStringLiteral("node_3"), QStringLiteral("noc")};
    document.connections.push_back(secondConnection);

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

void testProjectReaderRejectsOversizedProjectFiles() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("oversized.fpproj"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create oversized project fixture");
    require(file.resize(17 * 1024 * 1024),
            "failed to resize oversized project fixture");
    file.close();

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "oversized project file should be rejected");
    require(result.error.contains(QStringLiteral("too large")),
            "oversized project error should explain the size limit");
    require(ProjectReader::detectKind(path) == ProjectFileKind::Unknown,
            "oversized project file should not be parsed during kind detection");
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
        writeGeneratedProjectSnapshot(nullptr,
                                      tempDir.path(),
                                      QStringLiteral("generated_design"),
                                      QVector<ProjectIpInstanceRecord>{state});
    require(result.success, "generation project snapshot should be written");
    require(QFileInfo::exists(result.path), "generation project snapshot should exist");
    require(QFileInfo(result.path).fileName() == QStringLiteral("generated_design.fpproj"),
            "generation project snapshot should use design name");

    const ProjectReadResult readResult = ProjectReader::readFile(result.path);
    require(readResult.success, "generation project snapshot should be readable");
    require(readResult.document.projectName == QStringLiteral("generated_design"),
            "generation project snapshot should use design name as project name");
    require(readResult.document.instances.size() == 1,
            "generation project snapshot should include ProjectDocument instances");
    require(readResult.document.instances.first().package.id == QStringLiteral("finepaper.ravenoc"),
            "generation project snapshot instance should use package id");
    require(readResult.document.instances.first()
                .config.value(QStringLiteral("parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 64,
            "generation project snapshot should preserve IP-core parameters");
}

void testGenerationProjectSnapshotPreservesInstanceGraphConfigWithoutGraph() {
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.test");
    state.instanceId = QStringLiteral("test_0");
    state.id = state.instanceId;
    state.package = ProjectPackageRef{state.ipcoreId, QStringLiteral("1.0")};
    state.schema = QStringLiteral("finepaper-test-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("width"), 32}
        }}
    };
    state.hasGraphConfig = true;
    state.graphConfig = QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("snapshot_node")},
                {QStringLiteral("type"), QStringLiteral("ProjectXp")}
            }
        }},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };

    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("snapshot_design_id");
    design.name = QStringLiteral("snapshot_design");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const GeneratedProjectSnapshotResult result =
        writeGeneratedProjectSnapshot(&design,
                                      tempDir.path(),
                                      QStringLiteral("snapshot_design"),
                                      QVector<ProjectIpInstanceRecord>{state});
    require(result.success, "generation project snapshot without graph should be written");

    const ProjectReadResult readResult = ProjectReader::readFile(result.path);
    require(readResult.success, "generation project snapshot without graph should be readable");
    require(readResult.document.projectId == QStringLiteral("snapshot_design_id"),
            "snapshot should preserve ProjectDesign project id");
    require(readResult.document.instances.size() == 1,
            "snapshot should include the generation instance");
    const ProjectIpInstanceRecord& instance = readResult.document.instances.first();
    require(instance.hasGraphConfig,
            "snapshot should preserve instance-owned graph_config");
    require(instance.graphConfig.value(QStringLiteral("objects")).toArray().size() == 1,
            "snapshot graph_config should include instance graph objects");
    require(instance.graphConfig.value(QStringLiteral("objects")).toArray().first().toObject()
                .value(QStringLiteral("id")).toString() == QStringLiteral("snapshot_node"),
            "snapshot graph_config should preserve graph object ids");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        configureDefaultPackageRootsForTest();

        testProjectWritesInterfaceConnectionsWithoutFromTo();
        testProjectRoundTripRestoresModulesParametersAndConnections();
        testProjectLoadUsesLoadedPackageManifestsForInterfaceRules();
        testProjectLoadRejectsSavedConnectionClassNotAcceptedByInterfaces();
        testProjectLoadRejectsInterfaceConnectionMissingSavedClass();
        testProjectWriterUsesCanonicalProjectDocumentVocabulary();
        testProjectGraphConfigLayoutBridgePreservesNativeData();
        testProjectStateServiceClearsStaleGraphConfigObjects();
        testProjectSerializerUsesModuleIpcoreOwnership();
        testProjectDesignSerializerRoundTripsDesignWithoutGraphProjection();
        testLoadRejectsModuleWithoutMatchingIpcoreState();
        testLoadRejectsDuplicateIpcoreStateScope();
        testLoadRejectsIpcoreStateMissingOwnerFields();
        testIpcoreStateWriteAddsIpcoreDependency();
        testProjectStateServiceUpdatesIpcoreStateWithoutGraph();
        testProjectStateServiceDoesNotCreateMissingSection();
        testProjectStateServiceDoesNotOverwriteNonObjectSection();
        testProjectStateServiceParameterReturnsUndefinedForMissingValues();
        testProjectStateServiceParameterPreservesExplicitNull();
        testReaderRejectsWrongKind();
        testLoadRejectsDuplicateModuleIds();
        testLoadRejectsMissingModuleType();
        testLoadDisambiguatesBareModuleTypeByIpcoreId();
        testLoadRejectsInvalidParameterType();
        testLoadRejectsInvalidConnectionReference();
        testLoadRejectsEmptyConnectionIdWithoutChangingGraph();
        testLoadRejectsDuplicateConnectionIdsWithoutChangingGraph();
        testProjectLoadRejectsConnectionRuleFailure();
        testProjectLoadReportsIpcoreConnectionRuleFailure();
        testProjectLoadRejectsCrossInstanceConnectionWithinSameIpcore();
        testLoadRejectsConnectionInvalidatedByEarlierConnectionWithoutChangingGraph();
        testProjectReaderRejectsOversizedProjectFiles();
        testGenerationHelpersShapeIpcoreStateForGeneratorBoundary();
        testGenerationWritesProjectSnapshot();
        testGenerationProjectSnapshotPreservesInstanceGraphConfigWithoutGraph();
    } catch (const std::exception& error) {
        std::cerr << "projectdocument_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectdocument_test passed\n";
    return 0;
}
