#include "gui/noc_node_editor.h"

#include "gui/noc_editor_style.h"
#include "gui/workbench_config.h"

#include <QtNodes/AbstractConnectionPainter>
#include <QtNodes/AbstractNodePainter>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/StyleCollection>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>

#include <cmath>
#include <limits>
#include <unordered_set>

namespace finepaper {
namespace {

constexpr QtNodes::PortIndex kRouterWestInPort = portIndex(RouterInputPort::West);
constexpr QtNodes::PortIndex kRouterNorthInPort = portIndex(RouterInputPort::North);
constexpr QtNodes::PortIndex kRouterEndpointInPort = portIndex(RouterInputPort::Endpoint);
constexpr QtNodes::PortIndex kRouterEastOutPort = portIndex(RouterOutputPort::East);
constexpr QtNodes::PortIndex kRouterSouthOutPort = portIndex(RouterOutputPort::South);
constexpr QtNodes::PortIndex kEndpointOutPort = portIndex(EndpointOutputPort::Attachment);

class NocNodeModel final : public QtNodes::NodeDelegateModel {
public:
    QString name() const override { return QStringLiteral("FinepaperNoCNode"); }
    QString caption() const override { return m_caption; }

    unsigned int nPorts(QtNodes::PortType type) const override {
        if (m_router) {
            return type == QtNodes::PortType::In ? 3U : 2U;
        }
        return type == QtNodes::PortType::Out ? 1U : 0U;
    }

    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override {
        return {QStringLiteral("finepaper.noc-link"), QStringLiteral("NoC attachment")};
    }

    QtNodes::ConnectionPolicy portConnectionPolicy(QtNodes::PortType,
                                                    QtNodes::PortIndex) const override {
        return QtNodes::ConnectionPolicy::Many;
    }

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}
    QWidget* embeddedWidget() override { return nullptr; }

    bool isRouter() const { return m_router; }
    bool isCollapsed() const { return m_collapsed; }

    QString portCaption(QtNodes::PortType type, QtNodes::PortIndex index) const override {
        if (!m_router) {
            return QStringLiteral("EP");
        }
        if (type == QtNodes::PortType::In) {
            if (index == kRouterWestInPort) return QStringLiteral("W");
            if (index == kRouterNorthInPort) return QStringLiteral("N");
            return QStringLiteral("EP");
        }
        return index == kRouterEastOutPort ? QStringLiteral("E") : QStringLiteral("S");
    }

    bool portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override {
        return true;
    }

    void configure(QString caption, bool router, bool collapsed) {
        m_caption = std::move(caption);
        m_router = router;
        m_collapsed = router && collapsed;
        QtNodes::NodeStyle style = nodeStyle();
        style.ShadowEnabled = false;
        style.NormalBoundaryColor = QColor(QStringLiteral("#334155"));
        style.SelectedBoundaryColor = QColor(QStringLiteral("#f97316"));
        style.ConnectionPointColor = QColor(QStringLiteral("#1e293b"));
        style.FilledConnectionPointColor = QColor(QStringLiteral("#1e293b"));
        style.FontColor = QColor(QStringLiteral("#0f172a"));
        style.PenWidth = 1.4F;
        style.HoveredPenWidth = 2.0F;
        style.ConnectionPointDiameter = 8.0F;
        style.setBackgroundColor(router ? QColor(QStringLiteral("#dbeafe"))
                                        : QColor(QStringLiteral("#fef3c7")));
        setNodeStyle(style);
        emit requestNodeUpdate();
    }

private:
    QString m_caption;
    bool m_router = false;
    bool m_collapsed = false;
};

class NocNodeGeometry final : public QtNodes::AbstractNodeGeometry {
public:
    explicit NocNodeGeometry(QtNodes::AbstractGraphModel& graphModel)
        : QtNodes::AbstractNodeGeometry(graphModel) {}

