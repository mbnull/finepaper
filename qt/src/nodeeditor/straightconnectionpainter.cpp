// StraightConnectionPainter renders segmented orthogonal connection paths and highlight states.
#include "nodeeditor/straightconnectionpainter.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/portanchorgeometry.h"
#include "modules/moduletypemetadata.h"
#include "common/portlayout.h"
#include "nodeeditor/portcolors.h"
#include <QtNodes/StyleCollection>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QPainter>
#include <QPainterPathStroker>
#include <cmath>

namespace {

constexpr int kConnectedHighlightDataRole = 1;

QPointF normalForSide(const QString& side) {
    if (side == QStringLiteral("north")) return QPointF(0.0, -1.0);
    if (side == QStringLiteral("east")) return QPointF(1.0, 0.0);
    if (side == QStringLiteral("south")) return QPointF(0.0, 1.0);
    if (side == QStringLiteral("west")) return QPointF(-1.0, 0.0);
    return {};
}

QPointF portNormal(QtNodes::ConnectionGraphicsObject const& cgo,
                   QtNodes::NodeId nodeId,
                   QtNodes::PortType portType,
                   QtNodes::PortIndex portIndex) {
    auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&cgo.graphModel());
    if (!graphModel) {
        return {};
    }

    auto* model = graphModel->delegateModel<GraphNodeModel>(nodeId);
    const Port* port = model ? model->portAt(portType, portIndex) : nullptr;
    if (!model || !port) {
        return {};
    }

    QPointF edgeNormal;
    if (auto* scene = cgo.nodeScene()) {
        auto const& geometry = scene->nodeGeometry();
        edgeNormal = PortAnchorGeometry::normalFromEdge(
            geometry.portPosition(nodeId, portType, portIndex),
            geometry.size(nodeId));
    }

    if ((ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router") ||
         ModuleTypeMetadata::hasEditorLayout(model->module(), u"endpoint")) &&
        !edgeNormal.isNull()) {
        return edgeNormal;
    }

    if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(model->module(), *port);
        anchor && anchor->normalX.has_value() && anchor->normalY.has_value()) {
        return QPointF(*anchor->normalX, *anchor->normalY);
    }

    if (!edgeNormal.isNull()) {
        return edgeNormal;
    }

    if (PortLayout::isRouterPort(*port)) {
        return normalForSide(PortLayout::routerSideId(port->id()));
    }

    return normalForSide(PortLayout::fallbackSide(*port));
}

QPainterPath segmentedPath(QtNodes::ConnectionGraphicsObject const& cgo) {
    const QPointF out = cgo.out();
    const QPointF in = cgo.in();
    const QtNodes::ConnectionId connectionId = cgo.connectionId();
    const QPointF outNormal = portNormal(cgo,
                                         connectionId.outNodeId,
                                         QtNodes::PortType::Out,
                                         connectionId.outPortIndex);
    const QPointF inNormal = portNormal(cgo,
                                        connectionId.inNodeId,
                                        QtNodes::PortType::In,
                                        connectionId.inPortIndex);
    const bool facingHorizontally =
        std::abs(out.y() - in.y()) <= 0.5 &&
        std::abs(outNormal.x()) > 0.0 &&
        inNormal.x() == -outNormal.x() &&
        ((outNormal.x() > 0.0 && out.x() <= in.x()) ||
         (outNormal.x() < 0.0 && out.x() >= in.x()));
    const bool facingVertically =
        std::abs(out.x() - in.x()) <= 0.5 &&
        std::abs(outNormal.y()) > 0.0 &&
        inNormal.y() == -outNormal.y() &&
        ((outNormal.y() > 0.0 && out.y() <= in.y()) ||
         (outNormal.y() < 0.0 && out.y() >= in.y()));

    if (facingHorizontally || facingVertically) {
        QPainterPath path(out);
        path.lineTo(in);
        return path;
    }

    const qreal midX = (out.x() + in.x()) / 2.0;

    QPainterPath path(cgo.out());
    path.lineTo(midX, out.y());
    path.lineTo(midX, in.y());
    path.lineTo(in);
    return path;
}

QColor connectionColor(QtNodes::ConnectionGraphicsObject const& cgo) {
    auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&cgo.graphModel());
    QColor baseColor;
    if (graphModel) {
        auto* model = graphModel->delegateModel<GraphNodeModel>(cgo.connectionId().outNodeId);
        if (model) {
            if (const Port* sourcePort = model->portAt(QtNodes::PortType::Out, cgo.connectionId().outPortIndex)) {
                baseColor = PortColors::colorForPort(*sourcePort);
            }
        }
    }

    auto const& style = QtNodes::StyleCollection::connectionStyle();
    if (!baseColor.isValid()) {
        baseColor = style.normalColor();
    }

    const bool connectedHighlight = cgo.data(kConnectedHighlightDataRole).toBool();
    if (cgo.isSelected() || connectedHighlight) {
        return baseColor.lighter(140);
    }
    if (cgo.connectionState().hovered()) {
        return baseColor.lighter(120);
    }

    return baseColor;
}

qreal connectionLineWidth(QtNodes::ConnectionGraphicsObject const& cgo) {
    auto const& style = QtNodes::StyleCollection::connectionStyle();
    return cgo.isSelected() || cgo.data(kConnectedHighlightDataRole).toBool()
        ? style.lineWidth() + 1.5
        : style.lineWidth();
}

} // namespace

void StraightConnectionPainter::paint(QPainter* painter, QtNodes::ConnectionGraphicsObject const& cgo) const {
    QPen pen(connectionColor(cgo), connectionLineWidth(cgo));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(segmentedPath(cgo));
}

QPainterPath StraightConnectionPainter::getPainterStroke(QtNodes::ConnectionGraphicsObject const& cgo) const {
    auto const& style = QtNodes::StyleCollection::connectionStyle();
    QPainterPathStroker stroker;
    stroker.setWidth(style.lineWidth() + 8.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(segmentedPath(cgo));
}
