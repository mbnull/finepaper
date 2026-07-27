#include "gui/noc_node_editor.h"

#include "gui/animated_graphics_view.h"
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
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QMenu>
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

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace finepaper {
namespace {

constexpr QtNodes::PortIndex kRouterWestInPort = portIndex(RouterInputPort::West);
constexpr QtNodes::PortIndex kRouterNorthInPort = portIndex(RouterInputPort::North);
constexpr QtNodes::PortIndex kRouterEndpointInPort = portIndex(RouterInputPort::Endpoint);
constexpr QtNodes::PortIndex kRouterEastOutPort = portIndex(RouterOutputPort::East);
constexpr QtNodes::PortIndex kRouterSouthOutPort = portIndex(RouterOutputPort::South);
constexpr QtNodes::PortIndex kEndpointOutPort = portIndex(EndpointOutputPort::Attachment);

struct DraftConnectionStart {
    bool startFromOutput = false;
    QtNodes::NodeId nodeId = QtNodes::InvalidNodeId;
    QtNodes::PortIndex portIndex = QtNodes::InvalidPortIndex;
};

std::optional<DraftConnectionStart> resolveDraftConnectionStart(
    const QtNodes::ConnectionGraphicsObject& draftConnection) {
    const QtNodes::PortType requiredPort =
        draftConnection.connectionState().requiredPort();
    if (requiredPort == QtNodes::PortType::None) {
        return std::nullopt;
    }
    const QtNodes::ConnectionId connection = draftConnection.connectionId();
    const bool startFromOutput = requiredPort == QtNodes::PortType::In;
    const QtNodes::NodeId nodeId = startFromOutput
        ? connection.outNodeId
        : connection.inNodeId;
    const QtNodes::PortIndex portIndex = startFromOutput
        ? connection.outPortIndex
        : connection.inPortIndex;
    if (nodeId == QtNodes::InvalidNodeId
        || portIndex == QtNodes::InvalidPortIndex) {
        return std::nullopt;
    }
    return DraftConnectionStart{startFromOutput, nodeId, portIndex};
}

class NocNodeModel final : public QtNodes::NodeDelegateModel {
public:
    QString name() const override { return QStringLiteral("FinepaperNoCNode"); }
    QString caption() const override { return m_caption; }

    unsigned int nPorts(QtNodes::PortType type) const override {
        if (m_router) {
            return type == QtNodes::PortType::In
                ? kRouterEndpointInPort + m_attachmentPortLabels.size()
                : 2U;
        }
        return type == QtNodes::PortType::Out ? 1U : 0U;
    }

    QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override {
        return {QStringLiteral("finepaper.noc-link"), QStringLiteral("NoC attachment")};
    }

    QtNodes::ConnectionPolicy portConnectionPolicy(QtNodes::PortType,
                                                    QtNodes::PortIndex) const override {
        return QtNodes::ConnectionPolicy::One;
    }

    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override {}
    QWidget* embeddedWidget() override { return nullptr; }

    bool isRouter() const { return m_router; }
    bool isCollapsed() const { return m_collapsed; }
    bool isPending() const { return m_pending; }

    QString portCaption(QtNodes::PortType type, QtNodes::PortIndex index) const override {
        if (!m_router) {
            return QStringLiteral("EP");
        }
        if (type == QtNodes::PortType::In) {
            if (index == kRouterWestInPort) return QStringLiteral("W");
            if (index == kRouterNorthInPort) return QStringLiteral("N");
            return m_attachmentPortLabels.value(
                static_cast<qsizetype>(index - kRouterEndpointInPort),
                QStringLiteral("EP%1").arg(index - kRouterEndpointInPort));
        }
        return index == kRouterEastOutPort ? QStringLiteral("E") : QStringLiteral("S");
    }

    bool portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override {
        return true;
    }

    void configure(QString caption,
                   bool router,
                   bool collapsed,
                   bool pending,
                   QStringList attachmentPortLabels = {}) {
        m_caption = std::move(caption);
        m_router = router;
        m_collapsed = router && collapsed;
        m_pending = !router && pending;
        m_attachmentPortLabels = std::move(attachmentPortLabels);
        if (m_router && m_attachmentPortLabels.isEmpty()) {
            m_attachmentPortLabels.append(QStringLiteral("EP"));
        }
        QtNodes::NodeStyle style = nodeStyle();
        style.ShadowEnabled = false;
        style.NormalBoundaryColor = m_pending
            ? QColor(QStringLiteral("#2563eb"))
            : QColor(QStringLiteral("#334155"));
        style.SelectedBoundaryColor = QColor(QStringLiteral("#f97316"));
        style.ConnectionPointColor = QColor(QStringLiteral("#1e293b"));
        style.FilledConnectionPointColor = QColor(QStringLiteral("#1e293b"));
        style.FontColor = QColor(QStringLiteral("#0f172a"));
        style.PenWidth = 1.4F;
        style.HoveredPenWidth = 2.0F;
        style.ConnectionPointDiameter = 8.0F;
        style.setBackgroundColor(router
                                     ? QColor(QStringLiteral("#dbeafe"))
                                     : m_pending
                                           ? QColor(QStringLiteral("#e0f2fe"))
                                           : QColor(QStringLiteral("#fef3c7")));
        setNodeStyle(style);
        emit requestNodeUpdate();
    }

    unsigned int attachmentPortCount() const {
        return static_cast<unsigned int>(m_attachmentPortLabels.size());
    }

private:
    QString m_caption;
    bool m_router = false;
    bool m_collapsed = false;
    bool m_pending = false;
    QStringList m_attachmentPortLabels;
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
            const unsigned int count = std::max(1U, model->attachmentPortCount());
            const unsigned int attachmentIndex = index - kRouterEndpointInPort;
            const qreal top = height * 0.60;
            const qreal bottom = height - 14.0;
            const qreal y = top + (static_cast<qreal>(attachmentIndex) + 0.5)
                * (bottom - top) / static_cast<qreal>(count);
            return QPointF(0.0, y);
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
        const bool relatedHighlighted = node.data(relatedHighlightDataRole).toBool();
        QPen boundaryPen(node.isSelected()
                                 ? QColor(QStringLiteral("#f97316"))
                                 : relatedHighlighted
                                       ? QColor(QStringLiteral("#2563eb"))
                                       : model && model->isPending()
                                             ? QColor(QStringLiteral("#2563eb"))
                                             : QColor(QStringLiteral("#334155")),
                         node.isSelected() || relatedHighlighted ? 2.5 : 1.5);
        if (model && model->isPending()) {
            boundaryPen.setStyle(Qt::DashLine);
        }
        painter->setPen(boundaryPen);
        painter->setBrush(background);
        painter->drawRect(body);

        const qreal headerHeight = qMin<qreal>(32.0, body.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(model && model->isRouter()
                              ? QColor(QStringLiteral("#93c5fd"))
                              : model && model->isPending()
                                    ? QColor(QStringLiteral("#7dd3fc"))
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
        const bool relatedHighlighted = connection.data(
            relatedHighlightDataRole).toBool();
        if (relatedHighlighted) {
            color = QColor(QStringLiteral("#f97316"));
        } else if (connection.isSelected()) {
            color = style.selectedColor();
        } else if (connection.connectionState().hovered()) {
            color = style.hoveredColor();
        }
        QPen pen(color, relatedHighlighted || connection.isSelected()
                            ? style.lineWidth() + 2.0
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

    std::function<bool(const QtNodes::ConnectionId&)> userConnectionPossible;

    bool connectionPossible(QtNodes::ConnectionId const connectionId) const override {
        if (m_projectionMutation) {
            return QtNodes::DataFlowGraphModel::connectionPossible(connectionId);
        }
        return userConnectionPossible
            && userConnectionPossible(connectionId)
            && QtNodes::DataFlowGraphModel::connectionPossible(connectionId);
    }
    bool detachPossible(QtNodes::ConnectionId const) const override { return false; }

    bool deleteConnection(QtNodes::ConnectionId const connectionId) override {
        return m_projectionMutation
            && QtNodes::DataFlowGraphModel::deleteConnection(connectionId);
    }

    bool deleteNode(QtNodes::NodeId const nodeId) override {
        if (!m_projectionMutation) {
            return false;
        }
        return QtNodes::DataFlowGraphModel::deleteNode(nodeId);
    }

    QtNodes::NodeId addProjectedNode(QString caption,
                                     QPointF position,
                                     bool router,
                                     bool collapsed = false,
                                     bool pending = false,
                                     QStringList attachmentPortLabels = {}) {
        const QtNodes::NodeId nodeId = QtNodes::DataFlowGraphModel::addNode(
            QStringLiteral("FinepaperNoCNode"));
        if (auto* model = delegateModel<NocNodeModel>(nodeId)) {
            model->configure(std::move(caption),
                             router,
                             collapsed,
                             pending,
                             std::move(attachmentPortLabels));
        }
        setNodeData(nodeId, QtNodes::NodeRole::Position, position);
        return nodeId;
    }

    void addProjectedConnection(QtNodes::ConnectionId connection) {
        QtNodes::DataFlowGraphModel::addConnection(connection);
    }

    bool projectionMutation() const { return m_projectionMutation; }

    void beginProjectionMutation() { m_projectionMutation = true; }
    void endProjectionMutation() { m_projectionMutation = false; }

    void clearProjection() {
        const std::unordered_set<QtNodes::NodeId> nodeIds = allNodeIds();
        m_projectionMutation = true;
        for (QtNodes::NodeId nodeId : nodeIds) {
            QtNodes::DataFlowGraphModel::deleteNode(nodeId);
        }
        m_projectionMutation = false;
    }

private:
    bool m_projectionMutation = false;
};

class NocGraphicsScene final : public QtNodes::DataFlowGraphicsScene {
public:
    using QtNodes::DataFlowGraphicsScene::DataFlowGraphicsScene;

    QMenu* createSceneMenu(QPointF const) override { return nullptr; }
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
    m_view = new AnimatedGraphicsView(m_scene, this);
    m_view->setScaleRange(0.25, 2.5);
    m_view->setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);
    m_view->viewport()->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.userConnectionPossible = [this](const QtNodes::ConnectionId& connection) {
        const bool hasSource = connection.outNodeId != QtNodes::InvalidNodeId;
        const bool hasTarget = connection.inNodeId != QtNodes::InvalidNodeId;
        const auto source = m_metadata.constFind(connection.outNodeId);
        const auto target = m_metadata.constFind(connection.inNodeId);
        if (hasSource && source == m_metadata.constEnd()) {
            return false;
        }
        if (hasTarget && target == m_metadata.constEnd()) {
            return false;
        }

        // QtNodes asks whether an incomplete draft is permitted before the
        // opposite port exists.  Accept only the two valid starting handles;
        // the full Endpoint -> Router EP rule is checked once both ends exist.
        if (!hasSource) {
            return hasTarget
                && target->kind == NocEditorSelection::Kind::Router
                && isRouterAttachmentPort(connection.inPortIndex);
        }
        if (!hasTarget) {
            const bool sourceIsEndpoint =
                source->kind == NocEditorSelection::Kind::Endpoint
                || source->kind == NocEditorSelection::Kind::PendingEndpoint;
            return sourceIsEndpoint && connection.outPortIndex == kEndpointOutPort;
        }
        const bool sourceIsEndpoint =
            source->kind == NocEditorSelection::Kind::Endpoint
            || source->kind == NocEditorSelection::Kind::PendingEndpoint;
        return sourceIsEndpoint
            && connection.outPortIndex == kEndpointOutPort
            && target->kind == NocEditorSelection::Kind::Router
            && isRouterAttachmentPort(connection.inPortIndex);
    };

    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeSelected,
            this, [this](QtNodes::NodeId nodeId) { handleNodeSelection(nodeId); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (!m_scene->selectedItems().isEmpty()) {
            return;
        }
        const auto& graphModel = static_cast<const NocGraphModel&>(*m_graphModel);
        if (graphModel.projectionMutation()) {
            return;
        }
        m_selectedKind = NocEditorSelection::Kind::None;
        m_selectedId.clear();
        clearNeighborhoodHighlight();
        if (selectionChanged) {
            selectionChanged({});
        }
    });
    connect(m_graphModel.get(), &QtNodes::AbstractGraphModel::connectionCreated,
            this, [this](QtNodes::ConnectionId connectionId) {
                handleConnectionCreated(connectionId);
            });
}

NocNodeEditor::~NocNodeEditor() {
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    if (m_view && m_view->viewport()) {
        m_view->viewport()->removeEventFilter(this);
    }
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
        loadWorkspaceLayout();
        m_pendingEndpoints.clear();
        m_selectedKind = NocEditorSelection::Kind::None;
        m_selectedId.clear();
    }
    m_design = design ? std::optional<NocDesign>(*design) : std::nullopt;
    rebuildGraph();
}

void NocNodeEditor::setEndpointTypes(QVector<NocEndpointTypeItem> endpointTypes) {
    m_endpointTypes = std::move(endpointTypes);
}

void NocNodeEditor::setRouterAttachmentPorts(
    QVector<NocRouterAttachmentPortItem> ports) {
    ports.erase(std::remove_if(ports.begin(), ports.end(), [](const auto& port) {
        return port.id.trimmed().isEmpty();
    }), ports.end());
    if (ports.isEmpty()) {
        ports.append({QStringLiteral("0"), QStringLiteral("EP")});
    }
    if (m_routerAttachmentPorts == ports) {
        return;
    }
    m_routerAttachmentPorts = std::move(ports);
    if (m_design) {
        rebuildGraph(false);
    }
}

bool NocNodeEditor::setRouterVisualPosition(const QString& routerId, QPointF position) {
    if (!m_routerNodes.contains(routerId)) {
        return false;
    }
    m_routerLayout.insert(routerId, position);
    saveWorkspaceLayout();
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

std::optional<QPointF> NocNodeEditor::endpointVisualPosition(
    const QString& endpointId) const {
    for (auto iterator = m_metadata.constBegin(); iterator != m_metadata.constEnd(); ++iterator) {
        if (iterator->kind == NocEditorSelection::Kind::Endpoint
            && iterator->id == endpointId) {
            return m_graphModel->nodeData(
                iterator.key(), QtNodes::NodeRole::Position).toPointF();
        }
    }
    return std::nullopt;
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
    saveWorkspaceLayout();
    rebuildGraph(false);
    return true;
}

bool NocNodeEditor::routerCollapsed(const QString& routerId) const {
    return m_collapsedRouters.contains(routerId);
}

void NocNodeEditor::regularizeLayout() {
    m_routerLayout.clear();
    m_endpointLayout.clear();
    saveWorkspaceLayout();
    rebuildGraph(false);
}

void NocNodeEditor::zoomToFit() {
    if (m_view) {
        m_view->zoomFitAll();
    }
}

void NocNodeEditor::rebuildGraph(bool zoomToContents) {
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.clearProjection();
    m_metadata.clear();
    m_routerNodes.clear();

    if (!m_design) {
        return;
    }
    graphModel.beginProjectionMutation();

    const TopologyProjection projection = projectTopology(*m_design);
    if (!m_hasStoredCollapsedLayout) {
        for (const RouterView& router : projection.routers) {
            m_collapsedRouters.insert(router.id);
        }
        m_hasStoredCollapsedLayout = true;
        saveWorkspaceLayout();
    }
    QStringList attachmentPortLabels;
    attachmentPortLabels.reserve(m_routerAttachmentPorts.size());
    for (const NocRouterAttachmentPortItem& port : m_routerAttachmentPorts) {
        attachmentPortLabels.append(port.label.trimmed().isEmpty()
                                        ? QStringLiteral("EP")
                                        : port.label);
    }
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
            m_collapsedRouters.contains(router.id),
            false,
            attachmentPortLabels);
        m_routerNodes.insert(router.id, nodeId);
        m_metadata.insert(nodeId, NodeMetadata{
            NocEditorSelection::Kind::Router,
            router.id,
            router.position,
            visualPosition,
            {}});
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

    QSet<QString> designEndpointIds;
    for (const EndpointInstance& endpoint : m_design->endpoints) {
        designEndpointIds.insert(endpoint.id);
    }
    bool endpointLayoutPruned = false;
    for (auto iterator = m_endpointLayout.begin(); iterator != m_endpointLayout.end();) {
        if (!designEndpointIds.contains(iterator.key())) {
            iterator = m_endpointLayout.erase(iterator);
            endpointLayoutPruned = true;
        } else {
            ++iterator;
        }
    }
    if (endpointLayoutPruned) {
        saveWorkspaceLayout();
    }

    QHash<QString, int> endpointOffsets;
    QHash<QString, QSet<int>> usedAttachmentPortOffsets;
    for (const EndpointView& endpoint : projection.endpoints) {
        if (m_collapsedRouters.contains(endpoint.routerId)) {
            continue;
        }
        const int offset = endpointOffsets[endpoint.routerId]++;
        const QtNodes::NodeId routerNode = m_routerNodes.value(endpoint.routerId);
        const QPointF routerPosition = graphModel.nodeData(
            routerNode, QtNodes::NodeRole::Position).toPointF();
        const QPointF defaultEndpointPosition(
            routerPosition.x() - nocEditorMetrics().endpointHorizontalOffset,
            routerPosition.y() + nocEditorMetrics().endpointTopOffset
                + offset * nocEditorMetrics().endpointVerticalSpacing);
        const QPointF endpointPosition = m_endpointLayout.value(
            endpoint.id, defaultEndpointPosition);
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
            endpointPosition,
            endpoint.type});
        int attachmentOffset = -1;
        for (qsizetype index = 0; index < m_routerAttachmentPorts.size(); ++index) {
            if (m_routerAttachmentPorts.at(index).id == endpoint.slot) {
                attachmentOffset = static_cast<int>(index);
                break;
            }
        }
        QSet<int>& usedOffsets = usedAttachmentPortOffsets[endpoint.routerId];
        if (attachmentOffset < 0 || usedOffsets.contains(attachmentOffset)) {
            for (int index = 0; index < m_routerAttachmentPorts.size(); ++index) {
                if (!usedOffsets.contains(index)) {
                    attachmentOffset = index;
                    break;
                }
            }
        }
        if (attachmentOffset < 0 || attachmentOffset >= m_routerAttachmentPorts.size()) {
            continue;
        }
        usedOffsets.insert(attachmentOffset);
        graphModel.addProjectedConnection({
            endpointNode, kEndpointOutPort,
            routerNode,
            kRouterEndpointInPort + static_cast<QtNodes::PortIndex>(attachmentOffset)});
    }

    for (const PendingEndpoint& pending : std::as_const(m_pendingEndpoints)) {
        QString label = pending.type;
        for (const NocEndpointTypeItem& type : std::as_const(m_endpointTypes)) {
            if (type.id == pending.type) {
                label = type.label;
                break;
            }
        }
        const QtNodes::NodeId nodeId = graphModel.addProjectedNode(
            QStringLiteral("Unattached\n%1").arg(label),
            pending.scenePosition,
            false,
            false,
            true);
        m_metadata.insert(nodeId, NodeMetadata{
            NocEditorSelection::Kind::PendingEndpoint,
            pending.id,
            std::nullopt,
            pending.scenePosition,
            pending.type});
    }

    graphModel.endProjectionMutation();
    restoreSelection();

    if (zoomToContents) {
        QTimer::singleShot(0, this, [this] { zoomToFit(); });
    }
}