    QRectF boundingRect(QtNodes::NodeId nodeId) const override {
        const QSize nodeSize = size(nodeId);
        constexpr qreal margin = 14.0;
        return QRectF(-margin, -margin,
                      nodeSize.width() + margin * 2.0,
                      nodeSize.height() + margin * 2.0);
    }

    QSize size(QtNodes::NodeId nodeId) const override {
        const NocNodeModel* model = modelFor(nodeId);
        if (!model || !model->isRouter()) {
            return nocEditorMetrics().endpointSize;
        }
        return model->isCollapsed() ? nocEditorMetrics().collapsedRouterSize
                                    : nocEditorMetrics().expandedRouterSize;
    }

    void recomputeSize(QtNodes::NodeId) const override {}

    QPointF portPosition(QtNodes::NodeId nodeId,
                         QtNodes::PortType type,
                         QtNodes::PortIndex index) const override {
        const NocNodeModel* model = modelFor(nodeId);
        const QSize nodeSize = size(nodeId);
        const qreal width = nodeSize.width();
        const qreal height = nodeSize.height();
        if (!model || !model->isRouter()) {
            return QPointF(width, height / 2.0);
        }
        if (type == QtNodes::PortType::In) {
            if (index == kRouterWestInPort) return QPointF(0.0, height / 2.0);
            if (index == kRouterNorthInPort) return QPointF(width / 2.0, 0.0);
            return QPointF(0.0, height - 32.0);
        }
        if (index == kRouterEastOutPort) return QPointF(width, height / 2.0);
        return QPointF(width / 2.0, height);
    }

    QPointF portTextPosition(QtNodes::NodeId nodeId,
                             QtNodes::PortType type,
                             QtNodes::PortIndex index) const override {
        const QPointF port = portPosition(nodeId, type, index);
        const QSize nodeSize = size(nodeId);
        if (type == QtNodes::PortType::In && index == kRouterNorthInPort) {
            return QPointF(port.x() - 6.0, 18.0);
        }
        if (type == QtNodes::PortType::Out && index == kRouterSouthOutPort) {
            return QPointF(port.x() - 6.0, nodeSize.height() - 10.0);
        }
        return QPointF(port.x() <= nodeSize.width() / 2.0 ? 12.0
                                                           : nodeSize.width() - 22.0,
                       port.y() + 4.0);
    }

    QPointF captionPosition(QtNodes::NodeId) const override {
        return QPointF(28.0, 22.0);
    }

    QRectF captionRect(QtNodes::NodeId nodeId) const override {
        const QSize nodeSize = size(nodeId);
        return QRectF(26.0, 8.0, nodeSize.width() - 52.0, 24.0);
    }

    QPointF widgetPosition(QtNodes::NodeId) const override { return {}; }
    QRect resizeHandleRect(QtNodes::NodeId) const override { return {}; }

    static QRectF collapseButtonRect(QSize nodeSize) {
        return QRectF(nodeSize.width() - 24.0, 7.0, 17.0, 17.0);
    }

private:
    const NocNodeModel* modelFor(QtNodes::NodeId nodeId) const {
        auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&_graphModel);
        return graphModel ? graphModel->delegateModel<NocNodeModel>(nodeId) : nullptr;
    }
};

