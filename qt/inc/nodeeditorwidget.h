#pragma once

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QWidget>
#include <QMap>
#include "graph.h"
#include "commandmanager.h"

class NodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);

signals:
    void moduleSelected(QString moduleId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onModuleAdded(Module* module);
    void onModuleRemoved(const QString& moduleId);
    void onConnectionAdded(Connection* connection);
    void onConnectionRemoved(const QString& connectionId);
    void onConnectionCreated(QtNodes::ConnectionId connectionId);
    void onConnectionDeleted(QtNodes::ConnectionId connectionId);
    void onSelectionChanged();

private:
    QString getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;

    Graph* m_graph;
    CommandManager* m_commandManager;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel* m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene;
    QtNodes::GraphicsView* m_view;
    QMap<QString, QtNodes::NodeId> m_moduleToNodeId;
    QMap<QtNodes::NodeId, QString> m_nodeToModuleId;
    QMap<QString, QtNodes::ConnectionId> m_connectionToQtId;
    bool m_updatingFromGraph = false;
};
