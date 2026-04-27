// Node editor geometry tests for interface-anchor scaling and router layout.
#include "graph/module.h"
#include "graph/port.h"
#include "modules/moduleregistry.h"
#include "nodeeditor/endpointattachmentlayout.h"
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/routerconnectionresolver.h"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QApplication>
#include <QPointF>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void registerScaledAnchorType() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleType type;
    type.name = QStringLiteral("GeometryScaledXP");
    type.editorLayout = QStringLiteral("mesh_router");
    type.supportsCollapse = true;
    type.expandedNodeMinWidth = 136;
    type.expandedNodeHeight = 116;
    type.collapsedNodeMinWidth = 104;
    type.collapsedNodeHeight = 92;
    type.expandedCaptionLeftInset = 30.0;
    type.expandedCaptionTopInset = 6.0;
    type.collapsedCaptionLeftInset = 30.0;
    type.collapsedCaptionTopInset = 26.0;

    type.interfaceAnchors.insert(QStringLiteral("north"),
        ModuleInterfaceAnchor{QStringLiteral("north"), 68.0, 0.0, 0.0, -1.0, QStringLiteral("North"), 68.0, 18.0});
    type.interfaceAnchors.insert(QStringLiteral("east"),
        ModuleInterfaceAnchor{QStringLiteral("east"), 136.0, 58.0, 1.0, 0.0, QStringLiteral("East"), 112.0, 58.0});
    type.interfaceAnchors.insert(QStringLiteral("south"),
        ModuleInterfaceAnchor{QStringLiteral("south"), 68.0, 116.0, 0.0, 1.0, QStringLiteral("South"), 68.0, 98.0});
    type.interfaceAnchors.insert(QStringLiteral("west"),
        ModuleInterfaceAnchor{QStringLiteral("west"), 0.0, 58.0, -1.0, 0.0, QStringLiteral("West"), 24.0, 58.0});
    type.interfaceAnchors.insert(QStringLiteral("local0"),
        ModuleInterfaceAnchor{QStringLiteral("local0"), 0.0, 26.0, -1.0, 0.0, QStringLiteral("Local 0"), 32.0, 26.0});

    ModuleRegistry::instance().registerType(type);
    registered = true;
}

void registerEndpointTypeWithLeftDefaultAnchor() {
    static bool registered = false;
    if (registered) {
        return;
    }

    ModuleType type;
    type.name = QStringLiteral("GeometryEndpoint");
    type.editorLayout = QStringLiteral("endpoint");
    type.graphGroup = QStringLiteral("endpoints");
    type.expandedNodeMinWidth = 104;
    type.expandedNodeHeight = 54;
    type.interfaceAnchors.insert(QStringLiteral("noc"),
        ModuleInterfaceAnchor{QStringLiteral("noc"), 0.0, 27.0, -1.0, 0.0, QStringLiteral("NoC"), 26.0, 40.0});

    ModuleRegistry::instance().registerType(type);
    registered = true;
}

