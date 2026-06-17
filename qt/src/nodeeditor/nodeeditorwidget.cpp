// NodeEditorWidget — Qt widget that bridges the Graph data model and the
// QtNodes visual canvas. Listens to Graph signals to keep the canvas in sync,
// and keeps QtNodes interactions projected from the design-owned graph.
// m_updatingFromGraph guards against re-entrant signal loops when the widget
// itself drives a graph change.
#include "nodeeditor/nodeeditorwidget.h"
#include "nodeeditor/animatedgraphicsview.h"
#include "nodeeditor/editorgraphmodel.h"
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/graphnodepainter.h"
#include "modules/modulelabels.h"
#include "modules/moduletypemetadata.h"
#include "modules/moduleregistry.h"
#include "nodeeditor/nodeeditorentityfactory.h"
#include "nodeeditor/endpointattachmentlayout.h"
#include "nodeeditor/portanchorgeometry.h"
#include "common/portlayout.h"
#include "nodeeditor/straightconnectionpainter.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/locateNode.hpp>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QMenu>
#include <QSet>
#include <QGraphicsItem>
#include <algorithm>
#include <cmath>
#include <optional>

static std::optional<double> toDouble(const Parameter::Value& v) {
    if (auto* i = std::get_if<int>(&v)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&v)) return *d;
    return std::nullopt;
}

namespace {

class GraphUpdateGuard {
public:
    // Counts nested model-to-scene updates so QtNodes callbacks can distinguish
    // graph synchronization from direct user edits.
    explicit GraphUpdateGuard(int& counter) : m_counter(counter) { ++m_counter; }
    ~GraphUpdateGuard() { --m_counter; }

private:
    int& m_counter;
};

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) return nullptr;

    for (const auto& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

bool isHostAttachmentModule(const Module* module) {
    const QString graphRole = ModuleTypeMetadata::graphRole(module);
    return graphRole == QStringLiteral("host") ||
           graphRole == QStringLiteral("router");
}

bool isAttachedModule(const Module* module) {
    const QString graphRole = ModuleTypeMetadata::graphRole(module);
    return graphRole == QStringLiteral("attached") ||
           graphRole == QStringLiteral("attachment");
}

QString interfaceIdForPort(const Port& port) {
    return port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
}

bool isAttachmentPort(const Module* module, const QString& portId) {
    const Port* port = findPort(module, portId);
    if (!port) {
        return false;
    }

    const QString interfaceId = interfaceIdForPort(*port);
    const ModuleInterfaceMetadata* metadata =
        ModuleTypeMetadata::interfaceMetadata(module, interfaceId);
    if (metadata && metadata->autocompleteGroup == QStringLiteral("endpoint_attachment")) {
        return true;
    }

    return ModuleTypeMetadata::attachmentZone(module, interfaceId) != nullptr;
}

bool boolParameter(const Module* module, const QString& name, bool fallbackValue) {
    if (!module) return fallbackValue;

    const auto it = module->parameters().find(name);
    if (it == module->parameters().end()) return fallbackValue;

    const Parameter::Value value = it.value().value();
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }

    return fallbackValue;
}

bool isCollapsed(const Module* module) {
    return module && ModuleTypeMetadata::supportsCollapse(module) && boolParameter(module, "collapsed", true);
}

bool isEndpointAttachmentConnection(const Graph* graph,
                                    const Connection& connection,
                                    QString* hostModuleId = nullptr,
                                    QString* endpointModuleId = nullptr) {
    if (!graph) return false;

    const Module* sourceModule = graph->getModule(connection.source().moduleId);
    const Module* targetModule = graph->getModule(connection.target().moduleId);
    if (!sourceModule || !targetModule) return false;

    // Endpoint attachment presentation normalizes either stored direction to
    // host and attached-node IDs using manifest roles and attachment metadata.
    if (isAttachedModule(sourceModule) &&
        isHostAttachmentModule(targetModule) &&
        isAttachmentPort(sourceModule, connection.source().portId) &&
        isAttachmentPort(targetModule, connection.target().portId)) {
        if (hostModuleId) *hostModuleId = targetModule->id();
        if (endpointModuleId) *endpointModuleId = sourceModule->id();
        return true;
    }

    if (!isHostAttachmentModule(sourceModule) ||
        !isAttachedModule(targetModule) ||
        !isAttachmentPort(sourceModule, connection.source().portId) ||
        !isAttachmentPort(targetModule, connection.target().portId)) {
        return false;
    }

    if (hostModuleId) *hostModuleId = sourceModule->id();
    if (endpointModuleId) *endpointModuleId = targetModule->id();
    return true;
}

QPointF normalForSide(const QString& side) {
    if (side == QStringLiteral("north")) return QPointF(0.0, -1.0);
    if (side == QStringLiteral("east")) return QPointF(1.0, 0.0);
    if (side == QStringLiteral("south")) return QPointF(0.0, 1.0);
    if (side == QStringLiteral("west")) return QPointF(-1.0, 0.0);
    return {};
}

QPointF portNormal(const Module* module,
                   const Port* port,
                   const QPointF& portPosition,
                   QSize const& nodeSize) {
    if (!module || !port) {
        return {};
    }

    const QPointF edgeNormal = PortAnchorGeometry::normalFromEdge(portPosition, nodeSize);
    if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(module, *port);
        anchor && anchor->normalX.has_value() && anchor->normalY.has_value()) {
        return QPointF(*anchor->normalX, *anchor->normalY);
    }

    if ((ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router") ||
         ModuleTypeMetadata::hasEditorLayout(module, u"endpoint")) &&
        !edgeNormal.isNull()) {
        return edgeNormal;
    }

    if (!edgeNormal.isNull()) {
        return edgeNormal;
    }

    if (PortLayout::isRouterPort(*port)) {
        return normalForSide(PortLayout::routerSideId(port->id()));
    }
    return normalForSide(PortLayout::fallbackSide(*port));
}

