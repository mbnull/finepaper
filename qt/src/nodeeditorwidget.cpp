// NodeEditorWidget — Qt widget that bridges the Graph data model and the
// QtNodes visual canvas. Listens to Graph signals to keep the canvas in sync,
// and translates QtNodes connection/deletion events back into Commands.
// m_updatingFromGraph guards against re-entrant signal loops when the widget
// itself drives a graph change.
#include "nodeeditorwidget.h"
#include "animatedgraphicsview.h"
#include "editorgraphmodel.h"
#include "graphnodegeometry.h"
#include "graphnodemodel.h"
#include "graphnodepainter.h"
#include "modulelabels.h"
#include "moduletypemetadata.h"
#include "moduleregistry.h"
#include "portlayout.h"
#include "straightconnectionpainter.h"
#include "commands/arrangecommand.h"
#include "commands/addmodulecommand.h"
#include "commands/addconnectioncommand.h"
#include "commands/removemodulecommand.h"
#include "commands/removeconnectioncommand.h"
#include "commands/setparametercommand.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/locateNode.hpp>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
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
    explicit GraphUpdateGuard(int& counter) : m_counter(counter) { ++m_counter; }
    ~GraphUpdateGuard() { --m_counter; }

private:
    int& m_counter;
};

bool isMeshRouterModule(const Module* module) {
    return ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router");
}

bool isEndpointModule(const Module* module) {
    return ModuleTypeMetadata::isInGraphGroup(module, u"endpoints");
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
    if (!isMeshRouterModule(sourceModule) || !isEndpointModule(targetModule)) return false;
    if (!PortLayout::isEndpointPortId(connection.source().portId)) return false;

    if (hostModuleId) *hostModuleId = sourceModule->id();
    if (endpointModuleId) *endpointModuleId = targetModule->id();
    return true;
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) return nullptr;

    for (const auto& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

constexpr qreal kCanvasHalfExtent = 2000.0;
const QRectF kCanvasRect(-kCanvasHalfExtent, -kCanvasHalfExtent,
                         kCanvasHalfExtent * 2.0, kCanvasHalfExtent * 2.0);
constexpr int kConnectedHighlightDataRole = 1;

QString firstAvailablePort(const Graph* graph,
                           const Module* module,
                           Port::Direction direction,
                           const std::function<bool(const Port&)>& predicate) {
    if (!graph || !module) return {};

    for (const auto& port : module->ports()) {
        if (port.direction() != direction || !predicate(port)) {
            continue;
        }

        const bool occupied = std::any_of(graph->connections().begin(), graph->connections().end(),
            [&](const std::unique_ptr<Connection>& connection) {
                const PortRef& ref = direction == Port::Direction::Output
                    ? connection->source()
                    : connection->target();
                return ref.moduleId == module->id() && ref.portId == port.id();
            });

        if (!occupied) {
            return port.id();
        }
    }

    return {};
}

QString generateEntityId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
}

int nextModuleIndex(const Graph* graph, const QString& moduleType, const QString& externalIdPrefix) {
    QSet<int> used;
    QRegularExpression pattern("^" + QRegularExpression::escape(externalIdPrefix) + "_(\\d+)$",
                               QRegularExpression::CaseInsensitiveOption);

    for (const auto& module : graph->modules()) {
        if (module->type() != moduleType) {
            continue;
        }

        const auto match = pattern.match(ModuleLabels::externalId(module.get()));
        if (match.hasMatch()) {
            used.insert(match.captured(1).toInt());
        }
    }

    int index = 0;
    while (used.contains(index)) {
        ++index;
    }
    return index;
}

