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

constexpr qreal kRouterHorizontalSpacing = 340.0;
constexpr qreal kRouterVerticalSpacing = 300.0;
constexpr qreal kEndpointHorizontalOffset = 140.0;
constexpr qreal kEndpointVerticalSpacing = 72.0;

class NocNodeModel final : public QtNodes::NodeDelegateModel {
public:
    QString name() const override { return QStringLiteral("FinepaperNoCNode"); }
    QString caption() const override { return m_caption; }

    unsigned int nPorts(QtNodes::PortType type) const override {
        return type == QtNodes::PortType::In ? m_inputPorts : m_outputPorts;
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

    void configure(QString caption, bool router) {
        m_caption = std::move(caption);
        m_router = router;
        m_inputPorts = 1;
        m_outputPorts = 1;
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
    unsigned int m_inputPorts = 0;
    unsigned int m_outputPorts = 0;
    bool m_router = false;
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

        const qreal headerHeight = qMin<qreal>(28.0, body.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(model && model->isRouter()
                              ? QColor(QStringLiteral("#93c5fd"))
                              : QColor(QStringLiteral("#fcd34d")));
        painter->drawRect(QRectF(body.left(), body.top(), body.width(), headerHeight));

        painter->setPen(QColor(QStringLiteral("#0f172a")));
        painter->setFont(QFont(QStringLiteral("Sans Serif"), 9, QFont::DemiBold));
        painter->drawText(body.adjusted(10.0, 4.0, -10.0, -4.0),
                          Qt::AlignCenter | Qt::TextWordWrap,
                          model ? model->caption() : QString());

        drawPorts(painter, node, geometry, QtNodes::PortType::In);
        drawPorts(painter, node, geometry, QtNodes::PortType::Out);
    }

private:
    static void drawPorts(QPainter* painter,
                          QtNodes::NodeGraphicsObject& node,
                          QtNodes::AbstractNodeGeometry const& geometry,
                          QtNodes::PortType type) {
        const auto count = node.graphModel().nodeData<unsigned int>(
            node.nodeId(), type == QtNodes::PortType::In
                               ? QtNodes::NodeRole::InPortCount
                               : QtNodes::NodeRole::OutPortCount);
        painter->setPen(QPen(QColor(QStringLiteral("#0f172a")), 1.0));
        painter->setBrush(QColor(QStringLiteral("#475569")));
        for (unsigned int index = 0; index < count; ++index) {
            const QPointF center = geometry.portPosition(node.nodeId(), type, index);
            painter->drawRect(QRectF(center.x() - 4.0, center.y() - 4.0, 8.0, 8.0));
        }
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
        painter->drawPath(orthogonalConnectionPath(connection.out(), connection.in()));
    }

    QPainterPath getPainterStroke(
        QtNodes::ConnectionGraphicsObject const& connection) const override {
        auto const& style = QtNodes::StyleCollection::connectionStyle();
        QPainterPathStroker stroker;
        stroker.setWidth(style.lineWidth() + 10.0);
        stroker.setCapStyle(Qt::SquareCap);
        stroker.setJoinStyle(Qt::MiterJoin);
        return stroker.createStroke(
            orthogonalConnectionPath(connection.out(), connection.in()));
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

    QtNodes::NodeId addProjectedNode(QString caption, QPointF position, bool router) {
        const QtNodes::NodeId nodeId = QtNodes::DataFlowGraphModel::addNode(
            QStringLiteral("FinepaperNoCNode"));
        if (auto* model = delegateModel<NocNodeModel>(nodeId)) {
            model->configure(std::move(caption), router);
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
    std::function<void()> pointerReleased;

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
            pointerReleased();
        }
    }
};

QPointF routerScenePosition(RouterPosition position) {
    return QPointF(position.x * kRouterHorizontalSpacing,
                   position.y * kRouterVerticalSpacing);
}

} // namespace

NocNodeEditor::NocNodeEditor(QWidget* parent)
    : QWidget(parent),
      m_registry(std::make_shared<QtNodes::NodeDelegateModelRegistry>()),
      m_graphModel(std::make_unique<NocGraphModel>(m_registry)) {
    m_registry->registerModel<NocNodeModel>(QStringLiteral("NoC"));

    m_scene = new NocGraphicsScene(
        static_cast<NocGraphModel&>(*m_graphModel), this);
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
    view->pointerReleased = [this] { handlePointerReleased(); };

    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeSelected,
            this, [this](QtNodes::NodeId nodeId) { handleNodeSelection(nodeId); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_scene->selectedItems().isEmpty() && selectionChanged) {
            selectionChanged({});
        }
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
    for (const RouterView& router : projection.routers) {
        projectedRouterIds.insert(router.id);
        const QPointF visualPosition = m_routerLayout.value(
            router.id, routerScenePosition(router.position));
        const QtNodes::NodeId nodeId = graphModel.addProjectedNode(
            router.id, visualPosition, true);
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

    for (const LinkView& link : projection.links) {
        graphModel.addProjectedConnection({
            m_routerNodes.value(link.fromRouter), 0,
            m_routerNodes.value(link.toRouter), 0});
    }

    QHash<QString, int> endpointOffsets;
    for (const EndpointView& endpoint : projection.endpoints) {
        const int offset = endpointOffsets[endpoint.routerId]++;
        const QtNodes::NodeId routerNode = m_routerNodes.value(endpoint.routerId);
        const QPointF routerPosition = graphModel.nodeData(
            routerNode, QtNodes::NodeRole::Position).toPointF();
        const QPointF endpointPosition(
            routerPosition.x() + kEndpointHorizontalOffset,
            routerPosition.y() + offset * kEndpointVerticalSpacing);
        const QString caption = QStringLiteral("%1\n%2 · slot %3")
                                    .arg(endpoint.id, endpoint.type, endpoint.slot);
        const QtNodes::NodeId endpointNode = graphModel.addProjectedNode(
            caption, endpointPosition, false);
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
        graphModel.addProjectedConnection({endpointNode, 0, routerNode, 0});
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

void NocNodeEditor::handlePointerReleased() {
    if (!m_scene) {
        return;
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

void NocNodeEditor::loadRouterLayout() {
    m_routerLayout.clear();
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
