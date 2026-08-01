#include "ui/workbench/navigation/workbench_panel_navigator.h"

#include "ui/common/focus_target.h"
#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTextStream>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <chrono>

namespace {

using finepaper::ui::PanelVisibilityMode;
using finepaper::ui::WorkbenchPanelId;
using finepaper::ui::WorkbenchPanelIntent;
using finepaper::ui::WorkbenchPanelNavigator;
using finepaper::ui::WorkbenchPanelRoute;

int failures = 0;
constexpr auto observableOutcomeTimeout =
    std::chrono::milliseconds{500};

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

template <typename Predicate>
bool waitUntil(Predicate predicate) {
    QDeadlineTimer deadline(observableOutcomeTimeout);
    while (!predicate() && !deadline.hasExpired()) {
        QApplication::processEvents();
        QTest::qWait(5);
    }
    settleEvents();
    return predicate();
}

struct Activation final {
    WorkbenchPanelId id = WorkbenchPanelId::Package;
    WorkbenchPanelIntent intent = WorkbenchPanelIntent::Resume;
};

class NavigationHarness final {
public:
    NavigationHarness() {
        window.setObjectName(QStringLiteral("panelNavigationHarness"));
        window.resize(1100, 720);

        workspace = new QPushButton(
            QStringLiteral("NoC workspace"), &window);
        workspace->setObjectName(QStringLiteral("test.workspace"));
        window.setCentralWidget(workspace);

        package = createDock(
            QStringLiteral("Package Library"),
            QStringLiteral("test.package"),
            Qt::LeftDockWidgetArea,
            &packagePrimary,
            &packageSecondary);
        inspector = createDock(
            QStringLiteral("Inspector"),
            QStringLiteral("test.inspector"),
            Qt::RightDockWidgetArea,
            &inspectorPrimary,
            &inspectorSecondary);
        domain = createDock(
            QStringLiteral("Domain Manager"),
            QStringLiteral("test.domain"),
            Qt::RightDockWidgetArea,
            &domainPrimary,
            &domainSecondary);
        results = createDock(
            QStringLiteral("Diagnostics and Output"),
            QStringLiteral("test.results"),
            Qt::BottomDockWidgetArea,
            &resultsPrimary,
            &resultsSecondary);
        window.tabifyDockWidget(inspector, domain);
        inspector->raise();

        navigator = new WorkbenchPanelNavigator(
            window,
            QList<WorkbenchPanelRoute>{
                {WorkbenchPanelId::Package,
                 package,
                 PanelVisibilityMode::ResponsivePreference,
                 [this](WorkbenchPanelIntent intent) {
                     ++packageResolverCalls;
                     packageResolverIntents.append(intent);
                     packageResolverSawExposed = panelIsExposed(package);
                     return finepaper::ui::firstAvailableFocusTarget(
                         package,
                         {packagePrimary, packageSecondary});
                 }},
                {WorkbenchPanelId::Inspector,
                 inspector,
                 PanelVisibilityMode::ResponsivePreference,
                 [this](WorkbenchPanelIntent) {
                     ++inspectorResolverCalls;
                     inspectorResolverSawExposed =
                         panelIsExposed(inspector);
                     return finepaper::ui::firstAvailableFocusTarget(
                         inspector,
                         {inspectorPrimary, inspectorSecondary});
                 }},
                {WorkbenchPanelId::Domain,
                 domain,
                 PanelVisibilityMode::ResponsivePreference,
                 [this](WorkbenchPanelIntent intent) {
                     ++domainResolverCalls;
                     domainResolverIntents.append(intent);
                     domainResolverSawExposed = panelIsExposed(domain);
                     return finepaper::ui::firstAvailableFocusTarget(
                         domain,
                         {domainPrimary, domainSecondary});
                 }},
                {WorkbenchPanelId::Results,
                 results,
                 PanelVisibilityMode::NativeDock,
                 [this](WorkbenchPanelIntent intent) {
                     ++resultsResolverCalls;
                     resultsResolverIntents.append(intent);
                     resultsResolverSawExposed = panelIsExposed(results);
                     return finepaper::ui::firstAvailableFocusTarget(
                         results,
                         {resultsPrimary, resultsSecondary});
                 }},
            });

        QObject::connect(
            navigator,
            &WorkbenchPanelNavigator::panelActivationRequested,
            &window,
            [this](WorkbenchPanelId id, WorkbenchPanelIntent intent) {
                activations.append({id, intent});
                if (QDockWidget* dock = dockFor(id)) {
                    if (ignoreNextActivation) {
                        ignoreNextActivation = false;
                        return;
                    }
                    if (deferNextActivation) {
                        deferNextActivation = false;
                        const QPointer<QDockWidget> guardedDock = dock;
                        QTimer::singleShot(
                            50, &window, [guardedDock] {
                                if (guardedDock) {
                                    guardedDock->show();
                                    guardedDock->raise();
                                }
                            });
                        return;
                    }
                    dock->show();
                    dock->raise();
                }
            });
        QObject::connect(
            navigator,
            &WorkbenchPanelNavigator::workspaceFocusRequested,
            &window,
            [this] {
                ++workspaceFocusRequests;
                workspace->setFocus(Qt::ShortcutFocusReason);
            });
        QObject::connect(
            navigator,
            &WorkbenchPanelNavigator::panelActivationFailed,
            &window,
            [this](WorkbenchPanelId id, WorkbenchPanelIntent intent) {
                failedActivations.append({id, intent});
            });

        window.show();
        window.activateWindow();
        inspector->raise();
        settleEvents();
        clearObservations();
    }

