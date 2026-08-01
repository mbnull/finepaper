#include "features/topology/noc_editor_style.h"

#include "features/domain/domain_presentation.h"
#include "ui/theme/ui_tokens.h"

#include <QtNodes/GraphicsViewStyle>
#include <QtNodes/StyleCollection>

#include <cmath>
#include <optional>
#include <utility>

namespace finepaper {

namespace {

QColor moveToCanvasContrast(QColor candidate,
                            const QColor& canvas,
                            const QColor& guaranteedColor) {
    constexpr double minimumContrast = 3.0;
    if (!candidate.isValid()) {
        return guaranteedColor;
    }
    candidate.setAlpha(255);
    if (ui::contrastRatio(candidate, canvas) >= minimumContrast) {
        return candidate;
    }

    double lower = 0.0;
    double upper = 1.0;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const double middle = (lower + upper) / 2.0;
        if (ui::contrastRatio(
                ui::blend(candidate, guaranteedColor, middle), canvas)
            >= minimumContrast) {
            upper = middle;
        } else {
            lower = middle;
        }
    }
    return ui::blend(candidate, guaranteedColor, upper);
}

} // namespace

const NocEditorMetrics& nocEditorMetrics() {
    static const NocEditorMetrics metrics;
    return metrics;
}

NocEditorColors nocEditorColors(const QPalette& palette) {
    static thread_local qint64 cachedPaletteKey = 0;
    static thread_local std::optional<NocEditorColors> cachedColors =
        std::nullopt;
    const qint64 paletteKey = palette.cacheKey();
    if (cachedColors && cachedPaletteKey == paletteKey) {
        return *cachedColors;
    }

    const ui::UiColors semantic = ui::colors(palette);
    const bool dark = ui::isDarkPalette(palette);

    const QColor routerSurface = ui::blend(
        semantic.surfaceRaised, semantic.accent, dark ? 0.16 : 0.08);
    const QColor routerHeader = ui::blend(
        semantic.surfaceRaised, semantic.accent, dark ? 0.42 : 0.24);
    const QColor endpointSurface = ui::blend(
        semantic.surfaceRaised, semantic.warning, dark ? 0.14 : 0.065);
    const QColor endpointHeader = ui::blend(
        semantic.surfaceRaised, semantic.warning, dark ? 0.38 : 0.20);
    const QColor pendingHeader = ui::blend(
        semantic.surfaceRaised, semantic.accent, dark ? 0.50 : 0.30);
    const QColor connection = moveToCanvasContrast(
        ui::blend(semantic.canvas, semantic.text, dark ? 0.52 : 0.44),
        semantic.canvas,
        semantic.text);

    NocEditorColors result = {
        semantic.canvas,
        semantic.canvasFineGrid,
        semantic.canvasCoarseGrid,
        semantic.surfaceRaised,
        routerSurface,
        routerHeader,
        endpointSurface,
        endpointHeader,
        semantic.accentSubtle,
        pendingHeader,
        semantic.outline,
        semantic.outlineStrong,
        semantic.text,
        semantic.mutedText,
        semantic.accent,
        semantic.accentSubtle,
        semantic.success,
        semantic.warning,
        semantic.error,
        connection,
        ui::blend(semantic.text, semantic.surfaceRaised, 0.24),
        ui::blend(semantic.surface, semantic.mutedText, dark ? 0.34 : 0.26),
        ui::blend(semantic.surface, semantic.mutedText, dark ? 0.20 : 0.14),
    };
    cachedPaletteKey = paletteKey;
    cachedColors = result;
    return result;
}

QFont nocEditorFont(NocEditorFontRole role,
                    const QFont& applicationFont) {
    switch (role) {
    case NocEditorFontRole::Body:
        return ui::fontForRole(ui::UiFontRole::Body, applicationFont);
    case NocEditorFontRole::Label:
        return ui::fontForRole(ui::UiFontRole::Label, applicationFont);
    case NocEditorFontRole::Caption:
        return ui::fontForRole(ui::UiFontRole::Caption, applicationFont);
    }
    return applicationFont;
}

void applyNocEditorPalette(const QPalette& palette) {
    const NocEditorColors visual = nocEditorColors(palette);
    QtNodes::GraphicsViewStyle viewStyle;
    viewStyle.BackgroundColor = visual.canvas;
    viewStyle.FineGridColor = visual.fineGrid;
    viewStyle.CoarseGridColor = visual.coarseGrid;
    QtNodes::StyleCollection::setGraphicsViewStyle(std::move(viewStyle));
}

QColor readableNocCanvasAccent(const QColor& preferred,
                               const QPalette& palette) {
    const NocEditorColors visual = nocEditorColors(palette);
    return moveToCanvasContrast(preferred, visual.canvas, visual.text);
}

QColor nocDomainPatternInk(const QColor& background,
                           const QPalette& palette) {
    const NocEditorColors visual = nocEditorColors(palette);
    return ui::contrastRatio(visual.text, background)
            >= ui::contrastRatio(visual.surfaceRaised, background)
        ? visual.text : visual.surfaceRaised;
}

Qt::BrushStyle nocDomainPattern(const QString& domainId) {
    return domainPresentationPattern(domainId);
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