QPointF scaledViewPoint(const Module* module,
                        const QSize& nodeSize,
                        double x,
                        double y) {
    const double baselineWidth = static_cast<double>(ModuleTypeMetadata::expandedNodeMinWidth(module));
    const double baselineHeight = static_cast<double>(ModuleTypeMetadata::expandedNodeHeight(module));
    const double xScale = baselineWidth > 0.0 ? static_cast<double>(nodeSize.width()) / baselineWidth : 1.0;
    const double yScale = baselineHeight > 0.0 ? static_cast<double>(nodeSize.height()) / baselineHeight : 1.0;
    return QPointF(x * xScale, y * yScale);
}

struct HostAttachmentAnchor {
    QPointF position;
    QPointF normal;
    const ModuleAttachmentZone* zone = nullptr;
};

bool attachedModuleMatchesHost(const ModuleType* attachedType,
                               const ModuleType* hostType) {
    if (!attachedType || !hostType) {
        return false;
    }

    const QStringList hostModuleIds = ModuleTypeMetadata::attachHostModuleIds(attachedType);
    if (hostModuleIds.isEmpty()) {
        return true;
    }

    const QString hostModuleId = ModuleTypeMetadata::moduleId(hostType);
    const QString hostPackageId = ModuleTypeMetadata::packageId(hostType);
    const QString scopedHostTypeName = ModuleRegistry::scopedTypeName(hostPackageId, hostModuleId);
    return hostModuleIds.contains(hostModuleId) ||
           hostModuleIds.contains(hostType->name) ||
           hostModuleIds.contains(scopedHostTypeName);
}

const ModuleAttachmentZone* manifestAttachmentZoneForAttachedModule(const Module* hostModule,
                                                                    const Module* attachedModule) {
    const ModuleType* attachedType = ModuleTypeMetadata::type(attachedModule);
    const ModuleType* hostType = ModuleTypeMetadata::type(hostModule);
    const QString zoneId = ModuleTypeMetadata::attachZoneId(attachedType);
    if (zoneId.isEmpty() || !attachedModuleMatchesHost(attachedType, hostType)) {
        return nullptr;
    }
    return ModuleTypeMetadata::attachmentZone(hostModule, zoneId);
}

std::optional<HostAttachmentAnchor> hostAttachmentAnchor(const Module* module,
                                                         const Module* attachedModule,
                                                         const Port* port,
                                                         const QPointF& portPosition,
                                                         const QSize& nodeSize) {
    if (!module || !port) {
        return std::nullopt;
    }

    HostAttachmentAnchor anchor;
    const ModuleAttachmentZone* zone =
        manifestAttachmentZoneForAttachedModule(module, attachedModule);
    if (!zone) {
        zone = ModuleTypeMetadata::attachmentZone(module, *port);
    }
    if (zone) {
        anchor.position = scaledViewPoint(module, nodeSize, zone->x, zone->y);
        if (zone->normalX.has_value() && zone->normalY.has_value()) {
            anchor.normal = QPointF(*zone->normalX, *zone->normalY);
        }
        anchor.zone = zone;
    } else {
        anchor.position = portPosition;
    }

    if (anchor.normal.isNull()) {
        anchor.normal = portNormal(module, port, anchor.position, nodeSize);
    }
    if (anchor.normal.isNull()) {
        return std::nullopt;
    }

    return anchor;
}

QPointF attachmentPortAnchor(const Module* module,
                             const Port* port,
                             const QSize& nodeSize,
                             const QPointF& fallbackNormal) {
    if (module && port) {
        if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(module, *port)) {
            return scaledViewPoint(module, nodeSize, anchor->x, anchor->y);
        }
    }

    return EndpointAttachmentLayout::endpointAnchorForHostNormal(nodeSize, fallbackNormal);
}

constexpr qreal kCanvasHalfExtent = 2000.0;
const QRectF kCanvasRect(-kCanvasHalfExtent, -kCanvasHalfExtent,
                         kCanvasHalfExtent * 2.0, kCanvasHalfExtent * 2.0);
constexpr int kConnectedHighlightDataRole = 1;

struct DraftConnectionStart {
    bool startFromOutput = false;
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    QtNodes::PortIndex portIndex = QtNodes::InvalidPortIndex;
    QtNodes::PortType portType = QtNodes::PortType::None;
};

std::optional<DraftConnectionStart> resolveDraftConnectionStart(const QtNodes::ConnectionGraphicsObject& draftConnection) {
    const QtNodes::PortType requiredPort = draftConnection.connectionState().requiredPort();
    if (requiredPort == QtNodes::PortType::None) {
        return std::nullopt;
    }

    const QtNodes::ConnectionId connectionId = draftConnection.connectionId();
    DraftConnectionStart start;
    // QtNodes stores the missing side as "requiredPort"; invert that to recover
    // the already anchored side of the drag operation.
    start.startFromOutput = requiredPort == QtNodes::PortType::In;
    start.nodeId = start.startFromOutput ? connectionId.outNodeId : connectionId.inNodeId;
    start.portIndex = start.startFromOutput ? connectionId.outPortIndex : connectionId.inPortIndex;
    start.portType = start.startFromOutput ? QtNodes::PortType::Out : QtNodes::PortType::In;
    if (start.nodeId == QtNodes::InvalidNodeId || start.portIndex == QtNodes::InvalidPortIndex) {
        return std::nullopt;
    }

    return start;
}

QVector<ConnectionInterfaceRef> graphInterfaceRefs(
    const QVector<ProjectConnectionInterfaceRef>& projectInterfaces) {
    QVector<ConnectionInterfaceRef> interfaces;
    interfaces.reserve(projectInterfaces.size());
    for (const ProjectConnectionInterfaceRef& interfaceRef : projectInterfaces) {
        interfaces.push_back(ConnectionInterfaceRef{interfaceRef.instanceId, interfaceRef.interfaceId});
    }
    return interfaces;
}

bool samePortPair(const ConnectionResolvedOption& option,
                  const PortRef& source,
                  const PortRef& target) {
    return option.source.moduleId == source.moduleId &&
           option.source.portId == source.portId &&
           option.target.moduleId == target.moduleId &&
           option.target.portId == target.portId;
}