    void clearObservations() {
        activations.clear();
        failedActivations.clear();
        workspaceFocusRequests = 0;
        packageResolverSawExposed = false;
        inspectorResolverSawExposed = false;
        domainResolverSawExposed = false;
        resultsResolverSawExposed = false;
    }

    QDockWidget* dockFor(WorkbenchPanelId id) const {
        switch (id) {
        case WorkbenchPanelId::Package:
            return package;
        case WorkbenchPanelId::Inspector:
            return inspector;
        case WorkbenchPanelId::Domain:
            return domain;
        case WorkbenchPanelId::Results:
            return results;
        }
        return nullptr;
    }

    QMainWindow window;
    QPushButton* workspace = nullptr;
    QDockWidget* package = nullptr;
    QDockWidget* inspector = nullptr;
    QDockWidget* domain = nullptr;
    QDockWidget* results = nullptr;
    QPushButton* packagePrimary = nullptr;
    QPushButton* packageSecondary = nullptr;
    QPushButton* inspectorPrimary = nullptr;
    QPushButton* inspectorSecondary = nullptr;
    QPushButton* domainPrimary = nullptr;
    QPushButton* domainSecondary = nullptr;
    QPushButton* resultsPrimary = nullptr;
    QPushButton* resultsSecondary = nullptr;
    WorkbenchPanelNavigator* navigator = nullptr;
    QList<Activation> activations;
    QList<Activation> failedActivations;
    QList<WorkbenchPanelIntent> packageResolverIntents;
    QList<WorkbenchPanelIntent> domainResolverIntents;
    QList<WorkbenchPanelIntent> resultsResolverIntents;
    int workspaceFocusRequests = 0;
    int packageResolverCalls = 0;
    int inspectorResolverCalls = 0;
    int domainResolverCalls = 0;
    int resultsResolverCalls = 0;
    bool packageResolverSawExposed = false;
    bool inspectorResolverSawExposed = false;
    bool domainResolverSawExposed = false;
    bool resultsResolverSawExposed = false;
    bool deferNextActivation = false;
    bool ignoreNextActivation = false;

private:
    static bool panelIsExposed(QDockWidget* dock) {
        return dock && dock->isVisible()
            && !dock->visibleRegion().isEmpty();
    }

