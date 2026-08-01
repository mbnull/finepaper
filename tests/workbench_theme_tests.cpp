#include "ui/components/empty_state.h"
#include "ui/theme/ui_tokens.h"
#include "ui/theme/workbench_style.h"

#include <QApplication>
#include <QLabel>
#include <QPalette>
#include <QString>
#include <QTextStream>

#include <cmath>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

QPalette lightPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#f4f6f8")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#17202a")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#17202a")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2864dc")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#52606d")));
    return palette;
}

QPalette darkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#171a1f")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#eef2f7")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#111318")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#eef2f7")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#76a9ff")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#101318")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#aab4c0")));
    return palette;
}

void verifySemanticPalette(const QPalette& palette, bool expectedDark) {
    const finepaper::ui::UiColors token = finepaper::ui::colors(palette);
    check(finepaper::ui::isDarkPalette(palette) == expectedDark,
          QStringLiteral("palette darkness is derived from the active system palette"));
    check(finepaper::ui::contrastRatio(token.text, token.surface) >= 4.5,
          QStringLiteral("body text meets WCAG AA contrast against the surface"));
    check(finepaper::ui::contrastRatio(token.mutedText, token.surface) >= 4.5,
          QStringLiteral("muted text remains readable against the surface"));
    check(finepaper::ui::contrastRatio(token.onAccent, token.accent) >= 4.5,
          QStringLiteral("accent controls keep readable foreground text"));
    check(token.canvas != token.canvasFineGrid
              && token.canvasFineGrid != token.canvasCoarseGrid,
          QStringLiteral("canvas background and both grid levels remain distinguishable"));
    check(token.outline != token.surfaceRaised,
          QStringLiteral("raised cards retain a visible outline"));

    const QString sheet = finepaper::ui::workbenchStyleSheet(palette);
    check(sheet.contains(QStringLiteral("finepaperRole=\"primary\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"danger\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"canvasMode\""))
              && sheet.contains(QStringLiteral("QGraphicsView:focus")),
          QStringLiteral("workbench stylesheet exposes stable semantic component roles"));
    check(sheet.contains(token.surface.name(QColor::HexRgb))
              && sheet.contains(token.accent.name(QColor::HexRgb))
              && sheet.contains(token.canvas.name(QColor::HexRgb)),
          QStringLiteral("stylesheet is generated from semantic palette tokens"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("finepaper-workbench-theme-tests"));

    const QPalette light = lightPalette();
    const QPalette dark = darkPalette();
    verifySemanticPalette(light, false);
    verifySemanticPalette(dark, true);

    const QFont baseFont = application.font();
    const QFont titleFont = finepaper::ui::fontForRole(
        finepaper::ui::UiFontRole::Title, baseFont);
    check(titleFont.family() == baseFont.family(),
          QStringLiteral("type roles preserve the platform font family"));
    check(titleFont.weight() >= QFont::DemiBold,
          QStringLiteral("title type role has enough visual emphasis"));

    finepaper::ui::EmptyState emptyState;
    QFont enlargedFont = baseFont;
    enlargedFont.setPointSizeF(baseFont.pointSizeF() + 2.0);
    emptyState.setFont(enlargedFont);
    application.processEvents();
    QLabel* emptyTitle = nullptr;
    for (QLabel* label : emptyState.findChildren<QLabel*>()) {
        if (label->property("finepaperRole").toString()
            == QStringLiteral("title")) {
            emptyTitle = label;
            break;
        }
    }
    check(emptyTitle
              && emptyTitle->font().pointSizeF()
                  == finepaper::ui::fontForRole(
                         finepaper::ui::UiFontRole::Title,
                         enlargedFont).pointSizeF(),
          QStringLiteral(
              "Empty State role fonts follow runtime application font changes"));

    application.setPalette(light);
    finepaper::ui::applyWorkbenchStyle(application);
    const QString lightSheet = application.styleSheet();
    application.setPalette(dark);
    application.processEvents();
    const QString darkSheet = application.styleSheet();
    check(!lightSheet.isEmpty() && !darkSheet.isEmpty()
              && lightSheet != darkSheet,
          QStringLiteral("runtime system palette changes refresh the workbench theme"));
    check(darkSheet.contains(
              finepaper::ui::colors(dark).surface.name(QColor::HexRgb)),
          QStringLiteral("refreshed stylesheet uses the new palette tokens"));

    return failures == 0 ? 0 : 1;
}