void assignModuleIdentity(Graph* graph, Module* module) {
    if (!graph || !module) {
        return;
    }

    const QString externalPrefix = ModuleTypeMetadata::externalIdPrefix(module);
    const QString displayPrefix = ModuleTypeMetadata::displayPrefix(module);
    if (externalPrefix.isEmpty() || displayPrefix.isEmpty()) {
        return;
    }

    const int index = nextModuleIndex(graph, module->type(), externalPrefix);
    const int width = ModuleTypeMetadata::identityWidth(module);
    module->setParameter("display_name", QString("%1_%2").arg(displayPrefix).arg(index, width, 10, QChar('0')));
    module->setParameter("external_id", QString("%1_%2").arg(externalPrefix).arg(index, width, 10, QChar('0')));
}

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
    start.startFromOutput = requiredPort == QtNodes::PortType::In;
    start.nodeId = start.startFromOutput ? connectionId.outNodeId : connectionId.inNodeId;
    start.portIndex = start.startFromOutput ? connectionId.outPortIndex : connectionId.inPortIndex;
    start.portType = start.startFromOutput ? QtNodes::PortType::Out : QtNodes::PortType::In;
    if (start.nodeId == QtNodes::InvalidNodeId || start.portIndex == QtNodes::InvalidPortIndex) {
        return std::nullopt;
    }

    return start;
}

} // namespace

NodeEditorWidget::NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent),
      m_graph(graph),
      m_commandManager(commandManager),
      m_canvasRect(kCanvasRect) {

    m_registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    m_registry->registerModel<GraphNodeModel>("GraphNode");

    m_graphModel = new EditorGraphModel(m_registry);
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);
    m_scene->setSceneRect(m_canvasRect);
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

    connect(m_graph, &Graph::moduleAdded, this, &NodeEditorWidget::onModuleAdded);
    connect(m_graph, &Graph::moduleRemoved, this, &NodeEditorWidget::onModuleRemoved);
    connect(m_graph, &Graph::connectionAdded, this, &NodeEditorWidget::onConnectionAdded);
    connect(m_graph, &Graph::connectionRemoved, this, &NodeEditorWidget::onConnectionRemoved);

    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionCreated, this, &NodeEditorWidget::onConnectionCreated);
    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionDeleted, this, &NodeEditorWidget::onConnectionDeleted);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &NodeEditorWidget::onSelectionChanged);
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeMoved, this, &NodeEditorWidget::onNodeMoved);

    for (const auto& module : m_graph->modules()) {
        onModuleAdded(module.get());
    }
    for (const auto& connection : m_graph->connections()) {
        onConnectionAdded(connection.get());
    }

    refreshAllModulePresentations();
}

NodeEditorWidget::~NodeEditorWidget() {
    if (m_view && m_view->viewport()) {
        m_view->viewport()->removeEventFilter(this);
    }
}

bool NodeEditorWidget::isArrangeEnabled() const {
    return m_graphModel->isEditingLocked();
}

void NodeEditorWidget::setArrangeEnabled(bool enabled) {
    if (enabled) {
        auto command = std::make_unique<ArrangeCommand>(m_graph);
        m_commandManager->executeCommand(std::move(command));
    }

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
    if (!module || m_moduleToNodeId.contains(module->id())) {
        return;
    }

    GraphUpdateGuard guard(m_updatingFromGraph);
    auto nodeId = m_graphModel->addNode("GraphNode");
    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (nodeModel) {
        nodeModel->setModule(module);
    }
    m_moduleToNodeId[module->id()] = nodeId;
    m_nodeToModuleId[nodeId] = module->id();
    syncNodePositionFromParameters(module, nodeId);
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
    ensureConnectionInView(connection);

    QString hostModuleId;
    if (isEndpointAttachmentConnection(m_graph, *connection, &hostModuleId)) {
        refreshModulePresentation(hostModuleId);
    }
}