    QDockWidget* createDock(
        const QString& title,
        const QString& objectName,
        Qt::DockWidgetArea area,
        QPushButton** primary,
        QPushButton** secondary) {
        auto* dock = new QDockWidget(title, &window);
        dock->setObjectName(objectName);
        auto* content = new QWidget(dock);
        auto* layout = new QVBoxLayout(content);
        *primary = new QPushButton(
            QStringLiteral("Primary %1").arg(title), content);
        (*primary)->setObjectName(objectName + QStringLiteral(".primary"));
        *secondary = new QPushButton(
            QStringLiteral("Secondary %1").arg(title), content);
        (*secondary)->setObjectName(
            objectName + QStringLiteral(".secondary"));
        layout->addWidget(*primary);
        layout->addWidget(*secondary);
        dock->setWidget(content);
        window.addDockWidget(area, dock);
        return dock;
    }
};

void verifyCommandPresentation() {
    NavigationHarness harness;
    struct ExpectedCommand final {
        WorkbenchPanelId id;
        QString visibilityText;
        QString navigationText;
        QString visibilityObjectName;
        QString navigationObjectName;
        QKeySequence shortcut;
    };
    const std::array expected = {
        ExpectedCommand{
            WorkbenchPanelId::Package,
            QStringLiteral("NoC IP && Endpoint Library"),
            QStringLiteral("Go to Package Library"),
            finepaper::workbench::packageToggleActionName,
            finepaper::workbench::packageNavigationActionName,
            QKeySequence(QStringLiteral("Ctrl+B"))},
        ExpectedCommand{
            WorkbenchPanelId::Inspector,
            QStringLiteral("Inspector"),
            QStringLiteral("Go to Inspector"),
            finepaper::workbench::inspectorToggleActionName,
            finepaper::workbench::inspectorNavigationActionName,
            QKeySequence(QStringLiteral("Ctrl+Shift+B"))},
        ExpectedCommand{
            WorkbenchPanelId::Domain,
            QStringLiteral("Domain Manager"),
            QStringLiteral("Go to Domain Manager"),
            finepaper::workbench::domainManagerToggleActionName,
            finepaper::workbench::domainNavigationActionName,
            QKeySequence(QStringLiteral("Ctrl+Shift+D"))},
        ExpectedCommand{
            WorkbenchPanelId::Results,
            QStringLiteral("Diagnostics && Output"),
            QStringLiteral("Go to Diagnostics && Output"),
            finepaper::workbench::resultsToggleActionName,
            finepaper::workbench::resultsNavigationActionName,
            QKeySequence(QStringLiteral("Ctrl+J"))},
    };

    QMenu visibilityMenu(&harness.window);
    QMenu navigationMenu(&harness.window);
    harness.navigator->addVisibilityActions(visibilityMenu);
    harness.navigator->addNavigationActions(navigationMenu);
    check(visibilityMenu.actions().size()
              == static_cast<qsizetype>(expected.size())
              && navigationMenu.actions().size()
                  == static_cast<qsizetype>(expected.size()),
          QStringLiteral(
              "the navigator contributes one visibility and one navigation command per route"));

    for (qsizetype index = 0;
         index < static_cast<qsizetype>(expected.size()); ++index) {
        const ExpectedCommand& command = expected.at(
            static_cast<std::size_t>(index));
        QAction* visibility = harness.navigator->visibilityAction(command.id);
        QAction* navigation = harness.navigator->navigationAction(command.id);
        check(visibility && navigation
                  && visibility->text() == command.visibilityText
                  && navigation->text() == command.navigationText
                  && visibility->objectName()
                      == command.visibilityObjectName
                  && navigation->objectName()
                      == command.navigationObjectName,
              QStringLiteral(
                  "route %1 uses its centralized text and automation ids")
                  .arg(index));
        check(visibility && visibility->shortcuts().isEmpty()
                  && navigation
                  && navigation->shortcut() == command.shortcut
                  && navigation->shortcutContext() == Qt::WindowShortcut
                  && harness.window.actions().contains(navigation),
              QStringLiteral(
                  "route %1 keeps preference actions shortcut-free and assigns its key to navigation")
                  .arg(index));
        check(visibilityMenu.actions().value(index) == visibility
                  && navigationMenu.actions().value(index) == navigation,
              QStringLiteral(
                  "route %1 preserves centralized command ordering in menus")
                  .arg(index));
    }
}

void verifyExplicitActivationAndTabifiedDock() {
    NavigationHarness harness;
    harness.package->hide();
    settleEvents();
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.clearObservations();

    harness.navigator->activate(
        WorkbenchPanelId::Package,
        WorkbenchPanelIntent::ReviewDiagnostics);
    settleEvents();
    check(harness.activations.size() == 1
              && harness.activations.front().id
                  == WorkbenchPanelId::Package
              && harness.activations.front().intent
                  == WorkbenchPanelIntent::ReviewDiagnostics,
          QStringLiteral(
              "explicit activation emits the requested panel id and intent exactly once"));
    check(harness.package->isVisible()
              && !harness.package->visibleRegion().isEmpty()
              && harness.packageResolverSawExposed
              && QApplication::focusWidget() == harness.packagePrimary,
          QStringLiteral(
              "focus dispatch waits until the host exposes and raises the requested Dock"));

    harness.inspector->show();
    harness.domain->show();
    harness.inspector->raise();
    settleEvents();
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.clearObservations();
    harness.navigator->activate(
        WorkbenchPanelId::Domain,
        WorkbenchPanelIntent::EditSelection);
    settleEvents();
    check(harness.activations.size() == 1
              && harness.activations.front().id
                  == WorkbenchPanelId::Domain
              && harness.activations.front().intent
                  == WorkbenchPanelIntent::EditSelection
              && harness.domainResolverCalls == 1
              && harness.domainResolverIntents.size() == 1
              && harness.domainResolverIntents.front()
                  == WorkbenchPanelIntent::EditSelection,
          QStringLiteral(
              "a tabified inactive Dock retains the explicit activation intent"));
    check(harness.domain->isVisible()
              && !harness.domain->visibleRegion().isEmpty()
              && harness.domainResolverSawExposed
              && QApplication::focusWidget() == harness.domainPrimary,
          QStringLiteral(
              "activating a tabified inactive Dock raises its tab before resolving focus"));

    harness.clearObservations();
    harness.navigator->activate(WorkbenchPanelId::Inspector);
    settleEvents();
    check(harness.activations.size() == 1
              && harness.inspectorResolverCalls == 1
              && harness.inspectorResolverSawExposed
              && QApplication::focusWidget() == harness.inspectorPrimary,
          QStringLiteral(
              "the Inspector route resolves its target only after the tabified Dock is exposed"));

    harness.clearObservations();
    harness.domain->raise();
    harness.domainPrimary->setFocus(Qt::OtherFocusReason);
    settleEvents();
    check(harness.workspaceFocusRequests == 0
              && QApplication::focusWidget() == harness.domainPrimary,
          QStringLiteral(
              "switching tabified Docks does not steal a valid focus from the newly exposed panel"));
}

void verifyWorkspaceFocusRepair() {
    NavigationHarness harness;
    harness.inspector->show();
    harness.inspector->raise();
    settleEvents();

    for (QWidget* widget : harness.window.findChildren<QWidget*>()) {
        if (widget != harness.inspectorPrimary
            && widget != harness.workspace) {
            widget->setFocusPolicy(Qt::NoFocus);
        }
    }
    harness.inspectorPrimary->setFocusPolicy(Qt::StrongFocus);
    harness.inspectorPrimary->setFocus(Qt::OtherFocusReason);
    settleEvents();
    check(QApplication::focusWidget() == harness.inspectorPrimary,
          QStringLiteral(
              "the Inspector owns focus before responsive-style hiding"));

    harness.clearObservations();
    harness.inspector->hide();
    settleEvents();
    check(harness.workspaceFocusRequests <= 1
              && QApplication::focusWidget() == harness.workspace,
          QStringLiteral(
              "hiding the Dock that owned focus leaves one stable workspace focus target"));
}

void verifyDeferredExposureRetainsActivation() {
    NavigationHarness harness;
    harness.package->hide();
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.deferNextActivation = true;
    harness.clearObservations();

    harness.navigator->activate(
        WorkbenchPanelId::Package,
        WorkbenchPanelIntent::EditSelection);
    settleEvents();
    check(!harness.package->isVisible()
              && harness.packageResolverCalls == 0,
          QStringLiteral(
              "a delayed host exposure does not resolve focus against a hidden Dock"));
    const bool delayedFocusArrived = waitUntil([&harness] {
        return QApplication::focusWidget() == harness.packagePrimary;
    });
    check(delayedFocusArrived && harness.activations.size() == 1
              && harness.package->isVisible()
              && harness.packageResolverSawExposed
              && harness.packageResolverIntents.size() == 1
              && harness.packageResolverIntents.front()
                  == WorkbenchPanelIntent::EditSelection
              && QApplication::focusWidget() == harness.packagePrimary,
          QStringLiteral(
              "bounded focus retries retain an explicit activation until its Dock is exposed"));
}

void verifyActivationFailureIsBoundedAndObservable() {
    NavigationHarness harness;
    harness.package->hide();
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.ignoreNextActivation = true;
    harness.clearObservations();

    harness.navigator->activate(
        WorkbenchPanelId::Package,
        WorkbenchPanelIntent::EditSelection);
    const bool hiddenDockFailureArrived = waitUntil([&harness] {
        return harness.failedActivations.size() == 1;
    });
    check(hiddenDockFailureArrived
              && harness.failedActivations.front().id
                  == WorkbenchPanelId::Package
              && harness.failedActivations.front().intent
                  == WorkbenchPanelIntent::EditSelection
              && harness.workspaceFocusRequests == 0
              && QApplication::focusWidget() == harness.workspace,
          QStringLiteral(
              "a never-exposed Dock produces one bounded, observable activation failure"));

    harness.package->show();
    harness.package->raise();
    harness.packagePrimary->setEnabled(false);
    harness.packageSecondary->setEnabled(false);
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.clearObservations();
    harness.navigator->activate(WorkbenchPanelId::Package);
    const bool missingTargetFailureArrived = waitUntil([&harness] {
        return harness.failedActivations.size() == 1;
    });
    check(missingTargetFailureArrived
              && harness.packageResolverCalls > 0
              && harness.workspaceFocusRequests == 0
              && QApplication::focusWidget() == harness.workspace,
          QStringLiteral(
              "a visible Dock without a usable target reports failure and preserves valid workspace focus"));

    harness.package->hide();
    harness.workspace->clearFocus();
    settleEvents();
    harness.ignoreNextActivation = true;
    harness.clearObservations();
    harness.navigator->activate(WorkbenchPanelId::Package);
    const bool fallbackFailureArrived = waitUntil([&harness] {
        return harness.failedActivations.size() == 1;
    });
    check(fallbackFailureArrived
              && harness.workspaceFocusRequests == 1
              && QApplication::focusWidget() == harness.workspace,
          QStringLiteral(
              "activation exhaustion requests workspace focus exactly once when no valid focus remains"));
}

void verifyResolverIsEvaluatedForEveryActivation() {
    NavigationHarness harness;
    harness.package->show();
    harness.package->raise();
    settleEvents();
    const int callsBefore = harness.packageResolverCalls;

    harness.navigator->activate(
        WorkbenchPanelId::Package,
        WorkbenchPanelIntent::Resume);
    settleEvents();
    check(harness.packageResolverCalls == callsBefore + 1
              && QApplication::focusWidget() == harness.packagePrimary,
          QStringLiteral(
              "the first activation resolves the current primary Package control"));

    harness.packagePrimary->setEnabled(false);
    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.navigator->activate(
        WorkbenchPanelId::Package,
        WorkbenchPanelIntent::EditSelection);
    settleEvents();
    check(harness.packageResolverCalls == callsBefore + 2
              && harness.packageResolverIntents.size() >= 2
              && harness.packageResolverIntents.back()
                  == WorkbenchPanelIntent::EditSelection
              && QApplication::focusWidget() == harness.packageSecondary,
          QStringLiteral(
              "each activation re-evaluates the resolver instead of caching a rebuilt child"));
}

void verifyNativeResultsAction() {
    NavigationHarness harness;
    QAction* visibility = harness.navigator->visibilityAction(
        WorkbenchPanelId::Results);
    QAction* navigation = harness.navigator->navigationAction(
        WorkbenchPanelId::Results);
    check(visibility == harness.results->toggleViewAction()
              && visibility->shortcuts().isEmpty()
              && navigation
              && navigation->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+J")),
          QStringLiteral(
              "Results reuses the native Dock visibility action while navigation owns Ctrl+J"));

    harness.results->show();
    settleEvents();
    if (visibility->isChecked()) {
        visibility->trigger();
        settleEvents();
    }
    check(!harness.results->isVisible(),
          QStringLiteral(
              "the native Results visibility action still hides its Dock"));

    harness.workspace->setFocus(Qt::OtherFocusReason);
    harness.clearObservations();
    visibility->trigger();
    settleEvents();
    check(harness.results->isVisible()
              && harness.activations.isEmpty()
              && harness.resultsResolverCalls == 0
              && QApplication::focusWidget() == harness.workspace,
          QStringLiteral(
              "showing Results through its visibility preference does not navigate or steal focus"));

    harness.navigator->activate(WorkbenchPanelId::Results);
    settleEvents();
    check(harness.activations.size() == 1
              && harness.resultsResolverSawExposed
              && harness.resultsResolverIntents.size() == 1
              && harness.resultsResolverIntents.front()
                  == WorkbenchPanelIntent::Resume
              && QApplication::focusWidget() == harness.resultsPrimary,
          QStringLiteral(
              "the dedicated Results navigation command raises and focuses active content"));
}

void verifyDestroyedDockDisablesCommands() {
    NavigationHarness harness;
    QPointer<QAction> packageVisibility =
        harness.navigator->visibilityAction(WorkbenchPanelId::Package);
    QPointer<QAction> packageNavigation =
        harness.navigator->navigationAction(WorkbenchPanelId::Package);
    delete harness.package;
    harness.package = nullptr;
    settleEvents();
    check(packageVisibility && !packageVisibility->isEnabled()
              && packageNavigation && !packageNavigation->isEnabled(),
          QStringLiteral(
              "destroying a responsive Dock disables both surviving commands"));

    harness.clearObservations();
    harness.navigator->activate(WorkbenchPanelId::Package);
    settleEvents();
    check(harness.activations.isEmpty(),
          QStringLiteral(
              "destroyed Dock routes no longer emit activation requests"));

    QPointer<QAction> resultsVisibility =
        harness.navigator->visibilityAction(WorkbenchPanelId::Results);
    QPointer<QAction> resultsNavigation =
        harness.navigator->navigationAction(WorkbenchPanelId::Results);
    delete harness.results;
    harness.results = nullptr;
    settleEvents();
    check((!resultsVisibility || !resultsVisibility->isEnabled())
              && resultsNavigation && !resultsNavigation->isEnabled(),
          QStringLiteral(
              "destroying a native Dock removes or disables visibility and disables navigation"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("finepaper-workbench-panel-navigation-tests"));

    verifyCommandPresentation();
    verifyExplicitActivationAndTabifiedDock();
    verifyWorkspaceFocusRepair();
    verifyDeferredExposureRetainsActivation();
    verifyActivationFailureIsBoundedAndObservable();
    verifyResolverIsEvaluatedForEveryActivation();
    verifyNativeResultsAction();
    verifyDestroyedDockDisablesCommands();
    return failures == 0 ? 0 : 1;
}