class NocBlockNodePainter final : public QtNodes::AbstractNodePainter {
public:
    void paint(QPainter* painter, QtNodes::NodeGraphicsObject& node) const override {
        auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&node.graphModel());
        auto* model = graphModel
            ? graphModel->delegateModel<NocNodeModel>(node.nodeId())
            : nullptr;
        auto const& geometry = node.nodeScene()->nodeGeometry();
        const QSize size = geometry.size(node.nodeId());
        const QRectF body(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);
        const QColor background = model
            ? model->nodeStyle().backgroundColor()
            : QColor(QStringLiteral("#e2e8f0"));

        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(node.isSelected() ? QColor(QStringLiteral("#f97316"))
                                               : QColor(QStringLiteral("#334155")),
                             node.isSelected() ? 2.5 : 1.5));
        painter->setBrush(background);
        painter->drawRect(body);

        const qreal headerHeight = qMin<qreal>(32.0, body.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(model && model->isRouter()
                              ? QColor(QStringLiteral("#93c5fd"))
                              : QColor(QStringLiteral("#fcd34d")));
        painter->drawRect(QRectF(body.left(), body.top(), body.width(), headerHeight));

        painter->setPen(QColor(QStringLiteral("#0f172a")));
        painter->setFont(QFont(QStringLiteral("Sans Serif"), 9, QFont::DemiBold));
        const QRectF captionRect = model && model->isRouter()
            ? (model->isCollapsed()
                   ? body.adjusted(18.0, 32.0, -18.0, -18.0)
                   : body.adjusted(38.0, 38.0, -38.0, -38.0))
            : body.adjusted(12.0, 7.0, -12.0, -7.0);
        painter->drawText(captionRect,
                          Qt::AlignCenter | Qt::TextWordWrap,
                          model ? model->caption() : QString());

        if (model && model->isRouter()) {
            drawCollapseButton(painter, *model, size);
            painter->setPen(QPen(QColor(QStringLiteral("#64748b")), 1.0));
            painter->setBrush(Qt::NoBrush);
            const qreal inset = model->isCollapsed() ? 26.0 : 42.0;
            painter->drawRect(body.adjusted(inset, inset, -inset, -inset));
        }

        drawPorts(painter, node, model, geometry, QtNodes::PortType::In);
        drawPorts(painter, node, model, geometry, QtNodes::PortType::Out);
    }

private:
    static void drawPorts(QPainter* painter,
                          QtNodes::NodeGraphicsObject& node,
                          const NocNodeModel* model,
                          QtNodes::AbstractNodeGeometry const& geometry,
                          QtNodes::PortType type) {
        const auto count = node.graphModel().nodeData<unsigned int>(
            node.nodeId(), type == QtNodes::PortType::In
                               ? QtNodes::NodeRole::InPortCount
                               : QtNodes::NodeRole::OutPortCount);
        painter->setPen(QPen(QColor(QStringLiteral("#0f172a")), 1.0));
        painter->setBrush(QColor(QStringLiteral("#475569")));
        for (unsigned int index = 0; index < count; ++index) {
            if (model && model->isRouter() && model->isCollapsed()
                && type == QtNodes::PortType::In
                && index == kRouterEndpointInPort) {
                continue;
            }
            const QPointF center = geometry.portPosition(node.nodeId(), type, index);
            painter->drawRect(QRectF(center.x() - 4.0, center.y() - 4.0, 8.0, 8.0));

            const QString label = model ? model->portCaption(type, index) : QString();
            if (label.isEmpty()) {
                continue;
            }
            painter->setPen(QColor(QStringLiteral("#0f172a")));
            painter->setFont(QFont(QStringLiteral("Sans Serif"), 7, QFont::Bold));
            QRectF labelRect;
            const QSize nodeSize = geometry.size(node.nodeId());
            if (center.y() <= 0.5) {
                labelRect = QRectF(center.x() - 12.0, 8.0, 24.0, 16.0);
            } else if (center.y() >= nodeSize.height() - 0.5) {
                labelRect = QRectF(center.x() - 12.0,
                                   nodeSize.height() - 24.0,
                                   24.0,
                                   16.0);
            } else if (center.x() <= 0.5) {
                labelRect = QRectF(8.0, center.y() - 8.0, 28.0, 16.0);
            } else {
                labelRect = QRectF(nodeSize.width() - 36.0,
                                   center.y() - 8.0,
                                   28.0,
                                   16.0);
            }
            painter->drawText(labelRect, Qt::AlignCenter, label);
            painter->setPen(QPen(QColor(QStringLiteral("#0f172a")), 1.0));
            painter->setBrush(QColor(QStringLiteral("#475569")));
        }
    }

    static void drawCollapseButton(QPainter* painter,
                                   const NocNodeModel& model,
                                   QSize nodeSize) {
        const QRectF button = NocNodeGeometry::collapseButtonRect(nodeSize);
        painter->setPen(QPen(QColor(QStringLiteral("#334155")), 1.0));
        painter->setBrush(QColor(QStringLiteral("#f8fafc")));
        painter->drawRect(button);
        painter->setPen(QColor(QStringLiteral("#0f172a")));
        painter->setFont(QFont(QStringLiteral("Sans Serif"), 8, QFont::Bold));
        painter->drawText(button, Qt::AlignCenter,
                          model.isCollapsed() ? QStringLiteral("+") : QStringLiteral("−"));
    }
};

