#include "nodeeditorwidget.h"
#include "graphnodemodel.h"
#include <QVBoxLayout>

NodeEditorWidget::NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {

    m_graphModel = new QtNodes::DataFlowGraphModel();
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
    auto nodeModel = std::make_unique<GraphNodeModel>(module);
    m_graphModel->addNode(std::move(nodeModel));
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    // Find and remove node by module ID
    for (auto nodeId : m_graphModel->allNodeIds()) {
        auto* node = m_graphModel->delegateModel<GraphNodeModel>(nodeId);
        if (node && node->module()->id() == moduleId) {
            m_graphModel->deleteNode(nodeId);
            break;
        }
    }
}

void NodeEditorWidget::onConnectionAdded(Connection* connection) {
    // Minimal connection handling - QtNodes manages connections internally
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    // Minimal connection handling - QtNodes manages connections internally
}
