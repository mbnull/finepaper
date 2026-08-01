#include "features/topology/noc_node_editor.h"

#include "application/endpoint_domain_assignment.h"
#include "features/domain/presentation/domain_text.h"
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

#include <QApplication>
#include <QBrush>
#include <QDragEnterEvent>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QHash>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>
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
constexpr int kInteractiveConnectionHitRadius = 8;
constexpr qreal kScenePortHitRadius = 10.0;
constexpr unsigned int kMaximumVisibleRouterAttachmentPorts = 8U;
constexpr qreal kRouterNodeZValue = 10.0;
constexpr qreal kEndpointNodeZValue = 20.0;
constexpr qsizetype kMaximumVisibleDomainStripSegments = 8;
constexpr qreal kRouterAttachmentPortSpacing = 28.0;
constexpr qreal kRouterAttachmentUpperStart = 38.0;
constexpr qreal kRouterAttachmentCenterGapTop = 14.0;
constexpr qreal kRouterAttachmentCenterGapBottom = 26.0;
constexpr qreal kRouterAttachmentBottomInset = 26.0;
constexpr int kRouterAttachmentAggregateHorizontalPadding = 28;
constexpr qsizetype kRouterAttachmentAggregateCacheCapacity = 64;
constexpr int kExpandedRouterSizeDelta = 32;

constexpr qreal squared(qreal value) {
    return value * value;
}

qreal squaredDistance(const QPointF& first, const QPointF& second) {
    return squared(first.x() - second.x())
        + squared(first.y() - second.y());
}
constexpr int kSemanticElementKindDataRole = 0x464e02;
constexpr int kSemanticElementIdDataRole = 0x464e03;

constexpr unsigned int visibleRouterAttachmentPortCount(
    unsigned int logicalCount) {
    return logicalCount > kMaximumVisibleRouterAttachmentPorts
        ? kMaximumVisibleRouterAttachmentPorts : logicalCount;
}

constexpr unsigned int individualVisibleRouterAttachmentPortCount(
    unsigned int logicalCount) {
    return logicalCount > kMaximumVisibleRouterAttachmentPorts
        ? kMaximumVisibleRouterAttachmentPorts - 1U : logicalCount;
}

QString routerAttachmentAggregateText(unsigned int logicalCount,
                                      bool overflow) {
    if (overflow) {
        return QStringLiteral("%1 more ports").arg(
            logicalCount
            - individualVisibleRouterAttachmentPortCount(logicalCount));
    }
    return QStringLiteral("%1 endpoint ports").arg(logicalCount);
}

constexpr bool hasRouterAttachmentPortOverflow(unsigned int logicalCount) {
    return logicalCount > kMaximumVisibleRouterAttachmentPorts;
}

constexpr unsigned int visualRouterAttachmentPortOffset(
    unsigned int logicalOffset,
    unsigned int logicalCount) {
    const unsigned int individualCount =
        individualVisibleRouterAttachmentPortCount(logicalCount);
    return hasRouterAttachmentPortOverflow(logicalCount)
            && logicalOffset >= individualCount
        ? individualCount : logicalOffset;
}

