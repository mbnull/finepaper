#include "nodeeditorwidget.h"
#include "graphnodemodel.h"
#include "moduleregistry.h"
#include "commands/addmodulecommand.h"
#include "commands/addconnectioncommand.h"
#include "commands/removeconnectioncommand.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUuid>
#include <QGraphicsItem>

NodeEditorWidget::NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {

    m_registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    m_registry->registerModel<GraphNodeModel>("GraphNode");

    m_graphModel = new QtNodes::DataFlowGraphModel(m_registry);
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);
    m_view = new QtNodes::GraphicsView(m_scene);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setAcceptDrops(true);

    connect(m_graph, &Graph::moduleAdded, this, &NodeEditorWidget::onModuleAdded);
    connect(m_graph, &Graph::moduleRemoved, this, &NodeEditorWidget::onModuleRemoved);
    connect(m_graph, &Graph::connectionAdded, this, &NodeEditorWidget::onConnectionAdded);
    connect(m_graph, &Graph::connectionRemoved, this, &NodeEditorWidget::onConnectionRemoved);

    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionCreated, this, &NodeEditorWidget::onConnectionCreated);
    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionDeleted, this, &NodeEditorWidget::onConnectionDeleted);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &NodeEditorWidget::onSelectionChanged);

    for (const auto& module : m_graph->modules()) {
        onModuleAdded(module.get());
    }
    for (const auto& connection : m_graph->connections()) {
        onConnectionAdded(connection.get());
    }
}

void NodeEditorWidget::onModuleAdded(Module* module) {
    m_updatingFromGraph = true;
    auto nodeId = m_graphModel->addNode("GraphNode");
    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (nodeModel) {
        nodeModel->setModule(module);
    }
    m_moduleToNodeId[module->id()] = nodeId;
    m_nodeToModuleId[nodeId] = module->id();
    m_updatingFromGraph = false;
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    auto it = m_moduleToNodeId.find(moduleId);
    if (it != m_moduleToNodeId.end()) {
        m_updatingFromGraph = true;
        m_nodeToModuleId.remove(it.value());
        m_graphModel->deleteNode(it.value());
        m_moduleToNodeId.erase(it);
        m_updatingFromGraph = false;
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

    if (m_pendingConnections.contains(connId)) {
        m_pendingConnections.remove(connId);
        m_connectionToQtId[connection->id()] = connId;
        return;
    }

    m_updatingFromGraph = true;
    m_graphModel->addConnection(connId);
    m_connectionToQtId[connection->id()] = connId;
    m_updatingFromGraph = false;
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    auto it = m_connectionToQtId.find(connectionId);
    if (it != m_connectionToQtId.end()) {
        m_updatingFromGraph = true;
        m_pendingRemovals.insert(it.value());
        m_graphModel->deleteConnection(it.value());
        m_connectionToQtId.erase(it);
        m_updatingFromGraph = false;
    }
}

void NodeEditorWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-moduletype")) {
        event->acceptProposedAction();
    }
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

    QString moduleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto module = std::make_unique<Module>(moduleId, moduleType);

    for (const auto& port : type->defaultPorts) {
        module->addPort(port);
    }
    for (const auto& [name, param] : type->defaultParameters) {
        module->setParameter(name, param.value());
    }

    auto command = std::make_unique<AddModuleCommand>(m_graph, std::move(module));
    m_commandManager->executeCommand(std::move(command));

    if (m_moduleToNodeId.contains(moduleId)) {
        QPointF scenePos = m_view->mapToScene(event->pos());
        auto nodeId = m_moduleToNodeId.value(moduleId);
        m_scene->setNodePosition(nodeId, scenePos);
    }

    event->acceptProposedAction();
}

void NodeEditorWidget::onConnectionCreated(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph) return;

    m_pendingConnections.insert(connectionId);

    QString srcModuleId = m_nodeToModuleId.value(connectionId.outNodeId);
    QString tgtModuleId = m_nodeToModuleId.value(connectionId.inNodeId);
    if (srcModuleId.isEmpty() || tgtModuleId.isEmpty()) {
        m_pendingConnections.remove(connectionId);
        m_updatingFromGraph = true;
        m_graphModel->deleteConnection(connectionId);
        m_updatingFromGraph = false;
        return;
    }

    QString srcPortId = getPortId(connectionId.outNodeId, QtNodes::PortType::Out, connectionId.outPortIndex);
    QString tgtPortId = getPortId(connectionId.inNodeId, QtNodes::PortType::In, connectionId.inPortIndex);
    if (srcPortId.isEmpty() || tgtPortId.isEmpty()) {
        m_pendingConnections.remove(connectionId);
        m_updatingFromGraph = true;
        m_graphModel->deleteConnection(connectionId);
        m_updatingFromGraph = false;
        return;
    }

    PortRef source{srcModuleId, srcPortId};
    PortRef target{tgtModuleId, tgtPortId};
    if (!m_graph->isValidConnection(source, target)) {
        m_pendingConnections.remove(connectionId);
        m_updatingFromGraph = true;
        m_graphModel->deleteConnection(connectionId);
        m_updatingFromGraph = false;
        return;
    }

    QString connId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto connection = std::make_unique<Connection>(connId, source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
}

void NodeEditorWidget::onConnectionDeleted(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph) return;

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
