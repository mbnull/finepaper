#pragma once

#include "noc/model.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

namespace QtNodes {
class ConnectionGraphicsObject;
class DataFlowGraphModel;
class DataFlowGraphicsScene;
class NodeDelegateModelRegistry;
struct ConnectionId;
using NodeId = unsigned int;
}

class QEvent;
class QGraphicsPathItem;

namespace finepaper {

class AnimatedGraphicsView;

struct NocEditorSelection {
    enum class Kind {
        None,
        Router,
        Endpoint,
        PendingEndpoint
    };

    Kind kind = Kind::None;
    QString id;
    std::optional<RouterPosition> router;
};

struct NocEndpointTypeItem {
    QString id;
    QString label;
};

struct NocAttachmentTarget {
    RouterPosition router;
    std::optional<QString> exactSlot;
};

struct NocRouterAttachmentPortItem {
    QString id;
    QString label;
    std::optional<QString> exactSlot;

    bool operator==(const NocRouterAttachmentPortItem&) const = default;
};

class NocNodeEditor final : public QWidget {
public:
    explicit NocNodeEditor(QWidget* parent = nullptr);
    ~NocNodeEditor() override;

    void setDesign(const NocDesign* design);
    void setEndpointTypes(QVector<NocEndpointTypeItem> endpointTypes);
    void setRouterAttachmentPorts(QVector<NocRouterAttachmentPortItem> ports);
    void setEditingEnabled(bool enabled);
    bool editingEnabled() const;
    bool setRouterVisualPosition(const QString& routerId, QPointF position);
    std::optional<QPointF> routerVisualPosition(const QString& routerId) const;
    std::optional<QPointF> endpointVisualPosition(const QString& endpointId) const;
    bool setRouterCollapsed(const QString& routerId, bool collapsed);
    bool routerCollapsed(const QString& routerId) const;
    void regularizeLayout();
    void zoomToFit();

    std::function<bool(const QString&, NocAttachmentTarget)> endpointTypeDropped;
    std::function<bool(const QString&, NocAttachmentTarget)> endpointMoveRequested;
    std::function<bool(const EndpointInstance&, NocAttachmentTarget)> detachedEndpointDropped;
    std::function<bool(const QString&)> endpointRemovalRequested;
    std::function<void(const NocEditorSelection&)> selectionChanged;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct NodeMetadata {
        NocEditorSelection::Kind kind = NocEditorSelection::Kind::None;
        QString id;
        std::optional<RouterPosition> router;
        QPointF projectedPosition;
        QString endpointType;
    };

    struct PendingEndpoint {
        QString id;
        QString type;
        QPointF scenePosition;
        std::optional<EndpointInstance> detachedEndpoint;
    };

    struct RouterEndpointDraft {
        QtNodes::NodeId routerNode = 0;
        RouterPosition router;
        unsigned int portIndex = 0;
        QPointF startScenePosition;
        QGraphicsPathItem* graphicsItem = nullptr;
    };

    struct EndpointAttachmentDraft {
        QtNodes::NodeId endpointNode = 0;
        QPointF startScenePosition;
        QGraphicsPathItem* graphicsItem = nullptr;
    };

    void rebuildGraph(bool zoomToContents = true);
    void loadWorkspaceLayout();
    void saveWorkspaceLayout() const;
    void handleNodeSelection(QtNodes::NodeId nodeId);
    void handlePointerReleased(const QPoint& viewportPosition);
    void handleConnectionCreated(QtNodes::ConnectionId connectionId);
    bool tryCompleteDraftConnection(const QPoint& viewportPosition);
    QtNodes::ConnectionGraphicsObject* findDraftConnection() const;
    bool beginRouterEndpointDraft(const QPoint& viewportPosition);
    void updateRouterEndpointDraft(const QPoint& viewportPosition);
    bool completeRouterEndpointDraft(const QPoint& viewportPosition);
    void clearRouterEndpointDraft();
    bool beginEndpointAttachmentDraft(const QPoint& viewportPosition);
    void updateEndpointAttachmentDraft(const QPoint& viewportPosition);
    bool completeEndpointAttachmentDraft(const QPoint& viewportPosition);
    void clearEndpointAttachmentDraft();
    bool handleEndpointDrop(const QString& endpointType, const QPoint& viewportPosition);
    void addPendingEndpoint(const QString& endpointType, QPointF scenePosition);
    bool attachNodeToRouter(QtNodes::NodeId nodeId, NocAttachmentTarget target);
    void detachEndpoint(QtNodes::NodeId nodeId);
    void showContextMenu(const QPoint& viewportPosition, const QPoint& globalPosition);
    void showCanvasCreateMenu(QPointF scenePosition, const QPoint& globalPosition);
    void showNodeContextMenu(QtNodes::NodeId nodeId, const QPoint& globalPosition);
    std::optional<RouterPosition> routerAt(const QPointF& scenePosition) const;
    std::optional<QtNodes::NodeId> nodeAt(const QPoint& viewportPosition) const;
    std::optional<QtNodes::NodeId> nodeAtScene(
        const QPointF& scenePosition,
        std::optional<QtNodes::NodeId> ignoredNode = std::nullopt) const;
    bool blockedPortAt(const QPoint& viewportPosition) const;
    bool isRouterAttachmentPort(unsigned int portIndex) const;
    std::optional<QString> exactSlotForPort(unsigned int portIndex) const;
    bool attachmentPortAvailable(QtNodes::NodeId routerNode,
                                 unsigned int portIndex,
                                 std::optional<QtNodes::NodeId> ignoredEndpoint = std::nullopt) const;
    std::optional<unsigned int> firstAvailableAttachmentPort(
        QtNodes::NodeId routerNode,
        std::optional<QtNodes::NodeId> ignoredEndpoint = std::nullopt) const;
    QString endpointTypeLabel(const QString& endpointType) const;
    void restoreSelection();
    void highlightNeighborhood(QtNodes::NodeId nodeId);
    void clearNeighborhoodHighlight();

    std::optional<NocDesign> m_design;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene = nullptr;
    AnimatedGraphicsView* m_view = nullptr;
    QHash<QtNodes::NodeId, NodeMetadata> m_metadata;
    QHash<QString, QtNodes::NodeId> m_routerNodes;
    QHash<QString, QPointF> m_routerLayout;
    QHash<QString, QPointF> m_endpointLayout;
    QSet<QString> m_collapsedRouters;
    QVector<NocEndpointTypeItem> m_endpointTypes;
    QVector<NocRouterAttachmentPortItem> m_routerAttachmentPorts{{
        QStringLiteral("0"), QStringLiteral("EP"), std::nullopt}};
    QHash<QString, PendingEndpoint> m_pendingEndpoints;
    std::optional<RouterEndpointDraft> m_routerEndpointDraft;
    std::optional<EndpointAttachmentDraft> m_endpointAttachmentDraft;
    int m_nextPendingEndpoint = 0;
    NocEditorSelection::Kind m_selectedKind = NocEditorSelection::Kind::None;
    QString m_selectedId;
    bool m_hasStoredCollapsedLayout = false;
    bool m_editingEnabled = true;
    QString m_layoutKey;
};

} // namespace finepaper