class NocOrthogonalConnectionPainter final : public QtNodes::AbstractConnectionPainter {
public:
    void paint(QPainter* painter,
               QtNodes::ConnectionGraphicsObject const& connection) const override {
        auto const& style = QtNodes::StyleCollection::connectionStyle();
        QColor color = style.normalColor();
        if (connection.isSelected()) {
            color = style.selectedColor();
        } else if (connection.connectionState().hovered()) {
            color = style.hoveredColor();
        }
        QPen pen(color, connection.isSelected() ? style.lineWidth() + 1.5
                                                 : style.lineWidth());
        pen.setCapStyle(Qt::SquareCap);
        pen.setJoinStyle(Qt::MiterJoin);
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(orthogonalConnectionPath(
            connection.out(), connection.in(), routeAxis(connection)));
    }

    QPainterPath getPainterStroke(
        QtNodes::ConnectionGraphicsObject const& connection) const override {
        auto const& style = QtNodes::StyleCollection::connectionStyle();
        QPainterPathStroker stroker;
        stroker.setWidth(style.lineWidth() + 10.0);
        stroker.setCapStyle(Qt::SquareCap);
        stroker.setJoinStyle(Qt::MiterJoin);
        return stroker.createStroke(
            orthogonalConnectionPath(
                connection.out(), connection.in(), routeAxis(connection)));
    }

private:
    static OrthogonalRouteAxis routeAxis(
        QtNodes::ConnectionGraphicsObject const& connection) {
        auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(
            &connection.graphModel());
        const QtNodes::ConnectionId id = connection.connectionId();
        auto* source = graphModel
            ? graphModel->delegateModel<NocNodeModel>(id.outNodeId)
            : nullptr;
        return source && source->isRouter() && id.outPortIndex == kRouterSouthOutPort
            ? OrthogonalRouteAxis::Vertical
            : OrthogonalRouteAxis::Horizontal;
    }
};

class NocGraphModel final : public QtNodes::DataFlowGraphModel {
public:
    explicit NocGraphModel(std::shared_ptr<QtNodes::NodeDelegateModelRegistry> registry)
        : QtNodes::DataFlowGraphModel(std::move(registry)) {}

    bool connectionPossible(QtNodes::ConnectionId const) const override { return false; }
    bool detachPossible(QtNodes::ConnectionId const) const override { return false; }

    bool deleteConnection(QtNodes::ConnectionId const connectionId) override {
        return m_rebuilding && QtNodes::DataFlowGraphModel::deleteConnection(connectionId);
    }

    bool deleteNode(QtNodes::NodeId const nodeId) override {
        if (!m_rebuilding) {
            return false;
        }
        return QtNodes::DataFlowGraphModel::deleteNode(nodeId);
    }

    QtNodes::NodeId addProjectedNode(QString caption,
                                     QPointF position,
                                     bool router,
                                     bool collapsed = false) {
        const QtNodes::NodeId nodeId = QtNodes::DataFlowGraphModel::addNode(
            QStringLiteral("FinepaperNoCNode"));
        if (auto* model = delegateModel<NocNodeModel>(nodeId)) {
            model->configure(std::move(caption), router, collapsed);
        }
        setNodeData(nodeId, QtNodes::NodeRole::Position, position);
        return nodeId;
    }

    void addProjectedConnection(QtNodes::ConnectionId connection) {
        QtNodes::DataFlowGraphModel::addConnection(connection);
    }