QVector<IpcraftPackageManifest> activePackageManifests(
    const ActiveWorkspaceController* workspaceController) {
    if (!workspaceController) {
        return {};
    }

    const std::optional<ActiveWorkspaceContext> context = workspaceController->activeContext();
    if (!context.has_value() || context->entry.packageManifest.id.isEmpty()) {
        return {};
    }

    return {context->entry.packageManifest};
}

} // namespace

NodeEditorWidget::NodeEditorWidget(Graph* graph,
                                   ProjectStateService* projectStateService,
                                   ActiveWorkspaceController* workspaceController,
                                   CommandManager* commandManager,
                                   QWidget* parent)
    : QWidget(parent),
      m_graph(graph),
      m_projectStateService(projectStateService),
      m_workspaceController(workspaceController),
      m_commandManager(commandManager),
      m_canvasRect(kCanvasRect) {
    refreshConnectionRuleService();

    m_registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    m_registry->registerModel<GraphNodeModel>("GraphNode");

    m_graphModel = std::make_unique<EditorGraphModel>(m_registry);
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);
    m_scene->setSceneRect(m_canvasRect);
    // Geometry/painter are graph-aware customizations; the QtNodes scene still
    // owns low-level interaction and connection graphics.
    m_scene->setNodeGeometry(std::make_unique<GraphNodeGeometry>(*m_graphModel));
    m_scene->setNodePainter(std::make_unique<GraphNodePainter>());
    m_scene->setConnectionPainter(std::make_unique<StraightConnectionPainter>());
    m_view = new AnimatedGraphicsView(m_scene);
    m_view->setSceneRect(m_canvasRect);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);
    m_view->viewport()->installEventFilter(this);

    // Graph is a projection. Scene callbacks can validate intent, but durable
    // edits must wait for design-level patch commands.
    connect(m_graph, &Graph::moduleAdded, this, &NodeEditorWidget::onModuleAdded);
    connect(m_graph, &Graph::moduleRemoved, this, &NodeEditorWidget::onModuleRemoved);
    connect(m_graph, &Graph::connectionAdded, this, &NodeEditorWidget::onConnectionAdded);
    connect(m_graph, &Graph::connectionRemoved, this, &NodeEditorWidget::onConnectionRemoved);
    if (m_workspaceController) {
        connect(m_workspaceController,
                &ActiveWorkspaceController::activeWorkspaceChanged,
                this,
                [this]() { refreshVisibleGraphState(); });
    }

    connect(m_graphModel.get(), &QtNodes::DataFlowGraphModel::connectionCreated, this, &NodeEditorWidget::onConnectionCreated);
    connect(m_graphModel.get(), &QtNodes::DataFlowGraphModel::connectionDeleted, this, &NodeEditorWidget::onConnectionDeleted);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &NodeEditorWidget::onSelectionChanged);
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeMoved, this, &NodeEditorWidget::onNodeMoved);

    // Support constructing the widget around a pre-populated Graph, as happens
    // in tests and after model setup before the view is shown.
    for (const auto& module : m_graph->modules()) {
        onModuleAdded(module.get());
    }
    for (const auto& connection : m_graph->connections()) {
        onConnectionAdded(connection.get());
    }

    refreshAllModulePresentations();
}

NodeEditorWidget::~NodeEditorWidget() {
    if (m_scene) {
        disconnect(m_scene, &QGraphicsScene::selectionChanged, this, &NodeEditorWidget::onSelectionChanged);
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->removeEventFilter(this);
    }
    delete m_view;
    m_view = nullptr;
    delete m_scene;
    m_scene = nullptr;
    m_graphModel.reset();
}

bool NodeEditorWidget::isArrangeEnabled() const {
    return m_graphModel->isEditingLocked();
}

QString NodeEditorWidget::activeIpcoreId() const {
    if (!m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return {};
    }
    return m_workspaceController->state().ipcoreId;
}

QString NodeEditorWidget::activeInstanceId() const {
    if (!m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return {};
    }
    return m_workspaceController->state().instanceId;
}

QStringList NodeEditorWidget::availableCreateModuleTypes() const {
    if (!m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return {};
    }
    return m_workspaceController->state().moduleTypes;
}

QStringList NodeEditorWidget::visibleModuleIds() const {
    return m_moduleToNodeId.keys();
}

bool NodeEditorWidget::moduleBelongsToActiveWorkspace(const Module* module) const {
    if (!module || !m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return false;
    }

    const ActiveWorkspaceState& state = m_workspaceController->state();
    return module->ipcoreId() == state.ipcoreId &&
           module->instanceId() == state.instanceId;
}

bool NodeEditorWidget::connectionBelongsToActiveWorkspace(const Connection* connection) const {
    if (!connection) {
        return false;
    }

    return moduleBelongsToActiveWorkspace(m_graph->getModule(connection->source().moduleId)) &&
           moduleBelongsToActiveWorkspace(m_graph->getModule(connection->target().moduleId));
}

bool NodeEditorWidget::acceptsScopedModulePayload(const ScopedModulePayload& payload) const {
    if (!m_workspaceController) {
        return false;
    }

    const ActiveWorkspaceState& state = m_workspaceController->state();
    if (!state.hasActiveIp ||
        payload.ipcoreId != state.ipcoreId ||
        payload.instanceId != state.instanceId ||
        !state.moduleTypes.contains(payload.moduleType)) {
        return false;
    }

    const ModuleType* type = ModuleRegistry::instance().getType(payload.moduleType);
    return type && type->ipcoreId == state.ipcoreId;
}

void NodeEditorWidget::setArrangeEnabled(bool enabled) {
    m_graphModel->setEditingLocked(enabled);
    m_view->setEditingLocked(enabled);

    if (QAction* deleteAction = m_view->deleteSelectionAction()) {
        deleteAction->setEnabled(!enabled);
    }

    if (enabled) {
        m_view->zoomFitAll();
    }
}

void NodeEditorWidget::onModuleAdded(Module* module) {
    connect(module, &Module::parameterChanged, this, &NodeEditorWidget::onParameterChanged, Qt::UniqueConnection);
    ensureModuleInView(module);
}

