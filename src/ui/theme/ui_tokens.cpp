#include "ui/theme/ui_tokens.h"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace finepaper::ui {
namespace {

constexpr double kMinimumTextContrast = 4.5;
constexpr double kMinimumControlContrast = 3.0;
constexpr int kContrastSearchIterations = 24;

using ContrastBackgrounds = std::array<QColor, 2>;

const QColor kBlack(QStringLiteral("#000000"));
const QColor kWhite(QStringLiteral("#ffffff"));
const QColor kLightSurfaceFallback(QStringLiteral("#f6f7f9"));
const QColor kDarkSurfaceFallback(QStringLiteral("#171a1f"));
const QColor kLightAccentFallback(QStringLiteral("#2563eb"));
const QColor kDarkAccentFallback(QStringLiteral("#60a5fa"));
const QColor kLightSuccessFallback(QStringLiteral("#157a48"));
const QColor kDarkSuccessFallback(QStringLiteral("#4ade80"));
const QColor kLightWarningFallback(QStringLiteral("#9a5b00"));
const QColor kDarkWarningFallback(QStringLiteral("#fbbf24"));
const QColor kLightErrorFallback(QStringLiteral("#b42318"));
const QColor kDarkErrorFallback(QStringLiteral("#fb7185"));

double linearChannel(int channel) {
    const double value = static_cast<double>(channel) / 255.0;
    return value <= 0.04045
        ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color) {
    const QColor rgb = color.toRgb();
    return 0.2126 * linearChannel(rgb.red())
        + 0.7152 * linearChannel(rgb.green())
        + 0.0722 * linearChannel(rgb.blue());
}

QColor opaqueOver(const QColor& foreground, const QColor& background) {
    if (!foreground.isValid()) {
        return background;
    }
    if (foreground.alpha() == 255) {
        return foreground.toRgb();
    }
    const QColor opaqueBackground = background.isValid()
        ? background.toRgb() : kWhite;
    QColor opaqueForeground = foreground.toRgb();
    opaqueForeground.setAlpha(255);
    return blend(opaqueBackground, opaqueForeground, foreground.alphaF());
}

QColor paletteColor(const QPalette& palette,
                    QPalette::ColorRole role,
                    const QColor& fallback,
                    const QColor& background) {
    const QColor candidate = palette.color(QPalette::Active, role);
    return opaqueOver(candidate.isValid() ? candidate : fallback, background);
}

QColor contrastPole(const QColor& background) {
    return contrastRatio(kBlack, background)
            >= contrastRatio(kWhite, background)
        ? kBlack : kWhite;
}

double weakestContrast(const QColor& foreground,
                       const ContrastBackgrounds& backgrounds) {
    return (std::min)(contrastRatio(foreground, backgrounds[0]),
                      contrastRatio(foreground, backgrounds[1]));
}

QColor sharedContrastPole(const ContrastBackgrounds& backgrounds) {
    return weakestContrast(kBlack, backgrounds)
            >= weakestContrast(kWhite, backgrounds)
        ? kBlack : kWhite;
}

bool meetsContrast(const QColor& foreground,
                   const ContrastBackgrounds& backgrounds,
                   double minimumRatio) {
    return weakestContrast(foreground, backgrounds) >= minimumRatio;
}

QColor moveToContrast(const QColor& candidate,
                      const QColor& background,
                      const QColor& guaranteedColor,
                      double minimumRatio) {
    if (contrastRatio(candidate, background) >= minimumRatio) {
        return candidate;
    }
    if (contrastRatio(guaranteedColor, background) < minimumRatio) {
        return contrastPole(background);
    }

    double lower = 0.0;
    double upper = 1.0;
    for (int iteration = 0;
         iteration < kContrastSearchIterations;
         ++iteration) {
        const double middle = (lower + upper) / 2.0;
        if (contrastRatio(blend(candidate, guaranteedColor, middle), background)
            >= minimumRatio) {
            upper = middle;
        } else {
            lower = middle;
        }
    }
    return blend(candidate, guaranteedColor, upper);
}

QColor surfaceWithSharedOutlineContrast(const QColor& surface,
                                        const QColor& preferredRaised,
                                        double minimumRatio) {
    ContrastBackgrounds backgrounds = {surface, preferredRaised};
    if (meetsContrast(
            sharedContrastPole(backgrounds), backgrounds, minimumRatio)) {
        return preferredRaised;
    }

    // A single flat outline cannot satisfy the requested ratio when two
    // backgrounds straddle its dark and light contrast ranges. This can only
    // happen with a contradictory system palette (for example, a dark Window
    // paired with even darker WindowText and therefore classified as light).
    // Preserve the intended raised direction, but reduce its amount to the
    // closest point where black or white is a common usable boundary color.
    double compatibleAmount = 0.0;
    double incompatibleAmount = 1.0;
    QColor compatibleSurface = surface;
    for (int iteration = 0;
         iteration < kContrastSearchIterations;
         ++iteration) {
        const double amount = (compatibleAmount + incompatibleAmount) / 2.0;
        const QColor raised = blend(surface, preferredRaised, amount);
        backgrounds[1] = raised;
        if (meetsContrast(
                sharedContrastPole(backgrounds), backgrounds, minimumRatio)) {
            compatibleAmount = amount;
            compatibleSurface = raised;
        } else {
            incompatibleAmount = amount;
        }
    }
    return compatibleSurface;
}

QColor moveToSharedContrast(const QColor& candidate,
                            const ContrastBackgrounds& backgrounds,
                            const QColor& preferredTarget,
                            double minimumRatio) {
    if (meetsContrast(candidate, backgrounds, minimumRatio)) {
        return candidate;
    }

    const QColor target = meetsContrast(
                              preferredTarget, backgrounds, minimumRatio)
        ? preferredTarget : sharedContrastPole(backgrounds);
    if (!meetsContrast(target, backgrounds, minimumRatio)) {
        // surfaceWithSharedOutlineContrast() establishes this invariant for
        // semantic surfaces. Keep the best possible pole as a safe fallback
        // if this helper is reused with unrelated backgrounds in the future.
        return target;
    }

    double lower = 0.0;
    double upper = 1.0;
    for (int iteration = 0;
         iteration < kContrastSearchIterations;
         ++iteration) {
        const double middle = (lower + upper) / 2.0;
        if (meetsContrast(
                blend(candidate, target, middle), backgrounds, minimumRatio)) {
            upper = middle;
        } else {
            lower = middle;
        }
    }

    const QColor adjusted = blend(candidate, target, upper);
    return meetsContrast(adjusted, backgrounds, minimumRatio)
        ? adjusted : target;
}

QColor readableColor(const QColor& preferred,
                     const QColor& background) {
    return moveToContrast(
        preferred, background, contrastPole(background),
        kMinimumTextContrast);
}

QColor distinguishableAccent(const QColor& preferred,
                             const QColor& surface) {
    return moveToContrast(
        preferred, surface, contrastPole(surface),
        kMinimumControlContrast);
}

void scaleFont(QFont& font, double factor) {
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize((std::max)(
            1, qRound(font.pixelSize() * factor)));
    }
}

} // namespace