    void clearProjection() {
        const std::unordered_set<QtNodes::NodeId> nodeIds = allNodeIds();
        m_rebuilding = true;
        for (QtNodes::NodeId nodeId : nodeIds) {
            QtNodes::DataFlowGraphModel::deleteNode(nodeId);
        }
        m_rebuilding = false;
    }

private:
    bool m_rebuilding = false;
};

class NocGraphicsScene final : public QtNodes::DataFlowGraphicsScene {
public:
    using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

    QMenu* createSceneMenu(QPointF const) override { return nullptr; }
};

class NocGraphicsView final : public QtNodes::GraphicsView {
public:
    explicit NocGraphicsView(QtNodes::BasicGraphicsScene* scene, QWidget* parent = nullptr)
        : QtNodes::GraphicsView(scene, parent) {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
    }

    std::function<bool(const QString&, const QPoint&)> endpointDrop;
    std::function<void(const QPoint&)> pointerReleased;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(workbench::endpointTypeMime)) {
            event->acceptProposedAction();
            return;
        }
        QtNodes::GraphicsView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasFormat(workbench::endpointTypeMime)) {
            event->acceptProposedAction();
            return;
        }
        QtNodes::GraphicsView::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (event->mimeData()->hasFormat(workbench::endpointTypeMime) && endpointDrop) {
            const QString endpointType = QString::fromUtf8(
                event->mimeData()->data(workbench::endpointTypeMime));
            if (endpointDrop(endpointType, event->position().toPoint())) {
                event->acceptProposedAction();
                return;
            }
        }
        QtNodes::GraphicsView::dropEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        QtNodes::GraphicsView::mouseReleaseEvent(event);
        if (pointerReleased) {
            pointerReleased(event->position().toPoint());
        }
    }
};

QPointF routerScenePosition(RouterPosition position) {
    return QPointF(position.x * nocEditorMetrics().routerHorizontalSpacing,
                   position.y * nocEditorMetrics().routerVerticalSpacing);
}

} // namespace

NocNodeEditor::NocNodeEditor(QWidget* parent)
    : QWidget(parent),
      m_registry(std::make_shared<QtNodes::NodeDelegateModelRegistry>()),
      m_graphModel(std::make_unique<NocGraphModel>(m_registry)) {
    m_registry->registerModel<NocNodeModel>(QStringLiteral("NoC"));

    m_scene = new NocGraphicsScene(
        static_cast<NocGraphModel&>(*m_graphModel), this);
    m_scene->setNodeGeometry(std::make_unique<NocNodeGeometry>(*m_graphModel));
    m_scene->setNodePainter(std::make_unique<NocBlockNodePainter>());
    m_scene->setConnectionPainter(std::make_unique<NocOrthogonalConnectionPainter>());
    auto* view = new NocGraphicsView(m_scene, this);
    m_view = view;
    m_view->setScaleRange(0.25, 2.5);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    view->endpointDrop = [this](const QString& endpointType, const QPoint& position) {
        return handleEndpointDrop(endpointType, position);
    };
    view->pointerReleased = [this](const QPoint& position) {
        handlePointerReleased(position);
    };

    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeSelected,
            this, [this](QtNodes::NodeId nodeId) { handleNodeSelection(nodeId); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_scene->selectedItems().isEmpty() && selectionChanged) {
            selectionChanged({});
        }
    });
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeContextMenu,
            this, [this](QtNodes::NodeId nodeId, QPointF) {
                handleNodeContextMenu(nodeId);
            });
}

NocNodeEditor::~NocNodeEditor() {
    delete m_view;
    m_view = nullptr;
    delete m_scene;
    m_scene = nullptr;
    m_graphModel.reset();
}

void NocNodeEditor::setDesign(const NocDesign* design) {
    const QString layoutKey = design
        ? QStringLiteral("%1@%2:%3")
              .arg(design->package.id, design->package.version, design->id)
        : QString();
    if (layoutKey != m_layoutKey) {
        m_layoutKey = layoutKey;
        loadRouterLayout();
    }
    m_design = design ? std::optional<NocDesign>(*design) : std::nullopt;
    rebuildGraph();
}

