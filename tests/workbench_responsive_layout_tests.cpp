#include "ui/layouts/responsive_action_layout.h"
#include "ui/workbench/layout/workbench_layout_controller.h"
#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTextStream>
#include <QTest>
#include <QWidget>
#include <QtGlobal>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

void settleEvents() {
    for (int pass = 0; pass < 4; ++pass) {
        QApplication::processEvents();
        QTest::qWait(1);
    }
}

class WorkbenchHarness final {
public:
    explicit WorkbenchHarness(const QVariantMap& restoredPreferences = {}) {
        window.setObjectName(QStringLiteral("responsiveHarness"));
        window.resize(2200, 800);
        auto* centerTabs = new QTabWidget(&window);
        center = centerTabs;
        center->setObjectName(QStringLiteral("centerWorkspace"));
        for (const QString& title : {
                 QStringLiteral("NoC Editor"),
                 QStringLiteral("Domain Configuration"),
                 QStringLiteral("Design Extensions"),
                 QStringLiteral("Performance Analysis"),
                 QStringLiteral("Problem Report")}) {
            centerTabs->addTab(new QWidget(centerTabs), title);
        }
        centerTabs->setElideMode(Qt::ElideNone);
        window.setCentralWidget(center);

        package = createDock(
            QStringLiteral("Package Library"), Qt::LeftDockWidgetArea);
        inspector = createDock(
            QStringLiteral("Inspector"), Qt::RightDockWidgetArea);
        domain = createDock(
            QStringLiteral("Domain Manager"), Qt::RightDockWidgetArea);
        results = createDock(
            QStringLiteral("Diagnostics and Output"),
            Qt::BottomDockWidgetArea);
        window.tabifyDockWidget(inspector, domain);
        inspector->raise();
        window.resizeDocks(
            {package, inspector}, {300, 360}, Qt::Horizontal);
        window.resizeDocks({results}, {180}, Qt::Vertical);

        packageAction = createPanelAction(QStringLiteral("Package"));
        inspectorAction = createPanelAction(QStringLiteral("Inspector"));
        domainAction = createPanelAction(QStringLiteral("Domains"));

        controller = new finepaper::ui::WorkbenchLayoutController(
            &window,
            center,
            QList<finepaper::ui::WorkbenchPanelBinding>{
                {finepaper::ui::WorkbenchPanelRole::Package,
                 package, packageAction},
                {finepaper::ui::WorkbenchPanelRole::Inspector,
                 inspector, inspectorAction},
                {finepaper::ui::WorkbenchPanelRole::Domain,
                 domain, domainAction},
            },
            QList<QDockWidget*>{package, inspector, domain, results},
            [this](finepaper::ui::WorkbenchWidthMode mode) {
                presentation = mode;
            });
        if (restoredPreferences.isEmpty()) {
            controller->captureUserPanelVisibilityFromLayout();
        } else {
            controller->restoreUserPanelVisibility(restoredPreferences);
        }
        controller->start();
        window.show();
        settleEvents();
        controller->reevaluateNow();
        settleEvents();
    }

    int allPanelsWidth() const {
        return window.property(
            finepaper::workbench::workbenchAllPanelsWidthProperty).toInt();
    }

    int onePanelWidth() const {
        return window.property(
            finepaper::workbench::workbenchOnePanelWidthProperty).toInt();
    }

    void resizeWidth(int width) {
        window.resize(width, window.height());
        settleEvents();
        controller->reevaluateNow();
        settleEvents();
    }

    QMainWindow window;
    QWidget* center = nullptr;
    QDockWidget* package = nullptr;
    QDockWidget* inspector = nullptr;
    QDockWidget* domain = nullptr;
    QDockWidget* results = nullptr;
    QAction* packageAction = nullptr;
    QAction* inspectorAction = nullptr;
    QAction* domainAction = nullptr;
    finepaper::ui::WorkbenchWidthMode presentation =
        finepaper::ui::WorkbenchWidthMode::Wide;
    finepaper::ui::WorkbenchLayoutController* controller = nullptr;

private:
    QDockWidget* createDock(const QString& title, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, &window);
        dock->setObjectName(title.toLower().replace(QLatin1Char(' '),
                                                   QLatin1Char('-')));
        auto* content = new QLabel(
            QStringLiteral("Readable %1 content").arg(title), dock);
        content->setWordWrap(true);
        dock->setWidget(content);
        window.addDockWidget(area, dock);
        dock->show();
        return dock;
    }

    QAction* createPanelAction(const QString& text) {
        auto* action = new QAction(text, &window);
        action->setCheckable(true);
        action->setChecked(true);
        return action;
    }
};

