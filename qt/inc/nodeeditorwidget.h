#pragma once

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QWidget>
#include <unordered_map>
#include "graph.h"
#include "commandmanager.h"

class NodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);

private slots:
    void onModuleAdded(Module* module);
    void onModuleRemoved(const QString& moduleId);
    void onConnectionAdded(Connection* connection);
    void onConnectionRemoved(const QString& connectionId);

private:
    Graph* m_graph;
    CommandManager* m_commandManager;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel* m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene;
    QtNodes::GraphicsView* m_view;
    std::unordered_map<QString, QtNodes::NodeId> m_moduleToNodeId;
};