bool NocNodeEditor::setRouterVisualPosition(const QString& routerId, QPointF position) {
    if (!m_routerNodes.contains(routerId)) {
        return false;
    }
    m_routerLayout.insert(routerId, position);
    saveRouterLayout();
    rebuildGraph(false);
    return true;
}

std::optional<QPointF> NocNodeEditor::routerVisualPosition(const QString& routerId) const {
    const auto iterator = m_routerNodes.constFind(routerId);
    if (iterator == m_routerNodes.constEnd()) {
        return std::nullopt;
    }
    return m_graphModel->nodeData(
        iterator.value(), QtNodes::NodeRole::Position).toPointF();
}

bool NocNodeEditor::setRouterCollapsed(const QString& routerId, bool collapsed) {
    if (!m_routerNodes.contains(routerId)) {
        return false;
    }
    if (collapsed) {
        m_collapsedRouters.insert(routerId);
    } else {
        m_collapsedRouters.remove(routerId);
    }
    saveRouterLayout();
    rebuildGraph(false);
    return true;
}

bool NocNodeEditor::routerCollapsed(const QString& routerId) const {
    return m_collapsedRouters.contains(routerId);
}

void NocNodeEditor::zoomToFit() {
    if (m_view) {
        m_view->zoomFitAll();
    }
}