bool NocNodeEditor::eventFilter(QObject* watched, QEvent* event) {
    if (!m_view || watched != m_view->viewport()) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto* drag = static_cast<QDragEnterEvent*>(event);
        if (!drag->mimeData()->hasFormat(workbench::endpointTypeMime)) {
            return false;
        }
        const QString endpointType = QString::fromUtf8(
            drag->mimeData()->data(workbench::endpointTypeMime));
        const QPoint position = drag->position().toPoint();
        m_view->beginEndpointDrag(
            position,
            endpointTypeLabel(endpointType),
            routerAt(m_view->mapToScene(position)).has_value());
        drag->acceptProposedAction();
        return true;
    }
    case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (!drag->mimeData()->hasFormat(workbench::endpointTypeMime)) {
            return false;
        }
        const QString endpointType = QString::fromUtf8(
            drag->mimeData()->data(workbench::endpointTypeMime));
        const QPoint position = drag->position().toPoint();
        m_view->updateEndpointDrag(
            position,
            endpointTypeLabel(endpointType),
            routerAt(m_view->mapToScene(position)).has_value());
        drag->acceptProposedAction();
        return true;
    }
    case QEvent::DragLeave:
        m_view->endEndpointDrag();
        return true;
    case QEvent::Drop: {
        auto* drop = static_cast<QDropEvent*>(event);
        if (!drop->mimeData()->hasFormat(workbench::endpointTypeMime)) {
            return false;
        }
        const QString endpointType = QString::fromUtf8(
            drop->mimeData()->data(workbench::endpointTypeMime));
        const QPoint position = drop->position().toPoint();
        m_view->updateEndpointDrag(
            position,
            endpointTypeLabel(endpointType),
            routerAt(m_view->mapToScene(position)).has_value());
        if (handleEndpointDrop(endpointType, drop->position().toPoint())) {
            drop->acceptProposedAction();
        } else {
            drop->ignore();
        }
        m_view->endEndpointDrag();
        return true;
    }
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QPoint position = mouse->position().toPoint();
            if (beginEndpointAttachmentDraft(position)
                || beginRouterEndpointDraft(position)
                || blockedPortAt(position)) {
                mouse->accept();
                return true;
            }
        }
        return false;
    }
    case QEvent::MouseMove: {
        if (m_endpointAttachmentDraft) {
            updateEndpointAttachmentDraft(
                static_cast<QMouseEvent*>(event)->position().toPoint());
            event->accept();
            return true;
        }
        if (m_routerEndpointDraft) {
            updateRouterEndpointDraft(static_cast<QMouseEvent*>(event)->position().toPoint());
            event->accept();
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QPoint position = mouse->position().toPoint();
            if (m_endpointAttachmentDraft) {
                completeEndpointAttachmentDraft(position);
                mouse->accept();
                return true;
            }
            if (m_routerEndpointDraft) {
                completeRouterEndpointDraft(position);
                mouse->accept();
                return true;
            }
            if (tryCompleteDraftConnection(position)) {
                mouse->accept();
                return true;
            }
            QTimer::singleShot(0, this, [this, position] {
                handlePointerReleased(position);
            });
        }
        return false;
    }
    case QEvent::ContextMenu: {
        auto* context = static_cast<QContextMenuEvent*>(event);
        showContextMenu(context->pos(), context->globalPos());
        context->accept();
        return true;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void NocNodeEditor::handleNodeSelection(QtNodes::NodeId nodeId) {
    highlightNeighborhood(nodeId);
    const auto iterator = m_metadata.constFind(nodeId);
    if (iterator == m_metadata.constEnd()) {
        if (selectionChanged) {
            selectionChanged({});
        }
        return;
    }
    m_selectedKind = iterator->kind;
    m_selectedId = iterator->id;
    if (!selectionChanged) {
        return;
    }
    selectionChanged({iterator->kind,
                      iterator->kind == NocEditorSelection::Kind::PendingEndpoint
                          ? iterator->endpointType
                          : iterator->id,
                      iterator->router});
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
    const auto metadataIterator = m_metadata.constFind(nodeId);
    if (metadataIterator == m_metadata.constEnd()
        || (metadataIterator->kind != NocEditorSelection::Kind::Router
            && metadataIterator->kind != NocEditorSelection::Kind::Endpoint
            && metadataIterator->kind != NocEditorSelection::Kind::PendingEndpoint)) {
        return;
    }
    const NodeMetadata metadata = *metadataIterator;
    const QPointF position = m_graphModel->nodeData(
        nodeId, QtNodes::NodeRole::Position).toPointF();
    if (QLineF(position, metadata.projectedPosition).length() < 4.0) {
        return;
    }
    if (metadata.kind == NocEditorSelection::Kind::Router) {
        setRouterVisualPosition(metadata.id, position);
        return;
    }

    if (metadata.kind == NocEditorSelection::Kind::PendingEndpoint) {
        auto pending = m_pendingEndpoints.find(metadata.id);
        if (pending != m_pendingEndpoints.end()) {
            pending->scenePosition = position;
        }
    } else {
        m_endpointLayout.insert(metadata.id, position);
        saveWorkspaceLayout();
    }

    const std::optional<RouterPosition> targetRouter = routerAt(
        m_view->mapToScene(viewportPosition));
    if (targetRouter) {
        if (attachNodeToRouter(nodeId, *targetRouter)) {
            if (metadata.kind == NocEditorSelection::Kind::Endpoint) {
                m_endpointLayout.remove(metadata.id);
                saveWorkspaceLayout();
            }
            rebuildGraph(false);
            return;
        }
        rebuildGraph(false);
        return;
    }

    auto currentMetadata = m_metadata.find(nodeId);
    if (currentMetadata != m_metadata.end()) {
        currentMetadata->projectedPosition = position;
    }
    restoreSelection();
}

void NocNodeEditor::handleConnectionCreated(QtNodes::ConnectionId connectionId) {
    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    if (graphModel.projectionMutation()) {
        return;
    }
    const auto source = m_metadata.constFind(connectionId.outNodeId);
    const auto target = m_metadata.constFind(connectionId.inNodeId);
    if (source == m_metadata.constEnd()
        || target == m_metadata.constEnd()
        || target->kind != NocEditorSelection::Kind::Router
        || !isRouterAttachmentPort(connectionId.inPortIndex)
        || !target->router) {
        QTimer::singleShot(0, this, [this] { rebuildGraph(false); });
        return;
    }
    const QtNodes::NodeId sourceNode = connectionId.outNodeId;
    const RouterPosition router = *target->router;
    QTimer::singleShot(0, this, [this, sourceNode, router] {
        attachNodeToRouter(sourceNode, router);
        rebuildGraph(false);
    });
}

QtNodes::ConnectionGraphicsObject* NocNodeEditor::findDraftConnection() const {
    if (!m_scene) {
        return nullptr;
    }
    for (QGraphicsItem* item : m_scene->items()) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return connection;
        }
    }
    return nullptr;
}

