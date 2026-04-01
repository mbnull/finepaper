#include "graph.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QString>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   std::initializer_list<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    for (const auto& port : ports) {
        module->addPort(port);
    }
    return module;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testConnectionValidationPreventsPortReuse() {
    Graph graph;

    require(graph.addModule(makeModule(
        "source",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add source module");
    require(graph.addModule(makeModule(
        "target_a",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add first target module");
    require(graph.addModule(makeModule(
        "target_b",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add second target module");

    const PortRef source{"source", "out"};
    const PortRef targetA{"target_a", "in"};
    const PortRef targetB{"target_b", "in"};

    require(graph.isValidConnection(source, targetA), "expected first connection to be valid");

    graph.addConnection(std::make_unique<Connection>("c1", source, targetA));

    require(graph.connections().size() == 1, "expected connection to be stored");
    require(!graph.isValidConnection(source, targetB), "source port should not be reusable");
    require(!graph.isValidConnection(source, targetA), "duplicate connection should be rejected");
}

void testRemovingModuleAlsoRemovesAttachedConnections() {
    Graph graph;

    require(graph.addModule(makeModule(
        "producer",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add producer module");
    require(graph.addModule(makeModule(
        "consumer",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add consumer module");

    graph.addConnection(std::make_unique<Connection>(
        "producer_consumer",
        PortRef{"producer", "out"},
        PortRef{"consumer", "in"}));

    require(graph.connections().size() == 1, "expected connection before module removal");

    graph.removeModule("producer");

    require(graph.getModule("producer") == nullptr, "removed module should no longer exist");
    require(graph.connections().empty(), "attached connections should be removed with the module");
}

void testGraphForwardsModuleParameterChanges() {
    Graph graph;
    QString changedModuleId;
    QString changedParameterName;
    int signalCount = 0;

    QObject::connect(&graph, &Graph::parameterChanged, &graph,
        [&](const QString& moduleId, const QString& paramName) {
            ++signalCount;
            changedModuleId = moduleId;
            changedParameterName = paramName;
        });

    auto module = makeModule(
        "configurable",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")});
    Module* modulePtr = module.get();

    require(graph.addModule(std::move(module)), "failed to add configurable module");

    modulePtr->setParameter("buffer_depth", 8);

    require(signalCount == 1, "expected exactly one forwarded parameterChanged signal");
    require(changedModuleId == "configurable", "forwarded signal should include module id");
    require(changedParameterName == "buffer_depth", "forwarded signal should include parameter name");
}

void testFrameworkExportOmitsEditorOnlyCollapsedField() {
    Graph graph;

    auto xp = makeModule(
        "xp_internal",
        "XP",
        {Port("ep0", Port::Direction::Output, "endpoint", "EP0")});
    xp->setParameter("external_id", QString("xp_0_0"));
    xp->setParameter("x", 0);
    xp->setParameter("y", 0);
    xp->setParameter("collapsed", true);
    xp->setParameter("routing_algorithm", QString("xy"));

    auto endpoint = makeModule(
        "ep_internal",
        "Endpoint",
        {Port("noc", Port::Direction::Input, "endpoint", "NoC")});
    endpoint->setParameter("external_id", QString("ep_0"));
    endpoint->setParameter("type", QString("master"));
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    require(graph.addModule(std::move(xp)), "failed to add XP module");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module");

    graph.addConnection(std::make_unique<Connection>(
        "xp_ep",
        PortRef{"xp_internal", "ep0"},
        PortRef{"ep_internal", "noc"}));

    require(graph.connections().size() == 1, "expected endpoint connection to be stored");

    const QJsonObject frameworkRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Framework).object();
    const QJsonObject editorRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Editor).object();

    const QJsonObject frameworkConfig =
        frameworkRoot["xps"].toArray().first().toObject()["config"].toObject();
    const QJsonObject editorConfig =
        editorRoot["xps"].toArray().first().toObject()["config"].toObject();

    require(!frameworkConfig.contains("collapsed"),
            "framework export should omit editor-only collapsed state");
    require(editorConfig.contains("collapsed"),
            "editor export should preserve collapsed state");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testConnectionValidationPreventsPortReuse();
        testRemovingModuleAlsoRemovesAttachedConnections();
        testGraphForwardsModuleParameterChanges();
        testFrameworkExportOmitsEditorOnlyCollapsedField();
    } catch (const std::exception& error) {
        std::cerr << "graph_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "graph_test passed\n";
    return 0;
}
