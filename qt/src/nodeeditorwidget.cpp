#include "nodeeditorwidget.h"
#include "graphnodemodel.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QVBoxLayout>

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