void NodeEditorWidget::ensureModuleInView(Module* module) {
    if (!module || !moduleBelongsToActiveWorkspace(module) || m_moduleToNodeId.contains(module->id())) {
        return;
    }

    // Guard scene writes triggered by graph signals to avoid feedback loops
    // into onConnectionCreated/onConnectionDeleted handlers.
    GraphUpdateGuard guard(m_updatingFromGraph);
    auto nodeId = m_graphModel->addNode("GraphNode");
    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (nodeModel) {
        nodeModel->setModule(module);
    }
    m_moduleToNodeId[module->id()] = nodeId;
    m_nodeToModuleId[nodeId] = module->id();
    syncNodePositionFromParameters(module, nodeId);

    if (ModuleTypeMetadata::supportsCollapse(module)) {
        refreshModulePresentation(module->id());
    }
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    removeModuleFromView(moduleId);
}

void NodeEditorWidget::removeModuleFromView(const QString& moduleId) {
    auto it = m_moduleToNodeId.find(moduleId);
    if (it != m_moduleToNodeId.end()) {
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_nodeToModuleId.remove(it.value());
        m_graphModel->deleteNode(it.value());
        m_moduleToNodeId.erase(it);
    }
}

void NodeEditorWidget::onConnectionAdded(Connection* connection) {
    QString hostModuleId;
    const bool endpointAttachment =
        connection && isEndpointAttachmentConnection(m_graph, *connection, &hostModuleId);

    ensureConnectionInView(connection);

    if (endpointAttachment) {
        refreshModulePresentation(hostModuleId);
    }
}

bool NodeEditorWidget::ensureConnectionInView(Connection* connection) {
    if (!connection || !connectionBelongsToActiveWorkspace(connection)) {
        return false;
    }

    // Collapsed modules intentionally hide some endpoint nodes, so a missing
    // visual endpoint means the connection should wait until presentation expands.
    auto srcNodeIt = m_moduleToNodeId.find(connection->source().moduleId);
    auto tgtNodeIt = m_moduleToNodeId.find(connection->target().moduleId);
    if (srcNodeIt == m_moduleToNodeId.end() || tgtNodeIt == m_moduleToNodeId.end()) {
        // A stale optimistic connection cannot be matched when either endpoint
        // node is hidden, so clear the draft set and let presentation refresh retry.
        m_pendingConnections.clear();
        return false;
    }

    QtNodes::NodeId srcNodeId = srcNodeIt.value();
    QtNodes::NodeId tgtNodeId = tgtNodeIt.value();

    auto* srcModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(srcNodeId));
    auto* tgtModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(tgtNodeId));
    if (!srcModel || !tgtModel) {
        // Delegate lookup failure means the QtNodes model is not ready for this
        // edge yet; avoid creating an ID mapping to a non-renderable connection.
        m_pendingConnections.clear();
        return false;
    }

    // Graph stores semantic port IDs while QtNodes needs positional port
    // indexes. The GraphNodeModel is the only place that knows that mapping.
    const QtNodes::PortIndex srcPortIdx = srcModel->portIndex(connection->source().portId, QtNodes::PortType::Out);
    const QtNodes::PortIndex tgtPortIdx = tgtModel->portIndex(connection->target().portId, QtNodes::PortType::In);
    if (srcPortIdx == QtNodes::InvalidPortIndex || tgtPortIdx == QtNodes::InvalidPortIndex) {
        return false;
    }

    QtNodes::ConnectionId connId = {srcNodeId, srcPortIdx, tgtNodeId, tgtPortIdx};
    m_pendingRemovals.remove(connId);

    // Reuse an existing visual edge when undo/redo or graph refresh replays a
    // connection already known to QtNodes.
    auto existingIt = m_connectionToQtId.find(connection->id());
    if (existingIt != m_connectionToQtId.end() && m_graphModel->connectionExists(existingIt.value())) {
        return true;
    }
    if (existingIt != m_connectionToQtId.end()) {
        m_connectionToQtId.erase(existingIt);
    }

    if (m_pendingConnections.contains(connId)) {
        // This edge was user-created in the scene; graph confirmation arrived.
        // Bind IDs and skip re-adding the same visual connection.
        m_pendingConnections.remove(connId);
        m_connectionToQtId[connection->id()] = connId;
        return true;
    }

    {
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->addConnection(connId);
    }
    m_connectionToQtId[connection->id()] = connId;
    setConnectionHighlighted(connId, false);

    // Selection highlight depends on the edge map, so recompute it after a new
    // visual connection is registered.
    const auto selectedNodes = m_scene->selectedNodes();
    if (!selectedNodes.empty()) {
        updateConnectedConnectionHighlights(*selectedNodes.begin());
    }
    return true;
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    removeConnectionFromView(connectionId);
}

void NodeEditorWidget::removeConnectionFromView(const QString& connectionId) {
    auto it = m_connectionToQtId.find(connectionId);
    if (it == m_connectionToQtId.end()) {
        return;
    }

    const QtNodes::ConnectionId qtConnectionId = it.value();
    // Clear optimistic bookkeeping first so the ensuing QtNodes delete signal
    // cannot be mistaken for a second user removal.
    m_connectionToQtId.erase(it);
    m_pendingConnections.remove(qtConnectionId);
    m_pendingRemovals.remove(qtConnectionId);

    if (!m_graphModel->connectionExists(qtConnectionId)) {
        return;
    }

    GraphUpdateGuard guard(m_updatingFromGraph);
    m_graphModel->deleteConnection(qtConnectionId);
}

GraphNodeModel* NodeEditorWidget::graphNodeModel(QtNodes::NodeId nodeId) {
    return dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
}

const GraphNodeModel* NodeEditorWidget::graphNodeModel(QtNodes::NodeId nodeId) const {
    return dynamic_cast<const GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
}

void NodeEditorWidget::refreshNodeGraphics(QtNodes::NodeId nodeId, bool moveConnections) {
    if (auto* nodeGraphics = m_scene->nodeGraphicsObject(nodeId)) {
        nodeGraphics->setGeometryChanged();
        if (moveConnections) {
            nodeGraphics->moveConnections();
        }
        nodeGraphics->update();
    }
}

