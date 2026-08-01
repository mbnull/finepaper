#pragma once

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPalette>
#include <QPointF>
#include <QSize>
#include <QString>

namespace finepaper {

inline constexpr int relatedHighlightDataRole = 0x464e01;
inline constexpr int domainColorsDataRole = 0x464e10;
inline constexpr int domainAssignmentStateDataRole = 0x464e11;
inline constexpr int domainCrossingDataRole = 0x464e12;
inline constexpr int domainCrossingColorDataRole = 0x464e13;
inline constexpr int domainOverrideDataRole = 0x464e14;
inline constexpr int domainIdsDataRole = 0x464e15;
inline constexpr int domainPatternBrushesDataRole = 0x464e16;
inline constexpr int domainCrossingPaintColorDataRole = 0x464e17;

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
    QSize expandedRouterSize = {160, 160};
    QSize collapsedRouterSize = {96, 96};
    QSize endpointSize = {150, 72};
};

// Canvas-specific semantic colors derived from the active application
// palette. Raw fallback colors remain centralized in ui/theme/ui_tokens.cpp.
// Derived values are cached by QPalette::cacheKey(), so paint hot paths can
// request them without repeating contrast correction for every item.
struct NocEditorColors {
    QColor canvas;
    QColor fineGrid;
    QColor coarseGrid;
    QColor surfaceRaised;
    QColor routerSurface;
    QColor routerHeader;
    QColor endpointSurface;
    QColor endpointHeader;
    QColor pendingSurface;
    QColor pendingHeader;
    QColor outline;
    QColor outlineStrong;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor accentSubtle;
    QColor success;
    QColor warning;
    QColor error;
    QColor connection;
    QColor portFill;
    QColor domainUnavailable;
    QColor domainUnassigned;
};

enum class NocEditorFontRole {
    Body,
    Label,
    Caption,
};

const NocEditorMetrics& nocEditorMetrics();

[[nodiscard]] NocEditorColors nocEditorColors(const QPalette& palette);

[[nodiscard]] QFont nocEditorFont(NocEditorFontRole role,
                                  const QFont& applicationFont);

// Updates the QtNodes grid colors. Custom node and connection painters obtain
// their colors directly from the cache-backed nocEditorColors() helper.
void applyNocEditorPalette(const QPalette& palette);

// Domain colors are package data rather than palette colors. These helpers
// retain the authored hue while making the mark readable on the active canvas
// and selecting a contrasting palette-derived texture ink.
[[nodiscard]] QColor readableNocCanvasAccent(const QColor& preferred,
                                             const QPalette& palette);
[[nodiscard]] QColor nocDomainPatternInk(const QColor& background,
                                         const QPalette& palette);
[[nodiscard]] Qt::BrushStyle nocDomainPattern(const QString& domainId);

QPainterPath orthogonalConnectionPath(
    QPointF source,
    QPointF target,
    OrthogonalRouteAxis axis = OrthogonalRouteAxis::Horizontal);

} // namespace finepaper
