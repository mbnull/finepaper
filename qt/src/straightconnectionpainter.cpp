#include "straightconnectionpainter.h"
#include "graphnodemodel.h"
#include "portcolors.h"
#include <QtNodes/StyleCollection>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QPainter>
#include <QPainterPathStroker>

namespace {

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
    if (graphModel) {
        auto* model = graphModel->delegateModel<GraphNodeModel>(cgo.connectionId().outNodeId);
        if (model) {
            if (const Port* sourcePort = model->portAt(QtNodes::PortType::Out, cgo.connectionId().outPortIndex)) {
                return PortColors::colorForPort(*sourcePort);
            }
        }
    }

    auto const& style = QtNodes::StyleCollection::connectionStyle();
    if (cgo.isSelected()) return style.selectedColor();
    if (cgo.connectionState().hovered()) return style.hoveredColor();
    return style.normalColor();
}

} // namespace

void StraightConnectionPainter::paint(QPainter* painter, QtNodes::ConnectionGraphicsObject const& cgo) const {
    auto const& style = QtNodes::StyleCollection::connectionStyle();
    QPen pen(connectionColor(cgo), style.lineWidth());
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