bool NocNodeEditor::beginEndpointAttachmentDraft(const QPoint& viewportPosition) {
    if (!m_scene || !m_view || m_endpointAttachmentDraft) {
        return false;
    }
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (!nodeId) {
        return false;
    }
    const auto metadata = m_metadata.constFind(*nodeId);
    auto* node = m_scene->nodeGraphicsObject(*nodeId);
    if (metadata == m_metadata.constEnd()
        || (metadata->kind != NocEditorSelection::Kind::Endpoint
            && metadata->kind != NocEditorSelection::Kind::PendingEndpoint)
        || !node) {
        return false;
    }

    const QPointF localPosition = node->mapFromScene(m_view->mapToScene(viewportPosition));
    const QtNodes::PortIndex hitPort = m_scene->nodeGeometry().checkPortHit(
        *nodeId, QtNodes::PortType::Out, localPosition);
    if (hitPort != kEndpointOutPort) {
        return false;
    }

    const QPointF startScenePosition = node->mapToScene(
        m_scene->nodeGeometry().portPosition(
            *nodeId, QtNodes::PortType::Out, kEndpointOutPort));
    auto* graphicsItem = new QGraphicsPathItem;
    QPen pen(QColor(QStringLiteral("#2563eb")), 2.5, Qt::DashLine);
    pen.setDashPattern({7.0, 5.0});
    graphicsItem->setPen(pen);
    graphicsItem->setZValue(1000.0);
    graphicsItem->setData(Qt::UserRole, QStringLiteral("finepaper.endpointAttachmentDraft"));
    m_scene->addItem(graphicsItem);
    m_endpointAttachmentDraft = EndpointAttachmentDraft{
        *nodeId, startScenePosition, graphicsItem};
    node->setSelected(true);
    handleNodeSelection(*nodeId);
    updateEndpointAttachmentDraft(viewportPosition);
    return true;
}