bool NodeEditorWidget::ensureConnectionInView(Connection* connection) {
    if (!connection) {
        return false;
    }

    auto srcNodeIt = m_moduleToNodeId.find(connection->source().moduleId);
    auto tgtNodeIt = m_moduleToNodeId.find(connection->target().moduleId);
    if (srcNodeIt == m_moduleToNodeId.end() || tgtNodeIt == m_moduleToNodeId.end()) {
        m_pendingConnections.clear();
        return false;
    }

    QtNodes::NodeId srcNodeId = srcNodeIt.value();
    QtNodes::NodeId tgtNodeId = tgtNodeIt.value();

    auto* srcModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(srcNodeId));
    auto* tgtModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(tgtNodeId));
    if (!srcModel || !tgtModel) {
        m_pendingConnections.clear();
        return false;
    }

    const QtNodes::PortIndex srcPortIdx = srcModel->portIndex(connection->source().portId, QtNodes::PortType::Out);
    const QtNodes::PortIndex tgtPortIdx = tgtModel->portIndex(connection->target().portId, QtNodes::PortType::In);
    if (srcPortIdx == QtNodes::InvalidPortIndex || tgtPortIdx == QtNodes::InvalidPortIndex) {
        return false;
    }

    QtNodes::ConnectionId connId{srcNodeId, srcPortIdx, tgtNodeId, tgtPortIdx};
    m_pendingRemovals.remove(connId);

    auto existingIt = m_connectionToQtId.find(connection->id());
    if (existingIt != m_connectionToQtId.end() && m_graphModel->connectionExists(existingIt.value())) {
        return true;
    }
    if (existingIt != m_connectionToQtId.end()) {
        m_connectionToQtId.erase(existingIt);
    }

    if (m_pendingConnections.contains(connId)) {
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
    m_connectionToQtId.erase(it);
    m_pendingConnections.remove(qtConnectionId);
    m_pendingRemovals.remove(qtConnectionId);

    if (!m_graphModel->connectionExists(qtConnectionId)) {
        return;
    }

    GraphUpdateGuard guard(m_updatingFromGraph);
    m_graphModel->deleteConnection(qtConnectionId);
}

GraphNodeModel* NodeEditorWidget::graphNodeModel(QtNodes::NodeId nodeId) const {
    return dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
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

    m_pendingConnections.insert(connectionId);

    PortRef source;
    PortRef target;
    if (!resolveConnectionPorts(connectionId, source, target)) {
        m_pendingConnections.remove(connectionId);
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->deleteConnection(connectionId);
        return;
    }

    if (!m_graph->isValidConnection(source, target)) {
        m_pendingConnections.remove(connectionId);
        GraphUpdateGuard guard(m_updatingFromGraph);
        m_graphModel->deleteConnection(connectionId);
        return;
    }

    executeAddConnection(source, target);
}

void NodeEditorWidget::onConnectionDeleted(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;
    if (m_graphModel->isEditingLocked()) return;

    if (m_pendingRemovals.contains(connectionId)) {
        m_pendingRemovals.remove(connectionId);
        return;
    }

    for (auto it = m_connectionToQtId.begin(); it != m_connectionToQtId.end(); ++it) {
        if (it.value() == connectionId) {
            auto command = std::make_unique<RemoveConnectionCommand>(m_graph, it.key());
            m_commandManager->executeCommand(std::move(command));
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
    emit moduleSelected(QString());
}

QString NodeEditorWidget::getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    auto* model = graphNodeModel(nodeId);
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

    source.portId = getPortId(connectionId.outNodeId, QtNodes::PortType::Out, connectionId.outPortIndex);
    target.portId = getPortId(connectionId.inNodeId, QtNodes::PortType::In, connectionId.inPortIndex);
    if (source.portId.isEmpty() || target.portId.isEmpty()) {
        return false;
    }

    return true;
}

QtNodes::ConnectionGraphicsObject* NodeEditorWidget::findDraftConnection() const {
    const auto items = m_scene->items();
    for (QGraphicsItem* item : items) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return connection;
        }
    }

    return nullptr;
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
    if (requireToggleButton && !GraphNodeGeometry::xpToggleButtonRect(nodeSize).contains(localPos)) {
        return false;
    }

    toggleCollapsed(nodeModel->module()->id(), !nodeModel->isCollapsed());
    return true;
}