void NodeEditorWidget::onConnectionCreated(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;
    if (m_graphModel->isEditingLocked()) {
        {
            GraphUpdateGuard guard(m_updatingFromGraph);
            if (m_graphModel->connectionExists(connectionId)) {
                m_graphModel->deleteConnection(connectionId);
            }
        }
        return;
    }

    // Mark the temporary scene edge until intent validation either accepts it
    // for design-level planning or rolls it back to the projection.
    m_pendingConnections.insert(connectionId);

    // The scene has only node/port indexes. Resolve them back to model IDs
    // before asking Graph to enforce topology and interface constraints.
    PortRef source;
    PortRef target;
    if (!resolveConnectionPorts(connectionId, source, target)) {
        m_pendingConnections.remove(connectionId);
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->deleteConnection(connectionId);
        return;
    }

    refreshConnectionRuleService();
    const ConnectionCheckResult result = m_connectionRuleService->check(
        ConnectionRequest::portToPort(source, target, ConnectionRequestKind::PortToPort));
    if (!result.hasSingleOption()) {
        m_pendingConnections.remove(connectionId);
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->deleteConnection(connectionId);
        return;
    }

    // Do not mutate Graph here; refresh back to projection until the
    // design-level connection patch path is wired.
    executeAddConnection(result.options.first().source, result.options.first().target);
}

void NodeEditorWidget::onConnectionDeleted(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;
    if (m_graphModel->isEditingLocked()) return;

    // Programmatic deletes are staged in m_pendingRemovals to avoid treating
    // graph-to-scene synchronization as user intent.
    if (m_pendingRemovals.contains(connectionId)) {
        m_pendingRemovals.remove(connectionId);
        return;
    }

    for (auto it = m_connectionToQtId.begin(); it != m_connectionToQtId.end(); ++it) {
        if (it.value() == connectionId) {
            refreshVisibleGraphState();
            break;
        }
    }
}

void NodeEditorWidget::onSelectionChanged() {
    auto selectedNodes = m_scene->selectedNodes();
    if (!selectedNodes.empty()) {
        auto nodeId = *selectedNodes.begin();
        updateConnectedConnectionHighlights(nodeId);
        QString moduleId = m_nodeToModuleId.value(nodeId);
        if (!moduleId.isEmpty()) {
            emit moduleSelected(moduleId);
            return;
        }
    }

    updateConnectedConnectionHighlights(QtNodes::InvalidNodeId);

    for (QGraphicsItem* item : m_scene->selectedItems()) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (!connection || connection->connectionState().requiresPort()) {
            continue;
        }
        const QtNodes::ConnectionId selectedQtConnectionId = connection->connectionId();
        for (auto it = m_connectionToQtId.cbegin(); it != m_connectionToQtId.cend(); ++it) {
            if (it.value() == selectedQtConnectionId) {
                emit connectionSelected(it.key());
                return;
            }
        }
    }

    emit moduleSelected(QString());
}

QString NodeEditorWidget::portIdFor(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    const auto* model = graphNodeModel(nodeId);
    if (!model || !model->module()) return "";

    const Port* port = model->portAt(portType, portIndex);
    return port ? port->id() : "";
}

bool NodeEditorWidget::resolveConnectionPorts(QtNodes::ConnectionId connectionId, PortRef& source, PortRef& target) const {
    source.moduleId = m_nodeToModuleId.value(connectionId.outNodeId);
    target.moduleId = m_nodeToModuleId.value(connectionId.inNodeId);
    if (source.moduleId.isEmpty() || target.moduleId.isEmpty()) {
        return false;
    }

    source.portId = portIdFor(connectionId.outNodeId, QtNodes::PortType::Out, connectionId.outPortIndex);
    target.portId = portIdFor(connectionId.inNodeId, QtNodes::PortType::In, connectionId.inPortIndex);
    if (source.portId.isEmpty() || target.portId.isEmpty()) {
        return false;
    }

    return true;
}

QtNodes::ConnectionGraphicsObject* NodeEditorWidget::findDraftConnection() {
    const auto items = m_scene->items();
    for (QGraphicsItem* item : items) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return connection;
        }
    }

    return nullptr;
}

void NodeEditorWidget::refreshConnectionRuleService() {
    m_connectionRuleService = std::make_unique<ConnectionRuleService>(
        m_graph,
        activePackageManifests(m_workspaceController));
}

void NodeEditorWidget::setConnectionHighlighted(QtNodes::ConnectionId connectionId, bool highlighted) {
    auto* connectionGraphics = m_scene->connectionGraphicsObject(connectionId);
    if (!connectionGraphics) {
        return;
    }

    connectionGraphics->setData(kConnectedHighlightDataRole, highlighted);
    connectionGraphics->update();
}

void NodeEditorWidget::updateConnectedConnectionHighlights(QtNodes::NodeId selectedNodeId) {
    for (auto it = m_connectionToQtId.cbegin(); it != m_connectionToQtId.cend(); ++it) {
        const QtNodes::ConnectionId connectionId = it.value();
        const bool highlighted = selectedNodeId != QtNodes::InvalidNodeId &&
                                 (connectionId.outNodeId == selectedNodeId || connectionId.inNodeId == selectedNodeId);
        setConnectionHighlighted(connectionId, highlighted);
    }
}

bool NodeEditorWidget::tryToggleCollapsed(const QPoint& viewportPos, bool requireToggleButton) {
    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* nodeGraphics = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!nodeGraphics) {
        return false;
    }

    auto* nodeModel = graphNodeModel(nodeGraphics->nodeId());
    if (!nodeModel || !nodeModel->module() || !ModuleTypeMetadata::supportsCollapse(nodeModel->module())) {
        return false;
    }

    const QSize nodeSize = m_scene->nodeGeometry().size(nodeGraphics->nodeId());
    const QPointF localPos = nodeGraphics->mapFromScene(scenePos);
    // Double-click can toggle the whole node; single-click toggles only through
    // the explicit collapse affordance.
    if (requireToggleButton && !GraphNodeGeometry::xpToggleButtonRect(nodeSize).contains(localPos)) {
        return false;
    }

    toggleCollapsed(nodeModel->module()->id(), !nodeModel->isCollapsed());
    return true;
}