void NocNodeEditor::updateEndpointAttachmentDraft(const QPoint& viewportPosition) {
    if (!m_endpointAttachmentDraft || !m_view) {
        return;
    }
    QGraphicsPathItem* graphicsItem = m_endpointAttachmentDraft->graphicsItem;
    if (!graphicsItem) {
        return;
    }
    graphicsItem->setPath(orthogonalConnectionPath(
        m_endpointAttachmentDraft->startScenePosition,
        m_view->mapToScene(viewportPosition)));
}

bool NocNodeEditor::completeEndpointAttachmentDraft(const QPoint& viewportPosition) {
    if (!m_endpointAttachmentDraft || !m_view || !m_scene) {
        return false;
    }
    const EndpointAttachmentDraft draft = *m_endpointAttachmentDraft;
    const std::optional<QtNodes::NodeId> targetNode = nodeAtScene(
        m_view->mapToScene(viewportPosition));
    clearEndpointAttachmentDraft();
    if (!targetNode) {
        restoreSelection();
        return false;
    }
    const auto target = m_metadata.constFind(*targetNode);
    auto* routerGraphics = m_scene->nodeGraphicsObject(*targetNode);
    if (target == m_metadata.constEnd()
        || target->kind != NocEditorSelection::Kind::Router
        || !target->router
        || !routerGraphics) {
        restoreSelection();
        return false;
    }

    const QPointF localPosition = routerGraphics->mapFromScene(
        m_view->mapToScene(viewportPosition));
    const QtNodes::PortIndex hitPort = m_scene->nodeGeometry().checkPortHit(
        *targetNode, QtNodes::PortType::In, localPosition);
    std::optional<unsigned int> attachmentPort;
    if (isRouterAttachmentPort(hitPort)
        && attachmentPortAvailable(*targetNode, hitPort, draft.endpointNode)) {
        attachmentPort = hitPort;
    } else if (hitPort == QtNodes::InvalidPortIndex) {
        attachmentPort = firstAvailableAttachmentPort(*targetNode, draft.endpointNode);
    }
    if (!attachmentPort) {
        restoreSelection();
        return false;
    }
    if (!attachNodeToRouter(draft.endpointNode, *target->router)) {
        restoreSelection();
        return false;
    }
    rebuildGraph(false);
    return true;
}

