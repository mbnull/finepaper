#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QSize>

namespace finepaper {

inline constexpr int relatedHighlightDataRole = 0x464e01;
inline constexpr int domainColorsDataRole = 0x464e10;
inline constexpr int domainAssignmentStateDataRole = 0x464e11;
inline constexpr int domainCrossingDataRole = 0x464e12;
inline constexpr int domainCrossingColorDataRole = 0x464e13;
inline constexpr int domainOverrideDataRole = 0x464e14;

enum class OrthogonalRouteAxis {
    Horizontal,
    Vertical
};

enum class RouterInputPort : unsigned int {
    West = 0,
    North = 1,
    Endpoint = 2
};

enum class RouterOutputPort : unsigned int {
    East = 0,
    South = 1
};

enum class EndpointOutputPort : unsigned int {
    Attachment = 0
};

constexpr unsigned int portIndex(RouterInputPort port) {
    return static_cast<unsigned int>(port);
}

constexpr unsigned int portIndex(RouterOutputPort port) {
    return static_cast<unsigned int>(port);
}

constexpr unsigned int portIndex(EndpointOutputPort port) {
    return static_cast<unsigned int>(port);
}

struct NocEditorMetrics {
    qreal routerHorizontalSpacing = 340.0;
    qreal routerVerticalSpacing = 300.0;
    qreal endpointHorizontalOffset = 190.0;
    qreal endpointTopOffset = 48.0;
    qreal endpointVerticalSpacing = 72.0;
    QSize expandedRouterSize{160, 160};
    QSize collapsedRouterSize{96, 96};
    QSize endpointSize{150, 72};
};

const NocEditorMetrics& nocEditorMetrics();

QPainterPath orthogonalConnectionPath(
    QPointF source,
    QPointF target,
    OrthogonalRouteAxis axis = OrthogonalRouteAxis::Horizontal);

} // namespace finepaper