void NodeEditorWidget::toggleCollapsed(const QString& moduleId, bool collapsed) {
    auto command = std::make_unique<SetParameterCommand>(m_graph, moduleId, "collapsed", collapsed);
    m_commandManager->executeCommand(std::move(command));
}

bool NodeEditorWidget::resolveRouterDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                                    QtNodes::NodeId targetNodeId,
                                                    PortRef& source,
                                                    PortRef& target) const {
    const auto start = resolveDraftConnectionStart(draftConnection);
    if (!start) {
        return false;
    }
    if (start->nodeId == targetNodeId) {
        return false;
    }

    const QString startModuleId = m_nodeToModuleId.value(start->nodeId);
    const QString targetModuleId = m_nodeToModuleId.value(targetNodeId);
    if (startModuleId.isEmpty() || targetModuleId.isEmpty()) {
        return false;
    }

    const Module* startModule = m_graph->getModule(startModuleId);
    const Module* endModule = m_graph->getModule(targetModuleId);
    if (!startModule || !endModule || !isMeshRouterModule(startModule) || !isMeshRouterModule(endModule)) {
        return false;
    }

    const QString startPortId = getPortId(start->nodeId, start->portType, start->portIndex);
    if (!PortLayout::isDirectionalRouterPortId(startPortId)) {
        return false;
    }

    const QString startSide = PortLayout::routerSideId(startPortId);
    const QString oppositeSide = PortLayout::oppositeRouterSide(startSide);
    if (startSide.isEmpty() || oppositeSide.isEmpty()) {
        return false;
    }

    if (start->startFromOutput) {
        source = PortRef{startModuleId, PortLayout::routerOutputPortId(startSide)};
        target = PortRef{targetModuleId, PortLayout::routerInputPortId(oppositeSide)};
    } else {
        source = PortRef{targetModuleId, PortLayout::routerOutputPortId(oppositeSide)};
        target = PortRef{startModuleId, PortLayout::routerInputPortId(startSide)};
    }

    return true;
}

bool NodeEditorWidget::tryCompleteRouterDraftConnection(const QPoint& viewportPos) {
    return tryCompleteDraftConnection(viewportPos,
        [this](const QtNodes::ConnectionGraphicsObject& draftConnection,
               QtNodes::NodeId targetNodeId,
               PortRef& source,
               PortRef& target) {
            return resolveRouterDraftConnection(draftConnection, targetNodeId, source, target);
        });
}

bool NodeEditorWidget::resolveEndpointDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                                      QtNodes::NodeId targetNodeId,
                                                      PortRef& source,
                                                      PortRef& target) const {
    const auto start = resolveDraftConnectionStart(draftConnection);
    if (!start) {
        return false;
    }
    if (start->nodeId == targetNodeId) {
        return false;
    }

    const QString startModuleId = m_nodeToModuleId.value(start->nodeId);
    const QString targetModuleId = m_nodeToModuleId.value(targetNodeId);
    if (startModuleId.isEmpty() || targetModuleId.isEmpty()) {
        return false;
    }

    const Module* startModule = m_graph->getModule(startModuleId);
    const Module* endModule = m_graph->getModule(targetModuleId);
    if (!startModule || !endModule) {
        return false;
    }

    const QString startPortId = getPortId(start->nodeId, start->portType, start->portIndex);
    const Port* startPort = findPort(startModule, startPortId);
    if (!startPort || startPort->type() != "endpoint") {
        return false;
    }

    if (start->startFromOutput) {
        if (!isMeshRouterModule(startModule) || !isEndpointModule(endModule)) {
            return false;
        }

        const QString endpointPortId = firstAvailablePort(m_graph, endModule, Port::Direction::Input,
            [](const Port& port) { return port.type() == "endpoint"; });
        if (endpointPortId.isEmpty()) {
            return false;
        }

        source = PortRef{startModuleId, startPortId};
        target = PortRef{targetModuleId, endpointPortId};
        return true;
    }

    if (!isEndpointModule(startModule) || !isMeshRouterModule(endModule)) {
        return false;
    }

    const QString xpPortId = firstAvailablePort(m_graph, endModule, Port::Direction::Output,
        [](const Port& port) { return PortLayout::isEndpointPort(port); });
    if (xpPortId.isEmpty()) {
        return false;
    }

    source = PortRef{targetModuleId, xpPortId};
    target = PortRef{startModuleId, startPortId};
    return true;
}