void NocNodeEditor::clearEndpointAttachmentDraft() {
    if (!m_endpointAttachmentDraft) {
        return;
    }
    QGraphicsPathItem* graphicsItem = m_endpointAttachmentDraft->graphicsItem;
    m_endpointAttachmentDraft.reset();
    if (graphicsItem) {
        if (graphicsItem->scene()) {
            graphicsItem->scene()->removeItem(graphicsItem);
        }
        delete graphicsItem;
    }
}

bool NocNodeEditor::beginRouterEndpointDraft(const QPoint& viewportPosition) {
    if (!m_scene || !m_view || m_routerEndpointDraft) {
        return false;
    }
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (!nodeId) {
        return false;
    }
    const auto metadata = m_metadata.constFind(*nodeId);
    auto* node = m_scene->nodeGraphicsObject(*nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router
        || !node) {
        return false;
    }

    const QPointF localPosition = node->mapFromScene(m_view->mapToScene(viewportPosition));
    const QtNodes::PortIndex hitPort = m_scene->nodeGeometry().checkPortHit(
        *nodeId, QtNodes::PortType::In, localPosition);
    if (!isRouterAttachmentPort(hitPort)
        || !attachmentPortAvailable(*nodeId, hitPort)) {
        return false;
    }

    const QPointF startScenePosition = node->mapToScene(
        m_scene->nodeGeometry().portPosition(
            *nodeId, QtNodes::PortType::In, hitPort));
    auto* graphicsItem = new QGraphicsPathItem;
    QPen pen(QColor(QStringLiteral("#2563eb")), 2.5, Qt::DashLine);
    pen.setDashPattern({7.0, 5.0});
    graphicsItem->setPen(pen);
    graphicsItem->setZValue(1000.0);
    graphicsItem->setData(Qt::UserRole, QStringLiteral("finepaper.routerEndpointDraft"));
    m_scene->addItem(graphicsItem);
    m_routerEndpointDraft = RouterEndpointDraft{
        *nodeId, *metadata->router, hitPort, startScenePosition, graphicsItem};
    node->setSelected(true);
    handleNodeSelection(*nodeId);
    updateRouterEndpointDraft(viewportPosition);
    return true;
}