void verifyPureWidthPolicy() {
    using finepaper::ui::WorkbenchWidthMode;
    using finepaper::ui::WorkbenchWidthRequirements;
    using finepaper::ui::classifyWorkbenchWidth;

    const WorkbenchWidthRequirements wideEdge{1000, 1000, 700, 40};
    check(classifyWorkbenchWidth(wideEdge, WorkbenchWidthMode::Wide)
              == WorkbenchWidthMode::Wide,
          QStringLiteral("the exact measured wide requirement remains Wide"));
    check(classifyWorkbenchWidth(
              {999, 1000, 700, 40}, WorkbenchWidthMode::Wide)
              == WorkbenchWidthMode::Compact,
          QStringLiteral("shrinking below the measured full layout enters Compact"));
    check(classifyWorkbenchWidth(
              {699, 1000, 700, 40}, WorkbenchWidthMode::Compact)
              == WorkbenchWidthMode::CanvasOnly,
          QStringLiteral("shrinking below one readable side panel protects the canvas"));
    check(classifyWorkbenchWidth(
              {720, 1000, 700, 40}, WorkbenchWidthMode::CanvasOnly)
              == WorkbenchWidthMode::CanvasOnly,
          QStringLiteral("Canvas-only mode holds inside its font-derived hysteresis"));
    check(classifyWorkbenchWidth(
              {740, 1000, 700, 40}, WorkbenchWidthMode::CanvasOnly)
              == WorkbenchWidthMode::Compact,
          QStringLiteral("Canvas-only mode exits at the measured threshold plus hysteresis"));
    check(classifyWorkbenchWidth(
              {1020, 1000, 700, 40}, WorkbenchWidthMode::Compact)
              == WorkbenchWidthMode::Compact,
          QStringLiteral("Compact mode does not flicker near the full-layout threshold"));
    check(classifyWorkbenchWidth(
              {1040, 1000, 700, 40}, WorkbenchWidthMode::Compact)
              == WorkbenchWidthMode::Wide,
          QStringLiteral("Compact mode exits after the full-layout hysteresis"));
}

