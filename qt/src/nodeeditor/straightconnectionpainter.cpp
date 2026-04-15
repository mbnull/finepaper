// StraightConnectionPainter renders segmented orthogonal connection paths and highlight states.
#include "nodeeditor/straightconnectionpainter.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/portcolors.h"
#include <QtNodes/StyleCollection>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QPainter>
#include <QPainterPathStroker>

namespace {

constexpr int kConnectedHighlightDataRole = 1;

QPainterPath segmentedPath(QtNodes::ConnectionGraphicsObject const& cgo) {
    const QPointF out = cgo.out();
    const QPointF in = cgo.in();
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
