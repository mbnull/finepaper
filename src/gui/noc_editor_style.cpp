#include "gui/noc_editor_style.h"

#include <cmath>

namespace finepaper {

QPainterPath orthogonalConnectionPath(QPointF source, QPointF target) {
    QPainterPath path(source);
    if (std::abs(source.x() - target.x()) <= 0.5
        || std::abs(source.y() - target.y()) <= 0.5) {
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