bool isDarkPalette(const QPalette& palette) {
    const QColor window = palette.color(QPalette::Active, QPalette::Window);
    const QColor windowText = palette.color(
        QPalette::Active, QPalette::WindowText);
    if (!window.isValid() || !windowText.isValid()) {
        return false;
    }
    const double windowLuminance = relativeLuminance(window);
    const double textLuminance = relativeLuminance(windowText);
    if (std::abs(windowLuminance - textLuminance) > 0.02) {
        return windowLuminance < textLuminance;
    }
    return window.lightnessF() < 0.5;
}

QColor blend(const QColor& from, const QColor& to, double amount) {
    if (!from.isValid()) {
        return to;
    }
    if (!to.isValid()) {
        return from;
    }
    const double boundedAmount = std::clamp(amount, 0.0, 1.0);
    const QColor source = from.toRgb();
    const QColor target = to.toRgb();
    const auto channel = [boundedAmount](int sourceValue, int targetValue) {
        return qRound(static_cast<double>(sourceValue)
                      + (static_cast<double>(targetValue - sourceValue)
                         * boundedAmount));
    };
    return QColor(
        channel(source.red(), target.red()),
        channel(source.green(), target.green()),
        channel(source.blue(), target.blue()),
        channel(source.alpha(), target.alpha()));
}