void verifyControllerIntentAndFocus() {
    using finepaper::ui::WorkbenchPanelRole;
    using finepaper::ui::WorkbenchWidthMode;

    WorkbenchHarness harness;
    const int fullWidth = harness.allPanelsWidth();
    const int oneWidth = harness.onePanelWidth();
    check(fullWidth > oneWidth && oneWidth > 0,
          QStringLiteral("controller requirements are measured from distinct Dock stacks"));

    harness.resizeWidth(fullWidth + 160);
    check(harness.controller->widthMode() == WorkbenchWidthMode::Wide,
          QStringLiteral("a measured spacious window presents the Wide workbench"));

    harness.controller->revealPanel(WorkbenchPanelRole::Package);
    settleEvents();
    harness.resizeWidth(fullWidth - 1);
    check(harness.controller->widthMode() == WorkbenchWidthMode::Compact
              && harness.controller->panelAutoSuppressed(
                  WorkbenchPanelRole::Package)
              && harness.controller->userPanelVisible(
                  WorkbenchPanelRole::Package)
              && harness.packageAction->isChecked()
              && harness.packageAction->text().contains(
                  QStringLiteral("hidden to fit")),
          QStringLiteral(
              "Compact labels an automatically suppressed preference without "
              "overwriting user intent"));

    const QVariantMap compactPreferences =
        harness.controller->persistentUserPanelVisibility();
    check(compactPreferences.value(QStringLiteral("panels")).toMap()
              .value(harness.package->objectName()).toBool(),
          QStringLiteral("automatic suppression is not serialized as a user hide"));

    harness.resizeWidth(fullWidth + 160);
    check(harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.controller->userPanelVisible(
                  WorkbenchPanelRole::Package)
              && harness.packageAction->text()
                  == QStringLiteral("Package"),
          QStringLiteral(
              "returning to Wide restores the panel and its unsuppressed label"));

    harness.controller->setCompactPreferredPanel(
        WorkbenchPanelRole::Package);
    harness.resizeWidth(fullWidth - 1);
    check(harness.package->isVisible()
              && !harness.inspector->isVisible()
              && harness.controller->userPanelVisible(
                  WorkbenchPanelRole::Inspector),
          QStringLiteral("Compact content priority can keep Package available before a design exists"));
    harness.resizeWidth(fullWidth + 160);
    harness.controller->setCompactPreferredPanel(
        WorkbenchPanelRole::Inspector);

    harness.controller->setUserPanelVisible(
        WorkbenchPanelRole::Package, false);
    harness.resizeWidth(fullWidth - 1);
    harness.resizeWidth(fullWidth + 160);
    check(!harness.package->isVisible()
              && !harness.packageAction->isChecked()
              && !harness.controller->userPanelVisible(
                  WorkbenchPanelRole::Package),
          QStringLiteral("a user-hidden Package panel stays hidden across width changes"));

    harness.controller->setUserPanelVisible(
        WorkbenchPanelRole::Package, true);
    harness.controller->reevaluateNow();
    settleEvents();
    const int currentOneWidth = harness.onePanelWidth();
    harness.resizeWidth((std::max)(320, currentOneWidth - 1));
    check(harness.controller->widthMode() == WorkbenchWidthMode::CanvasOnly
              && !harness.package->isVisible()
              && !harness.inspector->isVisible(),
          QStringLiteral(
              "Canvas-only mode automatically suppresses both side stacks "
              "(window=%1, one=%2, mode=%3, package=%4, inspector=%5)")
              .arg(harness.window.width())
              .arg(currentOneWidth)
              .arg(static_cast<int>(harness.controller->widthMode()))
              .arg(harness.package->isVisible())
              .arg(harness.inspector->isVisible()));

    harness.controller->revealPanel(WorkbenchPanelRole::Package);
    settleEvents();
    check(harness.package->isVisible()
              && !harness.inspector->isVisible()
              && harness.controller->userPanelVisible(
                  WorkbenchPanelRole::Inspector),
          QStringLiteral("Compact panel navigation reveals one side without erasing the other preference"));

    harness.resizeWidth(fullWidth + 160);
    harness.results->show();
    settleEvents();
    check(harness.controller->enterCanvasFocus(),
          QStringLiteral("Canvas Focus captures the effective responsive layout"));
    harness.resizeWidth(fullWidth + 240);
    check(!harness.package->isVisible()
              && !harness.inspector->isVisible()
              && !harness.domain->isVisible()
              && !harness.results->isVisible(),
          QStringLiteral("responsive resize does not leak panels into Canvas Focus"));
    check(harness.controller->leaveCanvasFocus(),
          QStringLiteral("Canvas Focus restores its physical layout snapshot"));
    settleEvents();
    check(harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.results->isVisible(),
          QStringLiteral("leaving Canvas Focus reapplies current Wide user intent"));

    const QByteArray preTaskLayout = harness.window.saveState();
    check(harness.controller->enterPanelTaskFocus(
              WorkbenchPanelRole::Domain),
          QStringLiteral("a panel task captures the current workbench layout"));
    settleEvents();
    check(harness.controller->panelTaskFocusActive()
              && harness.controller->panelTaskFocusRole()
                  == WorkbenchPanelRole::Domain
              && harness.domain->isVisible()
              && harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.results->isVisible()
              && harness.controller->persistentWindowState()
                  == preTaskLayout,
          QStringLiteral(
              "a Wide panel task preserves the readable workbench while owning a transient layout snapshot"));
    harness.resizeWidth((std::max)(320, currentOneWidth - 1));
    settleEvents();
    check(harness.controller->panelTaskFocusActive()
              && harness.domain->isVisible()
              && !harness.package->isVisible()
              && !harness.inspector->isVisible()
              && !harness.results->isVisible(),
          QStringLiteral(
              "resizing an active Wide task into Canvas-only keeps the Domain task visible and isolated"));
    harness.resizeWidth(fullWidth + 160);
    settleEvents();
    check(harness.controller->panelTaskFocusActive()
              && harness.domain->isVisible()
              && harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.results->isVisible(),
          QStringLiteral(
              "widening an active task restores its captured multi-panel workbench without ending the task"));
    check(harness.controller->leavePanelTaskFocus(),
          QStringLiteral("finishing a panel task restores its layout snapshot"));
    settleEvents();
    check(!harness.controller->panelTaskFocusActive()
              && harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.results->isVisible(),
          QStringLiteral(
              "panel task completion restores every previously visible panel"));

    check(harness.controller->enterPanelTaskFocus(
              WorkbenchPanelRole::Domain),
          QStringLiteral(
              "a focused panel task can observe its Dock close command"));
    harness.domain->close();
    settleEvents();
    check(harness.controller->panelTaskFocusActive(),
          QStringLiteral(
              "closing the focused Dock retains its captured layout until the task owner finishes"));
    check(harness.controller->leavePanelTaskFocus(),
          QStringLiteral(
              "the task owner restores the captured layout after its focused Dock closes"));
    settleEvents();
    check(!harness.controller->panelTaskFocusActive()
              && harness.package->isVisible()
              && harness.inspector->isVisible()
              && harness.results->isVisible(),
          QStringLiteral(
              "focused Dock close restores the other panels without stale task focus"));
    harness.controller->setUserPanelVisible(
        WorkbenchPanelRole::Domain, true);
    settleEvents();

    harness.results->hide();
    settleEvents();
    check(harness.controller->enterPanelTaskFocus(
              WorkbenchPanelRole::Domain),
          QStringLiteral("a second panel task can acquire layout ownership"));
    harness.results->show();
    settleEvents();
    check(!harness.controller->panelTaskFocusActive()
              && harness.results->isVisible(),
          QStringLiteral(
              "manually reopening another panel releases automatic restore ownership"));
    check(harness.controller->leavePanelTaskFocus()
              && harness.results->isVisible(),
          QStringLiteral(
              "finishing after a manual layout change does not overwrite user intent"));

    harness.resizeWidth((std::max)(320, currentOneWidth - 1));
    harness.controller->revealPanel(WorkbenchPanelRole::Inspector);
    harness.results->show();
    settleEvents();
    check(harness.controller->widthMode() == WorkbenchWidthMode::CanvasOnly
              && harness.inspector->isVisible()
              && harness.results->isVisible()
              && harness.controller->enterPanelTaskFocus(
                  WorkbenchPanelRole::Domain),
          QStringLiteral(
              "a Canvas-only explicit Inspector route can hand layout ownership to a Domain task"));
    harness.controller->revealPanel(WorkbenchPanelRole::Inspector);
    settleEvents();
    check(!harness.controller->panelTaskFocusActive()
              && harness.inspector->isVisible(),
          QStringLiteral(
              "manual Canvas-only navigation keeps the newly revealed panel after releasing task ownership"));
    harness.results->show();
    settleEvents();
    check(harness.controller->enterPanelTaskFocus(
              WorkbenchPanelRole::Domain),
          QStringLiteral(
              "a restored Canvas-only Inspector route can start another Domain task"));
    check(harness.controller->leavePanelTaskFocus(),
          QStringLiteral(
              "the Canvas-only Domain task restores its captured explicit route"));
    settleEvents();
    check(harness.inspector->isVisible()
              && harness.domain->visibleRegion().isEmpty()
              && harness.results->isVisible(),
          QStringLiteral(
              "Canvas-only task completion restores the previous Inspector reveal and Results"));
    harness.resizeWidth(fullWidth + 160);

    harness.package->close();
    settleEvents();
    check(!harness.controller->userPanelVisible(
              WorkbenchPanelRole::Package)
              && !harness.packageAction->isChecked(),
          QStringLiteral("the Dock title-bar close command updates persistent user intent"));
    harness.package->toggleViewAction()->trigger();
    settleEvents();
    check(harness.controller->userPanelVisible(
              WorkbenchPanelRole::Package)
              && harness.packageAction->isChecked()
              && harness.package->isVisible(),
          QStringLiteral("the native Dock context-menu toggle updates the same user intent"));

    WorkbenchHarness restoredHarness(compactPreferences);
    restoredHarness.resizeWidth(restoredHarness.allPanelsWidth() + 160);
    check(restoredHarness.controller->userPanelVisible(
              WorkbenchPanelRole::Package)
              && restoredHarness.package->isVisible(),
          QStringLiteral("a Compact-session restart restores logical Package intent in a Wide window"));
}

