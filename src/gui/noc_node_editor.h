#pragma once

#include "noc/model.h"

#include <QHash>
#include <QPointF>
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

namespace finepaper {

struct NocEditorSelection {
    enum class Kind {
        None,
        Router,
        Endpoint
    };

    Kind kind = Kind::None;
    QString id;
    std::optional<RouterPosition> router;
};

class NocNodeEditor final : public QWidget {
public:
    explicit NocNodeEditor(QWidget* parent = nullptr);
    ~NocNodeEditor() override;

    void setDesign(const NocDesign* design);
    bool setRouterVisualPosition(const QString& routerId, QPointF position);
    std::optional<QPointF> routerVisualPosition(const QString& routerId) const;
    void zoomToFit();

    std::function<void(const QString&, RouterPosition)> endpointTypeDropped;
    std::function<void(const QString&, RouterPosition)> endpointMoveRequested;
    std::function<void(const NocEditorSelection&)> selectionChanged;

private:
    struct NodeMetadata {
        NocEditorSelection::Kind kind = NocEditorSelection::Kind::None;
        QString id;
        std::optional<RouterPosition> router;
        QPointF projectedPosition;
    };

    void rebuildGraph(bool zoomToContents = true);
    void loadRouterLayout();
    void saveRouterLayout() const;
    void handleNodeSelection(QtNodes::NodeId nodeId);
    void handlePointerReleased();
    bool handleEndpointDrop(const QString& endpointType, const QPoint& viewportPosition);
    std::optional<RouterPosition> nearestRouter(const QPointF& scenePosition) const;
    std::optional<QtNodes::NodeId> nodeAt(const QPoint& viewportPosition) const;

    std::optional<NocDesign> m_design;
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    std::unique_ptr<QtNodes::DataFlowGraphModel> m_graphModel;
    QtNodes::DataFlowGraphicsScene* m_scene = nullptr;
    QtNodes::GraphicsView* m_view = nullptr;
    QHash<QtNodes::NodeId, NodeMetadata> m_metadata;
    QHash<QString, QtNodes::NodeId> m_routerNodes;
    QHash<QString, QPointF> m_routerLayout;
    QString m_layoutKey;
};

} // namespace finepaper
