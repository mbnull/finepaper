// Node editor geometry tests for interface-anchor scaling and router layout.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "nodeeditor/endpointattachmentlayout.h"
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPointF>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

constexpr auto ScopedModuleMime = "application/x-finepaper-module";

PluginDescriptor nodeEditorRavenocDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("noc");
    return descriptor;
}

PluginDescriptor nodeEditorFabricDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.fabric");
    descriptor.name = QStringLiteral("Fabric");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("fabric");
    return descriptor;
}

ModuleType scopedEditorType(const QString& name, const QString& ipcoreId) {
    ModuleType type;
    type.name = name;
    type.ipcoreId = ipcoreId;
    type.paletteLabel = name;
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultPorts.push_back(Port(QStringLiteral("out"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Out")));
    return type;
}

struct ScopedNodeEditorHarness {
    Graph graph;
    CommandManager commandManager;
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    PluginDescriptor ravenoc = nodeEditorRavenocDescriptor();
    PluginDescriptor fabric = nodeEditorFabricDescriptor();
    IpCatalogService catalog;
    ProjectStateService stateService;
    ProjectIpService projectIpService;
    ActiveWorkspaceController workspaceController;
    NodeEditorWidget editor;

    ScopedNodeEditorHarness()
        : catalog(QList<PluginDescriptor>{ravenoc, fabric}, &registry),
          projectIpService(&stateService),
          workspaceController(&projectIpService, &catalog),
          editor(&graph, &stateService, &workspaceController, &commandManager) {
        require(registry.registerType(scopedEditorType(QStringLiteral("RaveTile"),
                                                       QStringLiteral("finepaper.ravenoc"))),
                "RaveTile test type should register");
        require(registry.registerType(scopedEditorType(QStringLiteral("FabricSwitch"),
                                                       QStringLiteral("finepaper.fabric"))),
                "FabricSwitch test type should register");
        catalog = IpCatalogService(QList<PluginDescriptor>{ravenoc, fabric}, &registry);
        editor.resize(320, 240);
        editor.show();
        QCoreApplication::processEvents();
    }

    IpCatalogEntry ravenocEntry() const {
        const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("finepaper.ravenoc"));
        require(entry.has_value(), "RaveNoC entry should exist");
        return *entry;
    }

    void selectRavenoc() {
        const ProjectIpServiceResult result = projectIpService.ensureInstanceForIpcore(ravenocEntry());
        require(result.success, "RaveNoC instance should be selected");
        QCoreApplication::processEvents();
    }
};

std::unique_ptr<QMimeData> scopedModuleMime(const QString& ipcoreId,
                                            const QString& instanceId,
                                            const QString& moduleType) {
    QJsonObject object;
    object.insert(QStringLiteral("ipcore"), ipcoreId);
    object.insert(QStringLiteral("instance"), instanceId);
    object.insert(QStringLiteral("type"), moduleType);
    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setData(ScopedModuleMime,
                      QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mimeData;
}

bool sendScopedDrop(NodeEditorWidget& editor, QMimeData* mimeData) {
    QDragEnterEvent enter(QPoint(16, 16),
                          Qt::CopyAction,
                          mimeData,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &enter);

    QDropEvent drop(QPointF(48, 64),
                    Qt::CopyAction,
                    mimeData,
                    Qt::LeftButton,
                    Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &drop);
    QCoreApplication::processEvents();
    return drop.isAccepted();
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

void testNodeEditorWidgetOwnsConnectionRuleServiceInputs() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    IpCatalogService catalog({}, &ModuleRegistry::instance());
    ActiveWorkspaceController workspaceController(&projectIpService, &catalog);
    CommandManager commandManager;
    NodeEditorWidget widget(&graph, &stateService, &workspaceController, &commandManager);

    require(!widget.isArrangeEnabled(),
            "widget should construct with project state service dependency");
}

void testScopedDropRejectsMissingActiveInstance() {
    ScopedNodeEditorHarness harness;
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop without selected active IP instance should be rejected");
    require(harness.graph.modules().empty(),
            "drop without selected active IP instance should not create a module");
}

void testScopedDropRejectsDifferentIpcore() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.fabric"),
                                     QStringLiteral("fabric_0"),
                                     QStringLiteral("FabricSwitch"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop for a different IP core should be rejected");
    require(harness.graph.modules().empty(),
            "drop for a different IP core should not create a module");
}

void testScopedDropRejectsLegacyModuleTypeMime() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = std::make_unique<QMimeData>();
    const QString legacyMime = QStringLiteral("application/x-") + QStringLiteral("moduletype");
    mimeData->setData(legacyMime, QByteArray("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "legacy module MIME should be rejected");
    require(harness.graph.modules().empty(),
            "legacy module MIME should not create a module");
}

void testScopedDropCreatesOwnedModule() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(accepted, "matching scoped module drop should be accepted");
    require(harness.graph.modules().size() == 1, "matching scoped drop should create one module");
    const Module* module = harness.graph.modules().front().get();
    require(module->type() == QStringLiteral("RaveTile"),
            "created module should use payload module type");
    require(module->ipcoreId() == QStringLiteral("finepaper.ravenoc"),
            "created module should keep active IP-core ownership");
    require(harness.commandManager.canUndo(),
            "scoped module creation should enter command history");
}

void testCreateMenuTypesFollowActiveWorkspace() {
    ScopedNodeEditorHarness harness;
    require(harness.editor.availableCreateModuleTypes().isEmpty(),
            "create menu should be empty without active workspace");

    harness.selectRavenoc();

    const QStringList moduleTypes = harness.editor.availableCreateModuleTypes();
    require(moduleTypes.size() == 1, "create menu should list active workspace modules only");
    require(moduleTypes.first() == QStringLiteral("RaveTile"),
            "create menu should list RaveNoC module type");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testExpandedMeshRouterUsesStatefulPortLayout();
        testEndpointAttachmentLayoutUsesHostAnchorNormal();
        testEndpointInterfaceUsesHorizontalSidesOnly();
        testEndpointInterfaceFlipsTowardHostAnchor();
        testEndpointInterfaceFollowsRelativeNodePosition();
        testStoredNodeSizeOverridesDefaultAndProvidesResizeHandle();
        testNodeEditorWidgetOwnsConnectionRuleServiceInputs();
        testScopedDropRejectsMissingActiveInstance();
        testScopedDropRejectsDifferentIpcore();
        testScopedDropRejectsLegacyModuleTypeMime();
        testScopedDropCreatesOwnedModule();
        testCreateMenuTypesFollowActiveWorkspace();
    } catch (const std::exception& error) {
        std::cerr << "nodeeditor_geometry_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "nodeeditor_geometry_test passed\n";
    return 0;
}