void NodeEditorWidget::toggleCollapsed(const QString& moduleId, bool collapsed) {
    Q_UNUSED(moduleId);
    Q_UNUSED(collapsed);
    refreshVisibleGraphState();
}

ConnectionRequest NodeEditorWidget::draftConnectionRequest(
    const QtNodes::ConnectionGraphicsObject& draftConnection,
    QtNodes::NodeId targetNodeId,
    const QPointF& scenePos) const {
    const auto start = resolveDraftConnectionStart(draftConnection);
    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.interactive = true;
    request.allowAutoComplete = true;
    request.allowAlternatives = true;

    if (!start) {
        return request;
    }

    request.start.moduleId = m_nodeToModuleId.value(start->nodeId);
    request.start.portId = portIdFor(start->nodeId, start->portType, start->portIndex);
    request.start.visualSide = start->startFromOutput ? ConnectionVisualSide::Output
                                                       : ConnectionVisualSide::Input;
    request.start.scenePos =
        m_graphModel->nodeData(start->nodeId, QtNodes::NodeRole::Position).value<QPointF>();

    request.end.moduleId = m_nodeToModuleId.value(targetNodeId);
    request.end.scenePos = scenePos;
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;
    request.end.visualSide = start->startFromOutput ? ConnectionVisualSide::Input
                                                     : ConnectionVisualSide::Output;
    return request;
}

bool NodeEditorWidget::tryCompleteDraftConnection(const QPoint& viewportPos) {
    auto* draftConnection = findDraftConnection();
    if (!draftConnection) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* targetNode = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!targetNode) {
        return false;
    }

    refreshConnectionRuleService();
    const ConnectionCheckResult result =
        m_connectionRuleService->check(draftConnectionRequest(*draftConnection,
                                                              targetNode->nodeId(),
                                                              scenePos));

    // Consume the temporary visual draft before delegating to the design-level
    // edit path. Until that path is wired, the view refreshes from projection.
    m_scene->resetDraftConnection();

    if (result.hasSingleOption()) {
        const ConnectionResolvedOption& option = result.options.first();
        executeAddConnection(option.source, option.target);
        return true;
    }

    if (result.status == ConnectionCheckStatus::NeedsSelection && !result.options.isEmpty()) {
        showConnectionOptionsMenu(viewportPos, result.options);
        return true;
    }

    return true;
}

void NodeEditorWidget::showConnectionOptionsMenu(
    const QPoint& viewportPos,
    const QVector<ConnectionResolvedOption>& options) {
    QMenu menu(this);
    for (const ConnectionResolvedOption& option : options) {
        QAction* action = menu.addAction(option.label);
        connect(action, &QAction::triggered, this, [this, option]() {
            executeAddConnection(option.source, option.target);
        });
    }
    menu.exec(m_view->viewport()->mapToGlobal(viewportPos));
}

void NodeEditorWidget::executeAddConnection(const PortRef& source, const PortRef& target) {
    refreshConnectionRuleService();
    ConnectionResolvedOption selectedOption;
    selectedOption.source = source;
    selectedOption.target = target;

    if (m_connectionRuleService) {
        const ConnectionCheckResult result = m_connectionRuleService->check(
            ConnectionRequest::portToPort(source, target, ConnectionRequestKind::Programmatic));
        for (const ConnectionResolvedOption& option : result.options) {
            if (samePortPair(option, source, target)) {
                selectedOption = option;
                break;
            }
        }
    }

    Q_UNUSED(selectedOption);
    refreshVisibleGraphState();
}

void NodeEditorWidget::highlightElement(const QString& elementId) {
    m_scene->clearSelection();

    auto nodeIt = m_moduleToNodeId.find(elementId);
    if (nodeIt != m_moduleToNodeId.end()) {
        auto nodeGraphics = m_scene->nodeGraphicsObject(nodeIt.value());
        if (nodeGraphics) {
            nodeGraphics->setSelected(true);
            m_view->centerOn(nodeGraphics);
        }
        return;
    }

    auto connIt = m_connectionToQtId.find(elementId);
    if (connIt != m_connectionToQtId.end()) {
        auto connGraphics = m_scene->connectionGraphicsObject(connIt.value());
        if (connGraphics) {
            connGraphics->setSelected(true);
            m_view->centerOn(connGraphics);
        }
    }
}

void NodeEditorWidget::onNodeMoved(QtNodes::NodeId nodeId) {
    if (m_updatingFromGraph > 0) return;

    QString moduleId = m_nodeToModuleId.value(nodeId);
    if (moduleId.isEmpty()) return;

    QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();
    const QPointF clampedPos = clampNodePosition(nodeId, pos);
    if (clampedPos != pos) {
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, clampedPos);
        pos = clampedPos;
    }

    refreshVisibleGraphState();
}

void NodeEditorWidget::onParameterChanged(const QString& paramName) {
    Module* module = qobject_cast<Module*>(sender());
    if (!module) return;

    auto nodeIt = m_moduleToNodeId.find(module->id());
    if (nodeIt == m_moduleToNodeId.end()) return;
    const QtNodes::NodeId nodeId = nodeIt.value();

    if (paramName == "display_name") {
        refreshNodeGraphics(nodeId, false);
        return;
    }

    if (paramName == "collapsed") {
        refreshNodeGraphics(nodeId, true);
        refreshModulePresentation(module->id());
        return;
    }

    if (paramName == "node_width" || paramName == "node_height") {
        refreshNodeGraphics(nodeId, true);
        if (ModuleTypeMetadata::supportsCollapse(module)) {
            refreshModulePresentation(module->id());
        }
        return;
    }

    if (paramName != "x" && paramName != "y") return;

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt == params.end() || yIt == params.end()) return;

    syncNodePositionFromParameters(module, nodeId);
}

void NodeEditorWidget::syncNodePositionFromParameters(Module* module, QtNodes::NodeId nodeId) {
    if (!module || nodeId == QtNodes::InvalidNodeId) {
        return;
    }

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt == params.end() || yIt == params.end()) {
        return;
    }

    const auto xOpt = toDouble(xIt.value().value());
    const auto yOpt = toDouble(yIt.value().value());
    if (!xOpt || !yOpt) {
        return;
    }

    GraphUpdateGuard guard(m_updatingFromGraph);
    const QPointF clampedPos = clampNodePosition(nodeId, QPointF(*xOpt, *yOpt));
    m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, clampedPos);
}

