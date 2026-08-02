#include "ui/components/empty_state.h"
#include "ui/components/segmented_action_control.h"
#include "ui/theme/ui_tokens.h"
#include "ui/theme/workbench_style.h"
#include "ui/workbench/widgets/workbench_dock_title_bar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyle>
#include <QString>
#include <QTextStream>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
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
    check(finepaper::ui::contrastRatio(token.outlineStrong, token.surface)
                  >= 3.0
              && finepaper::ui::contrastRatio(
                     token.outlineStrong, token.surfaceRaised)
                  >= 3.0,
          QStringLiteral(
              "strong control outlines meet non-text contrast on both shell surfaces"));
    check(token.canvas != token.canvasFineGrid
              && token.canvasFineGrid != token.canvasCoarseGrid,
          QStringLiteral("canvas background and both grid levels remain distinguishable"));
    check(token.outline != token.surfaceRaised,
          QStringLiteral("raised cards retain a visible outline"));

    const QString sheet = finepaper::ui::workbenchStyleSheet(palette);
    check(sheet.contains(QStringLiteral("finepaperRole=\"primary\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"danger\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"segmentedControl\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"segment\""))
              && sheet.contains(QStringLiteral("finepaperRole=\"dockTitleBar\""))
              && sheet.contains(QStringLiteral("QMainWindow::separator"))
              && sheet.contains(QStringLiteral("QGraphicsView:focus"))
              && sheet.contains(
                  QStringLiteral("border: 2px dashed %1")
                      .arg(token.accent.name(QColor::HexRgb))),
          QStringLiteral("workbench stylesheet exposes stable semantic component roles"));
    check(sheet.contains(token.surface.name(QColor::HexRgb))
              && sheet.contains(token.accent.name(QColor::HexRgb))
              && sheet.contains(token.canvas.name(QColor::HexRgb)),
          QStringLiteral("stylesheet is generated from semantic palette tokens"));
}