QPen draftConnectionPen(const QPalette& palette) {
    QPen pen(nocEditorColors(palette).accent, 2.5, Qt::DashLine);
    pen.setDashPattern({7.0, 5.0});
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

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
            m_attachmentPortLabels = {QStringLiteral("EP")};
        }
        const NocEditorColors visual = nocEditorColors(QApplication::palette());
        QtNodes::NodeStyle style = nodeStyle();
        style.ShadowEnabled = false;
        style.NormalBoundaryColor = m_pending
            ? visual.accent : visual.outlineStrong;
        style.SelectedBoundaryColor = visual.accent;
        style.ConnectionPointColor = visual.outlineStrong;
        style.FilledConnectionPointColor = visual.portFill;
        style.FontColor = visual.text;
        style.PenWidth = 1.4F;
        style.HoveredPenWidth = 2.0F;
        style.ConnectionPointDiameter = 10.0F;
        style.setBackgroundColor(router
                                     ? visual.routerSurface
                                     : m_pending
                                           ? visual.pendingSurface
                                           : visual.endpointSurface);
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
        if (model->isCollapsed()) {
            const QSize base = nocEditorMetrics().collapsedRouterSize;
            const int requiredEdge = attachmentAggregateTextEdge(
                model->attachmentPortCount(), false);
            const int edge = (std::max)(
                (std::max)(base.width(), base.height()), requiredEdge);
            return QSize(edge, edge);
        }
        const QSize base = nocEditorMetrics().expandedRouterSize;
        const unsigned int largestBandCount =
            (visibleRouterAttachmentPortCount(model->attachmentPortCount())
             + 1U) / 2U;
        const qreal requiredEdge = 104.0
            + static_cast<qreal>(largestBandCount > 0U
                                     ? largestBandCount - 1U : 0U)
                * kRouterAttachmentPortSpacing * 2.0;
        int edge = (std::max)(
            (std::max)(base.width(), base.height()),
            static_cast<int>(std::ceil(requiredEdge)));
        const int collapsedTextEdge = attachmentAggregateTextEdge(
            model->attachmentPortCount(), false);
        edge = (std::max)(
            edge, collapsedTextEdge + kExpandedRouterSizeDelta);
        if (hasRouterAttachmentPortOverflow(model->attachmentPortCount())) {
            edge = (std::max)(
                edge,
                attachmentAggregateTextEdge(
                    model->attachmentPortCount(), true));
        }
        return QSize(edge, edge);
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
            if (model->isCollapsed()) {
                return QPointF(0.0, height * 0.75);
            }
            const unsigned int logicalCount = (std::max)(
                1U, model->attachmentPortCount());
            const unsigned int count = visibleRouterAttachmentPortCount(
                logicalCount);
            const unsigned int attachmentIndex =
                visualRouterAttachmentPortOffset(
                    index - kRouterEndpointInPort, logicalCount);
            const unsigned int upperCount = (count + 1U) / 2U;
            const unsigned int lowerCount = count / 2U;
            const bool upperBand = attachmentIndex < upperCount;
            const unsigned int bandIndex = upperBand
                ? attachmentIndex : attachmentIndex - upperCount;
            const unsigned int bandCount = upperBand
                ? upperCount : lowerCount;
            const qreal bandStart = upperBand
                ? kRouterAttachmentUpperStart
                : height / 2.0 + kRouterAttachmentCenterGapBottom;
            const qreal bandEnd = upperBand
                ? height / 2.0 - kRouterAttachmentCenterGapTop
                : height - kRouterAttachmentBottomInset;
            const qreal y = bandCount <= 1U
                ? (bandStart + bandEnd) / 2.0
                : bandStart
                    + static_cast<qreal>(bandIndex)
                        * (bandEnd - bandStart)
                        / static_cast<qreal>(bandCount - 1U);
            return QPointF(0.0, y);
        }
        if (index == kRouterEastOutPort) return QPointF(width, height / 2.0);
        return QPointF(width / 2.0, height);
    }

    bool attachmentAggregateHit(QtNodes::NodeId nodeId,
                                const QPointF& nodePoint) const {
        const NocNodeModel* model = modelFor(nodeId);
        if (!model || !model->isRouter()
            || model->attachmentPortCount() == 0U) {
            return false;
        }
        const unsigned int logicalCount = model->attachmentPortCount();
        if (!model->isCollapsed()
            && !hasRouterAttachmentPortOverflow(logicalCount)) {
            return false;
        }
        const unsigned int visualOffset = model->isCollapsed()
            ? 0U : individualVisibleRouterAttachmentPortCount(logicalCount);
        const QPointF aggregate = portPosition(
            nodeId,
            QtNodes::PortType::In,
            kRouterEndpointInPort + visualOffset);
        return squaredDistance(nodePoint, aggregate)
            <= squared(kScenePortHitRadius);
    }

    QtNodes::PortIndex checkPortHit(
        QtNodes::NodeId nodeId,
        QtNodes::PortType type,
        QPointF nodePoint) const override {
        const NocNodeModel* model = modelFor(nodeId);
        if (!model || !model->isRouter()) {
            return QtNodes::AbstractNodeGeometry::checkPortHit(
                nodeId, type, nodePoint);
        }
        const auto hits = [this, nodeId, type, &nodePoint](
                              QtNodes::PortIndex index) {
            return squaredDistance(
                       nodePoint, portPosition(nodeId, type, index))
                <= squared(kScenePortHitRadius);
        };
        if (type == QtNodes::PortType::Out) {
            if (hits(kRouterEastOutPort)) {
                return kRouterEastOutPort;
            }
            return hits(kRouterSouthOutPort)
                ? kRouterSouthOutPort : QtNodes::InvalidPortIndex;
        }
        if (hits(kRouterWestInPort)) {
            return kRouterWestInPort;
        }
        if (hits(kRouterNorthInPort)) {
            return kRouterNorthInPort;
        }

        const unsigned int logicalCount = model->attachmentPortCount();
        if (!model->isCollapsed()) {
            const unsigned int individualCount =
                individualVisibleRouterAttachmentPortCount(logicalCount);
            for (unsigned int offset = 0; offset < individualCount; ++offset) {
                const QtNodes::PortIndex index =
                    kRouterEndpointInPort + offset;
                if (hits(index)) {
                    return index;
                }
            }
        }
        if (attachmentAggregateHit(nodeId, nodePoint)) {
            return kRouterEndpointInPort
                + (model->isCollapsed()
                       ? 0U
                       : individualVisibleRouterAttachmentPortCount(
                             logicalCount));
        }
        return QtNodes::InvalidPortIndex;
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

    static QRectF collapseButtonVisualRect(QSize nodeSize) {
        return QRectF(nodeSize.width() - 29.0, 6.0, 22.0, 22.0);
    }

    static QRectF collapseButtonHitRect(QSize nodeSize) {
        return QRectF(nodeSize.width() - 34.0, 1.0, 34.0, 34.0);
    }

private:
    int attachmentAggregateTextEdge(unsigned int logicalCount,
                                    bool overflow) const {
        const QFont captionFont = nocEditorFont(
            NocEditorFontRole::Caption, QApplication::font());
        if (captionFont != m_attachmentAggregateFont) {
            m_attachmentAggregateFont = captionFont;
            m_attachmentAggregateTextEdges.clear();
        }

        const quint64 cacheKey =
            (static_cast<quint64>(logicalCount) << 1U)
            | static_cast<quint64>(overflow);
        const auto cached = m_attachmentAggregateTextEdges.constFind(cacheKey);
        if (cached != m_attachmentAggregateTextEdges.cend()) {
            return *cached;
        }

        const int edge = QFontMetrics(captionFont).horizontalAdvance(
            routerAttachmentAggregateText(logicalCount, overflow))
            + kRouterAttachmentAggregateHorizontalPadding;
        if (m_attachmentAggregateTextEdges.size()
            >= kRouterAttachmentAggregateCacheCapacity) {
            m_attachmentAggregateTextEdges.clear();
        }
        m_attachmentAggregateTextEdges.insert(cacheKey, edge);
        return edge;
    }

    const NocNodeModel* modelFor(QtNodes::NodeId nodeId) const {
        auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&_graphModel);
        return graphModel ? graphModel->delegateModel<NocNodeModel>(nodeId) : nullptr;
    }

    mutable QFont m_attachmentAggregateFont;
    mutable QHash<quint64, int> m_attachmentAggregateTextEdges;
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
        const QPalette applicationPalette = QApplication::palette();
        const NocEditorColors visual = nocEditorColors(applicationPalette);
        const QColor background = !model
            ? visual.surfaceRaised
            : model->isRouter()
                ? visual.routerSurface
                : model->isPending()
                    ? visual.pendingSurface : visual.endpointSurface;
        const QColor header = model && model->isRouter()
            ? visual.routerHeader
            : model && model->isPending()
                ? visual.pendingHeader : visual.endpointHeader;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPainterPath bodyPath;
        bodyPath.addRoundedRect(body, 10.0, 10.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawPath(bodyPath);

        const qreal headerHeight = qMin<qreal>(34.0, body.height());
        painter->save();
        painter->setClipPath(bodyPath);
        painter->setBrush(header);
        painter->drawRect(QRectF(
            body.left(), body.top(), body.width(), headerHeight));
        painter->restore();

        const bool relatedHighlighted = node.data(relatedHighlightDataRole).toBool();
        QPen boundaryPen(node.isSelected()
                                 ? visual.accent
                                 : relatedHighlighted
                                       ? visual.warning
                                       : model && model->isPending()
                                             ? visual.accent
                                             : visual.outlineStrong,
                         node.isSelected() || relatedHighlighted ? 2.5 : 1.5);
        boundaryPen.setCapStyle(Qt::RoundCap);
        boundaryPen.setJoinStyle(Qt::RoundJoin);
        if (model && model->isPending()) {
            boundaryPen.setStyle(Qt::DashLine);
            boundaryPen.setDashPattern({6.0, 4.0});
        }
        painter->setPen(boundaryPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(bodyPath);

        if (model && model->isRouter()) {
            QPen corePen(visual.outline, 1.0);
            corePen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(corePen);
            painter->setBrush(Qt::NoBrush);
            const qreal inset = model->isCollapsed() ? 26.0 : 42.0;
            painter->drawRoundedRect(
                body.adjusted(inset, inset, -inset, -inset), 6.0, 6.0);
        }

        painter->setPen(visual.text);
        painter->setFont(nocEditorFont(
            NocEditorFontRole::Label, QApplication::font()));
        const QRectF captionRect = model && model->isRouter()
            ? (model->isCollapsed()
                   ? QRectF(body.left() + 14.0,
                            body.top() + body.height() * 0.30,
                            body.width() - 28.0,
                            body.height() * 0.30)
                   : body.adjusted(38.0, 38.0, -38.0, -38.0))
            : body.adjusted(12.0, 7.0, -12.0, -7.0);
        painter->drawText(captionRect,
                          Qt::AlignCenter | Qt::TextWordWrap,
                          model ? model->caption() : QString());

        if (model && model->isRouter()) {
            drawCollapseButton(painter, *model, size, visual);
        }

        drawDomainStrip(painter, node, body, visual);
        drawPorts(painter, node, model, geometry,
                  QtNodes::PortType::In, visual);
        drawPorts(painter, node, model, geometry,
                  QtNodes::PortType::Out, visual);
        painter->restore();
    }

private:
    static void drawDomainStrip(QPainter* painter,
                                const QtNodes::NodeGraphicsObject& node,
                                const QRectF& body,
                                const NocEditorColors& visual) {
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
        const QVariantList patternBrushes = node.data(
            domainPatternBrushesDataRole).toList();
        const qsizetype segmentCount = (std::min)(
            colorValues.size(), patternBrushes.size());
        if (segmentCount == 0) {
            return;
        }

        painter->save();
        QPainterPath stripPath;
        stripPath.addRoundedRect(strip, 3.0, 3.0);
        painter->setClipPath(stripPath);
        painter->setPen(Qt::NoPen);
        const auto fillTextured = [&](const QRectF& rectangle,
                                      const QColor& base,
                                      const QBrush& patternBrush) {
            painter->fillRect(rectangle, base);
            painter->fillRect(rectangle, patternBrush);
        };
        if (segmentCount == 1) {
            fillTextured(
                strip,
                colorValues.front().value<QColor>(),
                patternBrushes.front().value<QBrush>());
        } else {
            const qreal segmentWidth = strip.width()
                / static_cast<qreal>(segmentCount);
            for (qsizetype index = 0; index < segmentCount; ++index) {
                const qreal left = strip.left()
                    + static_cast<qreal>(index) * segmentWidth;
                const qreal right = index + 1 == segmentCount
                    ? strip.right() : left + segmentWidth;
                fillTextured(
                    QRectF(QPointF(left, strip.top()),
                           QPointF(right, strip.bottom())),
                    colorValues.at(index).value<QColor>(),
                    patternBrushes.at(index).value<QBrush>());
            }
        }
        QPen stripPen(visual.outlineStrong, 0.8);
        stripPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(stripPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(strip, 3.0, 3.0);
        painter->restore();
    }

    static void drawPorts(QPainter* painter,
                          QtNodes::NodeGraphicsObject& node,
                          const NocNodeModel* model,
                          QtNodes::AbstractNodeGeometry const& geometry,
                          QtNodes::PortType type,
                          const NocEditorColors& visual) {
        const unsigned int logicalCount = node.graphModel().nodeData<unsigned int>(
            node.nodeId(), type == QtNodes::PortType::In
                               ? QtNodes::NodeRole::InPortCount
                               : QtNodes::NodeRole::OutPortCount);
        QPen portPen(visual.outlineStrong, 1.25);
        portPen.setCapStyle(Qt::RoundCap);
        portPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(portPen);
        painter->setBrush(visual.portFill);
        unsigned int paintedCount = logicalCount;
        if (model && model->isRouter() && type == QtNodes::PortType::In) {
            paintedCount = kRouterEndpointInPort
                + (model->isCollapsed()
                       ? 1U
                       : visibleRouterAttachmentPortCount(
                             model->attachmentPortCount()));
        }
        QFont portFont = nocEditorFont(
            NocEditorFontRole::Caption, QApplication::font());
        portFont.setWeight(QFont::DemiBold);
        painter->setFont(portFont);
        const QFontMetricsF portMetrics(portFont);
        for (unsigned int index = 0; index < paintedCount; ++index) {
            const bool collapsedAggregate = model
                && model->isRouter() && model->isCollapsed()
                && type == QtNodes::PortType::In
                && index == kRouterEndpointInPort;
            const bool overflowAggregate = model
                && model->isRouter() && !model->isCollapsed()
                && type == QtNodes::PortType::In
                && hasRouterAttachmentPortOverflow(
                    model->attachmentPortCount())
                && index == kRouterEndpointInPort
                    + individualVisibleRouterAttachmentPortCount(
                          model->attachmentPortCount());
            const bool aggregateAttachment = collapsedAggregate
                || overflowAggregate;
            const QPointF center = geometry.portPosition(node.nodeId(), type, index);
            painter->drawEllipse(center, 5.0, 5.0);

            QString label = model ? model->portCaption(type, index) : QString();
            if (collapsedAggregate) {
                label = routerAttachmentAggregateText(
                    model->attachmentPortCount(), false);
            } else if (overflowAggregate) {
                label = routerAttachmentAggregateText(
                    model->attachmentPortCount(), true);
            }
            if (label.isEmpty()) {
                continue;
            }
            painter->setPen(visual.text);
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
                const bool westRouterPort = model && model->isRouter()
                    && type == QtNodes::PortType::In
                    && index == kRouterWestInPort;
                qreal labelLeft = 8.0;
                if (westRouterPort) {
                    labelLeft = model->isCollapsed() ? 12.0 : 40.0;
                }
                const qreal labelHeight = aggregateAttachment
                    ? (std::max)(16.0, portMetrics.height() + 2.0)
                    : 16.0;
                const qreal maximumWidth = (std::max)(
                    28.0,
                    static_cast<qreal>(nodeSize.width()) - labelLeft - 8.0);
                const qreal labelWidth = aggregateAttachment
                    ? (std::min)(
                          maximumWidth,
                          (std::max)(
                              48.0,
                              portMetrics.horizontalAdvance(label) + 6.0))
                    : 28.0;
                labelRect = QRectF(labelLeft,
                                   center.y() - labelHeight / 2.0,
                                   labelWidth,
                                   labelHeight);
            } else {
                labelRect = QRectF(nodeSize.width() - 36.0,
                                   center.y() - 8.0,
                                   28.0,
                                   16.0);
            }
            painter->drawText(labelRect, Qt::AlignCenter, label);
            painter->setPen(portPen);
            painter->setBrush(visual.portFill);
        }
    }

    static void drawCollapseButton(QPainter* painter,
                                   const NocNodeModel& model,
                                   QSize nodeSize,
                                   const NocEditorColors& visual) {
        const QRectF button = NocNodeGeometry::collapseButtonVisualRect(nodeSize);
        QPen buttonPen(visual.outlineStrong, 1.0);
        buttonPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(buttonPen);
        painter->setBrush(visual.surfaceRaised);
        painter->drawRoundedRect(button, 6.0, 6.0);

        QPen glyphPen(visual.text, 1.7);
        glyphPen.setCapStyle(Qt::RoundCap);
        painter->setPen(glyphPen);
        const QPointF center = button.center();
        painter->drawLine(QPointF(center.x() - 4.0, center.y()),
                          QPointF(center.x() + 4.0, center.y()));
        if (model.isCollapsed()) {
            painter->drawLine(QPointF(center.x(), center.y() - 4.0),
                              QPointF(center.x(), center.y() + 4.0));
        }
    }
};