double contrastRatio(const QColor& foreground, const QColor& background) {
    if (!foreground.isValid() || !background.isValid()) {
        return 1.0;
    }
    const double foregroundLuminance = relativeLuminance(foreground);
    const double backgroundLuminance = relativeLuminance(background);
    const double lighter = (std::max)(
        foregroundLuminance, backgroundLuminance);
    const double darker = (std::min)(
        foregroundLuminance, backgroundLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

UiColors colors(const QPalette& palette) {
    static thread_local qint64 cachedPaletteKey = 0;
    static thread_local std::optional<UiColors> cachedColors = std::nullopt;
    const qint64 paletteKey = palette.cacheKey();
    if (cachedColors && cachedPaletteKey == paletteKey) {
        return *cachedColors;
    }

    const bool dark = isDarkPalette(palette);
    const QColor fallbackSurface = dark
        ? kDarkSurfaceFallback : kLightSurfaceFallback;
    const QColor surface = paletteColor(
        palette, QPalette::Window, fallbackSurface, fallbackSurface);
    const QColor preferredText = paletteColor(
        palette, QPalette::WindowText,
        dark ? kWhite : kBlack, surface);
    const QColor text = readableColor(preferredText, surface);

    const QColor preferredSurfaceRaised = dark
        ? blend(surface, kWhite, 0.055)
        : blend(surface, kWhite, 0.58);
    const QColor surfaceRaised = surfaceWithSharedOutlineContrast(
        surface, preferredSurfaceRaised, kMinimumControlContrast);
    const QColor surfaceSunken = dark
        ? blend(surface, kBlack, 0.20)
        : blend(surface, kBlack, 0.045);
    const QColor outline = blend(surface, text, dark ? 0.22 : 0.16);
    const QColor outlineStrong = moveToSharedContrast(
        blend(surface, text, dark ? 0.36 : 0.30),
        {surface, surfaceRaised}, text, kMinimumControlContrast);

    const QColor preferredMutedText = paletteColor(
        palette, QPalette::PlaceholderText,
        blend(text, surface, 0.28), surface);
    const QColor mutedText = moveToContrast(
        preferredMutedText, surface, text, kMinimumTextContrast);

    const QColor preferredAccent = paletteColor(
        palette, QPalette::Highlight,
        dark ? kDarkAccentFallback : kLightAccentFallback, surface);
    const QColor accent = distinguishableAccent(preferredAccent, surface);
    const QColor preferredOnAccent = paletteColor(
        palette, QPalette::HighlightedText,
        contrastPole(accent), accent);
    const QColor onAccent = readableColor(preferredOnAccent, accent);
    const QColor accentContrastPole = onAccent.lightnessF() > 0.5
        ? kBlack : kWhite;
    const QColor accentHover = moveToContrast(
        blend(accent, accentContrastPole, 0.08),
        onAccent, accentContrastPole, kMinimumTextContrast);
    const QColor accentPressed = moveToContrast(
        blend(accent, accentContrastPole, 0.16),
        onAccent, accentContrastPole, kMinimumTextContrast);
    const QColor accentSubtle = blend(
        surface, accent, dark ? 0.24 : 0.13);

    const QColor success = moveToContrast(
        dark ? kDarkSuccessFallback : kLightSuccessFallback,
        surface, text, kMinimumTextContrast);
    const QColor warning = moveToContrast(
        dark ? kDarkWarningFallback : kLightWarningFallback,
        surface, text, kMinimumTextContrast);
    const QColor error = moveToContrast(
        dark ? kDarkErrorFallback : kLightErrorFallback,
        surface, text, kMinimumTextContrast);

    const QColor canvas = dark
        ? blend(surfaceSunken, kBlack, 0.18)
        : blend(surfaceSunken, text, 0.035);
    const QColor canvasFineGrid = blend(canvas, text, dark ? 0.10 : 0.075);
    const QColor canvasCoarseGrid = blend(canvas, text, dark ? 0.19 : 0.14);

    UiColors result = {
        surface,
        surfaceRaised,
        surfaceSunken,
        outline,
        outlineStrong,
        text,
        mutedText,
        accent,
        onAccent,
        accentHover,
        accentPressed,
        accentSubtle,
        success,
        warning,
        error,
        canvas,
        canvasFineGrid,
        canvasCoarseGrid,
    };
    cachedPaletteKey = paletteKey;
    cachedColors = result;
    return result;
}

QFont fontForRole(UiFontRole role, const QFont& baseFont) {
    QFont font = baseFont;
    switch (role) {
    case UiFontRole::Body:
        break;
    case UiFontRole::Label:
        font.setWeight(QFont::Medium);
        break;
    case UiFontRole::Caption:
        scaleFont(font, 0.92);
        break;
    case UiFontRole::Subtitle:
        scaleFont(font, 1.125);
        font.setWeight(QFont::DemiBold);
        break;
    case UiFontRole::Title:
        scaleFont(font, 1.25);
        font.setWeight(QFont::DemiBold);
        break;
    }
    return font;
}

} // namespace finepaper::ui