void verifyLargeTextUsesCompactWorkspaceNavigation() {
    using finepaper::ui::WorkbenchWidthMode;

    WorkbenchHarness harness;
    QFont largeFont = harness.window.font();
    if (largeFont.pointSizeF() > 0.0) {
        largeFont.setPointSizeF(largeFont.pointSizeF() * 2.0);
    } else if (largeFont.pixelSize() > 0) {
        largeFont.setPixelSize(qRound(largeFont.pixelSize() * 2.0));
    }
    harness.window.setFont(largeFont);
    settleEvents();
    harness.resizeWidth(1280);

    check(harness.controller->widthMode() == WorkbenchWidthMode::Compact,
          QStringLiteral(
              "large workspace labels request the compact selector before tabs elide"));
}

void verifyRuntimeFontMeasurement() {
    using finepaper::ui::WorkbenchWidthMode;

    WorkbenchHarness harness;
    const QFont normalFont = harness.window.font();
    const int normalRequirement = harness.allPanelsWidth();
    harness.resizeWidth(normalRequirement + 24);
    check(harness.controller->widthMode() == WorkbenchWidthMode::Wide,
          QStringLiteral("the normal-font measured edge starts in Wide mode"));

    QFont enlarged = harness.window.font();
    if (enlarged.pointSizeF() > 0.0) {
        enlarged.setPointSizeF(enlarged.pointSizeF() * 1.5);
    } else if (enlarged.pixelSize() > 0) {
        enlarged.setPixelSize(qRound(enlarged.pixelSize() * 1.5));
    }
    harness.window.setFont(enlarged);
    settleEvents();
    harness.controller->reevaluateNow();
    settleEvents();
    const int enlargedRequirement = harness.allPanelsWidth();
    check(enlargedRequirement > normalRequirement
              && harness.controller->widthMode()
                  != WorkbenchWidthMode::Wide,
          QStringLiteral(
              "runtime Large fonts increase measured requirements and enter responsive mode"));

    harness.window.setFont(normalFont);
    settleEvents();
    harness.controller->reevaluateNow();
    settleEvents();
    const int restoredRequirement = harness.allPanelsWidth();
    harness.resizeWidth(
        restoredRequirement
        + 4 * harness.window.fontMetrics().averageCharWidth()
        + 16);
    check(qAbs(restoredRequirement - normalRequirement) <= 2
              && harness.controller->widthMode()
                  == WorkbenchWidthMode::Wide,
          QStringLiteral(
              "restoring the normal font releases enlarged metric bounds and "
              "returns to Wide"));
}