void NocNodeEditor::updateRouterEndpointDraft(const QPoint& viewportPosition) {
    if (!m_routerEndpointDraft || !m_view) {
        return;
    }
    QGraphicsPathItem* graphicsItem = m_routerEndpointDraft->graphicsItem;
    if (!graphicsItem) {
        return;
    }
    graphicsItem->setPath(orthogonalConnectionPath(
        m_routerEndpointDraft->startScenePosition,
        m_view->mapToScene(viewportPosition)));
}

bool NocNodeEditor::completeRouterEndpointDraft(const QPoint& viewportPosition) {
    if (!m_routerEndpointDraft || !m_view) {
        return false;
    }
    const RouterEndpointDraft draft = *m_routerEndpointDraft;
    const std::optional<QtNodes::NodeId> targetNode = nodeAtScene(
        m_view->mapToScene(viewportPosition), draft.routerNode);
    clearRouterEndpointDraft();
    if (!targetNode) {
        restoreSelection();
        return false;
    }
    const NodeMetadata target = m_metadata.value(*targetNode);
    if (target.kind != NocEditorSelection::Kind::Endpoint
        && target.kind != NocEditorSelection::Kind::PendingEndpoint) {
        restoreSelection();
        return false;
    }
    if (!attachNodeToRouter(*targetNode, draft.router)) {
        restoreSelection();
        return false;
    }
    rebuildGraph(false);
    return true;
}

void NocNodeEditor::clearRouterEndpointDraft() {
    if (!m_routerEndpointDraft) {
        return;
    }
    QGraphicsPathItem* graphicsItem = m_routerEndpointDraft->graphicsItem;
    m_routerEndpointDraft.reset();
    if (graphicsItem) {
        if (graphicsItem->scene()) {
            graphicsItem->scene()->removeItem(graphicsItem);
        }
        delete graphicsItem;
    }
}

bool NocNodeEditor::tryCompleteDraftConnection(const QPoint& viewportPosition) {
    QtNodes::ConnectionGraphicsObject* draft = findDraftConnection();
    const std::optional<DraftConnectionStart> start = draft
        ? resolveDraftConnectionStart(*draft)
        : std::nullopt;
    if (!start) {
        return false;
    }

    const QPointF scenePosition = m_view->mapToScene(viewportPosition);
    std::optional<QtNodes::NodeId> endpointNode;
    std::optional<RouterPosition> router;
    const NodeMetadata startMetadata = m_metadata.value(start->nodeId);
    if (start->startFromOutput
        && start->portIndex == kEndpointOutPort
        && (startMetadata.kind == NocEditorSelection::Kind::Endpoint
            || startMetadata.kind == NocEditorSelection::Kind::PendingEndpoint)) {
        endpointNode = start->nodeId;
        router = routerAt(scenePosition);
    } else if (!start->startFromOutput
               && isRouterAttachmentPort(start->portIndex)
               && startMetadata.kind == NocEditorSelection::Kind::Router
               && startMetadata.router) {
        const std::optional<QtNodes::NodeId> target = nodeAtScene(
            scenePosition, start->nodeId);
        if (target) {
            const NodeMetadata targetMetadata = m_metadata.value(*target);
            if (targetMetadata.kind == NocEditorSelection::Kind::Endpoint
                || targetMetadata.kind == NocEditorSelection::Kind::PendingEndpoint) {
                endpointNode = *target;
                router = startMetadata.router;
            }
        }
    }

    if (!endpointNode || !router) {
        return false;
    }
    m_scene->resetDraftConnection();
    attachNodeToRouter(*endpointNode, *router);
    rebuildGraph(false);
    return true;
}

bool NocNodeEditor::attachNodeToRouter(QtNodes::NodeId nodeId, RouterPosition router) {
    const auto iterator = m_metadata.constFind(nodeId);
    if (iterator == m_metadata.constEnd()) {
        return false;
    }
    const NodeMetadata metadata = *iterator;
    if (metadata.kind == NocEditorSelection::Kind::PendingEndpoint) {
        const auto pending = m_pendingEndpoints.constFind(metadata.id);
        if (pending == m_pendingEndpoints.constEnd()) {
            return false;
        }
        if (pending->detachedEndpoint) {
            if (!detachedEndpointDropped
                || !detachedEndpointDropped(*pending->detachedEndpoint, router)) {
                return false;
            }
            const QString endpointId = pending->detachedEndpoint->id;
            m_pendingEndpoints.remove(metadata.id);
            m_selectedKind = NocEditorSelection::Kind::Endpoint;
            m_selectedId = endpointId;
            return true;
        }
        QSet<QString> endpointIdsBefore;
        if (m_design) {
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                endpointIdsBefore.insert(endpoint.id);
            }
        }
        if (!endpointTypeDropped
            || !endpointTypeDropped(metadata.endpointType, router)) {
            return false;
        }
        m_pendingEndpoints.remove(metadata.id);
        if (m_selectedKind == NocEditorSelection::Kind::PendingEndpoint
            && m_selectedId == metadata.id
            && m_design) {
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                if (!endpointIdsBefore.contains(endpoint.id)
                    && endpoint.type == metadata.endpointType
                    && endpoint.attachment.router == router) {
                    m_selectedKind = NocEditorSelection::Kind::Endpoint;
                    m_selectedId = endpoint.id;
                    break;
                }
            }
        }
        return true;
    }
    if (metadata.kind == NocEditorSelection::Kind::Endpoint) {
        if (metadata.router && *metadata.router == router) {
            return true;
        }
        return endpointMoveRequested && endpointMoveRequested(metadata.id, router);
    }
    return false;
}

void NocNodeEditor::detachEndpoint(QtNodes::NodeId nodeId) {
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Endpoint
        || !m_design
        || !endpointRemovalRequested) {
        return;
    }
    const auto endpoint = std::find_if(m_design->endpoints.cbegin(), m_design->endpoints.cend(),
                                       [&](const EndpointInstance& candidate) {
                                           return candidate.id == metadata->id;
                                       });
    if (endpoint == m_design->endpoints.cend()) {
        return;
    }
    QPointF scenePosition = metadata->projectedPosition;
    if (m_graphModel->nodeExists(nodeId)) {
        scenePosition = m_graphModel->nodeData(
            nodeId, QtNodes::NodeRole::Position).toPointF();
    }
    const EndpointInstance detached = *endpoint;
    endpointRemovalRequested(metadata->id);
    const QString pendingId = QStringLiteral("pending-endpoint-%1")
                                  .arg(++m_nextPendingEndpoint);
    m_pendingEndpoints.insert(pendingId, PendingEndpoint{
        pendingId, detached.type, scenePosition, detached});
    m_selectedKind = NocEditorSelection::Kind::PendingEndpoint;
    m_selectedId = pendingId;
    rebuildGraph(false);
}

