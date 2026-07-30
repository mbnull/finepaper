#include "features/topology/noc_node_editor.h"

#include "application/endpoint_domain_assignment.h"
#include "features/topology/animated_graphics_view.h"
#include "features/topology/noc_editor_style.h"
#include "ui/workbench/workbench_config.h"

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
#include <QSignalBlocker>
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
constexpr qreal kInteractivePortHitRadius = 14.0;
constexpr qreal kRouterNodeZValue = 10.0;
constexpr qreal kEndpointNodeZValue = 20.0;
constexpr int kSemanticElementKindDataRole = 0x464e02;
constexpr int kSemanticElementIdDataRole = 0x464e03;

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

NocEditorSelection::Kind selectionKindForElement(ElementKind kind) {
    switch (kind) {
    case ElementKind::Router:
        return NocEditorSelection::Kind::Router;
    case ElementKind::Endpoint:
        return NocEditorSelection::Kind::Endpoint;
    case ElementKind::RouterLink:
        return NocEditorSelection::Kind::RouterLink;
    case ElementKind::EndpointAttachment:
        return NocEditorSelection::Kind::EndpointAttachment;
    case ElementKind::Invalid:
        break;
    }
    return NocEditorSelection::Kind::None;
}

ElementKind elementKindForSelection(NocEditorSelection::Kind kind) {
    switch (kind) {
    case NocEditorSelection::Kind::Router:
        return ElementKind::Router;
    case NocEditorSelection::Kind::Endpoint:
        return ElementKind::Endpoint;
    case NocEditorSelection::Kind::RouterLink:
        return ElementKind::RouterLink;
    case NocEditorSelection::Kind::EndpointAttachment:
        return ElementKind::EndpointAttachment;
    case NocEditorSelection::Kind::None:
    case NocEditorSelection::Kind::PendingEndpoint:
        break;
    }
    return ElementKind::Invalid;
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

        drawDomainStrip(painter, node, body);
        drawPorts(painter, node, model, geometry, QtNodes::PortType::In);
        drawPorts(painter, node, model, geometry, QtNodes::PortType::Out);
    }