std::unique_ptr<Module> makeRouter(bool collapsed, const QString& id = QStringLiteral("router")) {
    auto module = std::make_unique<Module>(id, QStringLiteral("GeometryScaledXP"));
    module->addPort(Port(QStringLiteral("north"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("North"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("north")));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("south"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("South"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("south")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    module->addPort(Port(QStringLiteral("local0"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("Local 0"), {}, QStringLiteral("attachment"),
                         QStringLiteral("ni_link"), QStringLiteral("local0")));
    module->setParameter(QStringLiteral("display_name"),
                         QStringLiteral("Very Long Router Caption For Geometry Scaling"));
    module->setParameter(QStringLiteral("collapsed"), collapsed);
    return module;
}

std::unique_ptr<Module> makeEndpoint(const QString& id = QStringLiteral("endpoint")) {
    auto module = std::make_unique<Module>(id, QStringLiteral("GeometryEndpoint"));
    module->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("ni_link"), QStringLiteral("noc")));
    module->setParameter(QStringLiteral("display_name"), QStringLiteral("Endpoint"));
    return module;
}

GraphNodeModel* addGraphNode(QtNodes::DataFlowGraphModel& graphModel, Module* module, QtNodes::NodeId& nodeId) {
    nodeId = graphModel.addNode(QStringLiteral("GraphNode"));
    auto* model = dynamic_cast<GraphNodeModel*>(graphModel.delegateModel<GraphNodeModel>(nodeId));
    require(model != nullptr, "graph node model should be created");
    model->setModule(module);
    return model;
}

void testExpandedMeshRouterUsesStatefulPortLayout() {
    registerScaledAnchorType();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* nodeModel = addGraphNode(graphModel, router.get(), nodeId);
    GraphNodeGeometry geometry(graphModel);

    const QSize nodeSize = geometry.size(nodeId);
    require(nodeSize.width() > 136, "long caption should expand router width beyond view baseline");

    const QPointF north = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("north"), QtNodes::PortType::In));
    const QPointF east = geometry.portPosition(
        nodeId,
        QtNodes::PortType::Out,
        nodeModel->portIndex(QStringLiteral("east"), QtNodes::PortType::Out));
    const QPointF south = geometry.portPosition(
        nodeId,
        QtNodes::PortType::Out,
        nodeModel->portIndex(QStringLiteral("south"), QtNodes::PortType::Out));
    const QPointF west = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("west"), QtNodes::PortType::In));
    const QPointF local0 = geometry.portPosition(
        nodeId,
        QtNodes::PortType::In,
        nodeModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In));

    require(local0.x() == 0.0, "expanded local endpoint interface should remain on the left edge");
    require(north.x() == nodeSize.width() &&
                east.x() == nodeSize.width() &&
                south.x() == nodeSize.width() &&
                west.x() == nodeSize.width(),
            "expanded mesh router interfaces should all be stacked on the right edge");
    require(north.y() < east.y() && east.y() < south.y() && south.y() < west.y(),
            "expanded mesh router interfaces should be vertically ordered on the right edge");
}

void testRouterConnectionResolverReversesAgainstNodePositions() {
    std::unique_ptr<Module> start = makeRouter(false, QStringLiteral("start"));
    start->setParameter(QStringLiteral("display_name"), QStringLiteral("Start"));
    std::unique_ptr<Module> target = makeRouter(false, QStringLiteral("target"));
    target->setParameter(QStringLiteral("display_name"), QStringLiteral("Target"));

    const std::optional<RouterConnectionResolver::ResolvedRouterConnection> resolved =
        RouterConnectionResolver::resolveByPosition(
            start.get(),
            target.get(),
            QStringLiteral("east"),
            QPointF(220.0, 0.0),
            QPointF(0.0, 0.0));

    require(resolved.has_value(), "router connection resolver should produce a link");
    require(resolved->source.moduleId == QStringLiteral("target") &&
                resolved->source.portId == QStringLiteral("east"),
            "router to the west should become the source through its east interface");
    require(resolved->target.moduleId == QStringLiteral("start") &&
                resolved->target.portId == QStringLiteral("west"),
            "start router should become the target through its west interface");
}

void testEndpointAttachmentLayoutUsesHostAnchorNormal() {
    const QPointF topLeft = EndpointAttachmentLayout::endpointTopLeft(
        QPointF(100.0, 50.0),
        QPointF(-1.0, 0.0),
        QPointF(104.0, 27.0),
        52.0);

    require(topLeft.x() == -56.0, "left-facing local anchor should place endpoint to the left");
    require(topLeft.y() == 23.0, "endpoint NoC anchor should align vertically with host local anchor");
}

void testEndpointInterfaceUsesHorizontalSidesOnly() {
    const QSize endpointSize(104, 54);

    const QPointF facingLeftHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(-1.0, 0.0));
    const QPointF facingRightHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(1.0, 0.0));
    const QPointF verticalHost =
        EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, QPointF(0.0, -1.0));

    require(facingLeftHost == QPointF(104.0, 27.0),
            "endpoint attached left of a host should expose its interface on the right side");
    require(facingRightHost == QPointF(0.0, 27.0),
            "endpoint attached right of a host should expose its interface on the left side");
    require(verticalHost == QPointF(104.0, 27.0),
            "endpoint interface should stay on a horizontal side instead of top or bottom");
}