void sendKey(QWidget* target, int key) {
    if (!target) {
        return;
    }
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void verifySegmentedActionControl(QApplication& application) {
    finepaper::ui::SegmentedActionControl control;
    control.setAccessibleName(QStringLiteral("Canvas interaction mode"));

    QActionGroup modeGroup(&control);
    modeGroup.setExclusive(true);
    QAction selectAction(QStringLiteral("Select"), &control);
    QAction panAction(QStringLiteral("Pan"), &control);
    QAction inspectAction(QStringLiteral("Inspect"), &control);
    for (QAction* action : {&selectAction, &panAction, &inspectAction}) {
        action->setCheckable(true);
        modeGroup.addAction(action);
    }
    selectAction.setStatusTip(QStringLiteral("Select canvas objects"));
    selectAction.setChecked(true);

    QToolButton* selectButton = control.addAction(
        &selectAction, QStringLiteral("test.segment.select"));
    QToolButton* panButton = control.addAction(
        &panAction, QStringLiteral("test.segment.pan"));
    QToolButton* inspectButton = control.addAction(
        &inspectAction, QStringLiteral("test.segment.inspect"));
    control.show();
    application.processEvents();

    check(control.property("finepaperRole").toString()
                  == QStringLiteral("segmentedControl")
              && selectButton && panButton && inspectButton
              && selectButton->property("finepaperRole").toString()
                  == QStringLiteral("segment")
              && selectButton->defaultAction() == &selectAction
              && selectButton->toolButtonStyle() == Qt::ToolButtonTextOnly
              && selectButton->focusPolicy() == Qt::StrongFocus,
          QStringLiteral(
              "segmented controls project shared QActions as keyboard-focusable text"));

    selectButton->setFocus(Qt::TabFocusReason);
    sendKey(selectButton, Qt::Key_Right);
    check(panAction.isChecked() && panButton->hasFocus(),
          QStringLiteral(
              "Right Arrow moves and activates the next canvas mode"));

    control.setLayoutDirection(Qt::RightToLeft);
    panButton->setFocus(Qt::TabFocusReason);
    sendKey(panButton, Qt::Key_Right);
    check(selectAction.isChecked() && selectButton->hasFocus(),
          QStringLiteral(
              "segmented Arrow navigation follows right-to-left visual order"));
    sendKey(selectButton, Qt::Key_End);
    check(inspectAction.isChecked() && inspectButton->hasFocus(),
          QStringLiteral("End activates the final available segment"));

    selectAction.setText(QStringLiteral("&Choose"));
    selectAction.setStatusTip(QStringLiteral("Choose canvas objects"));
    application.processEvents();
    check(selectButton->text() == QStringLiteral("Choose")
              && selectButton->accessibleName() == QStringLiteral("Choose")
              && selectButton->accessibleDescription()
                  == QStringLiteral("Choose canvas objects"),
          QStringLiteral(
              "segment labels and accessible guidance stay synchronized with QAction "
              "(text=%1, name=%2, description=%3)")
              .arg(selectButton->text(),
                   selectButton->accessibleName(),
                   selectButton->accessibleDescription()));

    QFont enlargedFont = control.font();
    if (enlargedFont.pointSizeF() > 0.0) {
        enlargedFont.setPointSizeF(enlargedFont.pointSizeF() * 2.0);
    } else if (enlargedFont.pixelSize() > 0) {
        enlargedFont.setPixelSize(enlargedFont.pixelSize() * 2);
    }
    control.setFont(enlargedFont);
    control.adjustSize();
    application.processEvents();
    const QList<QToolButton*> segmentButtons = control.buttons();
    const bool enlargedLabelsFit = std::all_of(
        segmentButtons.cbegin(), segmentButtons.cend(),
        [](const QToolButton* button) {
            return button && button->height() >= button->fontMetrics().height()
                && button->width()
                    >= button->fontMetrics().horizontalAdvance(button->text());
        });
    check(enlargedLabelsFit,
          QStringLiteral(
              "segmented text remains visible when the system font is enlarged"));

    finepaper::ui::SegmentedActionControl optionalControl;
    QActionGroup optionalGroup(&optionalControl);
    optionalGroup.setExclusionPolicy(
        QActionGroup::ExclusionPolicy::ExclusiveOptional);
    QAction optionalAction(QStringLiteral("Optional"), &optionalControl);
    optionalAction.setCheckable(true);
    optionalGroup.addAction(&optionalAction);
    optionalControl.addAction(&optionalAction);
    optionalControl.show();
    optionalAction.setChecked(true);
    optionalAction.setChecked(false);
    application.processEvents();
    check(!optionalGroup.checkedAction(),
          QStringLiteral(
              "segmented controls preserve ExclusiveOptional action-group semantics"));

    finepaper::ui::SegmentedActionControl lifecycleControl;
    QActionGroup lifecycleGroup(&lifecycleControl);
    lifecycleGroup.setExclusionPolicy(
        QActionGroup::ExclusionPolicy::Exclusive);
    QAction stableAction(QStringLiteral("Stable"), &lifecycleControl);
    stableAction.setCheckable(true);
    lifecycleGroup.addAction(&stableAction);
    stableAction.setChecked(true);
    QToolButton* stableButton = lifecycleControl.addAction(&stableAction);
    QPointer<QToolButton> transientButton;
    {
        QAction transientAction(QStringLiteral("Transient"));
        transientAction.setCheckable(true);
        lifecycleGroup.addAction(&transientAction);
        transientButton = lifecycleControl.addAction(&transientAction);
        lifecycleControl.show();
        application.processEvents();
        check(lifecycleControl.buttons().size() == 2,
              QStringLiteral("both live actions are represented as segments"));
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    sendKey(stableButton, Qt::Key_Right);
    check(lifecycleControl.buttons().size() == 1
              && transientButton.isNull()
              && stableAction.isChecked(),
          QStringLiteral(
              "destroying an external action removes its segment from keyboard navigation"));
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

    const QString emptyDescription = QStringLiteral(
        "Choose an installed NoC IP Package to create a topology, or open an "
        "existing design. Package rules remain the source of truth for the "
        "Mesh, Endpoint types, parameters, and Domains.");
    emptyState.setEyebrow(QStringLiteral("NO DESIGN OPEN"));
    emptyState.setTitle(QStringLiteral("Start a NoC design"));
    emptyState.setDescription(emptyDescription);
    auto* createButton = emptyState.addActionButton(
        QStringLiteral("Create NoC Design"), QStringLiteral("primary"));
    auto* openButton = emptyState.addActionButton(
        QStringLiteral("Open Design…"), QStringLiteral("quiet"));
    auto* installButton = emptyState.addActionButton(
        QStringLiteral("Install NoC IP…"), QStringLiteral("primary"));

    QLabel* emptyDescriptionLabel = nullptr;
    for (QLabel* label : emptyState.findChildren<QLabel*>()) {
        if (label->text() == emptyDescription) {
            emptyDescriptionLabel = label;
            break;
        }
    }

    constexpr int wideCardWidth = 520;
    const int wideCardHeight = emptyState.heightForWidth(wideCardWidth);
    emptyState.resize(wideCardWidth, wideCardHeight);
    emptyState.show();
    application.processEvents();
    check(wideCardHeight > 0
              && createButton->geometry().top()
                  == openButton->geometry().top()
              && openButton->geometry().top()
                  == installButton->geometry().top(),
          QStringLiteral(
              "Empty State actions stay on one row when full labels fit"));

    constexpr int narrowCardWidth = 280;
    const int narrowCardHeight = emptyState.heightForWidth(narrowCardWidth);
    emptyState.resize(narrowCardWidth, narrowCardHeight);
    application.processEvents();
    check(narrowCardHeight > wideCardHeight
              && createButton->geometry().top()
                  < openButton->geometry().top()
              && openButton->geometry().top()
                  < installButton->geometry().top(),
          QStringLiteral(
              "Empty State actions stack vertically before labels clip"));
    check(emptyDescriptionLabel
              && emptyDescriptionLabel->geometry().bottom()
                  < createButton->geometry().top()
              && installButton->geometry().bottom()
                  <= emptyState.contentsRect().bottom(),
          QStringLiteral(
              "Empty State height-for-width keeps large-font content visible"));

    application.setPalette(light);
    finepaper::ui::applyWorkbenchStyle(application);
    const QFont readableApplicationFont = application.font();
    check((readableApplicationFont.pointSizeF() > 0.0
              && readableApplicationFont.pointSizeF()
                  >= finepaper::ui::UiMetrics::minimumBodyPointSize)
              || (readableApplicationFont.pixelSize() > 0
                  && readableApplicationFont.pixelSize()
                      >= finepaper::ui::UiMetrics::minimumBodyPixelSize),
          QStringLiteral(
              "the workbench raises unusually small platform body fonts to "
              "the readable token"));
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

    verifySegmentedActionControl(application);

    QSplitter horizontalSplitter(Qt::Horizontal);
    horizontalSplitter.addWidget(new QWidget(&horizontalSplitter));
    horizontalSplitter.addWidget(new QWidget(&horizontalSplitter));
    horizontalSplitter.resize(320, 120);
    horizontalSplitter.setSizes({150, 150});
    horizontalSplitter.show();
    application.processEvents();
    check(horizontalSplitter.handleWidth()
                  >= finepaper::ui::UiMetrics::resizerHitExtent,
          QStringLiteral(
              "splitter dividers expose the semantic resize hit target"));
    QSplitterHandle* splitterHandle = horizontalSplitter.handle(1);
    const QImage splitterHandleImage = splitterHandle
        ? splitterHandle->grab().toImage() : QImage{};
    double strongestSplitterLineContrast = 0.0;
    if (!splitterHandleImage.isNull()) {
        const int sampleY = splitterHandleImage.height() / 2;
        const QColor edgeColor = splitterHandleImage.pixelColor(0, sampleY);
        for (int x = 0; x < splitterHandleImage.width(); ++x) {
            strongestSplitterLineContrast = (std::max)(
                strongestSplitterLineContrast,
                finepaper::ui::contrastRatio(
                    splitterHandleImage.pixelColor(x, sampleY), edgeColor));
        }
    }
    check(splitterHandleImage.width()
                  >= finepaper::ui::UiMetrics::resizerHitExtent
              && strongestSplitterLineContrast >= 1.5,
          QStringLiteral(
              "the forgiving splitter hit target paints a visibly distinct centre line "
              "(width=%1, contrast=%2)")
              .arg(splitterHandleImage.width())
              .arg(strongestSplitterLineContrast, 0, 'f', 2));

    QMainWindow dockHost;
    auto* centralWidget = new QWidget(&dockHost);
    dockHost.setCentralWidget(centralWidget);
    auto* dock = new QDockWidget(QStringLiteral("Inspector"), &dockHost);
    dock->setWidget(new QWidget(dock));
    finepaper::ui::installWorkbenchDockTitleBar(dock);
    dockHost.addDockWidget(Qt::RightDockWidgetArea, dock);
    dockHost.resize(600, 320);
    dockHost.show();
    application.processEvents();
    check(dockHost.style()->pixelMetric(
              QStyle::PM_DockWidgetSeparatorExtent, nullptr, &dockHost)
                  >= finepaper::ui::UiMetrics::resizerHitExtent,
          QStringLiteral(
              "Dock separators expose the same forgiving resize hit target"));
    const int separatorLeft = centralWidget->geometry().right() + 1;
    const int separatorRight = dock->geometry().left() - 1;
    const int separatorWidth = separatorRight - separatorLeft + 1;
    const QImage dockHostImage = dockHost.grab().toImage();
    const qreal dockImageScale = dockHostImage.devicePixelRatio();
    const auto logicalPixel = [&dockHostImage, dockImageScale](int x, int y) {
        const int pixelX = std::clamp(
            qRound((static_cast<qreal>(x) + 0.5) * dockImageScale),
            0, dockHostImage.width() - 1);
        const int pixelY = std::clamp(
            qRound((static_cast<qreal>(y) + 0.5) * dockImageScale),
            0, dockHostImage.height() - 1);
        return dockHostImage.pixelColor(pixelX, pixelY);
    };
    double dockSeparatorCenterContrast = 0.0;
    if (separatorWidth > 0 && !dockHostImage.isNull()) {
        const int sampleY = centralWidget->geometry().center().y();
        const QColor separatorEdge = logicalPixel(separatorLeft, sampleY);
        for (int x = separatorLeft; x <= separatorRight; ++x) {
            dockSeparatorCenterContrast = (std::max)(
                dockSeparatorCenterContrast,
                finepaper::ui::contrastRatio(
                    logicalPixel(x, sampleY), separatorEdge));
        }
    }
    check(separatorWidth >= finepaper::ui::UiMetrics::resizerHitExtent
              && dockSeparatorCenterContrast >= 1.5,
          QStringLiteral(
              "the Dock resize strip paints a visibly distinct centre line inside "
              "its forgiving hit target (width=%1, contrast=%2)")
              .arg(separatorWidth)
              .arg(dockSeparatorCenterContrast, 0, 'f', 2));
    auto* floatButton = dock->titleBarWidget()->findChild<QToolButton*>(
        QStringLiteral("finepaper.dockTitleBar.floatButton"));
    auto* closeButton = dock->titleBarWidget()->findChild<QToolButton*>(
        QStringLiteral("finepaper.dockTitleBar.closeButton"));
    auto* dockTitle = dock->titleBarWidget()->findChild<QLabel*>(
        QStringLiteral("finepaper.dockTitleBar.title"));
    const QString fullDockTitle = QStringLiteral(
        "NoC Library and Domain Inspector");
    dock->setWindowTitle(fullDockTitle);
    application.processEvents();

    QLayout* dockTitleLayout = dock->titleBarWidget()->layout();
    const QMargins titleMargins = dockTitleLayout
        ? dockTitleLayout->contentsMargins() : QMargins{};
    const int titleSpacing = dockTitleLayout
        ? (std::max)(0, dockTitleLayout->spacing()) : 0;
    const int fullTitleWidth = dockTitle
        ? dockTitle->fontMetrics().horizontalAdvance(fullDockTitle) : 0;
    const int allLabelsWidth = titleMargins.left()
        + titleMargins.right()
        + fullTitleWidth
        + (floatButton ? floatButton->sizeHint().width() : 0)
        + (closeButton ? closeButton->sizeHint().width() : 0)
        + (2 * titleSpacing);
    const int wideDockWidth = allLabelsWidth
        + (floatButton ? floatButton->sizeHint().width() : titleSpacing);
    dockHost.resize(wideDockWidth + 320, dockHost.height());
    const auto resizeDock = [&application, &dockHost, dock](
                                int requestedWidth) {
        dockHost.resizeDocks(
            {dock}, {requestedWidth}, Qt::Horizontal);
        application.processEvents();
        // Hiding Float reduces the title bar's minimum width. A second pass
        // lets QMainWindow apply the requested dock width with that new hint.
        dockHost.resizeDocks(
            {dock}, {requestedWidth}, Qt::Horizontal);
        application.processEvents();
    };
    resizeDock(wideDockWidth);
    check(floatButton && floatButton->isVisible()
              && dockTitle && dockTitle->text() == fullDockTitle,
          QStringLiteral(
              "a wide dock title bar preserves the full title and Float command "
              "(actual=%1, requested=%2, Float=%3, title='%4')")
              .arg(dock->titleBarWidget()->width())
              .arg(wideDockWidth)
              .arg(floatButton && floatButton->isVisible())
              .arg(dockTitle ? dockTitle->text() : QString{}));

    const int titleAndCloseWidth = titleMargins.left()
        + titleMargins.right()
        + fullTitleWidth
        + (closeButton ? closeButton->sizeHint().width() : 0)
        + titleSpacing;
    resizeDock(titleAndCloseWidth);
    check(floatButton && !floatButton->isVisible()
              && dockTitle && dockTitle->text() == fullDockTitle,
          QStringLiteral(
              "a clearly narrow docked title bar hides secondary Float while "
              "preserving the full title (actual=%1, requested=%2, Float=%3, "
              "title='%4')")
              .arg(dock->titleBarWidget()->width())
              .arg(titleAndCloseWidth)
              .arg(floatButton && floatButton->isVisible())
              .arg(dockTitle ? dockTitle->text() : QString{}));

    const int elidedTitleBudget = dockTitle
        ? dockTitle->fontMetrics().horizontalAdvance(
              QStringLiteral("NoC Library…"))
        : 0;
    const int narrowTitleBarWidth = titleMargins.left()
        + titleMargins.right()
        + (closeButton ? closeButton->sizeHint().width() : 0)
        + titleSpacing
        + elidedTitleBudget;
    resizeDock(narrowTitleBarWidth);
    const int actualTitleBudget = dockTitle
        ? dock->titleBarWidget()->width()
            - titleMargins.left() - titleMargins.right()
            - (closeButton ? closeButton->sizeHint().width() : 0)
            - titleSpacing
        : 0;
    const QString expectedElidedTitle = dockTitle
        ? dockTitle->fontMetrics().elidedText(
              fullDockTitle, Qt::ElideRight, actualTitleBudget)
        : QString{};
    check(dockTitle && dockTitle->text() == expectedElidedTitle
              && dockTitle->text().endsWith(QChar(0x2026))
              && dockTitle->toolTip() == fullDockTitle
              && dockTitle->accessibleName() == fullDockTitle
              && closeButton && closeButton->isVisible(),
          QStringLiteral(
              "a narrow dock title uses a real ellipsis while retaining the full "
              "accessible title and Close command"));

    resizeDock(wideDockWidth);
    check(floatButton && floatButton->text() == QStringLiteral("Float")
              && floatButton->isVisible()
              && floatButton->height()
                  >= finepaper::ui::UiMetrics::controlCompactHeight
              && !floatButton->accessibleName().isEmpty()
              && closeButton
              && closeButton->text() == QStringLiteral("Close")
              && closeButton->height()
                  >= finepaper::ui::UiMetrics::controlCompactHeight,
          QStringLiteral(
              "Dock title controls are readable text targets instead of "
              "ambiguous glyphs"));
    if (floatButton) {
        floatButton->click();
        application.processEvents();
    }
    if (dock->isFloating()) {
        dock->titleBarWidget()->resize(
            narrowTitleBarWidth, dock->titleBarWidget()->height());
    }
    check(dock->isFloating()
              && floatButton
              && floatButton->text() == QStringLiteral("Dock")
              && floatButton->isVisible(),
          QStringLiteral(
              "the Float command becomes a discoverable Dock command even when "
              "the floating title bar is narrow"));
    if (floatButton) {
        floatButton->click();
        application.processEvents();
    }
    if (closeButton) {
        closeButton->click();
        application.processEvents();
    }
    check(!dock->isFloating() && !dock->isVisible(),
          QStringLiteral("text Dock controls preserve native float and close behavior"));

    return failures == 0 ? 0 : 1;
}
