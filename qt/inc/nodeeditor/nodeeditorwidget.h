// NodeEditorWidget provides visual node editor with drag-drop and connection management
#pragma once

#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QWidget>
#include <QMap>
#include <QList>
#include <QSet>
#include <QRectF>
#include <functional>
#include "graph/graph.h"
#include "commands/commandmanager.h"

class AnimatedGraphicsView;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QContextMenuEvent;
class QMouseEvent;
class EditorGraphModel;
class GraphNodeModel;
namespace QtNodes { class ConnectionGraphicsObject; }

class NodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    // Constructs the visual editor and binds it to Graph/CommandManager.
    NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    ~NodeEditorWidget() override;
    // Returns whether auto-arrange behavior is currently enabled in the view.
    bool isArrangeEnabled() const;

public slots:
    // Highlights a module/connection in the scene when selected from external UI (for example log panel).
    void highlightElement(const QString& elementId);
    // Enables/disables auto-arrange mode and updates related interaction state.
    void setArrangeEnabled(bool enabled);

signals:
    void moduleSelected(QString moduleId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // Mirror Graph mutations into QtNodes scene objects.
    void onModuleAdded(Module* module);
    void onModuleRemoved(const QString& moduleId);
    void onConnectionAdded(Connection* connection);
    void onConnectionRemoved(const QString& connectionId);
    // Convert user-made scene connections into graph commands.
    void onConnectionCreated(QtNodes::ConnectionId connectionId);
    void onConnectionDeleted(QtNodes::ConnectionId connectionId);
    // Keep selection/parameter changes synchronized across view and side panels.
    void onSelectionChanged();
    void onNodeMoved(QtNodes::NodeId nodeId);
    void onParameterChanged(const QString& paramName);

private:
    struct ModulePresentationState {
        QList<Connection*> moduleConnections;
        QList<Connection*> attachmentConnections;
        QSet<QString> endpointModuleIds;
    };

    void ensureModuleInView(Module* module);
    void removeModuleFromView(const QString& moduleId);
    bool ensureConnectionInView(Connection* connection);
    void removeConnectionFromView(const QString& connectionId);
    GraphNodeModel* graphNodeModel(QtNodes::NodeId nodeId) const;
    void refreshNodeGraphics(QtNodes::NodeId nodeId, bool moveConnections);
    QString getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;
    bool resolveConnectionPorts(QtNodes::ConnectionId connectionId, PortRef& source, PortRef& target) const;
    QtNodes::ConnectionGraphicsObject* findDraftConnection() const;
    void setConnectionHighlighted(QtNodes::ConnectionId connectionId, bool highlighted);
    void updateConnectedConnectionHighlights(QtNodes::NodeId selectedNodeId);
    bool tryToggleCollapsed(const QPoint& viewportPos, bool requireToggleButton);
    void toggleCollapsed(const QString& moduleId, bool collapsed);
    bool resolveRouterDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                      QtNodes::NodeId targetNodeId,
                                      PortRef& source,
                                      PortRef& target) const;
    bool resolveEndpointDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                        QtNodes::NodeId targetNodeId,
                                        PortRef& source,
                                        PortRef& target) const;
    bool tryCompleteRouterDraftConnection(const QPoint& viewportPos);
    bool tryCompleteEndpointDraftConnection(const QPoint& viewportPos);
    bool tryCompleteDraftConnection(const QPoint& viewportPos,
                                    const std::function<bool(const QtNodes::ConnectionGraphicsObject&,
                                                             QtNodes::NodeId,
                                                             PortRef&,
                                                             PortRef&)>& resolver);
    void executeAddConnection(const PortRef& source, const PortRef& target);
    void syncNodePositionFromParameters(Module* module, QtNodes::NodeId nodeId);
    ModulePresentationState collectModulePresentationState(const QString& moduleId) const;
    void hideModuleConnections(const ModulePresentationState& state);
    void applyCollapsedModulePresentation(const ModulePresentationState& state);
    void applyExpandedModulePresentation(const ModulePresentationState& state);
    bool handleViewportDragEnter(QDragEnterEvent* event);
    bool handleViewportDragMove(QDragMoveEvent* event);
    bool handleViewportDrop(QDropEvent* event);
    bool handleViewportMouseRelease(QMouseEvent* event);
    bool handleViewportMousePress(QMouseEvent* event);
    bool handleViewportMouseDoubleClick(QMouseEvent* event);
    bool handleViewportContextMenu(QContextMenuEvent* event);
    bool showNodeContextMenu(const QPoint& viewportPos, const QPoint& globalPos);
    bool showCanvasCreateMenu(const QPoint& viewportPos, const QPoint& globalPos);
    bool createModuleAt(const QString& moduleType, const QPointF& scenePos);
    QPointF clampNodePosition(QtNodes::NodeId nodeId, const QPointF& position) const;
    void refreshModulePresentation(const QString& moduleId);
    void refreshAllModulePresentations();

    Graph* m_graph;
    CommandManager* m_commandManager;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    EditorGraphModel* m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene;
    AnimatedGraphicsView* m_view;
    QMap<QString, QtNodes::NodeId> m_moduleToNodeId;
    QMap<QtNodes::NodeId, QString> m_nodeToModuleId;
    QMap<QString, QtNodes::ConnectionId> m_connectionToQtId;
    QSet<QtNodes::ConnectionId> m_pendingConnections;
    QSet<QtNodes::ConnectionId> m_pendingRemovals;
    int m_updatingFromGraph = 0;
    QRectF m_canvasRect;
};