void NocNodeEditor::showContextMenu(const QPoint& viewportPosition,
                                    const QPoint& globalPosition) {
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (nodeId) {
        showNodeContextMenu(*nodeId, globalPosition);
        return;
    }
    showCanvasCreateMenu(m_view->mapToScene(viewportPosition), globalPosition);
}

void NocNodeEditor::showCanvasCreateMenu(QPointF scenePosition,
                                         const QPoint& globalPosition) {
    if (!m_design || m_endpointTypes.isEmpty()) {
        return;
    }
    auto* menu = new QMenu(this);
    menu->setObjectName(workbench::canvasContextMenuName);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setTitle(QStringLiteral("Create Endpoint"));
    for (const NocEndpointTypeItem& type : std::as_const(m_endpointTypes)) {
        QAction* action = menu->addAction(QStringLiteral("Create %1").arg(type.label));
        action->setData(type.id);
        connect(action, &QAction::triggered, this, [this, type, scenePosition] {
            addPendingEndpoint(type.id, scenePosition);
        });
    }
    menu->popup(globalPosition);
}

void NocNodeEditor::showNodeContextMenu(QtNodes::NodeId nodeId,
                                        const QPoint& globalPosition) {
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()) {
        return;
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (metadata->kind == NocEditorSelection::Kind::Router) {
        menu->setObjectName(workbench::routerContextMenuName);
        if (metadata->router) {
            QMenu* createMenu = menu->addMenu(QStringLiteral("Add Endpoint"));
            createMenu->setObjectName(workbench::createEndpointMenuName);
            for (const NocEndpointTypeItem& type : std::as_const(m_endpointTypes)) {
                QAction* action = createMenu->addAction(type.label);
                action->setData(type.id);
                const RouterPosition router = *metadata->router;
                connect(action, &QAction::triggered, this, [this, type, router] {
                    if (endpointTypeDropped) {
                        endpointTypeDropped(type.id, router);
                    }
                });
            }
        }
        QAction* collapse = menu->addAction(
            routerCollapsed(metadata->id)
                ? QStringLiteral("Expand Router")
                : QStringLiteral("Collapse Router"));
        const QString routerId = metadata->id;
        connect(collapse, &QAction::triggered, this, [this, routerId] {
            setRouterCollapsed(routerId, !routerCollapsed(routerId));
        });
        menu->popup(globalPosition);
        return;
    }

    const bool pending = metadata->kind == NocEditorSelection::Kind::PendingEndpoint;
    if (!pending && metadata->kind != NocEditorSelection::Kind::Endpoint) {
        delete menu;
        return;
    }
    menu->setObjectName(workbench::endpointContextMenuName);
    QMenu* connectMenu = menu->addMenu(QStringLiteral("Connect to Router"));
    connectMenu->setObjectName(workbench::connectRouterMenuName);
    QVector<QPair<RouterPosition, QString>> routers;
    routers.reserve(m_routerNodes.size());
    for (auto iterator = m_routerNodes.constBegin(); iterator != m_routerNodes.constEnd(); ++iterator) {
        const NodeMetadata routerMetadata = m_metadata.value(iterator.value());
        if (routerMetadata.router) {
            routers.append({*routerMetadata.router, routerMetadata.id});
        }
    }
    std::sort(routers.begin(), routers.end(), [](const auto& left, const auto& right) {
        if (left.first.y != right.first.y) {
            return left.first.y < right.first.y;
        }
        return left.first.x < right.first.x;
    });
    for (const auto& router : std::as_const(routers)) {
        QAction* action = connectMenu->addAction(router.second);
        action->setData(router.second);
        connect(action, &QAction::triggered, this, [this, nodeId, position = router.first] {
            attachNodeToRouter(nodeId, position);
            rebuildGraph(false);
        });
    }
    menu->addSeparator();
    if (!pending) {
        QAction* detach = menu->addAction(QStringLiteral("Disconnect from Router"));
        detach->setObjectName(workbench::detachEndpointActionName);
        connect(detach, &QAction::triggered, this, [this, nodeId] {
            detachEndpoint(nodeId);
        });
    }
    QAction* remove = menu->addAction(
        pending ? QStringLiteral("Delete Unattached Endpoint")
                : QStringLiteral("Delete Endpoint"));
    remove->setObjectName(workbench::deleteEndpointActionName);
    const QString endpointId = metadata->id;
    connect(remove, &QAction::triggered, this, [this, pending, endpointId] {
        if (pending) {
            m_pendingEndpoints.remove(endpointId);
            rebuildGraph(false);
        } else if (endpointRemovalRequested) {
            endpointRemovalRequested(endpointId);
        }
    });
    menu->popup(globalPosition);
}

void NocNodeEditor::highlightNeighborhood(QtNodes::NodeId nodeId) {
    clearNeighborhoodHighlight();
    if (!m_scene || !m_graphModel->nodeExists(nodeId)) {
        return;
    }
    for (const QtNodes::ConnectionId& connectionId
         : m_graphModel->allConnectionIds(nodeId)) {
        if (auto* connection = m_scene->connectionGraphicsObject(connectionId)) {
            connection->setData(relatedHighlightDataRole, true);
            connection->update();
        }
        const QtNodes::NodeId relatedNodeId = connectionId.outNodeId == nodeId
            ? connectionId.inNodeId
            : connectionId.outNodeId;
        if (auto* relatedNode = m_scene->nodeGraphicsObject(relatedNodeId)) {
            relatedNode->setData(relatedHighlightDataRole, true);
            relatedNode->update();
        }
    }
}

void NocNodeEditor::clearNeighborhoodHighlight() {
    if (!m_scene) {
        return;
    }
    for (QGraphicsItem* item : m_scene->items()) {
        if (item->data(relatedHighlightDataRole).toBool()) {
            item->setData(relatedHighlightDataRole, false);
            item->update();
        }
    }
}

void NocNodeEditor::loadWorkspaceLayout() {
    m_routerLayout.clear();
    m_endpointLayout.clear();
    m_collapsedRouters.clear();
    m_hasStoredCollapsedLayout = false;
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
    const QVariantMap endpointLayouts = settings.value(
        workbench::endpointLayoutsSetting).toMap();
    const QVariantMap endpointLayout = endpointLayouts.value(m_layoutKey).toMap();
    for (auto iterator = endpointLayout.constBegin();
         iterator != endpointLayout.constEnd(); ++iterator) {
        if (iterator.value().canConvert<QPointF>()) {
            m_endpointLayout.insert(iterator.key(), iterator.value().toPointF());
        }
    }
    const QVariantMap collapsedLayouts = settings.value(
        workbench::collapsedRoutersSetting).toMap();
    m_hasStoredCollapsedLayout = collapsedLayouts.contains(m_layoutKey);
    const QStringList collapsed = collapsedLayouts.value(m_layoutKey).toStringList();
    for (const QString& routerId : collapsed) {
        m_collapsedRouters.insert(routerId);
    }
}