void verifyResponsiveActionLayout() {
    QWidget host;
    auto* layout = new finepaper::ui::ResponsiveActionLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* label = new QLabel(QStringLiteral("Output root"), &host);
    auto* path = new QLineEdit(QStringLiteral("/tmp/generated-output"), &host);
    path->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* browse = new QPushButton(QStringLiteral("Browse…"), &host);
    auto* generate = new QPushButton(QStringLiteral("Generate RTL"), &host);
    layout->addWidget(label);
    layout->addWidget(path);
    layout->addWidget(browse);
    layout->addWidget(generate);

    const int wideWidth = layout->sizeHint().width() + 180;
    host.resize(wideWidth, layout->heightForWidth(wideWidth));
    host.show();
    settleEvents();
    check(label->geometry().right() < path->geometry().left()
              && path->geometry().right() < browse->geometry().left()
              && browse->geometry().right() < generate->geometry().left()
              && path->width() > path->sizeHint().width(),
          QStringLiteral("wide Results controls share one row and the path consumes spare width"));

    const int narrowWidth = (std::max)(180, generate->sizeHint().width());
    const int narrowHeight = layout->heightForWidth(narrowWidth);
    host.resize(narrowWidth, narrowHeight);
    settleEvents();
    check(label->geometry().bottom() < path->geometry().top()
              && path->geometry().bottom() < browse->geometry().top()
              && browse->geometry().bottom() < generate->geometry().top()
              && generate->geometry().bottom() <= host.rect().bottom(),
          QStringLiteral("narrow Results controls stack completely before text clips"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("finepaper-workbench-responsive-tests"));
    QSettings().clear();

    verifyPureWidthPolicy();
    verifyControllerIntentAndFocus();
    verifyLargeTextUsesCompactWorkspaceNavigation();
    verifyRuntimeFontMeasurement();
    verifyResponsiveActionLayout();
    return failures == 0 ? 0 : 1;
}
