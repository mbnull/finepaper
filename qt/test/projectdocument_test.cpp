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
#include <QJsonDocument>
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

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.fpproj"));

    const ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("roundtrip"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, "project write should succeed");

    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, "project read should succeed");

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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testProjectRoundTripRestoresModulesParametersAndConnections();
    } catch (const std::exception& error) {
        std::cerr << "projectdocument_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectdocument_test passed\n";
    return 0;
}
