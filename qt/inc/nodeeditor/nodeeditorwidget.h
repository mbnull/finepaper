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
#include <QPointF>
#include <QSize>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include "connection/connectionruleservice.h"
#include "graph/graph.h"

class ActiveWorkspaceController;
class AnimatedGraphicsView;
class DesignEditingService;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QContextMenuEvent;
class QMimeData;
class QMouseEvent;
class EditorGraphModel;
class GraphNodeModel;
class ConnectionRuleService;
class ProjectStateService;
namespace QtNodes { class ConnectionGraphicsObject; }

class NodeEditorWidget : public QWidget {
    Q_OBJECT

public:
    // Constructs the visual editor and binds scene intents to durable design edits.
    NodeEditorWidget(Graph* graph,
                     ProjectStateService* projectStateService,
                     ActiveWorkspaceController* workspaceController,
                     DesignEditingService* designEditingService,
                     QWidget* parent = nullptr);
    ~NodeEditorWidget() override;
    // Returns whether auto-arrange behavior is currently enabled in the view.
    bool isArrangeEnabled() const;
    QStringList availableCreateModuleTypes() const;
    QStringList visibleModuleIds() const;

public slots:
    // Highlights a module/connection in the scene when selected from external UI (for example log panel).
    void highlightElement(const QString& elementId);
    // Enables/disables auto-arrange mode and updates related interaction state.
    void setArrangeEnabled(bool enabled);

signals:
    void moduleSelected(QString moduleId);
    void connectionSelected(QString connectionId);

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

    struct ResizeInteraction {
        bool active = false;
        QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
        QString moduleId;
        QPointF pressScenePos;
        QSize startSize;
        QSize currentSize;
        bool hadWidth = false;
        bool hadHeight = false;
        Parameter::Value oldWidth;
        Parameter::Value oldHeight;
    };

    struct ScopedModulePayload {
        QString ipcoreId;
        QString instanceId;
        QString moduleType;
    };

    void ensureModuleInView(Module* module);
    void removeModuleFromView(const QString& moduleId);
    bool ensureConnectionInView(Connection* connection);
    void removeConnectionFromView(const QString& connectionId);
    GraphNodeModel* graphNodeModel(QtNodes::NodeId nodeId);
    const GraphNodeModel* graphNodeModel(QtNodes::NodeId nodeId) const;
    void refreshNodeGraphics(QtNodes::NodeId nodeId, bool moveConnections);
    QString portIdFor(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;
    bool resolveConnectionPorts(QtNodes::ConnectionId connectionId, PortRef& source, PortRef& target) const;
    QtNodes::ConnectionGraphicsObject* findDraftConnection();
    void refreshConnectionRuleService();
    void setConnectionHighlighted(QtNodes::ConnectionId connectionId, bool highlighted);
    void updateConnectedConnectionHighlights(QtNodes::NodeId selectedNodeId);
    bool tryToggleCollapsed(const QPoint& viewportPos, bool requireToggleButton);
    void toggleCollapsed(const QString& moduleId, bool collapsed);
    bool tryCompleteDraftConnection(const QPoint& viewportPos);
    ConnectionRequest draftConnectionRequest(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                             QtNodes::NodeId targetNodeId,
                                             const QPointF& scenePos) const;
    void showConnectionOptionsMenu(const QPoint& viewportPos,
                                   const QVector<ConnectionResolvedOption>& options);
    void executeAddConnection(const ConnectionResolvedOption& option);
    void syncNodePositionFromParameters(Module* module, QtNodes::NodeId nodeId);
    ModulePresentationState collectModulePresentationState(const QString& moduleId) const;
    void hideModuleConnections(const ModulePresentationState& state);
    void applyCollapsedModulePresentation(const ModulePresentationState& state);
    void applyExpandedModulePresentation(const ModulePresentationState& state);
    void positionAttachedEndpoint(Connection* connection);
    bool handleViewportDragEnter(QDragEnterEvent* event);
    bool handleViewportDragMove(QDragMoveEvent* event);
    bool handleViewportDrop(QDropEvent* event);
    bool handleViewportMouseRelease(QMouseEvent* event);
    bool handleViewportMousePress(QMouseEvent* event);
    bool handleViewportMouseMove(QMouseEvent* event);
    bool handleViewportMouseDoubleClick(QMouseEvent* event);
    bool handleViewportContextMenu(QContextMenuEvent* event);
    bool showNodeContextMenu(const QPoint& viewportPos, const QPoint& globalPos);
    bool showCanvasCreateMenu(const QPoint& viewportPos, const QPoint& globalPos);
    std::optional<ScopedModulePayload> scopedModulePayload(const QMimeData* mimeData) const;
    bool acceptsScopedModulePayload(const ScopedModulePayload& payload) const;
    bool createModuleAt(const ScopedModulePayload& payload, const QPointF& scenePos);
    QString activeIpcoreId() const;
    QString activeInstanceId() const;
    bool moduleBelongsToActiveWorkspace(const Module* module) const;
    bool connectionBelongsToActiveWorkspace(const Connection* connection) const;
    void refreshVisibleGraphState();
    QPointF clampNodePosition(QtNodes::NodeId nodeId, const QPointF& position) const;
    QSize minimumNodeSize(QtNodes::NodeId nodeId) const;
    bool tryBeginNodeResize(const QPoint& viewportPos);
    void updateNodeResize(const QPoint& viewportPos);
    void finishNodeResize();
    void cancelNodeResize();
    void applyTransientNodeSize(const QString& moduleId, QtNodes::NodeId nodeId, QSize const& size);
    void restoreResizeParameters(Module* module);
    void refreshModulePresentation(const QString& moduleId);
    void refreshAllModulePresentations();

    Graph* m_graph;
    ProjectStateService* m_projectStateService;
    ActiveWorkspaceController* m_workspaceController;
    DesignEditingService* m_designEditingService;
    std::unique_ptr<ConnectionRuleService> m_connectionRuleService;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    std::unique_ptr<EditorGraphModel> m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene;
    AnimatedGraphicsView* m_view;
    QMap<QString, QtNodes::NodeId> m_moduleToNodeId;
    QMap<QtNodes::NodeId, QString> m_nodeToModuleId;
    QMap<QString, QtNodes::ConnectionId> m_connectionToQtId;
    QSet<QtNodes::ConnectionId> m_pendingConnections;
    QSet<QtNodes::ConnectionId> m_pendingRemovals;
    ResizeInteraction m_resize;
    int m_updatingFromGraph = 0;
    QRectF m_canvasRect;
};