bool NodeEditorWidget::showNodeContextMenu(const QPoint& viewportPos, const QPoint& globalPos) {
    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* nodeGraphics = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!nodeGraphics) {
        return showCanvasCreateMenu(viewportPos, globalPos);
    }

    const QString moduleId = m_nodeToModuleId.value(nodeGraphics->nodeId());
    if (moduleId.isEmpty()) {
        return false;
    }

    auto* nodeModel = graphNodeModel(nodeGraphics->nodeId());
    Module* module = nodeModel ? nodeModel->module() : nullptr;
    if (!module) {
        return false;
    }

    if (m_graphModel->isEditingLocked()) {
        m_scene->clearSelection();
        nodeGraphics->setSelected(true);
        return true;
    }

    QMenu menu(m_view);

    QAction* toggleAction = nullptr;
    if (ModuleTypeMetadata::supportsCollapse(module)) {
        toggleAction = menu.addAction(nodeModel->isCollapsed() ? "Expand Node" : "Collapse Node");
    }

    QAction* deleteAction = menu.addAction("Delete Node");

    QAction* selectedAction = menu.exec(globalPos);
    if (!selectedAction) {
        return true;
    }

    if (selectedAction == toggleAction) {
        toggleCollapsed(moduleId, !nodeModel->isCollapsed());
        return true;
    }

    if (selectedAction == deleteAction) {
        refreshVisibleGraphState();
        return true;
    }

    return true;
}

bool NodeEditorWidget::showCanvasCreateMenu(const QPoint& viewportPos, const QPoint& globalPos) {
    if (m_graphModel->isEditingLocked()) {
        return true;
    }

    const QStringList moduleTypes = availableCreateModuleTypes();
    if (moduleTypes.isEmpty()) {
        return true;
    }

    QMenu menu(m_view);
    for (const QString& moduleType : moduleTypes) {
        const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
        if (!type) {
            continue;
        }

        QAction* action = menu.addAction(ModuleTypeMetadata::paletteLabel(type));
        action->setData(moduleType);
    }

    QAction* selectedAction = menu.exec(globalPos);
    if (!selectedAction) {
        return true;
    }

    ScopedModulePayload payload;
    payload.ipcoreId = m_workspaceController->state().ipcoreId;
    payload.instanceId = m_workspaceController->state().instanceId;
    payload.moduleType = selectedAction->data().toString();
    return createModuleAt(payload, m_view->mapToScene(viewportPos));
}

bool NodeEditorWidget::createModuleAt(const ScopedModulePayload& payload, const QPointF& scenePos) {
    if (!acceptsScopedModulePayload(payload)) {
        return false;
    }

    Q_UNUSED(scenePos);
    refreshVisibleGraphState();
    return false;
}

void NodeEditorWidget::refreshVisibleGraphState() {
    const QStringList connectionIds = m_connectionToQtId.keys();
    for (const QString& connectionId : connectionIds) {
        removeConnectionFromView(connectionId);
    }

    const QStringList moduleIds = m_moduleToNodeId.keys();
    for (const QString& moduleId : moduleIds) {
        removeModuleFromView(moduleId);
    }

    for (const auto& module : m_graph->modules()) {
        ensureModuleInView(module.get());
    }
    for (const auto& connection : m_graph->connections()) {
        ensureConnectionInView(connection.get());
    }
    refreshAllModulePresentations();
}

QPointF NodeEditorWidget::clampNodePosition(QtNodes::NodeId nodeId, const QPointF& position) const {
    QSize nodeSize;
    if (nodeId != QtNodes::InvalidNodeId) {
        nodeSize = m_scene->nodeGeometry().size(nodeId);
    }

    const qreal maxX = m_canvasRect.right() - nodeSize.width();
    const qreal maxY = m_canvasRect.bottom() - nodeSize.height();
    return {
        std::clamp(position.x(), m_canvasRect.left(), maxX),
        std::clamp(position.y(), m_canvasRect.top(), maxY)
    };
}

void NodeEditorWidget::refreshModulePresentation(const QString& moduleId) {
    Module* hostModule = m_graph->getModule(moduleId);
    if (!hostModule || !ModuleTypeMetadata::supportsCollapse(hostModule)) {
        return;
    }

    if (auto nodeIt = m_moduleToNodeId.find(moduleId); nodeIt != m_moduleToNodeId.end()) {
        refreshNodeGraphics(nodeIt.value(), true);
    }

    const ModulePresentationState state = collectModulePresentationState(moduleId);
    // Presentation refresh starts by hiding all affected edges, then restores
    // only the subset that should be visible for the current collapse state.
    hideModuleConnections(state);

    const bool collapsed = isCollapsed(hostModule);
    if (collapsed) {
        applyCollapsedModulePresentation(state);
        return;
    }

    applyExpandedModulePresentation(state);
}

void NodeEditorWidget::refreshAllModulePresentations() {
    for (const auto& module : m_graph->modules()) {
        if (ModuleTypeMetadata::supportsCollapse(module.get())) {
            refreshModulePresentation(module->id());
        }
    }
}

NodeEditorWidget::ModulePresentationState NodeEditorWidget::collectModulePresentationState(const QString& moduleId) const {
    ModulePresentationState state;

    for (const auto& connection : m_graph->connections()) {
        if (!connectionBelongsToActiveWorkspace(connection.get())) {
            continue;
        }
        if (connection->source().moduleId != moduleId && connection->target().moduleId != moduleId) {
            continue;
        }

        state.moduleConnections.append(connection.get());

        QString connectionHostId;
        QString endpointModuleId;
        if (isEndpointAttachmentConnection(m_graph, *connection, &connectionHostId, &endpointModuleId) &&
            connectionHostId == moduleId) {
            state.attachmentConnections.append(connection.get());
            state.endpointModuleIds.insert(endpointModuleId);
        }
    }

    return state;
}

