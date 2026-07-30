#include "features/topology/noc_editor_style.h"

#include <cmath>

namespace finepaper {

const NocEditorMetrics& nocEditorMetrics() {
    static const NocEditorMetrics metrics;
    return metrics;
}

QPainterPath orthogonalConnectionPath(QPointF source,
                                       QPointF target,
                                       OrthogonalRouteAxis axis) {
    QPainterPath path(source);
    if (std::abs(source.x() - target.x()) <= 0.5
        || std::abs(source.y() - target.y()) <= 0.5) {
        path.lineTo(target);
        return path;
    }

    if (axis == OrthogonalRouteAxis::Vertical) {
        const qreal middleY = (source.y() + target.y()) / 2.0;
        path.lineTo(source.x(), middleY);
        path.lineTo(target.x(), middleY);
        path.lineTo(target);
        return path;
    }

    const qreal middleX = (source.x() + target.x()) / 2.0;
    path.lineTo(middleX, source.y());
    path.lineTo(middleX, target.y());
    path.lineTo(target);
    return path;
}

} // namespace finepaper