void testEndpointInterfaceFlipsTowardHostAnchor() {
    registerScaledAnchorType();
    registerEndpointTypeWithLeftDefaultAnchor();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    std::unique_ptr<Module> endpoint = makeEndpoint();
    QtNodes::NodeId routerNodeId = QtNodes::InvalidNodeId;
    QtNodes::NodeId endpointNodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* routerModel = addGraphNode(graphModel, router.get(), routerNodeId);
    GraphNodeModel* endpointModel = addGraphNode(graphModel, endpoint.get(), endpointNodeId);

    const QtNodes::PortIndex endpointPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::Out);
    const QtNodes::PortIndex endpointInputPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::In);
    const QtNodes::PortIndex routerPortIndex =
        routerModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In);
    graphModel.addConnection(QtNodes::ConnectionId{
        endpointNodeId,
        endpointPortIndex,
        routerNodeId,
        routerPortIndex
    });

    GraphNodeGeometry geometry(graphModel);
    const QSize endpointSize = geometry.size(endpointNodeId);
    const QPointF endpointPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::Out,
        endpointPortIndex);

    require(endpointPort.x() == endpointSize.width(),
            "endpoint interface should flip to the side facing the host anchor");
    require(endpointPort.y() == endpointSize.height() / 2.0,
            "flipped endpoint interface should stay vertically centered for a horizontal host anchor");

    const QPointF endpointInputPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::In,
        endpointInputPortIndex);
    require(endpointInputPort == endpointPort,
            "inout endpoint interface should use one visual anchor for both Qt port directions");
}

void testEndpointInterfaceFollowsRelativeNodePosition() {
    registerScaledAnchorType();
    registerEndpointTypeWithLeftDefaultAnchor();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    std::unique_ptr<Module> endpoint = makeEndpoint();
    QtNodes::NodeId routerNodeId = QtNodes::InvalidNodeId;
    QtNodes::NodeId endpointNodeId = QtNodes::InvalidNodeId;
    GraphNodeModel* routerModel = addGraphNode(graphModel, router.get(), routerNodeId);
    GraphNodeModel* endpointModel = addGraphNode(graphModel, endpoint.get(), endpointNodeId);

    graphModel.setNodeData(routerNodeId, QtNodes::NodeRole::Position, QPointF(0.0, 0.0));
    graphModel.setNodeData(endpointNodeId, QtNodes::NodeRole::Position, QPointF(260.0, 0.0));

    const QtNodes::PortIndex endpointPortIndex =
        endpointModel->portIndex(QStringLiteral("noc"), QtNodes::PortType::Out);
    const QtNodes::PortIndex routerPortIndex =
        routerModel->portIndex(QStringLiteral("local0"), QtNodes::PortType::In);
    graphModel.addConnection(QtNodes::ConnectionId{
        endpointNodeId,
        endpointPortIndex,
        routerNodeId,
        routerPortIndex
    });

    GraphNodeGeometry geometry(graphModel);
    const QPointF endpointPort = geometry.portPosition(
        endpointNodeId,
        QtNodes::PortType::Out,
        endpointPortIndex);

    require(endpointPort.x() == 0.0,
            "endpoint interface should move to the left side when the host is left of the endpoint");
}

void testStoredNodeSizeOverridesDefaultAndProvidesResizeHandle() {
    registerScaledAnchorType();

    auto registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    registry->registerModel<GraphNodeModel>(QStringLiteral("GraphNode"));
    QtNodes::DataFlowGraphModel graphModel(registry);

    std::unique_ptr<Module> router = makeRouter(false);
    router->setParameter(QStringLiteral("display_name"), QStringLiteral("Router"));
    router->setParameter(QStringLiteral("node_width"), 220);
    router->setParameter(QStringLiteral("node_height"), 164);
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    addGraphNode(graphModel, router.get(), nodeId);

    GraphNodeGeometry geometry(graphModel);
    const QSize nodeSize = geometry.size(nodeId);
    const QRect resizeHandle = geometry.resizeHandleRect(nodeId);

    require(nodeSize.width() == 220 && nodeSize.height() == 164,
            "stored node_width/node_height should override default node geometry");
    require(!resizeHandle.isEmpty(), "resizable nodes should expose a drag handle");
    require(resizeHandle.right() <= nodeSize.width() && resizeHandle.bottom() <= nodeSize.height(),
            "resize handle should sit inside the bottom-right node bounds");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    try {
        testExpandedMeshRouterUsesStatefulPortLayout();
        testRouterConnectionResolverReversesAgainstNodePositions();
        testEndpointAttachmentLayoutUsesHostAnchorNormal();
        testEndpointInterfaceUsesHorizontalSidesOnly();
        testEndpointInterfaceFlipsTowardHostAnchor();
        testEndpointInterfaceFollowsRelativeNodePosition();
        testStoredNodeSizeOverridesDefaultAndProvidesResizeHandle();
    } catch (const std::exception& error) {
        std::cerr << "nodeeditor_geometry_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "nodeeditor_geometry_test passed\n";
    return 0;
}