void NodeEditorWidget::hideModuleConnections(const ModulePresentationState& state) {
    for (Connection* connection : state.moduleConnections) {
        removeConnectionFromView(connection->id());
    }
}

void NodeEditorWidget::applyCollapsedModulePresentation(const ModulePresentationState& state) {
    // Collapsed router nodes absorb attached endpoint visuals while keeping
    // non-attachment links visible on the host node.
    for (const QString& endpointModuleId : state.endpointModuleIds) {
        removeModuleFromView(endpointModuleId);
    }

    for (Connection* connection : state.moduleConnections) {
        if (!state.attachmentConnections.contains(connection)) {
            ensureConnectionInView(connection);
        }
    }
}

void NodeEditorWidget::applyExpandedModulePresentation(const ModulePresentationState& state) {
    // Expanded routers expose endpoint modules again, lay them out around the
    // host, then restore all visible connections.
    for (const QString& endpointModuleId : state.endpointModuleIds) {
        ensureModuleInView(m_graph->getModule(endpointModuleId));
    }

    for (Connection* connection : state.attachmentConnections) {
        positionAttachedEndpoint(connection);
    }

    for (Connection* connection : state.moduleConnections) {
        ensureConnectionInView(connection);
    }
}

void NodeEditorWidget::positionAttachedEndpoint(Connection* connection) {
    if (!connection) {
        return;
    }

    // The presentation state stores attachments as graph connections. Resolve
    // which endpoint is visually hosted by which router before touching scene data.
    QString hostModuleId;
    QString endpointModuleId;
    if (!isEndpointAttachmentConnection(m_graph, *connection, &hostModuleId, &endpointModuleId)) {
        return;
    }

    const auto hostNodeIt = m_moduleToNodeId.find(hostModuleId);
    const auto endpointNodeIt = m_moduleToNodeId.find(endpointModuleId);
    if (hostNodeIt == m_moduleToNodeId.end() || endpointNodeIt == m_moduleToNodeId.end()) {
        // Collapsed or not-yet-created endpoint visuals are handled by the
        // caller's presentation pass.
        return;
    }

    // Need both Graph modules for metadata and QtNodes delegate models for
    // port-index lookup.
    Module* hostModule = m_graph->getModule(hostModuleId);
    Module* endpointModule = m_graph->getModule(endpointModuleId);
    auto* hostModel = graphNodeModel(hostNodeIt.value());
    auto* endpointModel = graphNodeModel(endpointNodeIt.value());
    if (!hostModule || !endpointModule || !hostModel || !endpointModel) {
        return;
    }

    const bool hostIsSource = connection->source().moduleId == hostModuleId;
    const bool endpointIsSource = connection->source().moduleId == endpointModuleId;
    // Connection orientation can vary; derive host/endpoint port IDs from the
    // actual endpoints instead of assuming source or target roles.
    const QString hostPortId = hostIsSource ? connection->source().portId : connection->target().portId;
    const QString endpointPortId = endpointIsSource ? connection->source().portId : connection->target().portId;
    const QtNodes::PortType hostPortType = hostIsSource ? QtNodes::PortType::Out : QtNodes::PortType::In;
    const QtNodes::PortType endpointPortType = endpointIsSource ? QtNodes::PortType::Out : QtNodes::PortType::In;
    const QtNodes::PortIndex hostPortIndex = hostModel->portIndex(hostPortId, hostPortType);
    const QtNodes::PortIndex endpointPortIndex = endpointModel->portIndex(endpointPortId, endpointPortType);
    if (hostPortIndex == QtNodes::InvalidPortIndex || endpointPortIndex == QtNodes::InvalidPortIndex) {
        // Missing visual ports usually mean metadata and graph data disagree;
        // skip positioning rather than placing an endpoint at an arbitrary point.
        return;
    }

    auto const& geometry = m_scene->nodeGeometry();
    const QPointF hostPosition =
        m_graphModel->nodeData(hostNodeIt.value(), QtNodes::NodeRole::Position).value<QPointF>();
    const QPointF hostPortPosition = geometry.portPosition(hostNodeIt.value(), hostPortType, hostPortIndex);
    const Port* hostPort = findPort(hostModule, hostPortId);
    const std::optional<HostAttachmentAnchor> hostAnchor =
        hostAttachmentAnchor(hostModule,
                             endpointModule,
                             hostPort,
                             hostPortPosition,
                             geometry.size(hostNodeIt.value()));
    if (!hostAnchor.has_value()) {
        // Without a stable outward normal, there is no deterministic side for
        // endpoint attachment.
        return;
    }

    const Port* endpointPort = findPort(endpointModule, endpointPortId);
    const QSize endpointSize = geometry.size(endpointNodeIt.value());
    const bool mirrorAttachedNode =
        hostAnchor->zone ? hostAnchor->zone->mirrorAttachedNode.value_or(true) : true;
    const QPointF endpointPortPosition = mirrorAttachedNode
        ? EndpointAttachmentLayout::endpointAnchorForHostNormal(endpointSize, hostAnchor->normal)
        : attachmentPortAnchor(endpointModule, endpointPort, endpointSize, hostAnchor->normal);
    // Offset is measured along the host-port normal so north/east/south/west
    // attachments share the same geometry path.
    const qreal configuredOffset = static_cast<qreal>(ModuleTypeMetadata::linkedEndpointOffsetX(hostModule));
    const qreal projectedEndpointAnchor = QPointF::dotProduct(endpointPortPosition, hostAnchor->normal);
    const qreal gap = std::max<qreal>(32.0, configuredOffset + projectedEndpointAnchor);
    const QPointF targetPosition = clampNodePosition(
        endpointNodeIt.value(),
        EndpointAttachmentLayout::endpointTopLeft(
            hostPosition + hostAnchor->position,
            hostAnchor->normal,
            endpointPortPosition,
            gap));

    // This is a visual presentation adjustment only. Persisted endpoint
    // coordinates remain controlled by explicit drag/command paths.
    GraphUpdateGuard guard(m_updatingFromGraph);
    m_graphModel->setNodeData(endpointNodeIt.value(), QtNodes::NodeRole::Position, targetPosition);
    refreshNodeGraphics(endpointNodeIt.value(), true);
}
