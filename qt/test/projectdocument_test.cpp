// Project document tests for .fpproj save/load behavior.
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/parameter.h"
#include "graph/port.h"
#include "modules/moduleregistry.h"
#include "project/graphprojectserializer.h"
#include "project/projectreader.h"
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
    graph.configureIpInstance(
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QHash<QString, Parameter>{
            {QStringLiteral("flit_data_width"),
             Parameter(QStringLiteral("flit_data_width"), 64)}
        });
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

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.fpproj"));

    const ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("roundtrip"));
    require(document.ipInstances.size() == 1, "project should capture IP instance");
    require(document.ipInstances.first().parameters.value(QStringLiteral("flit_data_width")).toInt() == 64,
            "project should capture IP instance parameter");
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, "project write should succeed");

    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, "project read should succeed");
    require(readResult.document.ipInstances.size() == 1,
            "project read should restore IP instance records");

    Graph restored;
    const GraphProjectLoadResult loadResult =
        GraphProjectSerializer::loadProject(readResult.document, restored);
    require(loadResult.success, "project graph restore should succeed");

    require(restored.modules().size() == 2, "project should restore both modules");
    require(restored.connections().size() == 1, "project should restore explicit connection");
    require(restored.ipInstance().has_value(), "project should restore graph IP instance");
    require(restored.ipInstance()->parameters.value(QStringLiteral("flit_data_width")).value() ==
                Parameter::Value(64),
            "project should restore graph IP instance parameter");
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

void testLoadRejectsSecondNocIpInstance() {
    ProjectDocument document = validProjectDocument();
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("noc_a"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        {}
    });
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("noc_b"),
        QStringLiteral("finepaper.othernoc"),
        QStringLiteral("noc"),
        QStringLiteral("OtherNoC"),
        {}
    });

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "second noc IP should be rejected");
    require(result.error.contains(QStringLiteral("at most one IP instance")),
            "error should mention single IP instance limit");
}

void testLoadRejectsMultipleIpInstances() {
    ProjectDocument document = validProjectDocument();
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("noc_a"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QJsonObject{{QStringLiteral("flit_data_width"), 64}}
    });
    document.ipInstances.push_back(ProjectIpInstanceRecord{
        QStringLiteral("monitor_a"),
        QStringLiteral("finepaper.monitor"),
        QStringLiteral("monitor"),
        QStringLiteral("TraceMonitor"),
        QJsonObject{{QStringLiteral("sample_depth"), 1024}}
    });

    Graph graph;
    const GraphProjectLoadResult result = GraphProjectSerializer::loadProject(document, graph);
    require(!result.success, "multiple IP instances should be rejected until plugin state owns them");
    require(result.error.contains(QStringLiteral("at most one IP instance")),
            "error should mention single IP instance limit");
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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testProjectRoundTripRestoresModulesParametersAndConnections();
        testReaderRejectsWrongKind();
        testLoadRejectsDuplicateModuleIds();
        testLoadRejectsSecondNocIpInstance();
        testLoadRejectsMultipleIpInstances();
        testLoadRejectsMissingModuleType();
        testLoadRejectsInvalidParameterType();
        testLoadRejectsInvalidConnectionReference();
        testLoadRejectsConnectionInvalidatedByEarlierConnectionWithoutChangingGraph();
        testReaderRejectsMalformedProjectGraphArrays();
        testReaderDetectsProjectAndLegacyJsonFiles();
    } catch (const std::exception& error) {
        std::cerr << "projectdocument_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectdocument_test passed\n";
    return 0;
}