void NocNodeEditor::rebuildGraph(bool zoomToContents) {
    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.clearProjection();
    m_metadata.clear();
    m_routerNodes.clear();

    if (!m_design) {
        return;
    }

    const TopologyProjection projection = projectTopology(*m_design);
    QSet<QString> projectedRouterIds;
    QHash<QString, RouterPosition> routerPositions;
    for (const RouterView& router : projection.routers) {
        projectedRouterIds.insert(router.id);
        routerPositions.insert(router.id, router.position);
        const QPointF visualPosition = m_routerLayout.value(
            router.id, routerScenePosition(router.position));
        const QtNodes::NodeId nodeId = graphModel.addProjectedNode(
            router.id,
            visualPosition,
            true,
            m_collapsedRouters.contains(router.id));
        m_routerNodes.insert(router.id, nodeId);
        m_metadata.insert(nodeId, NodeMetadata{
            NocEditorSelection::Kind::Router,
            router.id,
            router.position,
            visualPosition});
    }
    for (auto iterator = m_routerLayout.begin(); iterator != m_routerLayout.end();) {
        if (!projectedRouterIds.contains(iterator.key())) {
            iterator = m_routerLayout.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (auto iterator = m_collapsedRouters.begin(); iterator != m_collapsedRouters.end();) {
        if (!projectedRouterIds.contains(*iterator)) {
            iterator = m_collapsedRouters.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (const LinkView& link : projection.links) {
        const RouterPosition from = routerPositions.value(link.fromRouter);
        const RouterPosition to = routerPositions.value(link.toRouter);
        if (to.x > from.x) {
            graphModel.addProjectedConnection({
                m_routerNodes.value(link.fromRouter), kRouterEastOutPort,
                m_routerNodes.value(link.toRouter), kRouterWestInPort});
        } else {
            graphModel.addProjectedConnection({
                m_routerNodes.value(link.fromRouter), kRouterSouthOutPort,
                m_routerNodes.value(link.toRouter), kRouterNorthInPort});
        }
    }

    QHash<QString, int> endpointOffsets;
    for (const EndpointView& endpoint : projection.endpoints) {
        if (m_collapsedRouters.contains(endpoint.routerId)) {
            continue;
        }
        const int offset = endpointOffsets[endpoint.routerId]++;
        const QtNodes::NodeId routerNode = m_routerNodes.value(endpoint.routerId);
        const QPointF routerPosition = graphModel.nodeData(
            routerNode, QtNodes::NodeRole::Position).toPointF();
        const QPointF endpointPosition(
            routerPosition.x() - nocEditorMetrics().endpointHorizontalOffset,
            routerPosition.y() + nocEditorMetrics().endpointTopOffset
                + offset * nocEditorMetrics().endpointVerticalSpacing);
        const QString caption = QStringLiteral("%1\n%2 · slot %3")
                                    .arg(endpoint.id, endpoint.type, endpoint.slot);
        const QtNodes::NodeId endpointNode = graphModel.addProjectedNode(
            caption, endpointPosition, false, false);
        RouterPosition router;
        for (const EndpointInstance& instance : m_design->endpoints) {
            if (instance.id == endpoint.id) {
                router = instance.attachment.router;
                break;
            }
        }
        m_metadata.insert(endpointNode, NodeMetadata{
            NocEditorSelection::Kind::Endpoint,
            endpoint.id,
            router,
            endpointPosition});
        graphModel.addProjectedConnection({
            endpointNode, kEndpointOutPort,
            routerNode, kRouterEndpointInPort});
    }

    if (zoomToContents) {
        QTimer::singleShot(0, this, [this] { zoomToFit(); });
    }
}

void NocNodeEditor::handleNodeSelection(QtNodes::NodeId nodeId) {
    if (!selectionChanged) {
        return;
    }
    const auto iterator = m_metadata.constFind(nodeId);
    selectionChanged(iterator == m_metadata.constEnd()
                         ? NocEditorSelection{}
                         : NocEditorSelection{iterator->kind, iterator->id, iterator->router});
}

void NocNodeEditor::handlePointerReleased(const QPoint& viewportPosition) {
    if (!m_scene) {
        return;
    }

    const std::optional<QtNodes::NodeId> hitNodeId = nodeAt(viewportPosition);
    if (hitNodeId) {
        const auto hitMetadata = m_metadata.constFind(*hitNodeId);
        auto* node = m_scene->nodeGraphicsObject(*hitNodeId);
        if (hitMetadata != m_metadata.constEnd()
            && hitMetadata->kind == NocEditorSelection::Kind::Router
            && node) {
            const QPointF currentPosition = m_graphModel->nodeData(
                *hitNodeId, QtNodes::NodeRole::Position).toPointF();
            const bool nodeMoved = QLineF(
                currentPosition, hitMetadata->projectedPosition).length() >= 4.0;
            const QPointF scenePosition = m_view->mapToScene(viewportPosition);
            const QPointF localPosition = node->mapFromScene(scenePosition);
            const QSize nodeSize = m_scene->nodeGeometry().size(*hitNodeId);
            if (!nodeMoved
                && NocNodeGeometry::collapseButtonRect(nodeSize).contains(localPosition)) {
                setRouterCollapsed(hitMetadata->id,
                                   !routerCollapsed(hitMetadata->id));
                return;
            }
        }
    }

    const std::vector<QtNodes::NodeId> selected = m_scene->selectedNodes();
    if (selected.size() != 1) {
        return;
    }
    const QtNodes::NodeId nodeId = selected.front();
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()
        || (metadata->kind != NocEditorSelection::Kind::Router
            && metadata->kind != NocEditorSelection::Kind::Endpoint)) {
        return;
    }
    const QPointF position = m_graphModel->nodeData(
        nodeId, QtNodes::NodeRole::Position).toPointF();
    if (QLineF(position, metadata->projectedPosition).length() < 4.0) {
        return;
    }
    if (metadata->kind == NocEditorSelection::Kind::Router) {
        setRouterVisualPosition(metadata->id, position);
        return;
    }
    if (!endpointMoveRequested) {
        rebuildGraph(false);
        return;
    }
    const std::optional<RouterPosition> router = nearestRouter(position);
    if (router && (!metadata->router || *router != *metadata->router)) {
        endpointMoveRequested(metadata->id, *router);
    } else {
        rebuildGraph(false);
    }
}

void NocNodeEditor::handleNodeContextMenu(QtNodes::NodeId nodeId) {
    if (!endpointAttachmentRequested) {
        return;
    }
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router) {
        return;
    }
    endpointAttachmentRequested(*metadata->router);
}

void NocNodeEditor::loadRouterLayout() {
    m_routerLayout.clear();
    m_collapsedRouters.clear();
    if (m_layoutKey.isEmpty()) {
        return;
    }
    QSettings settings;
    const QVariantMap layouts = settings.value(workbench::routerLayoutsSetting).toMap();
    const QVariantMap layout = layouts.value(m_layoutKey).toMap();
    for (auto iterator = layout.constBegin(); iterator != layout.constEnd(); ++iterator) {
        if (iterator.value().canConvert<QPointF>()) {
            m_routerLayout.insert(iterator.key(), iterator.value().toPointF());
        }
    }
    const QVariantMap collapsedLayouts = settings.value(
        workbench::collapsedRoutersSetting).toMap();
    const QStringList collapsed = collapsedLayouts.value(m_layoutKey).toStringList();
    for (const QString& routerId : collapsed) {
        m_collapsedRouters.insert(routerId);
    }
}

void NocNodeEditor::saveRouterLayout() const {
    if (m_layoutKey.isEmpty()) {
        return;
    }
    QVariantMap layout;
    for (auto iterator = m_routerLayout.constBegin(); iterator != m_routerLayout.constEnd(); ++iterator) {
        layout.insert(iterator.key(), iterator.value());
    }
    QSettings settings;
    QVariantMap layouts = settings.value(workbench::routerLayoutsSetting).toMap();
    layouts.insert(m_layoutKey, layout);
    settings.setValue(workbench::routerLayoutsSetting, layouts);

    QVariantMap collapsedLayouts = settings.value(
        workbench::collapsedRoutersSetting).toMap();
    QStringList collapsed;
    collapsed.reserve(m_collapsedRouters.size());
    for (const QString& routerId : m_collapsedRouters) {
        collapsed.append(routerId);
    }
    collapsed.sort();
    collapsedLayouts.insert(m_layoutKey, collapsed);
    settings.setValue(workbench::collapsedRoutersSetting, collapsedLayouts);
}

bool NocNodeEditor::handleEndpointDrop(const QString& endpointType,
                                       const QPoint& viewportPosition) {
    if (!endpointTypeDropped) {
        return false;
    }
    const std::optional<QtNodes::NodeId> target = nodeAt(viewportPosition);
    if (!target) {
        return false;
    }
    const auto metadata = m_metadata.constFind(*target);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router) {
        return false;
    }
    endpointTypeDropped(endpointType, *metadata->router);
    return true;
}

std::optional<RouterPosition> NocNodeEditor::nearestRouter(const QPointF& scenePosition) const {
    if (m_routerNodes.isEmpty()) {
        return std::nullopt;
    }
    qreal bestDistance = std::numeric_limits<qreal>::max();
    std::optional<RouterPosition> best;
    for (auto iterator = m_routerNodes.constBegin(); iterator != m_routerNodes.constEnd(); ++iterator) {
        const QPointF routerPosition = m_graphModel->nodeData(
            iterator.value(), QtNodes::NodeRole::Position).toPointF();
        const qreal dx = routerPosition.x() - scenePosition.x();
        const qreal dy = routerPosition.y() - scenePosition.y();
        const qreal distance = std::hypot(dx, dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            const NodeMetadata metadata = m_metadata.value(iterator.value());
            best = metadata.router;
        }
    }
    return best;
}

std::optional<QtNodes::NodeId> NocNodeEditor::nodeAt(const QPoint& viewportPosition) const {
    if (!m_view) {
        return std::nullopt;
    }
    QGraphicsItem* item = m_view->itemAt(viewportPosition);
    while (item) {
        if (auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
            return node->nodeId();
        }
        item = item->parentItem();
    }
    return std::nullopt;
}

} // namespace finepaper