bool NodeEditorWidget::tryCompleteEndpointDraftConnection(const QPoint& viewportPos) {
    return tryCompleteDraftConnection(viewportPos,
        [this](const QtNodes::ConnectionGraphicsObject& draftConnection,
               QtNodes::NodeId targetNodeId,
               PortRef& source,
               PortRef& target) {
            return resolveEndpointDraftConnection(draftConnection, targetNodeId, source, target);
        });
}

bool NodeEditorWidget::tryCompleteDraftConnection(const QPoint& viewportPos,
                                                  const std::function<bool(const QtNodes::ConnectionGraphicsObject&,
                                                                           QtNodes::NodeId,
                                                                           PortRef&,
                                                                           PortRef&)>& resolver) {
    auto* draftConnection = findDraftConnection();
    if (!draftConnection) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* targetNode = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!targetNode) {
        return false;
    }

    PortRef source;
    PortRef target;
    if (!resolver(*draftConnection, targetNode->nodeId(), source, target)) {
        return false;
    }

    m_scene->resetDraftConnection();

    if (!m_graph->isValidConnection(source, target)) {
        return true;
    }

    executeAddConnection(source, target);
    return true;
}

void NodeEditorWidget::executeAddConnection(const PortRef& source, const PortRef& target) {
    auto connection = std::make_unique<Connection>(generateEntityId(), source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
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

    auto xCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "x", static_cast<int>(pos.x()));
    auto yCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "y", static_cast<int>(pos.y()));
    m_commandManager->executeCommand(std::move(xCmd));
    m_commandManager->executeCommand(std::move(yCmd));
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
        auto command = std::make_unique<RemoveModuleCommand>(m_graph, moduleId);
        m_commandManager->executeCommand(std::move(command));
        return true;
    }

    return true;
}

bool NodeEditorWidget::showCanvasCreateMenu(const QPoint& viewportPos, const QPoint& globalPos) {
    if (m_graphModel->isEditingLocked()) {
        return true;
    }

    const QStringList moduleTypes = ModuleRegistry::instance().availableTypes();
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

    return createModuleAt(selectedAction->data().toString(), m_view->mapToScene(viewportPos));
}

bool NodeEditorWidget::createModuleAt(const QString& moduleType, const QPointF& scenePos) {
    const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
    if (!type) {
        return false;
    }

    const QString moduleId = generateEntityId();
    auto module = std::make_unique<Module>(moduleId, moduleType);

    for (const auto& port : type->defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    assignModuleIdentity(m_graph, module.get());

    auto command = std::make_unique<AddModuleCommand>(m_graph, std::move(module));
    m_commandManager->executeCommand(std::move(command));

    if (!m_moduleToNodeId.contains(moduleId) || !m_graph->getModule(moduleId)) {
        return false;
    }

    const auto nodeId = m_moduleToNodeId.value(moduleId);
    const QPointF clampedPos = clampNodePosition(nodeId, scenePos);

    GraphUpdateGuard guard(m_updatingFromGraph);
    m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, clampedPos);
    auto xCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "x", static_cast<int>(clampedPos.x()));
    auto yCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "y", static_cast<int>(clampedPos.y()));
    m_commandManager->executeCommand(std::move(xCmd));
    m_commandManager->executeCommand(std::move(yCmd));
    return true;
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
    for (const QString& endpointModuleId : state.endpointModuleIds) {
        ensureModuleInView(m_graph->getModule(endpointModuleId));
    }

    for (Connection* connection : state.moduleConnections) {
        ensureConnectionInView(connection);
    }
}