class NocOrthogonalConnectionPainter final : public QtNodes::AbstractConnectionPainter {
public:
    void paint(QPainter* painter,
               QtNodes::ConnectionGraphicsObject const& connection) const override {
        const QPalette applicationPalette = QApplication::palette();
        const NocEditorColors visual = nocEditorColors(applicationPalette);
        QColor color = visual.connection;
        const bool relatedHighlighted = connection.data(
            relatedHighlightDataRole).toBool();
        const bool selected = connection.isSelected();
        const bool crossing = connection.data(domainCrossingDataRole).toBool();
        const bool hasOverride = connection.data(domainOverrideDataRole).toBool();
        const QColor crossingColor = connection.data(
            domainCrossingPaintColorDataRole).value<QColor>();
        if (selected) {
            color = visual.accent;
        } else if (relatedHighlighted) {
            color = visual.warning;
        } else if (crossing && crossingColor.isValid()) {
            color = crossingColor;
        } else if (connection.connectionState().hovered()) {
            color = visual.accent;
        }
        qreal width = 2.0;
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
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        if (crossing) {
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({6.0, 4.0});
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QPainterPath path = orthogonalConnectionPath(
            connection.out(), connection.in(), routeAxis(connection));
        painter->drawPath(path);
        if (hasOverride) {
            painter->save();
            painter->translate(path.pointAtPercent(0.5));
            painter->rotate(45.0);
            QPen markerPen(color, 1.5);
            markerPen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(markerPen);
            painter->setBrush(visual.surfaceRaised);
            painter->drawRoundedRect(
                QRectF(-4.5, -4.5, 9.0, 9.0), 1.5, 1.5);
            painter->restore();
        }
    }

    QPainterPath getPainterStroke(
        QtNodes::ConnectionGraphicsObject const& connection) const override {
        QPainterPathStroker stroker;
        qreal width = 12.0;
        if (connection.data(domainCrossingDataRole).toBool()) {
            width += 2.5;
        }
        if (connection.data(domainOverrideDataRole).toBool()) {
            width += 2.5;
        }
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
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
    applyNocEditorPalette(palette());
    m_registry->registerModel<NocNodeModel>(QStringLiteral("NoC"));

    m_scene = new NocGraphicsScene(
        static_cast<NocGraphModel&>(*m_graphModel), this);
    m_scene->setNodeGeometry(std::make_unique<NocNodeGeometry>(*m_graphModel));
    m_scene->setNodePainter(std::make_unique<NocBlockNodePainter>());
    m_scene->setConnectionPainter(std::make_unique<NocOrthogonalConnectionPainter>());
    m_view = new AnimatedGraphicsView(m_scene, this);
    m_view->semanticDeleteRequested = [this] {
        if (m_editingEnabled) {
            deleteSelectedSemanticItem();
        }
    };
    m_view->setObjectName(QStringLiteral("finepaper.canvasView"));
    m_view->setAccessibleName(QStringLiteral("NoC topology canvas"));
    updateCanvasAccessibleDescription();
    m_view->setScaleRange(0.25, 2.5);
    m_view->setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    setCanvasInteractionMode(NocCanvasInteractionMode::Pan);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
    graphModel.userConnectionPossible = [this](const QtNodes::ConnectionId& connection) {
        const bool hasSource = connection.outNodeId != QtNodes::InvalidNodeId;
        const bool hasTarget = connection.inNodeId != QtNodes::InvalidNodeId;
        const attachment::RuleDecision decision = attachment::connectionPossible(
            m_editingEnabled
                && m_attachmentPolicy.source
                    == attachment::PolicySource::Package,
            {hasSource
                 ? outputConnectionHandle(
                       connection.outNodeId, connection.outPortIndex)
                 : attachment::ConnectionHandleKind::Missing,
             hasTarget
                 ? inputConnectionHandle(
                       connection.inNodeId, connection.inPortIndex)
                 : attachment::ConnectionHandleKind::Missing});
        if (!decision.allowed) {
            return false;
        }
        return !hasTarget
            || attachmentPortAvailable(
                connection.inNodeId,
                connection.inPortIndex,
                hasSource
                    ? std::optional<QtNodes::NodeId>(connection.outNodeId)
                    : std::nullopt);
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
    // QGraphicsScene emits selectionChanged while its selected items are
    // removed. Disconnect before destroying the scene so teardown cannot
    // re-enter selection/highlight code with half-destroyed graphics items.
    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
    }
    if (m_graphModel) {
        disconnect(m_graphModel.get(), nullptr, this, nullptr);
        auto& graphModel = static_cast<NocGraphModel&>(*m_graphModel);
        graphModel.userConnectionPossible = {};
        graphModel.userConnectionDeletable = {};
    }
    if (m_view) {
        m_view->semanticDeleteRequested = {};
    }
    m_highlightedNodeIds.clear();
    m_highlightedConnectionIds.clear();
    m_stackingNodeIds.clear();
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    if (m_view && m_view->viewport()) {
        m_view->viewport()->removeEventFilter(this);
    }
    if (m_view) {
        m_view->removeEventFilter(this);
    }
    delete m_view;
    m_view = nullptr;
    delete m_scene;
    m_scene = nullptr;
    m_graphModel.reset();
}

void NocNodeEditor::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    const bool geometryChanged = event->type() == QEvent::FontChange
        || event->type() == QEvent::ApplicationFontChange
        || event->type() == QEvent::StyleChange;
    if (event->type() != QEvent::PaletteChange
        && event->type() != QEvent::ApplicationPaletteChange
        && event->type() != QEvent::FontChange
        && event->type() != QEvent::ApplicationFontChange
        && event->type() != QEvent::StyleChange) {
        return;
    }

    applyNocEditorPalette(palette());
    applyDomainPresentation();
    if (m_routerEndpointDraft && m_routerEndpointDraft->graphicsItem) {
        m_routerEndpointDraft->graphicsItem->setPen(
            draftConnectionPen(palette()));
    }
    if (m_endpointAttachmentDraft
        && m_endpointAttachmentDraft->graphicsItem) {
        m_endpointAttachmentDraft->graphicsItem->setPen(
            draftConnectionPen(palette()));
    }
    if (geometryChanged && m_scene && m_graphModel) {
        for (const QtNodes::NodeId nodeId : m_graphModel->allNodeIds()) {
            if (auto* node = m_scene->nodeGraphicsObject(nodeId)) {
                node->setGeometryChanged();
                m_scene->nodeGeometry().recomputeSize(nodeId);
                node->moveConnections();
            }
        }
    }
    if (m_scene) {
        m_scene->update();
    }
    if (m_view && m_view->viewport()) {
        m_view->viewport()->update();
    }
}

void NocNodeEditor::setDesign(const NocDesign* design) {
    applyDesign(design, m_attachmentPolicy);
}

void NocNodeEditor::setReducedMotion(bool reduced) {
    if (m_view) {
        m_view->setReducedMotion(reduced);
    }
}

bool NocNodeEditor::reducedMotion() const {
    return m_view && m_view->reducedMotion();
}

attachment::SlotResolution NocNodeEditor::endpointAttachmentAvailability(
    RouterPosition router) const {
    attachment::SlotResolution unavailable;
    if (!m_editingEnabled || !m_design) {
        unavailable.rejection = attachment::Rejection::ReadOnly;
        return unavailable;
    }
    return attachment::resolveSlot(
        *m_design,
        m_attachmentIndex,
        NocAttachmentTarget{router, std::nullopt},
        {},
        attachment::SlotResolutionDetail::ValidateOnly);
}

void NocNodeEditor::setDesign(
    const NocDesign* design,
    attachment::Policy policy) {
    applyDesign(design, std::move(policy));
}

void NocNodeEditor::applyDesign(
    const NocDesign* design,
    attachment::Policy policy) {
    ++m_graphRevision;
    m_pendingConnectionDetachments.clear();
    const std::optional<TopologyWorkspaceIdentity> workspaceIdentity = design
        ? std::optional<TopologyWorkspaceIdentity>{TopologyWorkspaceIdentity{
              design->package.id, design->package.version, design->id}}
        : std::nullopt;
    const bool newDocumentProjection =
        workspaceIdentity != m_workspaceIdentity || m_workspaceReloadPending;
    if (newDocumentProjection) {
        m_workspaceIdentity = workspaceIdentity;
        m_workspaceReloadPending = false;
        loadWorkspaceState();
        m_pendingEndpoints.clear();
        m_selectedItems.clear();
    }
    m_attachmentPolicy = std::move(policy);
    m_design = design ? std::optional<NocDesign>(*design) : std::nullopt;
    m_attachmentIndex = m_design
        ? attachment::buildDesignIndex(*m_design, m_attachmentPolicy)
        : attachment::DesignIndex{};
    if (m_semanticMutationDepth > 0) {
        m_projectionRefreshDeferred = true;
        m_projectionZoomDeferred =
            m_projectionZoomDeferred || newDocumentProjection;
        return;
    }
    rebuildGraph(newDocumentProjection);
}

void NocNodeEditor::beginDocumentSession(QString sessionToken) {
    if (sessionToken == m_documentSessionToken) {
        return;
    }
    m_documentSessionToken = std::move(sessionToken);
    ++m_graphRevision;
    m_workspaceReloadPending = true;
    m_lastWorkspaceDiagnostic.clear();
    m_lastWorkspaceDiagnosticKind = std::nullopt;
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    if (m_view) {
        m_view->endEndpointDrag();
    }
    m_pendingEndpoints.clear();
    m_pendingConnectionDetachments.clear();
    m_selectedItems.clear();
    m_nextPendingEndpoint = 0;
    m_canvasSelectionGesture = false;
    m_canvasItemGesture = false;
}

void NocNodeEditor::syncDesignState(const NocDesign& design) {
    m_design = design;
    m_attachmentIndex = attachment::buildDesignIndex(
        *m_design, m_attachmentPolicy);
}

void NocNodeEditor::setEndpointTypes(QVector<NocEndpointTypeItem> endpointTypes) {
    m_endpointTypes = std::move(endpointTypes);
}

void NocNodeEditor::setAttachmentPolicy(attachment::Policy policy) {
    if (m_attachmentPolicy == policy) {
        return;
    }
    m_attachmentPolicy = std::move(policy);
    m_attachmentIndex = m_design
        ? attachment::buildDesignIndex(*m_design, m_attachmentPolicy)
        : attachment::DesignIndex{};
    if (m_design) {
        if (m_semanticMutationDepth > 0) {
            m_projectionRefreshDeferred = true;
        } else {
            rebuildGraph(false);
        }
    }
}

void NocNodeEditor::beginSemanticMutation() {
    ++m_semanticMutationDepth;
}

void NocNodeEditor::endSemanticMutation(bool refreshProjection) {
    if (m_semanticMutationDepth <= 0) {
        return;
    }
    --m_semanticMutationDepth;
    if (m_semanticMutationDepth > 0) {
        m_projectionRefreshDeferred =
            m_projectionRefreshDeferred || refreshProjection;
        return;
    }
    const bool shouldRefresh =
        refreshProjection || m_projectionRefreshDeferred;
    const bool zoomToContents = m_projectionZoomDeferred;
    m_projectionRefreshDeferred = false;
    m_projectionZoomDeferred = false;
    if (shouldRefresh) {
        rebuildGraph(zoomToContents);
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
    if (m_selectedItems.size() == 1) {
        ensureSelectionVisible(m_selectedItems.front());
    }
    if (m_view) {
        m_view->setFocus(Qt::OtherFocusReason);
    }
}

bool NocNodeEditor::setRouterVisualPosition(const QString& routerId, QPointF position) {
    if (!m_routerNodes.contains(routerId)
        || !std::isfinite(position.x())
        || !std::isfinite(position.y())) {
        return false;
    }
    m_workspaceState.routerPositionOverrides.insert(routerId, position);
    saveWorkspaceState();
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
    if (!m_workspaceState.collapsedRouterIds) {
        m_workspaceState.collapsedRouterIds.emplace();
    }
    if (collapsed) {
        m_workspaceState.collapsedRouterIds->insert(routerId);
    } else {
        m_workspaceState.collapsedRouterIds->remove(routerId);
    }
    saveWorkspaceState();
    rebuildGraph(false);
    return true;
}

bool NocNodeEditor::routerCollapsed(const QString& routerId) const {
    return m_workspaceState.collapsedRouterIds
        && m_workspaceState.collapsedRouterIds->contains(routerId);
}

TopologyWorkspaceRegularizeResult NocNodeEditor::regularizeLayout() {
    const bool repairingWorkspace = m_workspacePersistenceBlocked;
    m_workspacePersistenceBlocked = false;
    const TopologyWorkspaceState previousState = m_workspaceState;
    m_workspaceState.routerPositionOverrides.clear();
    m_workspaceState.endpointPositionOverrides.clear();
    const bool saved = saveWorkspaceState();
    if (!saved) {
        // Keep the visible workspace and the durable record in agreement. A
        // failed Regularize can be retried after storage becomes available;
        // it must not silently discard this session's placement state.
        m_workspaceState = previousState;
    }
    if (repairingWorkspace) {
        m_workspacePersistenceBlocked = !saved;
        if (saved) {
            reportWorkspaceDiagnostic(
                TopologyWorkspaceDiagnosticKind::RepairSucceeded);
        }
    }
    rebuildGraph(false);
    if (!saved) {
        return TopologyWorkspaceRegularizeResult::SaveFailed;
    }
    return repairingWorkspace
        ? TopologyWorkspaceRegularizeResult::Repaired
        : TopologyWorkspaceRegularizeResult::Saved;
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
    if (m_view) {
        QString layerLabel = domain_text::domainTypeDisplayText(
            m_domainPresentation.activeDomainType,
            m_domainPresentation.domainTypeLabel);
        if (m_domainPresentation.status == DomainPresentationStatus::Inactive
            || m_domainPresentation.activeDomainType.trimmed().isEmpty()) {
            m_view->setDomainLegend({}, {}, {});
        } else {
            QVector<CanvasDomainLegendEntry> entries;
            entries.reserve(m_domainPresentation.legend.size());
            for (const DomainLegendEntry& entry : m_domainPresentation.legend) {
                entries.append(CanvasDomainLegendEntry{
                    entry.id,
                    domain_text::domainInstanceDisplayText(
                        entry.id, entry.name),
                    entry.color,
                });
            }
            const QString emptyMessage =
                m_domainPresentation.status
                        == DomainPresentationStatus::MissingPackageType
                    ? QStringLiteral("Layer unavailable for this package")
                    : QStringLiteral("No assigned domains");
            m_view->setDomainLegend(
                std::move(layerLabel), std::move(entries), emptyMessage);
        }
        updateCanvasAccessibleDescription();
    }
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
    ++m_graphRevision;
    m_pendingConnectionDetachments.clear();
    clearEndpointAttachmentDraft();
    clearRouterEndpointDraft();
    clearNeighborhoodHighlight();
    m_stackingNodeIds.clear();
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
    QSet<QString> projectedRouterIds;
    for (const RouterView& router : projection.routers) {
        projectedRouterIds.insert(router.id);
    }
    QSet<QString> designEndpointIds;
    for (const EndpointInstance& endpoint : m_design->endpoints) {
        designEndpointIds.insert(endpoint.id);
    }
    bool workspaceChanged = m_workspaceState.retainKnownElements(
        projectedRouterIds, designEndpointIds);
    if (!m_workspaceState.collapsedRouterIds) {
        m_workspaceState.collapsedRouterIds.emplace();
        for (const RouterView& router : projection.routers) {
            m_workspaceState.collapsedRouterIds->insert(router.id);
        }
        workspaceChanged = true;
    }
    if (workspaceChanged) {
        saveWorkspaceState();
    }
    QStringList attachmentPortLabels;
    attachmentPortLabels.reserve(m_attachmentPolicy.ports.size());
    for (const attachment::PortDefinition& port : m_attachmentPolicy.ports) {
        attachmentPortLabels.append(port.label.trimmed().isEmpty()
                                        ? QStringLiteral("EP")
                                        : port.label);
    }
    QHash<QString, RouterPosition> routerPositions;
    for (const RouterView& router : projection.routers) {
        routerPositions.insert(router.id, router.position);
        const QPointF visualPosition =
            m_workspaceState.routerPositionOverrides.value(
                router.id, routerScenePosition(router.position));
        const QtNodes::NodeId nodeId = graphModel.addProjectedNode(
            router.id,
            visualPosition,
            true,
            m_workspaceState.collapsedRouterIds->contains(router.id),
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

    QHash<QString, int> endpointOffsets;
    for (const EndpointView& endpoint : projection.endpoints) {
        if (m_workspaceState.collapsedRouterIds->contains(endpoint.routerId)) {
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
        const QPointF endpointPosition =
            m_workspaceState.endpointPositionOverrides.value(
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
        const auto occupancy = m_attachmentIndex.routers.constFind(
            endpoint.routerId);
        if (occupancy == m_attachmentIndex.routers.constEnd()) {
            continue;
        }
        const std::optional<attachment::PortOffset> attachmentOffset =
            occupancy->layout.portForEndpoint(endpoint.id);
        if (!attachmentOffset) {
            continue;
        }
        const QtNodes::ConnectionId connection{
            endpointNode, kEndpointOutPort,
            routerNode,
            kRouterEndpointInPort
                + static_cast<QtNodes::PortIndex>(*attachmentOffset)};
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
            node->setZValue(
                iterator->kind == NocEditorSelection::Kind::Router
                    ? kRouterNodeZValue : kEndpointNodeZValue);
            if (iterator->kind == NocEditorSelection::Kind::Router) {
                const QString action = routerCollapsed(iterator->id)
                    ? QStringLiteral("expand")
                    : QStringLiteral("collapse");
                node->setToolTip(
                    QStringLiteral(
                        "Router %1. Click the top-right control to %2 "
                        "details, or select the Router and press Space.")
                        .arg(iterator->id, action));
            }
        }
    }
    applyDomainPresentation();
    restoreSelection();

    if (zoomToContents) {
        deferForCurrentGraph([this] { zoomToFit(); });
    }
}

bool NocNodeEditor::eventFilter(QObject* watched, QEvent* event) {
    if (!m_view
        || (watched != m_view && watched != m_view->viewport())) {
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_view && event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::KeyPress: {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool backwards = key->key() == Qt::Key_Backtab
            || (key->key() == Qt::Key_Tab
                && key->modifiers() == Qt::ShiftModifier);
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
            && (key->modifiers() == Qt::NoModifier
                || key->modifiers() == Qt::ShiftModifier)) {
            if (selectAdjacentCanvasItem(backwards)) {
                key->accept();
                return true;
            }
            return focusNextPrevChild(!backwards);
        }
        if ((key->key() == Qt::Key_Menu
             && key->modifiers() == Qt::NoModifier)
            || (key->key() == Qt::Key_F10
                && key->modifiers() == Qt::ShiftModifier)) {
            if (showSelectedContextMenuFromKeyboard()) {
                key->accept();
                return true;
            }
            return false;
        }
        if (key->key() == Qt::Key_Space
            && key->modifiers() == Qt::NoModifier
            && m_selectedItems.size() == 1
            && m_selectedItems.front().kind
                == NocEditorSelection::Kind::Router) {
            const QString routerId = m_selectedItems.front().id;
            setRouterCollapsed(routerId, !routerCollapsed(routerId));
            key->accept();
            return true;
        }
        if (key->key() == Qt::Key_Delete
            && key->modifiers() == Qt::NoModifier
            && m_editingEnabled
            && deleteSelectedSemanticItem()) {
            key->accept();
            return true;
        }
        return false;
    }
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
            endpointDragTargetAt(position));
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
            endpointDragTargetAt(position));
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
            endpointDragTargetAt(position));
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
                deferForCurrentGraph([this] {
                    if (m_view) {
                        m_view->setDragMode(m_view->persistentDragMode());
                    }
                });
            }
            deferForCurrentGraph([this, position] {
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

    applyNodeStacking();
    QVector<SelectionIdentity> selected;
    QSet<QtNodes::NodeId> selectedNodeIds;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
            const auto metadata = m_metadata.constFind(node->nodeId());
            if (metadata != m_metadata.constEnd()) {
                selectedNodeIds.insert(node->nodeId());
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
    m_stackingNodeIds = std::move(selectedNodeIds);

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
                setNodeRelatedHighlight(connection->outNodeId);
                setNodeRelatedHighlight(connection->inNodeId);
            }
        }
    }
    deferForCurrentGraph([this] { applyNodeStacking(); });
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
            const bool nodeMoved = squaredDistance(
                currentPosition, hitMetadata->projectedPosition) >= squared(4.0);
            const QPointF scenePosition = m_view->mapToScene(viewportPosition);
            const QPointF localPosition = node->mapFromScene(scenePosition);
            const QSize nodeSize = m_scene->nodeGeometry().size(*hitNodeId);
            if (!nodeMoved
                && NocNodeGeometry::collapseButtonHitRect(nodeSize).contains(localPosition)) {
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
        if (squaredDistance(position, metadata.projectedPosition)
            < squared(4.0)) {
            continue;
        }
        if (!m_editingEnabled
            && metadata.kind != NocEditorSelection::Kind::Router) {
            readOnlyEndpointMoved = true;
            continue;
        }

        if (metadata.kind == NocEditorSelection::Kind::Router) {
            m_workspaceState.routerPositionOverrides.insert(
                metadata.id, position);
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
            m_workspaceState.endpointPositionOverrides.insert(
                metadata.id, position);
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
        saveWorkspaceState();
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
        deferForCurrentGraph([this] { rebuildGraph(false); });
        return;
    }
    const auto source = m_metadata.constFind(connectionId.outNodeId);
    const auto target = m_metadata.constFind(connectionId.inNodeId);
    if (source == m_metadata.constEnd()
        || target == m_metadata.constEnd()
        || target->kind != NocEditorSelection::Kind::Router
        || !isRouterAttachmentPort(connectionId.inPortIndex)
        || !target->router) {
        deferForCurrentGraph([this] { rebuildGraph(false); });
        return;
    }
    const QtNodes::NodeId sourceNode = connectionId.outNodeId;
    const attachment::TargetResolution resolution = resolveAttachmentTarget(
        connectionId.inNodeId,
        attachment::RouterHitKind::AttachmentPort,
        connectionId.inPortIndex,
        sourceNode);
    if (!resolution.decision.allowed || !resolution.target) {
        reportAttachmentRejection(
            resolution.decision.rejection, target->router);
        deferForCurrentGraph([this] { rebuildGraph(false); });
        return;
    }
    const NocAttachmentTarget attachmentTarget = *resolution.target;
    deferForCurrentGraph([this, sourceNode, attachmentTarget] {
        if (!attachNodeToRouter(sourceNode, attachmentTarget)) {
            rebuildGraph(false);
        }
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
    deferForCurrentGraph([this, endpointId] {
        m_pendingConnectionDetachments.remove(endpointId);
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
    const QPoint portPosition = m_view->mapFromScene(node->mapToScene(
        m_scene->nodeGeometry().portPosition(
            *nodeId, QtNodes::PortType::Out, kEndpointOutPort)));
    if (squaredDistance(QPointF(viewportPosition), QPointF(portPosition))
        > squared(kInteractivePortHitRadius)) {
        return false;
    }

    const QPointF startScenePosition = node->mapToScene(
        m_scene->nodeGeometry().portPosition(
            *nodeId, QtNodes::PortType::Out, kEndpointOutPort));
    auto* graphicsItem = new QGraphicsPathItem;
    graphicsItem->setPen(draftConnectionPen(palette()));
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

    const attachment::TargetResolution resolution = resolveAttachmentTargetAt(
        *targetNode,
        m_view->mapToScene(viewportPosition),
        draft.endpointNode);
    if (!resolution.decision.allowed || !resolution.target) {
        reportAttachmentRejection(
            resolution.decision.rejection, target->router);
        restoreSelection();
        return false;
    }
    if (!attachNodeToRouter(draft.endpointNode, *resolution.target)) {
        restoreSelection();
        return false;
    }
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
    const std::optional<QtNodes::NodeId> nodeId = nodeAt(viewportPosition);
    if (!nodeId) {
        return false;
    }
    const auto metadata = m_metadata.constFind(*nodeId);
    auto* node = m_scene->nodeGraphicsObject(*nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router || !node) {
        return false;
    }

    const unsigned int logicalAttachmentCount =
        static_cast<unsigned int>(m_attachmentPolicy.ports.size());
    const bool collapsed = routerCollapsed(metadata->id);
    const bool hasAggregate = collapsed
        || hasRouterAttachmentPortOverflow(logicalAttachmentCount);
    const unsigned int individualCount = collapsed
        ? 0U
        : individualVisibleRouterAttachmentPortCount(
              logicalAttachmentCount);
    const auto& geometry = m_scene->nodeGeometry();
    QtNodes::PortIndex hitPort = QtNodes::InvalidPortIndex;
    QPointF startScenePosition;
    qreal closestSquaredDistance = squared(
        kInteractivePortHitRadius + 0.001);
    for (unsigned int offset = 0; offset < individualCount; ++offset) {
        const QtNodes::PortIndex portIndex = kRouterEndpointInPort + offset;
        if (!attachmentPortAvailable(*nodeId, portIndex)) {
            continue;
        }
        const QPointF candidateScenePosition = node->mapToScene(
            geometry.portPosition(
                *nodeId, QtNodes::PortType::In, portIndex));
        const QPoint candidateViewportPosition = m_view->mapFromScene(
            candidateScenePosition);
        const qreal candidateSquaredDistance = squaredDistance(
            QPointF(viewportPosition),
            QPointF(candidateViewportPosition));
        if (candidateSquaredDistance < closestSquaredDistance) {
            closestSquaredDistance = candidateSquaredDistance;
            hitPort = portIndex;
            startScenePosition = candidateScenePosition;
        }
    }

    if (hasAggregate) {
        const unsigned int aggregateOffset = collapsed ? 0U : individualCount;
        const QPointF aggregateScenePosition = node->mapToScene(
            geometry.portPosition(
                *nodeId,
                QtNodes::PortType::In,
                kRouterEndpointInPort + aggregateOffset));
        const qreal aggregateSquaredDistance = squaredDistance(
            QPointF(viewportPosition),
            QPointF(m_view->mapFromScene(aggregateScenePosition)));
        if (aggregateSquaredDistance < closestSquaredDistance) {
            const attachment::TargetResolution bodyResolution =
                resolveAttachmentTarget(
                    *nodeId,
                    attachment::RouterHitKind::Body,
                    std::nullopt,
                    std::nullopt);
            if (bodyResolution.decision.allowed
                && bodyResolution.displayPort) {
                hitPort = kRouterEndpointInPort
                    + static_cast<QtNodes::PortIndex>(
                        *bodyResolution.displayPort);
                startScenePosition = aggregateScenePosition;
            }
        }
    }
    if (hitPort == QtNodes::InvalidPortIndex) {
        return false;
    }

    auto* graphicsItem = new QGraphicsPathItem;
    graphicsItem->setPen(draftConnectionPen(palette()));
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
    const attachment::TargetResolution resolution = resolveAttachmentTarget(
        draft.routerNode,
        attachment::RouterHitKind::AttachmentPort,
        draft.portIndex,
        *targetNode);
    if (!resolution.decision.allowed || !resolution.target) {
        reportAttachmentRejection(
            resolution.decision.rejection, draft.router);
        restoreSelection();
        return false;
    }
    if (!attachNodeToRouter(*targetNode, *resolution.target)) {
        restoreSelection();
        return false;
    }
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
    std::optional<QtNodes::NodeId> endpointNode = std::nullopt;
    std::optional<QtNodes::NodeId> routerNode = std::nullopt;
    attachment::RouterHitKind hitKind = attachment::RouterHitKind::Body;
    std::optional<unsigned int> hitPort = std::nullopt;
    bool hitRouterAtPointer = false;
    const NodeMetadata startMetadata = m_metadata.value(start->nodeId);
    if (start->startFromOutput
        && start->portIndex == kEndpointOutPort
        && (startMetadata.kind == NocEditorSelection::Kind::Endpoint
            || startMetadata.kind == NocEditorSelection::Kind::PendingEndpoint)) {
        endpointNode = start->nodeId;
        routerNode = routerNodeAt(scenePosition);
        hitRouterAtPointer = true;
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
                routerNode = start->nodeId;
                hitKind = attachment::RouterHitKind::AttachmentPort;
                hitPort = start->portIndex;
            }
        }
    }

    if (!endpointNode || !routerNode) {
        return false;
    }
    const attachment::TargetResolution resolution = hitRouterAtPointer
        ? resolveAttachmentTargetAt(
              *routerNode, scenePosition, *endpointNode)
        : resolveAttachmentTarget(
              *routerNode, hitKind, hitPort, *endpointNode);
    if (!resolution.decision.allowed || !resolution.target) {
        const auto router = m_metadata.constFind(*routerNode);
        reportAttachmentRejection(
            resolution.decision.rejection,
            router == m_metadata.constEnd() ? std::nullopt : router->router);
        return false;
    }
    m_scene->resetDraftConnection();
    if (!attachNodeToRouter(*endpointNode, *resolution.target)) {
        rebuildGraph(false);
        return false;
    }
    return true;
}

bool NocNodeEditor::attachNodeToRouter(QtNodes::NodeId nodeId,
                                       NocAttachmentTarget target) {
    if (!m_editingEnabled
        || m_attachmentPolicy.source != attachment::PolicySource::Package) {
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
        attachment::EndpointSubject subject;
        subject.lifecycle = pendingEndpoint.detached
            ? attachment::EndpointLifecycle::Detached
            : attachment::EndpointLifecycle::PendingNew;
        if (pendingEndpoint.detached) {
            subject.endpointId = pendingEndpoint.detached->endpoint.id;
            subject.durableAttachment =
                pendingEndpoint.detached->endpoint.attachment;
        }
        const attachment::TransitionPlan plan = attachment::decideTransition(
            m_editingEnabled,
            m_attachmentPolicy,
            {subject, attachment::TransitionIntent::Attach, target});
        if (!plan.decision.allowed) {
            reportAttachmentRejection(plan.decision.rejection, target.router);
            return false;
        }
        if (plan.command
            == attachment::AttachmentCommandKind::RestoreDetachedEndpoint) {
            if (!detachedEndpointDropped) {
                return false;
            }
            beginSemanticMutation();
            const bool restored = detachedEndpointDropped(
                *pendingEndpoint.detached, target);
            if (!restored) {
                endSemanticMutation(true);
                return false;
            }
            const QString endpointId = pendingEndpoint.detached->endpoint.id;
            m_workspaceState.endpointPositionOverrides.insert(
                endpointId, visualPosition);
            saveWorkspaceState();
            m_pendingEndpoints.remove(metadata.id);
            m_selectedItems = {{NocEditorSelection::Kind::Endpoint, endpointId}};
            endSemanticMutation(true);
            return true;
        }
        if (plan.command
            != attachment::AttachmentCommandKind::CreateEndpoint
            || !endpointTypeDropped) {
            return false;
        }
        beginSemanticMutation();
        const attachment::CreateEndpointResult created = endpointTypeDropped(
            metadata.endpointType, target);
        if (!created.success || created.endpointId.trimmed().isEmpty()) {
            endSemanticMutation(true);
            return false;
        }
        m_pendingEndpoints.remove(metadata.id);
        m_workspaceState.endpointPositionOverrides.insert(
            created.endpointId, visualPosition);
        saveWorkspaceState();
        for (SelectionIdentity& selected : m_selectedItems) {
            if (selected.kind == NocEditorSelection::Kind::PendingEndpoint
                && selected.id == metadata.id) {
                selected = {NocEditorSelection::Kind::Endpoint,
                            created.endpointId};
            }
        }
        endSemanticMutation(true);
        return true;
    }
    if (metadata.kind == NocEditorSelection::Kind::Endpoint) {
        if (!m_design) {
            return false;
        }
        const auto endpoint = std::find_if(
            m_design->endpoints.cbegin(),
            m_design->endpoints.cend(),
            [&](const EndpointInstance& candidate) {
                return candidate.id == metadata.id;
            });
        if (endpoint == m_design->endpoints.cend()) {
            return false;
        }
        const attachment::TransitionPlan plan = attachment::decideTransition(
            m_editingEnabled,
            m_attachmentPolicy,
            {{attachment::EndpointLifecycle::Attached,
              endpoint->id,
              endpoint->attachment},
             attachment::TransitionIntent::Attach,
             target});
        if (!plan.decision.allowed) {
            reportAttachmentRejection(plan.decision.rejection, target.router);
            return false;
        }
        QPointF visualPosition = metadata.projectedPosition;
        if (m_graphModel->nodeExists(nodeId)) {
            visualPosition = m_graphModel->nodeData(
                nodeId, QtNodes::NodeRole::Position).toPointF();
        }
        if (plan.command
            == attachment::AttachmentCommandKind::PreserveAttachedEndpoint) {
            m_workspaceState.endpointPositionOverrides.insert(
                metadata.id, visualPosition);
            saveWorkspaceState();
            return true;
        }
        if (plan.command != attachment::AttachmentCommandKind::MoveEndpoint
            || !endpointMoveRequested) {
            return false;
        }
        beginSemanticMutation();
        const bool moved = endpointMoveRequested(metadata.id, target);
        if (!moved) {
            endSemanticMutation(true);
            return false;
        }
        m_workspaceState.endpointPositionOverrides.insert(
            metadata.id, visualPosition);
        saveWorkspaceState();
        endSemanticMutation(true);
        return true;
    }
    return false;
}

bool NocNodeEditor::detachEndpoint(QtNodes::NodeId nodeId,
                                   bool restoreProjectionOnFailure) {
    const auto fail = [this, restoreProjectionOnFailure] {
        if (restoreProjectionOnFailure) {
            rebuildGraph(false);
        }
        return false;
    };
    const auto metadata = m_metadata.constFind(nodeId);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Endpoint
        || !m_design
        || !endpointRemovalRequested
        || !m_editingEnabled
        || m_attachmentPolicy.source != attachment::PolicySource::Package) {
        return fail();
    }
    const auto endpoint = std::find_if(m_design->endpoints.cbegin(), m_design->endpoints.cend(),
                                       [&](const EndpointInstance& candidate) {
                                           return candidate.id == metadata->id;
                                       });
    if (endpoint == m_design->endpoints.cend()) {
        return fail();
    }
    const attachment::TransitionPlan plan = attachment::decideTransition(
        m_editingEnabled,
        m_attachmentPolicy,
        {{attachment::EndpointLifecycle::Attached,
          endpoint->id,
          endpoint->attachment},
         attachment::TransitionIntent::Detach,
         std::nullopt});
    if (!plan.decision.allowed
        || plan.command
            != attachment::AttachmentCommandKind::DetachToRecoverableDraft) {
        reportAttachmentRejection(plan.decision.rejection, endpoint->attachment.router);
        return fail();
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
    beginSemanticMutation();
    if (!endpointRemovalRequested(endpointId)) {
        endSemanticMutation(true);
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
    endSemanticMutation(true);
    return true;
}

bool NocNodeEditor::deleteEndpointNode(QtNodes::NodeId nodeId) {
    const auto metadataIterator = m_metadata.constFind(nodeId);
    if (metadataIterator == m_metadata.constEnd() || !m_editingEnabled) {
        return false;
    }
    const NodeMetadata metadata = *metadataIterator;
    const bool pending =
        metadata.kind == NocEditorSelection::Kind::PendingEndpoint;
    if (!pending && metadata.kind != NocEditorSelection::Kind::Endpoint) {
        return false;
    }

    if (pending) {
        const auto pendingIterator = m_pendingEndpoints.constFind(metadata.id);
        if (pendingIterator == m_pendingEndpoints.cend()) {
            return true;
        }
        const bool detached = pendingIterator->detached.has_value();
        const QString endpointLabel = detached
            ? pendingIterator->detached->endpoint.id : pendingIterator->type;
        QMessageBox confirmation(
            QMessageBox::Warning,
            detached
                ? QStringLiteral("Delete detached Endpoint?")
                : QStringLiteral("Discard unattached Endpoint?"),
            detached
                ? QStringLiteral(
                      "Permanently delete Endpoint %1 and discard the Domain, "
                      "attachment, and configuration data held by this canvas draft?")
                      .arg(endpointLabel.toHtmlEscaped())
                : QStringLiteral(
                      "Discard the unattached %1 Endpoint from the canvas?")
                      .arg(endpointLabel.toHtmlEscaped()),
            QMessageBox::Yes | QMessageBox::Cancel,
            this);
        confirmation.setObjectName(
            QStringLiteral("finepaper.deletePendingEndpointConfirmation"));
        confirmation.setTextFormat(Qt::RichText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.setEscapeButton(QMessageBox::Cancel);
        if (QPushButton* deleteButton = qobject_cast<QPushButton*>(
                confirmation.button(QMessageBox::Yes))) {
            deleteButton->setText(
                detached
                    ? QStringLiteral("Delete Endpoint")
                    : QStringLiteral("Discard Endpoint"));
            deleteButton->setProperty(
                "finepaperRole", QStringLiteral("danger"));
        }
        if (confirmation.exec() != QMessageBox::Yes) {
            return true;
        }
        const auto iterator = m_pendingEndpoints.constFind(metadata.id);
        if (iterator == m_pendingEndpoints.cend()) {
            return true;
        }
        attachment::EndpointSubject subject;
        subject.lifecycle = iterator->detached
            ? attachment::EndpointLifecycle::Detached
            : attachment::EndpointLifecycle::PendingNew;
        if (iterator->detached) {
            subject.endpointId = iterator->detached->endpoint.id;
            subject.durableAttachment = iterator->detached->endpoint.attachment;
        }
        const attachment::TransitionPlan plan = attachment::decideTransition(
            m_editingEnabled,
            m_attachmentPolicy,
            {subject, attachment::TransitionIntent::Delete, std::nullopt});
        if (!plan.decision.allowed) {
            reportAttachmentRejection(plan.decision.rejection);
            return true;
        }
        const QString durableEndpointId = subject.endpointId;
        m_pendingEndpoints.remove(metadata.id);
        if (plan.command
                == attachment::AttachmentCommandKind::DiscardDetachedDraft
            && !durableEndpointId.isEmpty()
            && detachedEndpointDeletionRequested) {
            detachedEndpointDeletionRequested(durableEndpointId);
        }
        rebuildGraph(false);
        return true;
    }

    if (!m_design || !endpointDeletionRequested) {
        return true;
    }
    const auto endpoint = std::find_if(
        m_design->endpoints.cbegin(),
        m_design->endpoints.cend(),
        [&](const EndpointInstance& candidate) {
            return candidate.id == metadata.id;
        });
    if (endpoint == m_design->endpoints.cend()) {
        return true;
    }
    const attachment::TransitionPlan plan = attachment::decideTransition(
        m_editingEnabled,
        m_attachmentPolicy,
        {{attachment::EndpointLifecycle::Attached,
          endpoint->id,
          endpoint->attachment},
         attachment::TransitionIntent::Delete,
         std::nullopt});
    if (plan.decision.allowed
        && plan.command
            == attachment::AttachmentCommandKind::DeleteAttachedEndpoint) {
        const QString endpointId = metadata.id;
        beginSemanticMutation();
        endpointDeletionRequested(endpointId);
        endSemanticMutation(true);
    } else {
        reportAttachmentRejection(
            plan.decision.rejection, endpoint->attachment.router);
    }
    return true;
}

bool NocNodeEditor::deleteSelectedSemanticItem() {
    if (m_selectedItems.size() != 1) {
        return false;
    }
    const SelectionIdentity selection = m_selectedItems.front();
    if (selection.kind == NocEditorSelection::Kind::Endpoint) {
        const auto node = m_elementNodes.constFind(
            ElementRef{ElementKind::Endpoint, selection.id});
        return node != m_elementNodes.constEnd()
            && deleteEndpointNode(node.value());
    }
    if (selection.kind == NocEditorSelection::Kind::PendingEndpoint) {
        for (auto iterator = m_metadata.constBegin();
             iterator != m_metadata.constEnd(); ++iterator) {
            if (iterator->kind == selection.kind
                && iterator->id == selection.id) {
                return deleteEndpointNode(iterator.key());
            }
        }
        return true;
    }
    if (selection.kind == NocEditorSelection::Kind::EndpointAttachment) {
        const auto connection = m_elementConnections.constFind(
            ElementRef{ElementKind::EndpointAttachment, selection.id});
        if (connection != m_elementConnections.constEnd()) {
            detachEndpoint(connection->outNodeId, true);
        }
        return true;
    }
    return false;
}

QGraphicsItem* NocNodeEditor::graphicsItemForSelection(
    const SelectionIdentity& selection) {
    if (!m_scene) {
        return nullptr;
    }
    if (const std::optional<QtNodes::NodeId> nodeId =
            nodeIdForSelection(selection)) {
        return m_scene->nodeGraphicsObject(*nodeId);
    }

    const ElementKind kind = elementKindForSelection(selection.kind);
    const ElementRef element{kind, selection.id};
    if (kind == ElementKind::RouterLink
        || kind == ElementKind::EndpointAttachment) {
        const auto connection = m_elementConnections.constFind(element);
        return connection == m_elementConnections.constEnd()
            ? nullptr
            : m_scene->connectionGraphicsObject(connection.value());
    }
    return nullptr;
}

std::optional<QtNodes::NodeId> NocNodeEditor::nodeIdForSelection(
    const SelectionIdentity& selection) const {
    if (selection.kind == NocEditorSelection::Kind::PendingEndpoint) {
        for (auto iterator = m_metadata.constBegin();
             iterator != m_metadata.constEnd(); ++iterator) {
            if (iterator->kind == selection.kind
                && iterator->id == selection.id) {
                return iterator.key();
            }
        }
        return std::nullopt;
    }

    const ElementKind kind = elementKindForSelection(selection.kind);
    const ElementRef element{kind, selection.id};
    if (kind == ElementKind::Router || kind == ElementKind::Endpoint) {
        const auto node = m_elementNodes.constFind(element);
        return node == m_elementNodes.constEnd()
            ? std::nullopt
            : std::optional<QtNodes::NodeId>{node.value()};
    }
    return std::nullopt;
}

void NocNodeEditor::ensureSelectionVisible(
    const SelectionIdentity& selection) {
    if (!m_view) {
        return;
    }
    if (QGraphicsItem* item = graphicsItemForSelection(selection)) {
        m_view->ensureVisible(item, 48, 48);
    }
}

bool NocNodeEditor::selectAdjacentCanvasItem(bool backwards) {
    if (!m_scene || !m_view) {
        return false;
    }
    struct CanvasTarget {
        SelectionIdentity selection;
        QPointF center;
    };
    QVector<CanvasTarget> targets;
    targets.reserve(m_metadata.size() + m_elementConnections.size());
    for (auto iterator = m_metadata.constBegin();
         iterator != m_metadata.constEnd(); ++iterator) {
        if (iterator->kind != NocEditorSelection::Kind::Router
            && iterator->kind != NocEditorSelection::Kind::Endpoint
            && iterator->kind
                != NocEditorSelection::Kind::PendingEndpoint) {
            continue;
        }
        const SelectionIdentity selection{iterator->kind, iterator->id};
        if (QGraphicsItem* item = graphicsItemForSelection(selection)) {
            targets.append({selection, item->sceneBoundingRect().center()});
        }
    }
    for (auto iterator = m_elementConnections.constBegin();
         iterator != m_elementConnections.constEnd(); ++iterator) {
        const NocEditorSelection::Kind kind = selectionKindForElement(
            iterator.key().kind);
        const SelectionIdentity selection{kind, iterator.key().id};
        if (kind != NocEditorSelection::Kind::None) {
            if (QGraphicsItem* item = graphicsItemForSelection(selection)) {
                targets.append(
                    {selection, item->sceneBoundingRect().center()});
            }
        }
    }
    std::sort(targets.begin(), targets.end(), [](const CanvasTarget& left,
                                                  const CanvasTarget& right) {
        if (left.center.y() != right.center.y()) {
            return left.center.y() < right.center.y();
        }
        if (left.center.x() != right.center.x()) {
            return left.center.x() < right.center.x();
        }
        if (left.selection.kind != right.selection.kind) {
            return static_cast<int>(left.selection.kind)
                < static_cast<int>(right.selection.kind);
        }
        return left.selection.id < right.selection.id;
    });
    if (targets.isEmpty()) {
        return false;
    }

    qsizetype targetIndex = backwards ? targets.size() - 1 : 0;
    if (m_selectedItems.size() == 1) {
        const auto current = std::find_if(
            targets.cbegin(), targets.cend(), [this](const CanvasTarget& target) {
                return target.selection == m_selectedItems.front();
            });
        if (current != targets.cend()) {
            const qsizetype currentIndex = current - targets.cbegin();
            if ((!backwards && currentIndex + 1 >= targets.size())
                || (backwards && currentIndex == 0)) {
                return false;
            }
            targetIndex = currentIndex + (backwards ? -1 : 1);
        }
    }

    m_selectedItems = {targets.at(targetIndex).selection};
    restoreSelection();
    ensureSelectionVisible(m_selectedItems.front());
    return true;
}

bool NocNodeEditor::showSelectedContextMenuFromKeyboard() {
    if (!m_view || m_selectedItems.size() != 1) {
        return false;
    }
    const SelectionIdentity selection = m_selectedItems.front();
    QGraphicsItem* item = graphicsItemForSelection(selection);
    if (!item) {
        return false;
    }
    const QPoint viewportPosition = m_view->mapFromScene(
        item->sceneBoundingRect().center());
    const QPoint globalPosition = m_view->viewport()->mapToGlobal(
        viewportPosition);
    if (auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
        showNodeContextMenu(node->nodeId(), globalPosition);
        return true;
    }
    auto* connection =
        qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
    if (!connection
        || !isEndpointAttachmentConnection(connection->connectionId())) {
        return false;
    }
    showConnectionContextMenu(connection->connectionId(), globalPosition);
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
    const std::optional<ElementRef> attachment = elementForConnection(
        connectionId);
    connect(disconnect, &QAction::triggered, this, [this, attachment] {
        if (!attachment) {
            return;
        }
        const auto current = m_elementConnections.constFind(*attachment);
        if (current != m_elementConnections.constEnd()) {
            m_graphModel->deleteConnection(current.value());
        }
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
                        beginSemanticMutation();
                        endpointTypeDropped(
                            type.id, NocAttachmentTarget{router, std::nullopt});
                        endSemanticMutation(true);
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
    const SelectionIdentity endpointSelection{metadata->kind, metadata->id};
    QMenu* connectMenu = menu->addMenu(QStringLiteral("Connect to Router"));
    connectMenu->setObjectName(workbench::connectRouterMenuName);
    QVector<std::pair<RouterPosition, QString>> routers;
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
        connect(action, &QAction::triggered, this,
                [this, endpointSelection, position = router.first] {
            if (const std::optional<QtNodes::NodeId> currentNode =
                    nodeIdForSelection(endpointSelection)) {
                attachNodeToRouter(
                    *currentNode,
                    NocAttachmentTarget{position, std::nullopt});
            }
        });
    }
    menu->addSeparator();
    if (!pending) {
        QAction* detach = menu->addAction(QStringLiteral("Disconnect from Router"));
        detach->setObjectName(workbench::detachEndpointActionName);
        connect(detach, &QAction::triggered, this, [this, endpointSelection] {
            if (const std::optional<QtNodes::NodeId> currentNode =
                    nodeIdForSelection(endpointSelection)) {
                detachEndpoint(*currentNode);
            }
        });
    }
    QAction* remove = menu->addAction(
        pending ? QStringLiteral("Delete Unattached Endpoint")
                : QStringLiteral("Delete Endpoint"));
    remove->setObjectName(workbench::deleteEndpointActionName);
    connect(remove, &QAction::triggered, this, [this, endpointSelection] {
        if (const std::optional<QtNodes::NodeId> currentNode =
                nodeIdForSelection(endpointSelection)) {
            deleteEndpointNode(*currentNode);
        }
    });
    menu->popup(globalPosition);
}

void NocNodeEditor::highlightNeighborhood(QtNodes::NodeId nodeId) {
    if (!m_scene || !m_graphModel->nodeExists(nodeId)) {
        return;
    }
    for (const QtNodes::ConnectionId& connectionId
         : m_graphModel->allConnectionIds(nodeId)) {
        setConnectionRelatedHighlight(connectionId);
        const QtNodes::NodeId relatedNodeId = connectionId.outNodeId == nodeId
            ? connectionId.inNodeId
            : connectionId.outNodeId;
        setNodeRelatedHighlight(relatedNodeId);
    }
}

void NocNodeEditor::setNodeRelatedHighlight(QtNodes::NodeId nodeId) {
    if (!m_scene || m_highlightedNodeIds.contains(nodeId)) {
        return;
    }
    if (auto* node = m_scene->nodeGraphicsObject(nodeId)) {
        node->setData(relatedHighlightDataRole, true);
        node->update();
        m_highlightedNodeIds.insert(nodeId);
    }
}

void NocNodeEditor::setConnectionRelatedHighlight(
    QtNodes::ConnectionId connectionId) {
    if (!m_scene) {
        return;
    }
    if (auto* connection = m_scene->connectionGraphicsObject(connectionId)) {
        connection->setData(relatedHighlightDataRole, true);
        connection->update();
        m_highlightedConnectionIds.append(connectionId);
    }
}

void NocNodeEditor::clearNeighborhoodHighlight() {
    if (m_scene) {
        for (const QtNodes::NodeId nodeId : std::as_const(m_highlightedNodeIds)) {
            if (auto* node = m_scene->nodeGraphicsObject(nodeId)) {
                node->setData(relatedHighlightDataRole, false);
                node->update();
            }
        }
        for (const QtNodes::ConnectionId& connectionId
             : std::as_const(m_highlightedConnectionIds)) {
            if (auto* connection =
                    m_scene->connectionGraphicsObject(connectionId)) {
                connection->setData(relatedHighlightDataRole, false);
                connection->update();
            }
        }
    }
    m_highlightedNodeIds.clear();
    m_highlightedConnectionIds.clear();
}

void NocNodeEditor::loadWorkspaceState() {
    m_workspacePersistenceBlocked = false;
    if (!m_workspaceIdentity) {
        m_workspaceState = {};
        return;
    }
    const TopologyWorkspaceLoadResult loaded =
        m_workspaceStore.load(*m_workspaceIdentity);
    if (!loaded.ok()) {
        m_workspaceState = {};
        m_workspacePersistenceBlocked = true;
        qWarning() << "Could not load the topology workspace state for"
                   << m_workspaceIdentity->designId << ':' << loaded.error;
        reportWorkspaceDiagnostic(
            TopologyWorkspaceDiagnosticKind::LoadFailed, loaded.error);
        return;
    }
    m_workspaceState = loaded.state.value_or(TopologyWorkspaceState{});
    if (!loaded.warning.isEmpty()) {
        qWarning() << "Could not import legacy topology workspace state for"
                   << m_workspaceIdentity->designId << ':' << loaded.warning;
        reportWorkspaceDiagnostic(
            TopologyWorkspaceDiagnosticKind::LegacyImportSkipped,
            loaded.warning);
    }
}

bool NocNodeEditor::saveWorkspaceState() {
    if (!m_workspaceIdentity || m_workspacePersistenceBlocked) {
        return false;
    }
    const TopologyWorkspaceSaveResult saved = m_workspaceStore.save(
        *m_workspaceIdentity, m_workspaceState);
    if (!saved.success) {
        qWarning() << "Could not persist the topology workspace state for"
                   << m_workspaceIdentity->designId << ':' << saved.error;
        reportWorkspaceDiagnostic(
            TopologyWorkspaceDiagnosticKind::SaveFailed, saved.error);
        return false;
    }
    if (m_lastWorkspaceDiagnosticKind
        == TopologyWorkspaceDiagnosticKind::SaveFailed) {
        m_lastWorkspaceDiagnostic.clear();
        m_lastWorkspaceDiagnosticKind = std::nullopt;
        reportWorkspaceDiagnostic(
            TopologyWorkspaceDiagnosticKind::SaveRecovered);
    }
    return true;
}

void NocNodeEditor::deferForCurrentGraph(std::function<void()> operation) {
    const quint64 graphRevision = m_graphRevision;
    QTimer::singleShot(
        0,
        this,
        [this, graphRevision, operation = std::move(operation)] {
            if (graphRevision == m_graphRevision) {
                operation();
            }
        });
}

void NocNodeEditor::reportWorkspaceDiagnostic(
    TopologyWorkspaceDiagnosticKind kind,
    const QString& details) {
    if (!workspaceDiagnosticRaised) {
        return;
    }
    const QString designId = m_workspaceIdentity
        ? m_workspaceIdentity->designId : QString();
    const QString diagnosticKey = QStringLiteral("%1\n%2\n%3")
        .arg(static_cast<int>(kind))
        .arg(designId, details);
    if (diagnosticKey == m_lastWorkspaceDiagnostic) {
        return;
    }
    m_lastWorkspaceDiagnostic = diagnosticKey;
    m_lastWorkspaceDiagnosticKind = kind;
    workspaceDiagnosticRaised({kind, designId, details});
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
    const QPointF scenePosition = m_view->mapToScene(viewportPosition);
    const std::optional<QtNodes::NodeId> target = routerNodeAt(scenePosition);
    if (!target) {
        addPendingEndpoint(endpointType, scenePosition);
        return true;
    }
    const auto metadata = m_metadata.constFind(*target);
    if (metadata == m_metadata.constEnd()
        || metadata->kind != NocEditorSelection::Kind::Router
        || !metadata->router) {
        return false;
    }
    const attachment::TargetResolution resolution = resolveAttachmentTargetAt(
        *target, scenePosition);
    if (!resolution.decision.allowed || !resolution.target) {
        reportAttachmentRejection(
            resolution.decision.rejection, metadata->router);
        return false;
    }
    if (!endpointTypeDropped) {
        return false;
    }
    beginSemanticMutation();
    const attachment::CreateEndpointResult created = endpointTypeDropped(
        endpointType, *resolution.target);
    endSemanticMutation(true);
    if (!created.success || created.endpointId.trimmed().isEmpty()) {
        return false;
    }
    selectElements({{ElementKind::Endpoint, created.endpointId}});
    return true;
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

std::optional<QtNodes::NodeId> NocNodeEditor::routerNodeAt(
    const QPointF& scenePosition) const {
    if (!m_scene) {
        return std::nullopt;
    }
    QSet<QtNodes::NodeId> visited;
    for (QGraphicsItem* hitItem : m_scene->items(
             scenePosition,
             Qt::IntersectsItemShape,
             Qt::DescendingOrder)) {
        QGraphicsItem* item = hitItem;
        while (item) {
            if (auto* node =
                    qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                if (visited.contains(node->nodeId())) {
                    break;
                }
                visited.insert(node->nodeId());
                const auto metadata = m_metadata.constFind(node->nodeId());
                if (metadata != m_metadata.constEnd()
                    && metadata->kind == NocEditorSelection::Kind::Router) {
                    return node->nodeId();
                }
                break;
            }
            item = item->parentItem();
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
    if (nodeAt(viewportPosition)) {
        return nullptr;
    }
    const QRect hitArea(
        viewportPosition
            - QPoint(kInteractiveConnectionHitRadius,
                     kInteractiveConnectionHitRadius),
        QSize(kInteractiveConnectionHitRadius * 2 + 1,
              kInteractiveConnectionHitRadius * 2 + 1));
    QtNodes::ConnectionGraphicsObject* firstConnection = nullptr;
    for (QGraphicsItem* hitItem
         : m_view->items(hitArea, Qt::IntersectsItemShape)) {
        QGraphicsItem* item = hitItem;
        while (item) {
            if (qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                break;
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
    QSet<QtNodes::NodeId> visited;
    for (QGraphicsItem* hitItem : m_scene->items(
             scenePosition,
             Qt::IntersectsItemShape,
             Qt::DescendingOrder)) {
        QGraphicsItem* item = hitItem;
        while (item) {
            if (auto* node =
                    qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                if (visited.contains(node->nodeId())) {
                    break;
                }
                visited.insert(node->nodeId());
                if (!ignoredNode || node->nodeId() != *ignoredNode) {
                    return node->nodeId();
                }
                break;
            }
            item = item->parentItem();
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
            + static_cast<unsigned int>(m_attachmentPolicy.ports.size());
}

attachment::ConnectionHandleKind NocNodeEditor::outputConnectionHandle(
    QtNodes::NodeId nodeId,
    unsigned int portIndex) const {
    const auto node = m_metadata.constFind(nodeId);
    if (node == m_metadata.constEnd()) {
        return attachment::ConnectionHandleKind::Other;
    }
    if ((node->kind == NocEditorSelection::Kind::Endpoint
         || node->kind == NocEditorSelection::Kind::PendingEndpoint)
        && portIndex == kEndpointOutPort) {
        return attachment::ConnectionHandleKind::EndpointAttachmentOutput;
    }
    if (node->kind == NocEditorSelection::Kind::Router) {
        return attachment::ConnectionHandleKind::RouterTopologyPort;
    }
    return attachment::ConnectionHandleKind::Other;
}

attachment::ConnectionHandleKind NocNodeEditor::inputConnectionHandle(
    QtNodes::NodeId nodeId,
    unsigned int portIndex) const {
    const auto node = m_metadata.constFind(nodeId);
    if (node == m_metadata.constEnd()) {
        return attachment::ConnectionHandleKind::Other;
    }
    if (node->kind == NocEditorSelection::Kind::Router) {
        return isRouterAttachmentPort(portIndex)
            ? attachment::ConnectionHandleKind::RouterAttachmentInput
            : attachment::ConnectionHandleKind::RouterTopologyPort;
    }
    return attachment::ConnectionHandleKind::Other;
}

attachment::TargetResolution NocNodeEditor::resolveAttachmentTarget(
    QtNodes::NodeId routerNode,
    attachment::RouterHitKind hitKind,
    std::optional<unsigned int> portIndex,
    std::optional<QtNodes::NodeId> ignoredEndpoint) const {
    attachment::TargetResolution rejected;
    rejected.decision = attachment::RuleDecision::reject(
        attachment::Rejection::UnknownRouter);
    if (!m_design) {
        return rejected;
    }
    const auto router = m_metadata.constFind(routerNode);
    if (router == m_metadata.constEnd()
        || router->kind != NocEditorSelection::Kind::Router
        || !router->router) {
        return rejected;
    }

    QString ignoredEndpointId;
    if (ignoredEndpoint) {
        const auto endpoint = m_metadata.constFind(*ignoredEndpoint);
        if (endpoint != m_metadata.constEnd()
            && endpoint->kind == NocEditorSelection::Kind::Endpoint) {
            ignoredEndpointId = endpoint->id;
        }
    }
    std::optional<attachment::PortOffset> offset = std::nullopt;
    if (portIndex) {
        if (!isRouterAttachmentPort(*portIndex)) {
            rejected.decision = attachment::RuleDecision::reject(
                attachment::Rejection::InvalidPort);
            return rejected;
        }
        offset = static_cast<attachment::PortOffset>(
            *portIndex - kRouterEndpointInPort);
    }
    return attachment::resolveTarget(
        *m_design,
        m_attachmentIndex,
        m_editingEnabled,
        attachment::RouterHit{*router->router, hitKind, offset},
        ignoredEndpointId);
}

attachment::TargetResolution NocNodeEditor::resolveAttachmentTargetAt(
    QtNodes::NodeId routerNode,
    const QPointF& scenePosition,
    std::optional<QtNodes::NodeId> ignoredEndpoint) const {
    if (!m_scene) {
        attachment::TargetResolution rejected;
        rejected.decision = attachment::RuleDecision::reject(
            attachment::Rejection::UnknownRouter);
        return rejected;
    }
    auto* routerGraphics = m_scene->nodeGraphicsObject(routerNode);
    const auto routerMetadata = m_metadata.constFind(routerNode);
    if (!routerGraphics || routerMetadata == m_metadata.constEnd()
        || routerMetadata->kind != NocEditorSelection::Kind::Router) {
        attachment::TargetResolution rejected;
        rejected.decision = attachment::RuleDecision::reject(
            attachment::Rejection::UnknownRouter);
        return rejected;
    }

    const auto& geometry = m_scene->nodeGeometry();
    const unsigned int logicalAttachmentCount =
        static_cast<unsigned int>(m_attachmentPolicy.ports.size());
    const bool collapsed = routerCollapsed(routerMetadata->id);
    const bool hasAggregate = collapsed
        || hasRouterAttachmentPortOverflow(logicalAttachmentCount);
    if (hasAggregate && m_view) {
        const unsigned int aggregateOffset = collapsed
            ? 0U
            : individualVisibleRouterAttachmentPortCount(
                  logicalAttachmentCount);
        const QPointF aggregateScenePosition = routerGraphics->mapToScene(
            geometry.portPosition(
                routerNode,
                QtNodes::PortType::In,
                kRouterEndpointInPort + aggregateOffset));
        if (squaredDistance(
                QPointF(m_view->mapFromScene(scenePosition)),
                QPointF(m_view->mapFromScene(aggregateScenePosition)))
            <= squared(kInteractivePortHitRadius)) {
            return resolveAttachmentTarget(
                routerNode,
                attachment::RouterHitKind::Body,
                std::nullopt,
                ignoredEndpoint);
        }
    }

    const QPointF localPosition = routerGraphics->mapFromScene(scenePosition);
    const QtNodes::PortIndex inputPort = geometry.checkPortHit(
        routerNode, QtNodes::PortType::In, localPosition);
    if (inputPort != QtNodes::InvalidPortIndex) {
        const bool attachmentPort = isRouterAttachmentPort(inputPort);
        return resolveAttachmentTarget(
            routerNode,
            attachmentPort
                ? attachment::RouterHitKind::AttachmentPort
                : attachment::RouterHitKind::TopologyPort,
            attachmentPort
                ? std::optional<unsigned int>(inputPort) : std::nullopt,
            ignoredEndpoint);
    }

    const QtNodes::PortIndex outputPort = geometry.checkPortHit(
        routerNode, QtNodes::PortType::Out, localPosition);
    return resolveAttachmentTarget(
        routerNode,
        outputPort == QtNodes::InvalidPortIndex
            ? attachment::RouterHitKind::Body
            : attachment::RouterHitKind::TopologyPort,
        std::nullopt,
        ignoredEndpoint);
}

EndpointDragTarget NocNodeEditor::endpointDragTargetAt(
    const QPoint& viewportPosition) const {
    if (!m_view) {
        return EndpointDragTarget::Canvas;
    }
    const QPointF scenePosition = m_view->mapToScene(viewportPosition);
    const std::optional<QtNodes::NodeId> routerNode = routerNodeAt(
        scenePosition);
    if (!routerNode) {
        return EndpointDragTarget::Canvas;
    }
    return resolveAttachmentTargetAt(*routerNode, scenePosition)
            .decision.allowed
        ? EndpointDragTarget::AttachToRouter
        : EndpointDragTarget::Blocked;
}

void NocNodeEditor::reportAttachmentRejection(
    attachment::Rejection rejection,
    std::optional<RouterPosition> router) const {
    if (attachmentRejected && rejection != attachment::Rejection::None) {
        attachmentRejected(rejection, router);
    }
}

bool NocNodeEditor::attachmentPortAvailable(
    QtNodes::NodeId routerNode,
    unsigned int portIndex,
    std::optional<QtNodes::NodeId> ignoredEndpoint) const {
    if (!m_graphModel->nodeExists(routerNode)) {
        return false;
    }
    return resolveAttachmentTarget(
               routerNode,
               attachment::RouterHitKind::AttachmentPort,
               portIndex,
               ignoredEndpoint)
        .decision.allowed;
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
    std::optional<RouterPosition> router = std::nullopt;
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
    // QtNodes only raises selected nodes. Track that small set so restoring
    // semantic Router/Endpoint layering does not sort and scan the full scene
    // on every pointer or context-menu event.
    for (QtNodes::NodeId nodeId : std::as_const(m_stackingNodeIds)) {
        const auto metadata = m_metadata.constFind(nodeId);
        if (metadata == m_metadata.constEnd()) {
            continue;
        }
        auto* node = m_scene->nodeGraphicsObject(nodeId);
        if (!node) {
            continue;
        }
        node->setZValue(metadata->kind == NocEditorSelection::Kind::Router
                            ? kRouterNodeZValue
                            : kEndpointNodeZValue);
    }
}

void NocNodeEditor::applyDomainPresentation() {
    if (!m_scene) {
        return;
    }

    const QPalette applicationPalette = palette();
    const NocEditorColors visual = nocEditorColors(applicationPalette);

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
        QVariantList patternBrushes;
        QStringList domainIds;
        if (presentation) {
            domainIds = presentation->domainIds;
            const qsizetype visibleCount = (std::min)(
                kMaximumVisibleDomainStripSegments,
                presentation->colors.size());
            colors.reserve(visibleCount);
            patternBrushes.reserve(visibleCount);
            for (qsizetype index = 0; index < visibleCount; ++index) {
                const bool overflow = presentation->colors.size()
                        > kMaximumVisibleDomainStripSegments
                    && index + 1 == visibleCount;
                QColor base = overflow
                    ? visual.domainUnassigned
                    : presentation->colors.at(index);
                if (!base.isValid()) {
                    base = visual.domainUnassigned;
                }
                QColor ink = nocDomainPatternInk(base, applicationPalette);
                ink.setAlpha(150);
                const QString domainId = overflow
                    ? QString{} : presentation->domainIds.value(index);
                const Qt::BrushStyle fallback = overflow
                    ? Qt::Dense4Pattern
                    : (index % 2 == 0
                           ? Qt::BDiagPattern : Qt::FDiagPattern);
                colors.append(base);
                patternBrushes.append(QBrush(
                    ink,
                    domainId.isEmpty()
                        ? fallback : nocDomainPattern(domainId)));
            }
            if (colors.isEmpty()
                && presentation->state
                    != DomainAssignmentDisplayState::Inactive
                && presentation->state
                    != DomainAssignmentDisplayState::NotApplicable) {
                const bool unavailable = presentation->state
                    == DomainAssignmentDisplayState::Unavailable;
                const QColor base = unavailable
                    ? visual.domainUnavailable : visual.domainUnassigned;
                QColor ink = nocDomainPatternInk(base, applicationPalette);
                ink.setAlpha(150);
                colors.append(base);
                patternBrushes.append(QBrush(
                    ink,
                    unavailable
                        ? Qt::DiagCrossPattern : Qt::BDiagPattern));
            }
        }
        graphics->setData(domainColorsDataRole, colors);
        graphics->setData(domainPatternBrushesDataRole, patternBrushes);
        graphics->setData(domainIdsDataRole, domainIds);
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
        graphics->setData(
            domainCrossingPaintColorDataRole,
            crossing && crossing->primaryAccent.isValid()
                ? readableNocCanvasAccent(
                      crossing->primaryAccent, applicationPalette)
                : QColor{});
        graphics->setData(domainOverrideDataRole, hasOverride);
        graphics->update();
    }
}

void NocNodeEditor::updateCanvasAccessibleDescription() {
    if (!m_view) {
        return;
    }

    QString description = QStringLiteral(
        "Pan with a pointer drag. Hold Shift to box-select, or use the "
        "Select and Pan toolbar actions. With keyboard focus on the canvas, "
        "Tab and Shift+Tab move through elements, Space collapses or expands "
        "a selected Router, Shift+F10 opens element actions, and Delete "
        "removes an editable Endpoint or disconnects its attachment.");
    if (m_domainPresentation.status == DomainPresentationStatus::Inactive
        || m_domainPresentation.activeDomainType.trimmed().isEmpty()) {
        m_view->setAccessibleDescription(description);
        return;
    }

    const QString layerLabel = domain_text::domainTypeDisplayText(
        m_domainPresentation.activeDomainType,
        m_domainPresentation.domainTypeLabel);
    description += QStringLiteral(" Current domain layer: %1.").arg(layerLabel);
    if (m_domainPresentation.status
        == DomainPresentationStatus::MissingPackageType) {
        description += QStringLiteral(
            " This layer is unavailable for the current package.");
        m_view->setAccessibleDescription(description);
        return;
    }

    constexpr qsizetype maximumSpokenEntries = 8;
    QStringList domainNames;
    const qsizetype visibleCount = (std::min)(
        maximumSpokenEntries, m_domainPresentation.legend.size());
    domainNames.reserve(visibleCount);
    for (qsizetype index = 0; index < visibleCount; ++index) {
        const DomainLegendEntry& entry = m_domainPresentation.legend.at(index);
        domainNames.append(domain_text::domainInstanceDisplayText(
            entry.id, entry.name));
    }
    if (domainNames.isEmpty()) {
        description += QStringLiteral(" No assigned domains.");
    } else {
        description += QStringLiteral(" Domains: %1.").arg(
            domainNames.join(QStringLiteral(", ")));
        if (m_domainPresentation.legend.size() > visibleCount) {
            description += QStringLiteral(" %1 more domains.").arg(
                m_domainPresentation.legend.size() - visibleCount);
        }
    }
    description += QStringLiteral(
        " Dashed connections indicate Domain crossings. A diamond marker "
        "indicates an edge override.");
    m_view->setAccessibleDescription(description);
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
