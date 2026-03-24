// NodeEditorWidget provides visual node editor with drag-drop and connection management
#pragma once

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QWidget>
#include <QMap>
#include <QSet>
#include "graph.h"
#include "commandmanager.h"

class AnimatedGraphicsView;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
namespace QtNodes { class ConnectionGraphicsObject; }

class NodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);

public slots:
    void highlightElement(const QString& elementId);

signals:
    void moduleSelected(QString moduleId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onModuleAdded(Module* module);
    void onModuleRemoved(const QString& moduleId);
    void onConnectionAdded(Connection* connection);
    void onConnectionRemoved(const QString& connectionId);
    void onConnectionCreated(QtNodes::ConnectionId connectionId);
    void onConnectionDeleted(QtNodes::ConnectionId connectionId);
    void onSelectionChanged();
    void onNodeMoved(QtNodes::NodeId nodeId);
    void onParameterChanged(const QString& paramName);

private:
    void ensureModuleInView(Module* module);
    void removeModuleFromView(const QString& moduleId);
    bool ensureConnectionInView(Connection* connection);
    void removeConnectionFromView(const QString& connectionId);
    QString getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;
    bool resolveConnectionPorts(QtNodes::ConnectionId connectionId, PortRef& source, PortRef& target) const;
    QtNodes::ConnectionGraphicsObject* findDraftConnection() const;
    bool tryToggleXpCollapsed(const QPoint& viewportPos);
    bool resolveXpRouterDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                        QtNodes::NodeId targetNodeId,
                                        PortRef& source,
                                        PortRef& target) const;
    bool tryCompleteXpRouterDraftConnection(const QPoint& viewportPos);
    void refreshXpPresentation(const QString& xpModuleId);
    void refreshAllXpPresentations();

    Graph* m_graph;
    CommandManager* m_commandManager;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel* m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene;
    AnimatedGraphicsView* m_view;
    QMap<QString, QtNodes::NodeId> m_moduleToNodeId;
    QMap<QtNodes::NodeId, QString> m_nodeToModuleId;
    QMap<QString, QtNodes::ConnectionId> m_connectionToQtId;
    QSet<QtNodes::ConnectionId> m_pendingConnections;
    QSet<QtNodes::ConnectionId> m_pendingRemovals;
    int m_updatingFromGraph = 0;
};
