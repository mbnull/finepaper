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

    for (const auto& module : m_graph->modules()) {
        onModuleAdded(module.get());
    }
}

void NodeEditorWidget::onModuleAdded(Module* module) {
    QtNodes::NodeId nodeId = m_graphModel->addNode("GraphNode");
    m_moduleToNodeId[module->id()] = nodeId;
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    auto it = m_moduleToNodeId.find(moduleId);
    if (it != m_moduleToNodeId.end()) {
        m_graphModel->deleteNode(it->second);
        m_moduleToNodeId.erase(it);
    }
}

void NodeEditorWidget::onConnectionAdded(Connection*) {}

void NodeEditorWidget::onConnectionRemoved(const QString&) {}

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
        module->setParameter(name, param.value);
    }

    auto command = std::make_unique<AddModuleCommand>(m_graph, std::move(module));
    m_commandManager->execute(std::move(command));

    event->acceptProposedAction();
}
