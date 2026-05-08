// ConnectionRuleService tests for v1 editor-time connection decisions.
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"

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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testAllowsSimplePortToPortConnection();
        testRejectsMissingPortWithReason();
        testRejectsSameSideTopologyRule();
        testRejectsOccupiedCardinalityOnePort();
    } catch (const std::exception& error) {
        std::cerr << "connectionruleservice_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "connectionruleservice_test passed\n";
    return 0;
}
