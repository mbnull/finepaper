#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

namespace finepaper::ui {

struct UiMetrics final {
    static constexpr int spacing4 = 4;
    static constexpr int spacing8 = 8;
    static constexpr int spacing12 = 12;
    static constexpr int spacing16 = 16;
    static constexpr int spacing24 = 24;
    static constexpr int spacing32 = 32;

    static constexpr int radiusSmall = 6;
    static constexpr int radiusLarge = 10;

    static constexpr int controlCompactHeight = 32;
    static constexpr int controlHeight = 36;
};

struct UiColors final {
    QColor surface;
    QColor surfaceRaised;
    QColor surfaceSunken;
    QColor outline;
    QColor outlineStrong;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor onAccent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentSubtle;
    QColor success;
    QColor warning;
    QColor error;
    QColor canvas;
    QColor canvasFineGrid;
    QColor canvasCoarseGrid;
};

enum class UiFontRole {
    Body,
    Label,
    Caption,
    Subtitle,
    Title,
};

// Returns true when Window is perceptually darker than WindowText. This keeps
// high-contrast system palettes useful without assuming a particular theme.
[[nodiscard]] bool isDarkPalette(const QPalette& palette);

// Interpolates from `from` to `to`. An amount of zero returns `from`; one
// returns `to`.
[[nodiscard]] QColor blend(const QColor& from,
                           const QColor& to,
                           double amount);

// WCAG 2.x contrast ratio in the inclusive range [1, 21]. Callers should pass
// opaque colors; the semantic colors returned by colors() are opaque.
[[nodiscard]] double contrastRatio(const QColor& foreground,
                                   const QColor& background);

// Derives semantic colors from the active system palette. The returned text /
// surface and onAccent / accent pairs always meet a 4.5:1 contrast ratio.
[[nodiscard]] UiColors colors(const QPalette& palette);

// Keeps the system family and point/pixel sizing model. Roles scale from the
// supplied application font instead of imposing a fixed body font.
[[nodiscard]] QFont fontForRole(UiFontRole role, const QFont& baseFont);

} // namespace finepaper::ui
