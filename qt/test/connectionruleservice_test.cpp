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
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("Out"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeConsumer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Consumer"));
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("In"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeRouter(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Router"));
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    return module;
}

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
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "allowed connection should pass through IP-core layer");
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
    require(result.layer == ConnectionRuleLayer::FeaturePlugin,
            "same-side topology should be rejected by feature layer");
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
    require(result.layer == ConnectionRuleLayer::FeaturePlugin,
            "cardinality should be rejected by feature layer");
    require(result.reasonCode == QStringLiteral("port_occupied"),
            "occupied rejection should report port_occupied");
}

void testVisualSideOrientsInOutPortToNodeCompletion() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");

    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.allowAutoComplete = true;
    request.allowAlternatives = true;
    request.start.moduleId = QStringLiteral("left");
    request.start.portId = QStringLiteral("west");
    request.start.visualSide = ConnectionVisualSide::Input;
    request.end.moduleId = QStringLiteral("right");
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;
    request.end.visualSide = ConnectionVisualSide::Output;

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(request);

    require(result.status == ConnectionCheckStatus::Allowed,
            "input-side inout drag to a node should resolve to one oriented option");
    require(result.options.size() == 1, "visual side should avoid symmetric duplicate options");
    require(result.options.first().source.moduleId == QStringLiteral("right") &&
                result.options.first().source.portId == QStringLiteral("east"),
            "body target output side should become the source through its east interface");
    require(result.options.first().target.moduleId == QStringLiteral("left") &&
                result.options.first().target.portId == QStringLiteral("west"),
            "input-side start port should become the target");
}

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
    endpoint.interfaceMetadata[QStringLiteral("noc")].matchFields.clear();
    endpoint.interfaceMetadata[QStringLiteral("noc")].acceptedValues.clear();
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
            result.reasonCode.toLocal8Bit().constData());
    require(result.options.size() == 1,
            "autocomplete group should suppress same-bus nonmatching hidden ports");
    require(result.options.first().target.portId == QStringLiteral("endpoint"),
            "node-body autocomplete should choose endpoint_attachment hidden port");
}

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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testAllowsSimplePortToPortConnection();
        testRejectsMissingPortWithReason();
        testStructuralLayerRunsBeforeSemanticLayers();
        testDuplicateConnectionIsStructuralRejection();
        testSelfLoopIsStructuralRejection();
        testRejectsSameSideTopologyRule();
        testRejectsOccupiedCardinalityOnePort();
        testVisualSideOrientsInOutPortToNodeCompletion();
        testNodeBodyAutocompleteUsesMatchingGroup();
        testRejectsCrossIpcoreConnectionAtIpcoreLayer();
        testRejectsInterfaceFieldMismatchAtIpcoreLayer();
    } catch (const std::exception& error) {
        std::cerr << "connectionruleservice_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "connectionruleservice_test passed\n";
    return 0;
}
