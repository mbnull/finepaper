#include "graphnodepainter.h"
#include "graphnodemodel.h"
#include "portcolors.h"
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QPainter>
#include <QSet>

namespace {

QColor nodeBackground(const GraphNodeModel* model) {
    if (!model || !model->module()) return QColor(224, 224, 224);
    if (model->module()->type() == "XP") return QColor(124, 185, 232);
    if (model->module()->type() == "Endpoint") return QColor(214, 244, 182);
    return QColor(224, 224, 224);
}

void drawPorts(QPainter* painter,
               QtNodes::NodeGraphicsObject& ngo,
               const GraphNodeModel* model,
               QtNodes::PortType portType,
               QSet<QString>& paintedRouterSides) {
    if (!model) return;

    auto const& geometry = ngo.nodeScene()->nodeGeometry();
    const unsigned int portCount = model->nPorts(portType);

    for (unsigned int index = 0; index < portCount; ++index) {
        const Port* port = model->portAt(portType, index);
        if (!port) continue;

        if (port->type() == "router") {
            const QString side = PortLayout::routerSideId(port->id());
            if (paintedRouterSides.contains(side)) {
                continue;
            }
            paintedRouterSides.insert(side);
        }

        const QPointF center = geometry.portPosition(ngo.nodeId(), portType, index);
        const QColor fill = PortColors::colorForPort(*port);

        painter->setPen(QPen(QColor(38, 38, 38), 1.25));
        painter->setBrush(fill);
        painter->drawEllipse(center, 5.5, 5.5);

        if (port->type() == "router") {
            QRectF textRect(center.x() - 22.0, center.y() - 8.0, 14.0, 16.0);
            painter->setPen(QColor(22, 22, 22));
            painter->setFont(QFont(QStringLiteral("Sans Serif"), 7, QFont::Bold));
            painter->drawText(textRect, Qt::AlignCenter, port->name());
        }
    }
}

} // namespace

void GraphNodePainter::paint(QPainter* painter, QtNodes::NodeGraphicsObject& ngo) const {
    auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&ngo.graphModel());
    auto* model = graphModel ? graphModel->delegateModel<GraphNodeModel>(ngo.nodeId()) : nullptr;
    auto const& geometry = ngo.nodeScene()->nodeGeometry();
    const QSize nodeSize = geometry.size(ngo.nodeId());
    const QRectF bodyRect(0.0, 0.0, nodeSize.width(), nodeSize.height());

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen borderPen(ngo.isSelected() ? QColor(214, 108, 32) : QColor(44, 44, 44),
                   ngo.isSelected() ? 2.4 : 1.4);
    painter->setPen(borderPen);
    painter->setBrush(nodeBackground(model));
    painter->drawRoundedRect(bodyRect, 8.0, 8.0);

    painter->setPen(QColor(22, 22, 22));
    painter->setFont(QFont(QStringLiteral("Sans Serif"), 9, QFont::DemiBold));
    painter->drawText(geometry.captionRect(ngo.nodeId()), Qt::AlignCenter, model ? model->caption() : QStringLiteral("Node"));

    QSet<QString> paintedRouterSides;
    drawPorts(painter, ngo, model, QtNodes::PortType::In, paintedRouterSides);
    drawPorts(painter, ngo, model, QtNodes::PortType::Out, paintedRouterSides);
}
