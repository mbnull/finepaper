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
        return;
    }

    QtNodes::NodeId srcNodeId = srcNodeIt.value();
    QtNodes::NodeId tgtNodeId = tgtNodeIt.value();

    auto* srcModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(srcNodeId));
    auto* tgtModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(tgtNodeId));
    if (!srcModel || !tgtModel) return;

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

    m_updatingFromGraph = true;
    auto connId = m_graphModel->addConnection(srcNodeId, srcPortIdx, tgtNodeId, tgtPortIdx);
    m_connectionToQtId[connection->id()] = connId;
    m_updatingFromGraph = false;
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    auto it = m_connectionToQtId.find(connectionId);
    if (it != m_connectionToQtId.end()) {
        m_updatingFromGraph = true;
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

    event->acceptProposedAction();
}
