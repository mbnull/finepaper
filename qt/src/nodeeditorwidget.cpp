// NodeEditorWidget — Qt widget that bridges the Graph data model and the
// QtNodes visual canvas. Listens to Graph signals to keep the canvas in sync,
// and translates QtNodes connection/deletion events back into Commands.
// m_updatingFromGraph guards against re-entrant signal loops when the widget
// itself drives a graph change.
#include "nodeeditorwidget.h"
#include "graphnodegeometry.h"
#include "graphnodemodel.h"
#include "graphnodepainter.h"
#include "modulelabels.h"
#include "moduleregistry.h"
#include "straightconnectionpainter.h"
#include "commands/addmodulecommand.h"
#include "commands/addconnectioncommand.h"
#include "commands/removeconnectioncommand.h"
#include "commands/setparametercommand.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUuid>
#include <QGraphicsItem>
#include <QRegularExpression>
#include <optional>
#include <QSet>

static std::optional<double> toDouble(const Parameter::Value& v) {
    if (auto* i = std::get_if<int>(&v)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&v)) return *d;
    return std::nullopt;
}

namespace {

int nextModuleIndex(const Graph* graph, const QString& prefix) {
    QSet<int> used;
    QRegularExpression pattern("^" + QRegularExpression::escape(prefix) + "_(\\d+)$", QRegularExpression::CaseInsensitiveOption);

    for (const auto& module : graph->modules()) {
        const auto match = pattern.match(ModuleLabels::displayName(module.get()));
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
    if (!graph || !module) return;

    if (module->type() == "XP") {
        const int index = nextModuleIndex(graph, "XP");
        module->setParameter("display_name", QString("XP_%1").arg(index, 2, 10, QChar('0')));
        module->setParameter("external_id", QString("xp_%1").arg(index, 2, 10, QChar('0')));
    } else if (module->type() == "Endpoint") {
        const int index = nextModuleIndex(graph, "EP");
        module->setParameter("display_name", QString("EP_%1").arg(index, 2, 10, QChar('0')));
        module->setParameter("external_id", QString("ep_%1").arg(index, 2, 10, QChar('0')));
    }
}

} // namespace

NodeEditorWidget::NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {

    m_registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    m_registry->registerModel<GraphNodeModel>("GraphNode");

    m_graphModel = new QtNodes::DataFlowGraphModel(m_registry);
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);
    m_scene->setNodeGeometry(std::make_unique<GraphNodeGeometry>(*m_graphModel));
    m_scene->setNodePainter(std::make_unique<GraphNodePainter>());
    m_scene->setConnectionPainter(std::make_unique<StraightConnectionPainter>());
    m_view = new QtNodes::GraphicsView(m_scene);

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
}

void NodeEditorWidget::onModuleAdded(Module* module) {
    ++m_updatingFromGraph;
    auto nodeId = m_graphModel->addNode("GraphNode");
    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (nodeModel) {
        nodeModel->setModule(module);
    }
    m_moduleToNodeId[module->id()] = nodeId;
    m_nodeToModuleId[nodeId] = module->id();

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt != params.end() && yIt != params.end()) {
        const auto& xValue = xIt.value().value();
        const auto& yValue = yIt.value().value();
        auto xOpt = toDouble(xValue);
        auto yOpt = toDouble(yValue);
        if (xOpt && yOpt) {
            m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, QPointF(*xOpt, *yOpt));
        }
    }

    connect(module, &Module::parameterChanged, this, &NodeEditorWidget::onParameterChanged);
    --m_updatingFromGraph;
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    auto it = m_moduleToNodeId.find(moduleId);
    if (it != m_moduleToNodeId.end()) {
        ++m_updatingFromGraph;
        m_nodeToModuleId.remove(it.value());
        m_graphModel->deleteNode(it.value());
        m_moduleToNodeId.erase(it);
        --m_updatingFromGraph;
    }
}

void NodeEditorWidget::onConnectionAdded(Connection* connection) {
    auto srcNodeIt = m_moduleToNodeId.find(connection->source().moduleId);
    auto tgtNodeIt = m_moduleToNodeId.find(connection->target().moduleId);
    if (srcNodeIt == m_moduleToNodeId.end() || tgtNodeIt == m_moduleToNodeId.end()) {
        m_pendingConnections.clear();
        return;
    }

    QtNodes::NodeId srcNodeId = srcNodeIt.value();
    QtNodes::NodeId tgtNodeId = tgtNodeIt.value();

    auto* srcModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(srcNodeId));
    auto* tgtModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(tgtNodeId));
    if (!srcModel || !tgtModel) {
        m_pendingConnections.clear();
        return;
    }

    QtNodes::PortIndex srcPortIdx = 0;
    for (const auto& port : srcModel->module()->ports()) {
        if (port.direction() == Port::Direction::Output) {
            if (port.id() == connection->source().portId) break;
            srcPortIdx++;
        }
    }

    QtNodes::PortIndex tgtPortIdx = 0;
    for (const auto& port : tgtModel->module()->ports()) {
        if (port.direction() == Port::Direction::Input) {
            if (port.id() == connection->target().portId) break;
            tgtPortIdx++;
        }
    }

    QtNodes::ConnectionId connId{srcNodeId, srcPortIdx, tgtNodeId, tgtPortIdx};
    m_pendingRemovals.remove(connId);

    if (m_pendingConnections.contains(connId)) {
        m_pendingConnections.remove(connId);
        m_connectionToQtId[connection->id()] = connId;
        return;
    }

    ++m_updatingFromGraph;
    m_graphModel->addConnection(connId);
    m_connectionToQtId[connection->id()] = connId;
    --m_updatingFromGraph;
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    auto it = m_connectionToQtId.find(connectionId);
    if (it != m_connectionToQtId.end()) {
        const QtNodes::ConnectionId qtConnectionId = it.value();
        m_connectionToQtId.erase(it);

        if (!m_graphModel->connectionExists(qtConnectionId)) {
            m_pendingRemovals.remove(qtConnectionId);
            return;
        }

        ++m_updatingFromGraph;
        m_pendingRemovals.insert(qtConnectionId);
        if (!m_graphModel->deleteConnection(qtConnectionId)) {
            m_pendingRemovals.remove(qtConnectionId);
        }
        --m_updatingFromGraph;
    }
}

void NodeEditorWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-moduletype")) {
        event->acceptProposedAction();
    }
}

bool NodeEditorWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_view->viewport()) {
        // Forward drag/drop events from the viewport to our handlers.
        // Without this, the GraphicsView child widget consumes all drag events
        // and our dragEnterEvent/dropEvent on NodeEditorWidget never fire.
        if (event->type() == QEvent::DragEnter) {
            auto* e = static_cast<QDragEnterEvent*>(event);
            if (e->mimeData()->hasFormat("application/x-moduletype")) {
                e->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* e = static_cast<QDragMoveEvent*>(event);
            if (e->mimeData()->hasFormat("application/x-moduletype")) {
                e->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto* e = static_cast<QDropEvent*>(event);
            if (e->mimeData()->hasFormat("application/x-moduletype")) {
                dropEvent(e);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void NodeEditorWidget::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat("application/x-moduletype")) {
        return;
    }

    QString moduleType = QString::fromUtf8(event->mimeData()->data("application/x-moduletype"));
    const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
    if (!type) {
        return;
    }

    QString moduleId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
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

    if (m_moduleToNodeId.contains(moduleId)) {
        QPointF scenePos = m_view->mapToScene(event->position().toPoint());
        auto nodeId = m_moduleToNodeId.value(moduleId);

        Module* module = m_graph->getModule(moduleId);
        if (module) {
            ++m_updatingFromGraph;
            m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
            auto xCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "x", static_cast<int>(scenePos.x()));
            auto yCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "y", static_cast<int>(scenePos.y()));
            m_commandManager->executeCommand(std::move(xCmd));
            m_commandManager->executeCommand(std::move(yCmd));
            --m_updatingFromGraph;
        }
    }

    event->acceptProposedAction();
}

void NodeEditorWidget::onConnectionCreated(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;

    m_pendingConnections.insert(connectionId);

    QString srcModuleId = m_nodeToModuleId.value(connectionId.outNodeId);
    QString tgtModuleId = m_nodeToModuleId.value(connectionId.inNodeId);
    if (srcModuleId.isEmpty() || tgtModuleId.isEmpty()) {
        m_pendingConnections.remove(connectionId);
        ++m_updatingFromGraph;
        m_graphModel->deleteConnection(connectionId);
        --m_updatingFromGraph;
        return;
    }

    QString srcPortId = getPortId(connectionId.outNodeId, QtNodes::PortType::Out, connectionId.outPortIndex);
    QString tgtPortId = getPortId(connectionId.inNodeId, QtNodes::PortType::In, connectionId.inPortIndex);
    if (srcPortId.isEmpty() || tgtPortId.isEmpty()) {
        m_pendingConnections.remove(connectionId);
        ++m_updatingFromGraph;
        m_graphModel->deleteConnection(connectionId);
        --m_updatingFromGraph;
        return;
    }

    PortRef source{srcModuleId, srcPortId};
    PortRef target{tgtModuleId, tgtPortId};
    if (!m_graph->isValidConnection(source, target)) {
        m_pendingConnections.remove(connectionId);
        ++m_updatingFromGraph;
        m_graphModel->deleteConnection(connectionId);
        --m_updatingFromGraph;
        return;
    }

    QString connId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
    auto connection = std::make_unique<Connection>(connId, source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
}

void NodeEditorWidget::onConnectionDeleted(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;

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
        QString moduleId = m_nodeToModuleId.value(nodeId);
        if (!moduleId.isEmpty()) {
            emit moduleSelected(moduleId);
            return;
        }
    }
    emit moduleSelected(QString());
}

QString NodeEditorWidget::getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    auto* model = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (!model || !model->module()) return "";

    unsigned int idx = 0;
    for (const auto& port : model->module()->ports()) {
        if ((portType == QtNodes::PortType::Out && port.direction() == Port::Direction::Output) ||
            (portType == QtNodes::PortType::In && port.direction() == Port::Direction::Input)) {
            if (idx == portIndex) {
                return port.id();
            }
            idx++;
        }
    }
    return "";
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

    if (paramName == "display_name") {
        if (auto* nodeGraphics = m_scene->nodeGraphicsObject(nodeIt.value())) {
            nodeGraphics->setGeometryChanged();
            nodeGraphics->update();
        }
        return;
    }

    if (paramName != "x" && paramName != "y") return;

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt == params.end() || yIt == params.end()) return;

    ++m_updatingFromGraph;
    const auto& xValue = xIt.value().value();
    const auto& yValue = yIt.value().value();
    auto xOpt = toDouble(xValue);
    auto yOpt = toDouble(yValue);
    if (xOpt && yOpt) {
        m_graphModel->setNodeData(nodeIt.value(), QtNodes::NodeRole::Position, QPointF(*xOpt, *yOpt));
    }
    --m_updatingFromGraph;
}