private:
    static void drawDomainStrip(QPainter* painter,
                                const QtNodes::NodeGraphicsObject& node,
                                const QRectF& body) {
        const auto state = static_cast<DomainAssignmentDisplayState>(
            node.data(domainAssignmentStateDataRole).toInt());
        if (state == DomainAssignmentDisplayState::Inactive
            || state == DomainAssignmentDisplayState::NotApplicable) {
            return;
        }

        const QRectF strip(body.left() + 2.0,
                           body.bottom() - 10.0,
                           body.width() - 4.0,
                           8.0);
        const QVariantList colorValues = node.data(domainColorsDataRole).toList();
        QVector<QColor> colors;
        colors.reserve(colorValues.size());
        for (const QVariant& value : colorValues) {
            const QColor color = value.value<QColor>();
            if (color.isValid()) {
                colors.append(color);
            }
        }

        painter->save();
        painter->setClipRect(strip);
        painter->setPen(Qt::NoPen);
        if (state == DomainAssignmentDisplayState::Assigned && !colors.isEmpty()) {
            painter->setBrush(colors.front());
            painter->drawRect(strip);
        } else if (state == DomainAssignmentDisplayState::Multiple
                   && !colors.isEmpty()) {
            const qreal segmentWidth = strip.width()
                / static_cast<qreal>(colors.size());
            for (qsizetype index = 0; index < colors.size(); ++index) {
                const qreal left = strip.left()
                    + static_cast<qreal>(index) * segmentWidth;
                const qreal right = index + 1 == colors.size()
                    ? strip.right() : left + segmentWidth;
                painter->setBrush(colors.at(index));
                painter->drawRect(QRectF(
                    QPointF(left, strip.top()), QPointF(right, strip.bottom())));
            }
        } else {
            painter->setBrush(state == DomainAssignmentDisplayState::Unavailable
                                  ? QColor(QStringLiteral("#94a3b8"))
                                  : QColor(QStringLiteral("#cbd5e1")));
            painter->drawRect(strip);
            painter->setPen(QPen(QColor(QStringLiteral("#64748b")), 1.0));
            for (qreal x = strip.left() - strip.height();
                 x < strip.right(); x += 7.0) {
                painter->drawLine(QPointF(x, strip.bottom()),
                                  QPointF(x + strip.height(), strip.top()));
            }
        }
        painter->restore();
    }

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
        const bool selected = connection.isSelected();
        const bool crossing = connection.data(domainCrossingDataRole).toBool();
        const bool hasOverride = connection.data(domainOverrideDataRole).toBool();
        const QColor crossingColor = connection.data(
            domainCrossingColorDataRole).value<QColor>();
        if (selected) {
            color = QColor(QStringLiteral("#f97316"));
        } else if (relatedHighlighted) {
            color = QColor(QStringLiteral("#f97316"));
        } else if (crossing && crossingColor.isValid()) {
            color = crossingColor;
        } else if (connection.connectionState().hovered()) {
            color = style.hoveredColor();
        }
        qreal width = style.lineWidth();
        if (crossing) {
            width += 1.25;
        }
        if (hasOverride) {
            width += 1.25;
        }
        if (relatedHighlighted || selected) {
            width += 2.0;
        }
        QPen pen(color, width);
        pen.setCapStyle(Qt::SquareCap);
        pen.setJoinStyle(Qt::MiterJoin);
        if (crossing) {
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({6.0, 4.0});
        }
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QPainterPath path = orthogonalConnectionPath(
            connection.out(), connection.in(), routeAxis(connection));
        painter->drawPath(path);
        if (hasOverride) {
            painter->save();
            painter->translate(path.pointAtPercent(0.5));
            painter->rotate(45.0);
            painter->setPen(QPen(color, 1.5));
            painter->setBrush(QColor(QStringLiteral("#f8fafc")));
            painter->drawRect(QRectF(-4.0, -4.0, 8.0, 8.0));
            painter->restore();
        }
    }

    QPainterPath getPainterStroke(
        QtNodes::ConnectionGraphicsObject const& connection) const override {
        auto const& style = QtNodes::StyleCollection::connectionStyle();
        QPainterPathStroker stroker;
        qreal width = style.lineWidth() + 10.0;
        if (connection.data(domainCrossingDataRole).toBool()) {
            width += 2.5;
        }
        if (connection.data(domainOverrideDataRole).toBool()) {
            width += 2.5;
        }
        stroker.setWidth(width);
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
    std::function<bool(const QtNodes::ConnectionId&)> userConnectionDeletable;

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
        if (m_projectionMutation) {
            return QtNodes::DataFlowGraphModel::deleteConnection(connectionId);
        }
        return userConnectionDeletable
            && userConnectionDeletable(connectionId)
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
    setCanvasInteractionMode(NocCanvasInteractionMode::Select);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.userConnectionPossible = [this](const QtNodes::ConnectionId& connection) {
        if (!m_editingEnabled) {
            return false;
        }
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
    graphModel.userConnectionDeletable = [this](const QtNodes::ConnectionId& connection) {
        return m_editingEnabled && isEndpointAttachmentConnection(connection);
    };

    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, [this] { handleSceneSelectionChanged(); });
    connect(m_graphModel.get(), &QtNodes::AbstractGraphModel::connectionCreated,
            this, [this](QtNodes::ConnectionId connectionId) {
                handleConnectionCreated(connectionId);
            });
    connect(m_graphModel.get(), &QtNodes::AbstractGraphModel::connectionDeleted,
            this, [this](QtNodes::ConnectionId connectionId) {
                handleConnectionDeleted(connectionId);
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
        ? workbench::designWorkspaceKey(
              design->package.id, design->package.version, design->id)
        : QString();
    if (layoutKey != m_layoutKey) {
        m_layoutKey = layoutKey;
        loadWorkspaceLayout();
        m_pendingEndpoints.clear();
        m_selectedItems.clear();
    }
    m_design = design ? std::optional<NocDesign>(*design) : std::nullopt;
    rebuildGraph();
}

void NocNodeEditor::syncDesignState(const NocDesign& design) {
    m_design = design;
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
        ports.append({QStringLiteral("0"), QStringLiteral("EP"), std::nullopt});
    }
    if (m_routerAttachmentPorts == ports) {
        return;
    }
    m_routerAttachmentPorts = std::move(ports);
    if (m_design) {
        rebuildGraph(false);
    }
}

void NocNodeEditor::setEditingEnabled(bool enabled) {
    if (m_editingEnabled == enabled) {
        return;
    }
    m_editingEnabled = enabled;
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    if (m_view) {
        m_view->endEndpointDrag();
        m_view->setAcceptDrops(enabled);
        if (m_view->viewport()) {
            m_view->viewport()->setAcceptDrops(enabled);
        }
    }
    for (auto iterator = m_metadata.constBegin(); iterator != m_metadata.constEnd(); ++iterator) {
        if (auto* node = m_scene ? m_scene->nodeGraphicsObject(iterator.key()) : nullptr) {
            const bool movable = iterator->kind == NocEditorSelection::Kind::Router
                || (enabled && (iterator->kind == NocEditorSelection::Kind::Endpoint
                                || iterator->kind == NocEditorSelection::Kind::PendingEndpoint));
            node->setFlag(QGraphicsItem::ItemIsMovable, movable);
        }
    }
}

bool NocNodeEditor::editingEnabled() const {
    return m_editingEnabled;
}

void NocNodeEditor::setCanvasInteractionMode(NocCanvasInteractionMode mode) {
    m_canvasInteractionMode = mode;
    m_canvasSelectionGesture = false;
    m_canvasItemGesture = false;
    if (!m_view) {
        return;
    }
    m_view->setPersistentDragMode(
        mode == NocCanvasInteractionMode::Select
            ? QGraphicsView::RubberBandDrag
            : QGraphicsView::ScrollHandDrag);
    m_view->setCursor(mode == NocCanvasInteractionMode::Select
                          ? Qt::ArrowCursor
                          : Qt::OpenHandCursor);
}

void NocNodeEditor::selectElements(const QVector<ElementRef>& elements) {
    QVector<SelectionIdentity> requested;
    requested.reserve(elements.size());
    for (const ElementRef& element : elements) {
        const NocEditorSelection::Kind kind = selectionKindForElement(element.kind);
        const SelectionIdentity identity{kind, element.id};
        if (kind != NocEditorSelection::Kind::None
            && !element.id.trimmed().isEmpty()
            && !requested.contains(identity)) {
            requested.append(identity);
        }
    }
    std::sort(requested.begin(), requested.end(), [](const auto& left,
                                                      const auto& right) {
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        return left.id < right.id;
    });
    m_selectedItems = std::move(requested);
    restoreSelection();
    if (m_view) {
        m_view->setFocus(Qt::OtherFocusReason);
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

void NocNodeEditor::setDomainPresentation(
    DomainPresentationSnapshot presentation) {
    if (m_domainPresentation == presentation) {
        return;
    }
    m_domainPresentation = std::move(presentation);
    applyDomainPresentation();
}

const DomainPresentationSnapshot& NocNodeEditor::domainPresentation() const {
    return m_domainPresentation;
}

QStringList NocNodeEditor::detachedEndpointDraftIds() const {
    QStringList endpointIds;
    for (const PendingEndpoint& pending : m_pendingEndpoints) {
        if (pending.detached) {
            endpointIds.append(pending.detached->endpoint.id);
        }
    }
    std::sort(endpointIds.begin(), endpointIds.end());
    endpointIds.removeDuplicates();
    return endpointIds;
}

void NocNodeEditor::rebuildGraph(bool zoomToContents) {
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.clearProjection();
    m_metadata.clear();
    m_routerNodes.clear();
    m_elementNodes.clear();
    m_elementConnections.clear();

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
        const ElementRef element{ElementKind::Router, router.id};
        m_elementNodes.insert(element, nodeId);
        m_metadata.insert(nodeId, NodeMetadata{
            NocEditorSelection::Kind::Router,
            router.id,
            router.position,
            visualPosition,
            {}});
        if (auto* node = m_scene->nodeGraphicsObject(nodeId)) {
            node->setData(kSemanticElementKindDataRole,
                          static_cast<int>(element.kind));
            node->setData(kSemanticElementIdDataRole, element.id);
        }
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
        QtNodes::ConnectionId connection;
        if (to.x > from.x) {
            connection = {
                m_routerNodes.value(link.fromRouter), kRouterEastOutPort,
                m_routerNodes.value(link.toRouter), kRouterWestInPort};
        } else {
            connection = {
                m_routerNodes.value(link.fromRouter), kRouterSouthOutPort,
                m_routerNodes.value(link.toRouter), kRouterNorthInPort};
        }
        graphModel.addProjectedConnection(connection);
        const ElementRef element{ElementKind::RouterLink, link.id};
        m_elementConnections.insert(element, connection);
        if (auto* graphics = m_scene->connectionGraphicsObject(connection)) {
            graphics->setData(kSemanticElementKindDataRole,
                              static_cast<int>(element.kind));
            graphics->setData(kSemanticElementIdDataRole, element.id);
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
        const ElementRef endpointElement{ElementKind::Endpoint, endpoint.id};
        m_elementNodes.insert(endpointElement, endpointNode);
        m_metadata.insert(endpointNode, NodeMetadata{
            NocEditorSelection::Kind::Endpoint,
            endpoint.id,
            endpoint.router,
            endpointPosition,
            endpoint.type});
        if (auto* node = m_scene->nodeGraphicsObject(endpointNode)) {
            node->setData(kSemanticElementKindDataRole,
                          static_cast<int>(endpointElement.kind));
            node->setData(kSemanticElementIdDataRole, endpointElement.id);
        }
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
        const QtNodes::ConnectionId connection{
            endpointNode, kEndpointOutPort,
            routerNode,
            kRouterEndpointInPort + static_cast<QtNodes::PortIndex>(attachmentOffset)};
        graphModel.addProjectedConnection(connection);
        const ElementRef attachmentElement{
            ElementKind::EndpointAttachment, endpoint.id};
        m_elementConnections.insert(attachmentElement, connection);
        if (auto* graphics = m_scene->connectionGraphicsObject(connection)) {
            graphics->setData(kSemanticElementKindDataRole,
                              static_cast<int>(attachmentElement.kind));
            graphics->setData(kSemanticElementIdDataRole, attachmentElement.id);
        }
    }

    for (const PendingEndpoint& pending : std::as_const(m_pendingEndpoints)) {
        QString label = pending.type;
        for (const NocEndpointTypeItem& type : std::as_const(m_endpointTypes)) {
            if (type.id == pending.type) {
                label = type.label;
                break;
            }
        }
        if (pending.detached) {
            label += QStringLiteral("\n%1")
                         .arg(pending.detached->endpoint.id);
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
    for (auto iterator = m_metadata.constBegin(); iterator != m_metadata.constEnd(); ++iterator) {
        if (auto* node = m_scene->nodeGraphicsObject(iterator.key())) {
            const bool movable = iterator->kind == NocEditorSelection::Kind::Router
                || (m_editingEnabled
                    && (iterator->kind == NocEditorSelection::Kind::Endpoint
                        || iterator->kind == NocEditorSelection::Kind::PendingEndpoint));
            node->setFlag(QGraphicsItem::ItemIsMovable, movable);
        }
    }
    applyDomainPresentation();
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
        if (!m_editingEnabled) {
            drag->ignore();
            return true;
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
        if (!m_editingEnabled) {
            drag->ignore();
            return true;
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
        if (!m_editingEnabled) {
            drop->ignore();
            m_view->endEndpointDrag();
            return true;
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
            m_canvasSelectionGesture = false;
            m_canvasItemGesture = false;
            // QtNodes temporarily raises the last selected node. Restore the
            // semantic hit order immediately before QGraphicsView resolves
            // the press so an Endpoint remains draggable when it overlaps a
            // Router.
            applyNodeStacking();
            const QPoint position = mouse->position().toPoint();
            if ((!m_editingEnabled && blockedPortAt(position))
                || (m_editingEnabled
                    && (beginEndpointAttachmentDraft(position)
                        || beginRouterEndpointDraft(position)))
                || blockedPortAt(position)) {
                mouse->accept();
                return true;
            }
            if (m_canvasInteractionMode == NocCanvasInteractionMode::Select) {
                const bool hitItem = nodeAt(position).has_value()
                    || connectionAt(position);
                m_canvasSelectionGesture = !hitItem;
                m_canvasItemGesture = hitItem;
                if (hitItem) {
                    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
                }
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
            if (m_editingEnabled && m_endpointAttachmentDraft) {
                completeEndpointAttachmentDraft(position);
                mouse->accept();
                return true;
            }
            if (m_editingEnabled && m_routerEndpointDraft) {
                completeRouterEndpointDraft(position);
                mouse->accept();
                return true;
            }
            if (m_editingEnabled && tryCompleteDraftConnection(position)) {
                mouse->accept();
                return true;
            }
            if (m_canvasSelectionGesture) {
                m_canvasSelectionGesture = false;
                return false;
            }
            if (m_canvasItemGesture) {
                m_canvasItemGesture = false;
                QTimer::singleShot(0, m_view, [view = m_view] {
                    view->setDragMode(view->persistentDragMode());
                });
            }
            QTimer::singleShot(0, this, [this, position] {
                handlePointerReleased(position);
            });
        }
        return false;
    }
    case QEvent::ContextMenu: {
        auto* context = static_cast<QContextMenuEvent*>(event);
        applyNodeStacking();
        showContextMenu(context->pos(), context->globalPos());
        context->accept();
        return true;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void NocNodeEditor::handleSceneSelectionChanged() {
    const auto& graphModel = static_cast<const NocGraphModel&>(*m_graphModel);
    if (!m_scene || graphModel.projectionMutation()) {
        return;
    }

    QVector<SelectionIdentity> selected;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
            const auto metadata = m_metadata.constFind(node->nodeId());
            if (metadata != m_metadata.constEnd()) {
                const SelectionIdentity identity{metadata->kind, metadata->id};
                if (!selected.contains(identity)) {
                    selected.append(identity);
                }
            }
            continue;
        }
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (!connection) {
            continue;
        }
        const std::optional<ElementRef> element = elementForConnection(
            connection->connectionId());
        if (!element) {
            continue;
        }
        const SelectionIdentity identity{
            selectionKindForElement(element->kind), element->id};
        if (identity.kind != NocEditorSelection::Kind::None
            && !selected.contains(identity)) {
            selected.append(identity);
        }
    }
    std::sort(selected.begin(), selected.end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        return left.id < right.id;
    });
    m_selectedItems = std::move(selected);

    clearNeighborhoodHighlight();
    if (m_selectedItems.size() == 1) {
        const SelectionIdentity& identity = m_selectedItems.front();
        const ElementKind elementKind = elementKindForSelection(identity.kind);
        const ElementRef element{elementKind, identity.id};
        if (elementKind == ElementKind::Router
            || elementKind == ElementKind::Endpoint) {
            const auto node = m_elementNodes.constFind(element);
            if (node != m_elementNodes.constEnd()) {
                highlightNeighborhood(node.value());
            }
        } else if (elementKind == ElementKind::RouterLink
                   || elementKind == ElementKind::EndpointAttachment) {
            const auto connection = m_elementConnections.constFind(element);
            if (connection != m_elementConnections.constEnd()) {
                if (auto* source = m_scene->nodeGraphicsObject(
                        connection->outNodeId)) {
                    source->setData(relatedHighlightDataRole, true);
                    source->update();
                }
                if (auto* target = m_scene->nodeGraphicsObject(
                        connection->inNodeId)) {
                    target->setData(relatedHighlightDataRole, true);
                    target->update();
                }
            }
        }
    }
    QTimer::singleShot(0, this, [this] { applyNodeStacking(); });
    emitSelectionChanged();
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
    if (selected.empty()) {
        return;
    }

    bool workspaceChanged = false;
    bool routerMoved = false;
    bool readOnlyEndpointMoved = false;
    for (const QtNodes::NodeId nodeId : selected) {
        const auto metadataIterator = m_metadata.constFind(nodeId);
        if (metadataIterator == m_metadata.constEnd()) {
            continue;
        }
        const NodeMetadata metadata = *metadataIterator;
        if (metadata.kind != NocEditorSelection::Kind::Router
            && metadata.kind != NocEditorSelection::Kind::Endpoint
            && metadata.kind != NocEditorSelection::Kind::PendingEndpoint) {
            continue;
        }
        const QPointF position = m_graphModel->nodeData(
            nodeId, QtNodes::NodeRole::Position).toPointF();
        if (QLineF(position, metadata.projectedPosition).length() < 4.0) {
            continue;
        }
        if (!m_editingEnabled
            && metadata.kind != NocEditorSelection::Kind::Router) {
            readOnlyEndpointMoved = true;
            continue;
        }

        if (metadata.kind == NocEditorSelection::Kind::Router) {
            m_routerLayout.insert(metadata.id, position);
            routerMoved = true;
            workspaceChanged = true;
        } else if (metadata.kind == NocEditorSelection::Kind::PendingEndpoint) {
            auto pending = m_pendingEndpoints.find(metadata.id);
            if (pending != m_pendingEndpoints.end()) {
                pending->scenePosition = position;
            }
        } else {
            // Endpoint placement is Workspace state only. Logical attachment
            // changes only through an explicit connection operation.
            m_endpointLayout.insert(metadata.id, position);
            workspaceChanged = true;
        }

        auto currentMetadata = m_metadata.find(nodeId);
        if (currentMetadata != m_metadata.end()) {
            currentMetadata->projectedPosition = position;
        }
    }

    if (readOnlyEndpointMoved) {
        rebuildGraph(false);
        return;
    }
    if (workspaceChanged) {
        saveWorkspaceLayout();
    }
    if (routerMoved) {
        rebuildGraph(false);
        return;
    }
    restoreSelection();
}

void NocNodeEditor::handleConnectionCreated(QtNodes::ConnectionId connectionId) {
    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    if (graphModel.projectionMutation()) {
        return;
    }
    if (!m_editingEnabled) {
        QTimer::singleShot(0, this, [this] { rebuildGraph(false); });
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
    const std::optional<QString> exactSlot = exactSlotForPort(connectionId.inPortIndex);
    QTimer::singleShot(0, this, [this, sourceNode, router, exactSlot] {
        attachNodeToRouter(sourceNode, NocAttachmentTarget{router, exactSlot});
        rebuildGraph(false);
    });
}

bool NocNodeEditor::isEndpointAttachmentConnection(
    QtNodes::ConnectionId connectionId) const {
    const std::optional<ElementRef> element = elementForConnection(connectionId);
    return element && element->kind == ElementKind::EndpointAttachment;
}

void NocNodeEditor::handleConnectionDeleted(QtNodes::ConnectionId connectionId) {
    const auto& graphModel = static_cast<const NocGraphModel&>(*m_graphModel);
    if (graphModel.projectionMutation()
        || !m_editingEnabled
        || !isEndpointAttachmentConnection(connectionId)) {
        return;
    }
    const std::optional<ElementRef> attachment = elementForConnection(connectionId);
    const QString endpointId = attachment ? attachment->id : QString();
    if (endpointId.isEmpty() || m_pendingConnectionDetachments.contains(endpointId)) {
        return;
    }
    m_pendingConnectionDetachments.insert(endpointId);
    const QString layoutKey = m_layoutKey;
    QTimer::singleShot(0, this, [this, endpointId, layoutKey] {
        m_pendingConnectionDetachments.remove(endpointId);
        if (layoutKey != m_layoutKey) {
            return;
        }
        for (auto iterator = m_metadata.constBegin();
             iterator != m_metadata.constEnd(); ++iterator) {
            if (iterator->kind == NocEditorSelection::Kind::Endpoint
                && iterator->id == endpointId) {
                detachEndpoint(iterator.key(), true);
                return;
            }
        }
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
    if (!m_editingEnabled || !m_scene || !m_view || m_endpointAttachmentDraft) {
        return false;
    }
    std::optional<QtNodes::NodeId> nodeId;
    qreal closestDistance = kInteractivePortHitRadius;
    for (auto iterator = m_metadata.constBegin();
         iterator != m_metadata.constEnd(); ++iterator) {
        if (iterator->kind != NocEditorSelection::Kind::Endpoint
            && iterator->kind != NocEditorSelection::Kind::PendingEndpoint) {
            continue;
        }
        auto* candidate = m_scene->nodeGraphicsObject(iterator.key());
        if (!candidate) {
            continue;
        }
        const QPoint portPosition = m_view->mapFromScene(candidate->mapToScene(
            m_scene->nodeGeometry().portPosition(
                iterator.key(), QtNodes::PortType::Out, kEndpointOutPort)));
        const qreal distance = QLineF(
            QPointF(viewportPosition), QPointF(portPosition)).length();
        if (distance <= closestDistance) {
            closestDistance = distance;
            nodeId = iterator.key();
        }
    }
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
    if (!m_editingEnabled || !m_endpointAttachmentDraft || !m_view || !m_scene) {
        return false;
    }
    const EndpointAttachmentDraft draft = *m_endpointAttachmentDraft;
    const std::optional<QtNodes::NodeId> targetNode = routerNodeAt(
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
    std::optional<QString> exactSlot;
    if (isRouterAttachmentPort(hitPort)
        && attachmentPortAvailable(*targetNode, hitPort, draft.endpointNode)) {
        attachmentPort = hitPort;
        exactSlot = exactSlotForPort(hitPort);
    } else if (hitPort == QtNodes::InvalidPortIndex) {
        attachmentPort = firstAvailableAttachmentPort(*targetNode, draft.endpointNode);
    }
    if (!attachmentPort) {
        restoreSelection();
        return false;
    }
    if (!attachNodeToRouter(
            draft.endpointNode, NocAttachmentTarget{*target->router, exactSlot})) {
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
    if (!m_editingEnabled || !m_scene || !m_view || m_routerEndpointDraft) {
        return false;
    }
    std::optional<QtNodes::NodeId> nodeId;
    QtNodes::PortIndex hitPort = QtNodes::InvalidPortIndex;
    qreal closestDistance = kInteractivePortHitRadius;
    for (auto iterator = m_metadata.constBegin();
         iterator != m_metadata.constEnd(); ++iterator) {
        if (iterator->kind != NocEditorSelection::Kind::Router
            || !iterator->router) {
            continue;
        }
        auto* candidate = m_scene->nodeGraphicsObject(iterator.key());
        if (!candidate) {
            continue;
        }
        for (QtNodes::PortIndex portIndex = kRouterEndpointInPort;
             portIndex < kRouterEndpointInPort
                 + static_cast<QtNodes::PortIndex>(m_routerAttachmentPorts.size());
             ++portIndex) {
            if (!attachmentPortAvailable(iterator.key(), portIndex)) {
                continue;
            }
            const QPoint portPosition = m_view->mapFromScene(candidate->mapToScene(
                m_scene->nodeGeometry().portPosition(
                    iterator.key(), QtNodes::PortType::In, portIndex)));
            const qreal distance = QLineF(
                QPointF(viewportPosition), QPointF(portPosition)).length();
            if (distance <= closestDistance) {
                closestDistance = distance;
                nodeId = iterator.key();
                hitPort = portIndex;
            }
        }
    }
    if (!nodeId || hitPort == QtNodes::InvalidPortIndex) {
        return false;
    }
    const auto metadata = m_metadata.constFind(*nodeId);
    auto* node = m_scene->nodeGraphicsObject(*nodeId);
    if (metadata == m_metadata.constEnd() || !metadata->router || !node) {
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
    if (!m_editingEnabled || !m_routerEndpointDraft || !m_view) {
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
    if (!attachNodeToRouter(
            *targetNode,
            NocAttachmentTarget{draft.router, exactSlotForPort(draft.portIndex)})) {
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
    if (!m_editingEnabled) {
        return false;
    }
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
    std::optional<QString> exactSlot;
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
                exactSlot = exactSlotForPort(start->portIndex);
            }
        }
    }

    if (!endpointNode || !router) {
        return false;
    }
    m_scene->resetDraftConnection();
    attachNodeToRouter(*endpointNode, NocAttachmentTarget{*router, exactSlot});
    rebuildGraph(false);
    return true;
}

bool NocNodeEditor::attachNodeToRouter(QtNodes::NodeId nodeId,
                                       NocAttachmentTarget target) {
    if (!m_editingEnabled) {
        return false;
    }
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
        const PendingEndpoint pendingEndpoint = *pending;
        const QPointF visualPosition = pendingEndpoint.scenePosition;
        if (pendingEndpoint.detached) {
            if (!detachedEndpointDropped
                || !detachedEndpointDropped(
                    *pendingEndpoint.detached,
                    target)) {
                return false;
            }
            const QString endpointId = pendingEndpoint.detached->endpoint.id;
            m_endpointLayout.insert(endpointId, visualPosition);
            saveWorkspaceLayout();
            m_pendingEndpoints.remove(metadata.id);
            m_selectedItems = {{NocEditorSelection::Kind::Endpoint, endpointId}};
            return true;
        }
        QSet<QString> endpointIdsBefore;
        if (m_design) {
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                endpointIdsBefore.insert(endpoint.id);
            }
        }
        if (!endpointTypeDropped
            || !endpointTypeDropped(metadata.endpointType, target)) {
            return false;
        }
        m_pendingEndpoints.remove(metadata.id);
        QString createdEndpointId;
        if (m_design) {
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                if (!endpointIdsBefore.contains(endpoint.id)
                    && endpoint.type == metadata.endpointType
                    && endpoint.attachment.router == target.router) {
                    createdEndpointId = endpoint.id;
                    break;
                }
            }
        }
        if (!createdEndpointId.isEmpty()) {
            m_endpointLayout.insert(createdEndpointId, visualPosition);
            saveWorkspaceLayout();
            for (SelectionIdentity& selected : m_selectedItems) {
                if (selected.kind == NocEditorSelection::Kind::PendingEndpoint
                    && selected.id == metadata.id) {
                    selected = {NocEditorSelection::Kind::Endpoint,
                                createdEndpointId};
                }
            }
        }
        return true;
    }
    if (metadata.kind == NocEditorSelection::Kind::Endpoint) {
        QPointF visualPosition = metadata.projectedPosition;
        if (m_graphModel->nodeExists(nodeId)) {
            visualPosition = m_graphModel->nodeData(
                nodeId, QtNodes::NodeRole::Position).toPointF();
        }
        if (metadata.router && *metadata.router == target.router
            && !target.exactSlot) {
            m_endpointLayout.insert(metadata.id, visualPosition);
            saveWorkspaceLayout();
            return true;
        }
        if (!endpointMoveRequested || !endpointMoveRequested(metadata.id, target)) {
            return false;
        }
        m_endpointLayout.insert(metadata.id, visualPosition);
        saveWorkspaceLayout();
        return true;
    }
    return false;
}

bool NocNodeEditor::detachEndpoint(QtNodes::NodeId nodeId,
                                   bool restoreProjectionOnFailure) {
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Endpoint
        || !m_design
        || !endpointRemovalRequested
        || !m_editingEnabled) {
        return false;
    }
    const auto endpoint = std::find_if(m_design->endpoints.cbegin(), m_design->endpoints.cend(),
                                       [&](const EndpointInstance& candidate) {
                                           return candidate.id == metadata->id;
                                       });
    if (endpoint == m_design->endpoints.cend()) {
        return false;
    }
    QPointF scenePosition = metadata->projectedPosition;
    if (m_graphModel->nodeExists(nodeId)) {
        scenePosition = m_graphModel->nodeData(
            nodeId, QtNodes::NodeRole::Position).toPointF();
    }
    const QString endpointId = metadata->id;
    const EndpointInstance detached = *endpoint;
    const EndpointDomainAssignments detachedAssignments =
        endpointDomainAssignments(*m_design, endpointId);
    QVector<DomainEdgeOverride> detachedOverrides;
    for (const DomainEdgeOverride& edgeOverride : m_design->edgeOverrides) {
        if (edgeOverride.edge
            == ElementRef{ElementKind::EndpointAttachment, endpointId}) {
            detachedOverrides.append(edgeOverride);
        }
    }
    QVector<ElementConfiguration> detachedConfigurations;
    for (const ElementConfiguration& configuration
         : m_design->elementConfigurations) {
        if (configuration.element
            == ElementRef{ElementKind::EndpointAttachment, endpointId}) {
            detachedConfigurations.append(configuration);
        }
    }
    if (!endpointRemovalRequested(endpointId)) {
        if (restoreProjectionOnFailure) {
            rebuildGraph(false);
        }
        return false;
    }
    const QString pendingId = QStringLiteral("pending-endpoint-%1")
                                  .arg(++m_nextPendingEndpoint);
    m_pendingEndpoints.insert(pendingId, PendingEndpoint{
        pendingId,
        detached.type,
        scenePosition,
        NocDetachedEndpointSnapshot{
            detached,
            detachedAssignments,
            detachedOverrides,
            detachedConfigurations}});
    m_selectedItems = {{NocEditorSelection::Kind::PendingEndpoint, pendingId}};
    rebuildGraph(false);
    return true;
}

void NocNodeEditor::showContextMenu(const QPoint& viewportPosition,
                                    const QPoint& globalPosition) {
    if (QtNodes::ConnectionGraphicsObject* connection = connectionAt(viewportPosition)) {
        if (m_scene && !connection->isSelected()) {
            m_scene->clearSelection();
            connection->setSelected(true);
        }
        showConnectionContextMenu(connection->connectionId(), globalPosition);
        return;
    }
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (nodeId) {
        if (m_scene) {
            if (auto* node = m_scene->nodeGraphicsObject(*nodeId)) {
                if (!node->isSelected()) {
                    m_scene->clearSelection();
                    node->setSelected(true);
                }
            }
        }
        showNodeContextMenu(*nodeId, globalPosition);
        return;
    }
    showCanvasCreateMenu(m_view->mapToScene(viewportPosition), globalPosition);
}

void NocNodeEditor::showConnectionContextMenu(
    QtNodes::ConnectionId connectionId,
    const QPoint& globalPosition) {
    if (!m_editingEnabled || !isEndpointAttachmentConnection(connectionId)) {
        return;
    }
    if (m_scene) {
        if (auto* connection = m_scene->connectionGraphicsObject(connectionId)) {
            if (!connection->isSelected()) {
                m_scene->clearSelection();
                connection->setSelected(true);
            }
        }
    }
    auto* menu = new QMenu(this);
    menu->setObjectName(workbench::connectionContextMenuName);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* disconnect = menu->addAction(
        QStringLiteral("Disconnect Endpoint from Router"));
    disconnect->setObjectName(workbench::disconnectConnectionActionName);
    connect(disconnect, &QAction::triggered, this, [this, connectionId] {
        m_graphModel->deleteConnection(connectionId);
    });
    menu->popup(globalPosition);
}

void NocNodeEditor::showCanvasCreateMenu(QPointF scenePosition,
                                         const QPoint& globalPosition) {
    if (!m_editingEnabled || !m_design || m_endpointTypes.isEmpty()) {
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
        if (m_editingEnabled && metadata->router) {
            QMenu* createMenu = menu->addMenu(QStringLiteral("Add Endpoint"));
            createMenu->setObjectName(workbench::createEndpointMenuName);
            for (const NocEndpointTypeItem& type : std::as_const(m_endpointTypes)) {
                QAction* action = createMenu->addAction(type.label);
                action->setData(type.id);
                const RouterPosition router = *metadata->router;
                connect(action, &QAction::triggered, this, [this, type, router] {
                    if (endpointTypeDropped) {
                        endpointTypeDropped(
                            type.id, NocAttachmentTarget{router, std::nullopt});
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
    if (!m_editingEnabled) {
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
            attachNodeToRouter(
                nodeId, NocAttachmentTarget{position, std::nullopt});
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
            const auto iterator = m_pendingEndpoints.constFind(endpointId);
            const QString durableEndpointId =
                iterator != m_pendingEndpoints.cend() && iterator->detached
                ? iterator->detached->endpoint.id : QString();
            m_pendingEndpoints.remove(endpointId);
            if (!durableEndpointId.isEmpty()
                && detachedEndpointDeletionRequested) {
                detachedEndpointDeletionRequested(durableEndpointId);
            }
            rebuildGraph(false);
        } else if (endpointDeletionRequested) {
            endpointDeletionRequested(endpointId);
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
    if (!m_editingEnabled || !m_design || endpointType.trimmed().isEmpty()) {
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
    std::optional<QString> exactSlot;
    if (auto* routerGraphics = m_scene->nodeGraphicsObject(*target)) {
        const QPointF localPosition = routerGraphics->mapFromScene(
            m_view->mapToScene(viewportPosition));
        const QtNodes::PortIndex hitPort = m_scene->nodeGeometry().checkPortHit(
            *target, QtNodes::PortType::In, localPosition);
        if (isRouterAttachmentPort(hitPort)) {
            if (!attachmentPortAvailable(*target, hitPort)) {
                return false;
            }
            exactSlot = exactSlotForPort(hitPort);
        }
    }
    return endpointTypeDropped
        && endpointTypeDropped(
            endpointType, NocAttachmentTarget{*metadata->router, exactSlot});
}

void NocNodeEditor::addPendingEndpoint(const QString& endpointType,
                                       QPointF scenePosition) {
    if (!m_editingEnabled) {
        return;
    }
    const QString id = QStringLiteral("pending-endpoint-%1")
                           .arg(++m_nextPendingEndpoint);
    m_pendingEndpoints.insert(
        id,
        PendingEndpoint{id, endpointType, scenePosition, std::nullopt});
    m_selectedItems = {{NocEditorSelection::Kind::PendingEndpoint, id}};
    rebuildGraph(false);
}

std::optional<RouterPosition> NocNodeEditor::routerAt(
    const QPointF& scenePosition) const {
    const std::optional<QtNodes::NodeId> routerNode = routerNodeAt(scenePosition);
    return routerNode ? m_metadata.value(*routerNode).router : std::nullopt;
}

std::optional<QtNodes::NodeId> NocNodeEditor::routerNodeAt(
    const QPointF& scenePosition) const {
    for (auto iterator = m_routerNodes.constBegin(); iterator != m_routerNodes.constEnd(); ++iterator) {
        auto* routerGraphics = m_scene
            ? m_scene->nodeGraphicsObject(iterator.value())
            : nullptr;
        if (routerGraphics
            && routerGraphics->sceneBoundingRect().contains(scenePosition)) {
            return iterator.value();
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

QtNodes::ConnectionGraphicsObject* NocNodeEditor::connectionAt(
    const QPoint& viewportPosition) const {
    if (!m_view) {
        return nullptr;
    }
    QtNodes::ConnectionGraphicsObject* firstConnection = nullptr;
    for (QGraphicsItem* hitItem : m_view->items(viewportPosition)) {
        QGraphicsItem* item = hitItem;
        while (item) {
            if (qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                return nullptr;
            }
            if (auto* connection =
                    qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item)) {
                if (isEndpointAttachmentConnection(connection->connectionId())) {
                    return connection;
                }
                if (!firstConnection) {
                    firstConnection = connection;
                }
                break;
            }
            item = item->parentItem();
        }
    }
    return firstConnection;
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

std::optional<QString> NocNodeEditor::exactSlotForPort(unsigned int portIndex) const {
    if (!isRouterAttachmentPort(portIndex)) {
        return std::nullopt;
    }
    const qsizetype offset = static_cast<qsizetype>(portIndex - kRouterEndpointInPort);
    return m_routerAttachmentPorts.value(offset).exactSlot;
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

std::optional<ElementRef> NocNodeEditor::elementForConnection(
    QtNodes::ConnectionId connectionId) const {
    if (m_scene) {
        if (auto* graphics = m_scene->connectionGraphicsObject(connectionId)) {
            const ElementKind kind = static_cast<ElementKind>(
                graphics->data(kSemanticElementKindDataRole).toInt());
            const QString id = graphics->data(kSemanticElementIdDataRole).toString();
            const ElementRef tagged{kind, id};
            const auto projected = m_elementConnections.constFind(tagged);
            if (kind != ElementKind::Invalid && !id.isEmpty()
                && projected != m_elementConnections.constEnd()
                && projected.value() == connectionId) {
                return tagged;
            }
        }
    }
    for (auto iterator = m_elementConnections.constBegin();
         iterator != m_elementConnections.constEnd(); ++iterator) {
        if (iterator.value() == connectionId) {
            return iterator.key();
        }
    }
    return std::nullopt;
}

std::optional<NocEditorSelection> NocNodeEditor::selectionForIdentity(
    const SelectionIdentity& identity) const {
    if (identity.kind == NocEditorSelection::Kind::PendingEndpoint) {
        const auto pending = m_pendingEndpoints.constFind(identity.id);
        if (pending == m_pendingEndpoints.constEnd()) {
            return std::nullopt;
        }
        return NocEditorSelection{
            identity.kind,
            pending->detached ? pending->detached->endpoint.id
                              : pending->type,
            std::nullopt};
    }

    const ElementKind elementKind = elementKindForSelection(identity.kind);
    if (elementKind == ElementKind::Invalid) {
        return std::nullopt;
    }
    const ElementRef element{elementKind, identity.id};
    if (elementKind == ElementKind::Router || elementKind == ElementKind::Endpoint) {
        const auto node = m_elementNodes.constFind(element);
        if (node == m_elementNodes.constEnd()) {
            return std::nullopt;
        }
        const auto metadata = m_metadata.constFind(node.value());
        if (metadata == m_metadata.constEnd()) {
            return std::nullopt;
        }
        return NocEditorSelection{
            identity.kind, identity.id, metadata->router};
    }
    if (!m_elementConnections.contains(element)) {
        return std::nullopt;
    }
    std::optional<RouterPosition> router;
    if (elementKind == ElementKind::EndpointAttachment && m_design) {
        const auto endpoint = std::find_if(
            m_design->endpoints.cbegin(), m_design->endpoints.cend(),
            [&identity](const EndpointInstance& candidate) {
                return candidate.id == identity.id;
            });
        if (endpoint != m_design->endpoints.cend()) {
            router = endpoint->attachment.router;
        }
    }
    return NocEditorSelection{identity.kind, identity.id, router};
}

void NocNodeEditor::emitSelectionChanged() {
    NocEditorSelectionSet semanticSelection;
    semanticSelection.items.reserve(m_selectedItems.size());
    for (const SelectionIdentity& identity : std::as_const(m_selectedItems)) {
        const std::optional<NocEditorSelection> item = selectionForIdentity(identity);
        if (item) {
            semanticSelection.items.append(*item);
        }
    }
    if (semanticSelectionChanged) {
        semanticSelectionChanged(semanticSelection);
    }
    if (selectionChanged) {
        selectionChanged(semanticSelection.items.size() == 1
                             ? semanticSelection.items.front()
                             : NocEditorSelection{});
    }
}

void NocNodeEditor::applyNodeStacking() {
    if (!m_scene) {
        return;
    }
    for (auto iterator = m_metadata.constBegin();
         iterator != m_metadata.constEnd(); ++iterator) {
        if (auto* node = m_scene->nodeGraphicsObject(iterator.key())) {
            node->setZValue(iterator->kind == NocEditorSelection::Kind::Router
                                ? kRouterNodeZValue
                                : kEndpointNodeZValue);
        }
    }
}

void NocNodeEditor::applyDomainPresentation() {
    if (!m_scene) {
        return;
    }

    for (auto iterator = m_metadata.constBegin();
         iterator != m_metadata.constEnd(); ++iterator) {
        auto* graphics = m_scene->nodeGraphicsObject(iterator.key());
        if (!graphics) {
            continue;
        }
        const ElementKind kind = elementKindForSelection(iterator->kind);
        const auto domainElement = kind == ElementKind::Invalid
            ? m_domainPresentation.elements.constEnd()
            : m_domainPresentation.elements.constFind(
                  ElementRef{kind, iterator->id});
        const DomainElementPresentation* presentation =
            domainElement == m_domainPresentation.elements.constEnd()
            ? nullptr : &domainElement.value();
        QVariantList colors;
        if (presentation) {
            colors.reserve(presentation->colors.size());
            for (const QColor& color : presentation->colors) {
                colors.append(color);
            }
        }
        graphics->setData(domainColorsDataRole, colors);
        graphics->setData(
            domainAssignmentStateDataRole,
            static_cast<int>(presentation
                                 ? presentation->state
                                 : DomainAssignmentDisplayState::Inactive));
        graphics->update();
    }

    for (auto iterator = m_elementConnections.constBegin();
         iterator != m_elementConnections.constEnd(); ++iterator) {
        auto* graphics = m_scene->connectionGraphicsObject(iterator.value());
        if (!graphics) {
            continue;
        }
        const auto domainCrossing = m_domainPresentation.crossings.constFind(
            iterator.key());
        const DomainCrossingPresentation* crossing =
            domainCrossing == m_domainPresentation.crossings.constEnd()
            ? nullptr : &domainCrossing.value();
        const bool hasOverride = crossing
            && (crossing->overridePolicy.has_value()
                || !crossing->overrideProperties.isEmpty());
        graphics->setData(domainCrossingDataRole, crossing != nullptr);
        graphics->setData(domainCrossingColorDataRole,
                          crossing ? crossing->primaryAccent : QColor{});
        graphics->setData(domainOverrideDataRole, hasOverride);
        graphics->update();
    }
}

void NocNodeEditor::restoreSelection() {
    if (!m_scene) {
        return;
    }
    const QVector<SelectionIdentity> requested = m_selectedItems;
    {
        const QSignalBlocker blocker(m_scene);
        m_scene->clearSelection();
        for (const SelectionIdentity& identity : requested) {
            if (identity.kind == NocEditorSelection::Kind::PendingEndpoint) {
                for (auto iterator = m_metadata.constBegin();
                     iterator != m_metadata.constEnd(); ++iterator) {
                    if (iterator->kind == identity.kind
                        && iterator->id == identity.id) {
                        if (auto* node = m_scene->nodeGraphicsObject(iterator.key())) {
                            node->setSelected(true);
                        }
                        break;
                    }
                }
                continue;
            }
            const ElementKind kind = elementKindForSelection(identity.kind);
            const ElementRef element{kind, identity.id};
            if (kind == ElementKind::Router || kind == ElementKind::Endpoint) {
                const auto node = m_elementNodes.constFind(element);
                if (node != m_elementNodes.constEnd()) {
                    if (auto* graphics = m_scene->nodeGraphicsObject(node.value())) {
                        graphics->setSelected(true);
                    }
                }
            } else if (kind == ElementKind::RouterLink
                       || kind == ElementKind::EndpointAttachment) {
                const auto connection = m_elementConnections.constFind(element);
                if (connection != m_elementConnections.constEnd()) {
                    if (auto* graphics = m_scene->connectionGraphicsObject(
                            connection.value())) {
                        graphics->setSelected(true);
                    }
                }
            }
        }
    }
    handleSceneSelectionChanged();
}

} // namespace finepaper
