// PortAnchorGeometry provides small geometry helpers shared by node layout and connection painting.
#pragma once

#include <QPointF>
#include <QSize>
#include <QtGlobal>
#include <cmath>

namespace PortAnchorGeometry {

inline QPointF cardinalNormal(QPointF normal) {
    if (std::abs(normal.x()) >= std::abs(normal.y())) {
        if (normal.x() > 0.0) return QPointF(1.0, 0.0);
        if (normal.x() < 0.0) return QPointF(-1.0, 0.0);
    } else {
        if (normal.y() > 0.0) return QPointF(0.0, 1.0);
        if (normal.y() < 0.0) return QPointF(0.0, -1.0);
    }
    return {};
}

inline QPointF oppositeNormal(QPointF normal) {
    const QPointF cardinal = cardinalNormal(normal);
    return cardinal.isNull() ? QPointF() : QPointF(-cardinal.x(), -cardinal.y());
}

inline QPointF anchorForNormal(QSize const& nodeSize, QPointF normal) {
    const QPointF cardinal = cardinalNormal(normal);
    if (cardinal.x() < 0.0) return QPointF(0.0, nodeSize.height() / 2.0);
    if (cardinal.x() > 0.0) return QPointF(nodeSize.width(), nodeSize.height() / 2.0);
    if (cardinal.y() < 0.0) return QPointF(nodeSize.width() / 2.0, 0.0);
    if (cardinal.y() > 0.0) return QPointF(nodeSize.width() / 2.0, nodeSize.height());
    return QPointF(nodeSize.width(), nodeSize.height() / 2.0);
}

inline QPointF normalFromEdge(const QPointF& position,
                              QSize const& nodeSize,
                              qreal tolerance = 0.5) {
    if (position.x() <= tolerance) return QPointF(-1.0, 0.0);
    if (position.x() >= nodeSize.width() - tolerance) return QPointF(1.0, 0.0);
    if (position.y() <= tolerance) return QPointF(0.0, -1.0);
    if (position.y() >= nodeSize.height() - tolerance) return QPointF(0.0, 1.0);
    return {};
}

} // namespace PortAnchorGeometry
