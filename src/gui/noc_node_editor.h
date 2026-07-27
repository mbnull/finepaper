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
class DataFlowGraphModel;
class DataFlowGraphicsScene;
class GraphicsView;
class NodeDelegateModelRegistry;
using NodeId = unsigned int;
}

class QEvent;

namespace finepaper {

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

class NocNodeEditor final : public QWidget {
public:
    explicit NocNodeEditor(QWidget* parent = nullptr);
    ~NocNodeEditor() override;

    void setDesign(const NocDesign* design);
    void setEndpointTypes(QVector<NocEndpointTypeItem> endpointTypes);
    bool setRouterVisualPosition(const QString& routerId, QPointF position);
    std::optional<QPointF> routerVisualPosition(const QString& routerId) const;
    std::optional<QPointF> endpointVisualPosition(const QString& endpointId) const;
    bool setRouterCollapsed(const QString& routerId, bool collapsed);
    bool routerCollapsed(const QString& routerId) const;
    void zoomToFit();

    std::function<bool(const QString&, RouterPosition)> endpointTypeDropped;
    std::function<bool(const QString&, RouterPosition)> endpointMoveRequested;
    std::function<void(const QString&)> endpointRemovalRequested;
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
    };

    void rebuildGraph(bool zoomToContents = true);
    void loadWorkspaceLayout();
    void saveWorkspaceLayout() const;
    void handleNodeSelection(QtNodes::NodeId nodeId);
    void handlePointerReleased(const QPoint& viewportPosition);
    bool handleEndpointDrop(const QString& endpointType, const QPoint& viewportPosition);
    void addPendingEndpoint(const QString& endpointType, QPointF scenePosition);
    bool attachNodeToRouter(QtNodes::NodeId nodeId, RouterPosition router);
    void showContextMenu(const QPoint& viewportPosition, const QPoint& globalPosition);
    void showCanvasCreateMenu(QPointF scenePosition, const QPoint& globalPosition);
    void showNodeContextMenu(QtNodes::NodeId nodeId, const QPoint& globalPosition);
    std::optional<RouterPosition> routerAt(const QPointF& scenePosition) const;
    std::optional<QtNodes::NodeId> nodeAt(const QPoint& viewportPosition) const;
    bool portAt(const QPoint& viewportPosition) const;
    void highlightNeighborhood(QtNodes::NodeId nodeId);
    void clearNeighborhoodHighlight();

    std::optional<NocDesign> m_design;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene = nullptr;
    QtNodes::GraphicsView* m_view = nullptr;
    QHash<QtNodes::NodeId, NodeMetadata> m_metadata;
    QHash<QString, QtNodes::NodeId> m_routerNodes;
    QHash<QString, QPointF> m_routerLayout;
    QHash<QString, QPointF> m_endpointLayout;
    QSet<QString> m_collapsedRouters;
    QVector<NocEndpointTypeItem> m_endpointTypes;
    QHash<QString, PendingEndpoint> m_pendingEndpoints;
    int m_nextPendingEndpoint = 0;
    QString m_layoutKey;
};

} // namespace finepaper