void NocNodeEditor::saveWorkspaceLayout() const {
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

    QVariantMap endpointLayout;
    for (auto iterator = m_endpointLayout.constBegin();
         iterator != m_endpointLayout.constEnd(); ++iterator) {
        endpointLayout.insert(iterator.key(), iterator.value());
    }
    QVariantMap endpointLayouts = settings.value(
        workbench::endpointLayoutsSetting).toMap();
    endpointLayouts.insert(m_layoutKey, endpointLayout);
    settings.setValue(workbench::endpointLayoutsSetting, endpointLayouts);

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
    if (!m_design || endpointType.trimmed().isEmpty()) {
        return false;
    }
    const bool knownType = std::any_of(
        m_endpointTypes.cbegin(), m_endpointTypes.cend(),
        [&endpointType](const NocEndpointTypeItem& type) {
            return type.id == endpointType;
        });
    if (!knownType) {
        return false;
    }
    const std::optional<QtNodes::NodeId> target = nodeAt(viewportPosition);
    if (!target) {
        addPendingEndpoint(endpointType, m_view->mapToScene(viewportPosition));
        return true;
    }
    const auto metadata = m_metadata.constFind(*target);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router) {
        addPendingEndpoint(endpointType, m_view->mapToScene(viewportPosition));
        return true;
    }
    return endpointTypeDropped
        && endpointTypeDropped(endpointType, *metadata->router);
}

void NocNodeEditor::addPendingEndpoint(const QString& endpointType,
                                       QPointF scenePosition) {
    const QString id = QStringLiteral("pending-endpoint-%1")
                           .arg(++m_nextPendingEndpoint);
    m_pendingEndpoints.insert(id, PendingEndpoint{id, endpointType, scenePosition, std::nullopt});
    rebuildGraph(false);
}

std::optional<RouterPosition> NocNodeEditor::routerAt(
    const QPointF& scenePosition) const {
    for (auto iterator = m_routerNodes.constBegin(); iterator != m_routerNodes.constEnd(); ++iterator) {
        auto* routerGraphics = m_scene
            ? m_scene->nodeGraphicsObject(iterator.value())
            : nullptr;
        if (routerGraphics
            && routerGraphics->sceneBoundingRect().contains(scenePosition)) {
            return m_metadata.value(iterator.value()).router;
        }
    }
    return std::nullopt;
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

std::optional<QtNodes::NodeId> NocNodeEditor::nodeAtScene(
    const QPointF& scenePosition,
    std::optional<QtNodes::NodeId> ignoredNode) const {
    if (!m_scene) {
        return std::nullopt;
    }
    for (auto iterator = m_metadata.constBegin(); iterator != m_metadata.constEnd(); ++iterator) {
        if (ignoredNode && iterator.key() == *ignoredNode) {
            continue;
        }
        auto* node = m_scene->nodeGraphicsObject(iterator.key());
        if (node && node->sceneBoundingRect().contains(scenePosition)) {
            return iterator.key();
        }
    }
    return std::nullopt;
}

bool NocNodeEditor::blockedPortAt(const QPoint& viewportPosition) const {
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (!nodeId || !m_scene) {
        return false;
    }
    auto* node = m_scene->nodeGraphicsObject(*nodeId);
    if (!node) {
        return false;
    }
    const QPointF nodePosition = node->mapFromScene(
        m_view->mapToScene(viewportPosition));
    const auto& geometry = m_scene->nodeGeometry();
    const QtNodes::PortIndex input = geometry.checkPortHit(
        *nodeId, QtNodes::PortType::In, nodePosition);
    const QtNodes::PortIndex output = geometry.checkPortHit(
        *nodeId, QtNodes::PortType::Out, nodePosition);
    if (input == QtNodes::InvalidPortIndex
        && output == QtNodes::InvalidPortIndex) {
        return false;
    }
    return true;
}

bool NocNodeEditor::isRouterAttachmentPort(unsigned int portIndex) const {
    return portIndex >= kRouterEndpointInPort
        && portIndex < kRouterEndpointInPort
            + static_cast<unsigned int>(m_routerAttachmentPorts.size());
}

bool NocNodeEditor::attachmentPortAvailable(
    QtNodes::NodeId routerNode,
    unsigned int portIndex,
    std::optional<QtNodes::NodeId> ignoredEndpoint) const {
    if (!isRouterAttachmentPort(portIndex) || !m_graphModel->nodeExists(routerNode)) {
        return false;
    }
    for (const QtNodes::ConnectionId& connection : m_graphModel->allConnectionIds(routerNode)) {
        if (connection.inNodeId != routerNode || connection.inPortIndex != portIndex) {
            continue;
        }
        if (!ignoredEndpoint || connection.outNodeId != *ignoredEndpoint) {
            return false;
        }
    }
    return true;
}

std::optional<unsigned int> NocNodeEditor::firstAvailableAttachmentPort(
    QtNodes::NodeId routerNode,
    std::optional<QtNodes::NodeId> ignoredEndpoint) const {
    for (unsigned int portIndex = kRouterEndpointInPort;
         portIndex < kRouterEndpointInPort
             + static_cast<unsigned int>(m_routerAttachmentPorts.size());
         ++portIndex) {
        if (attachmentPortAvailable(routerNode, portIndex, ignoredEndpoint)) {
            return portIndex;
        }
    }
    return std::nullopt;
}

QString NocNodeEditor::endpointTypeLabel(const QString& endpointType) const {
    for (const NocEndpointTypeItem& type : m_endpointTypes) {
        if (type.id == endpointType) {
            return type.label;
        }
    }
    return endpointType;
}

void NocNodeEditor::restoreSelection() {
    if (!m_scene || m_selectedKind == NocEditorSelection::Kind::None
        || m_selectedId.isEmpty()) {
        return;
    }
    for (auto iterator = m_metadata.constBegin(); iterator != m_metadata.constEnd(); ++iterator) {
        if (iterator->kind != m_selectedKind || iterator->id != m_selectedId) {
            continue;
        }
        if (auto* node = m_scene->nodeGraphicsObject(iterator.key())) {
            if (!node->isSelected()) {
                node->setSelected(true);
            }
            handleNodeSelection(iterator.key());
            return;
        }
    }
    m_selectedKind = NocEditorSelection::Kind::None;
    m_selectedId.clear();
    clearNeighborhoodHighlight();
    if (selectionChanged) {
        selectionChanged({});
    }
}

} // namespace finepaper
