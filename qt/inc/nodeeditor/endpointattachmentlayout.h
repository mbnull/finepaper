// EndpointAttachmentLayout computes visual placement for endpoints attached to router anchors.
#pragma once

#include "nodeeditor/portanchorgeometry.h"
#include <QPointF>
#include <QSize>

namespace EndpointAttachmentLayout {

inline QPointF endpointAnchorForHostNormal(QSize const& endpointSize, QPointF hostNormal) {
    if (hostNormal.x() > 0.0) {
        return QPointF(0.0, endpointSize.height() / 2.0);
    }

    return QPointF(endpointSize.width(), endpointSize.height() / 2.0);
}

inline QPointF endpointTopLeft(const QPointF& hostAnchorScene,
                               const QPointF& hostNormal,
                               const QPointF& endpointAnchorLocal,
                               qreal gap) {
    return hostAnchorScene + (hostNormal * gap) - endpointAnchorLocal;
}

} // namespace EndpointAttachmentLayout
