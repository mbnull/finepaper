#include "ui/workbench/navigation/workbench_panel_navigator.h"

#include "ui/common/focus_target.h"
#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QWidget>

#include <chrono>
#include <utility>

namespace finepaper::ui {
namespace {

struct PanelCommandPresentation final {
    QString visibilityText;
    QString navigationText;
    QString visibilityObjectName;
    QString navigationObjectName;
    QString statusTip;
    QKeySequence shortcut;
};

constexpr int focusDispatchRetryCount = 8;
constexpr auto focusDispatchRetryInterval =
    std::chrono::milliseconds{25};

PanelCommandPresentation presentationFor(WorkbenchPanelId id) {
    switch (id) {
    case WorkbenchPanelId::Package:
        return {
            WorkbenchPanelNavigator::tr("NoC IP && Endpoint Library"),
            WorkbenchPanelNavigator::tr("Go to Package Library"),
            workbench::packageToggleActionName,
            workbench::packageNavigationActionName,
            WorkbenchPanelNavigator::tr(
                "Open the NoC IP and Endpoint library and continue from its primary control"),
            QKeySequence(QStringLiteral("Ctrl+B")),
        };
    case WorkbenchPanelId::Inspector:
        return {
            WorkbenchPanelNavigator::tr("Inspector"),
            WorkbenchPanelNavigator::tr("Go to Inspector"),
            workbench::inspectorToggleActionName,
            workbench::inspectorNavigationActionName,
            WorkbenchPanelNavigator::tr(
                "Open the Inspector and continue editing the current context"),
            QKeySequence(QStringLiteral("Ctrl+Shift+B")),
        };
    case WorkbenchPanelId::Domain:
        return {
            WorkbenchPanelNavigator::tr("Domain Manager"),
            WorkbenchPanelNavigator::tr("Go to Domain Manager"),
            workbench::domainManagerToggleActionName,
            workbench::domainNavigationActionName,
            WorkbenchPanelNavigator::tr(
                "Open the Package-driven Domain Manager and continue from its current task"),
            QKeySequence(QStringLiteral("Ctrl+Shift+D")),
        };
    case WorkbenchPanelId::Results:
        return {
            WorkbenchPanelNavigator::tr("Diagnostics && Output"),
            WorkbenchPanelNavigator::tr("Go to Diagnostics && Output"),
            workbench::resultsToggleActionName,
            workbench::resultsNavigationActionName,
            WorkbenchPanelNavigator::tr(
                "Open diagnostics and generated output at the active result"),
            QKeySequence(QStringLiteral("Ctrl+J")),
        };
    }
    return {};
}

} // namespace

WorkbenchPanelNavigator::WorkbenchPanelNavigator(
    QMainWindow& window,
    QList<WorkbenchPanelRoute> routes)
    : QObject(&window), m_window(window) {
    m_lastNonNullFocus = QApplication::focusWidget();
    connect(qApp, &QApplication::focusChanged,
            this, [this](QWidget*, QWidget* focus) {
                if (focus) {
                    m_lastNonNullFocus = focus;
                }
            });
    for (WorkbenchPanelRoute& route : routes) {
        const std::size_t routeIndex =
            static_cast<std::size_t>(route.id);
        if (!route.dock || routeIndex >= m_routes.size()
            || this->route(route.id)) {
            continue;
        }

        const PanelCommandPresentation presentation =
            presentationFor(route.id);
        QAction* visibilityAction = nullptr;
        if (route.visibilityMode == PanelVisibilityMode::NativeDock) {
            visibilityAction = route.dock->toggleViewAction();
        } else {
            visibilityAction = new QAction(this);
            visibilityAction->setCheckable(true);
            visibilityAction->setChecked(true);
        }
        visibilityAction->setObjectName(
            presentation.visibilityObjectName);
        visibilityAction->setText(presentation.visibilityText);
        visibilityAction->setShortcuts({});
        visibilityAction->setStatusTip(
            route.visibilityMode == PanelVisibilityMode::NativeDock
                ? tr("Show or hide this panel.")
                : tr("Keep this panel available whenever the layout has room."));

        auto* navigationAction = new QAction(
            presentation.navigationText, this);
        navigationAction->setObjectName(
            presentation.navigationObjectName);
        navigationAction->setShortcut(presentation.shortcut);
        navigationAction->setShortcutContext(Qt::WindowShortcut);
        navigationAction->setStatusTip(presentation.statusTip);
        navigationAction->setWhatsThis(tr(
            "This is a navigation command. It reveals and focuses the panel "
            "without changing another panel's saved responsive preference."));
        m_window.addAction(navigationAction);

        RouteState state;
        state.dock = route.dock;
        state.visibilityAction = visibilityAction;
        state.navigationAction = navigationAction;
        state.focusTarget = std::move(route.focusTarget);
        m_routes[routeIndex] = std::move(state);

        connect(navigationAction, &QAction::triggered,
                this, [this, id = route.id] {
                    activate(id);
                });
        connect(visibilityAction, &QAction::toggled,
                this, [this, id = route.id](bool visible) {
                    if (!visible) {
                        if (RouteState* state = this->route(id)) {
                            requestFocusRepair(state->dock);
                        }
                    }
                });
        connect(route.dock, &QDockWidget::visibilityChanged,
                this, [this, id = route.id](bool visible) {
                    RouteState* state = this->route(id);
                    if (!state) {
                        return;
                    }
                    if (visible && m_pendingActivation
                        && m_pendingActivation->id == id) {
                        scheduleFocusDispatch();
                    } else if (!visible) {
                        requestFocusRepair(state->dock);
                    }
                });
        connect(route.dock, &QObject::destroyed,
                this, [this, id = route.id] {
                    RouteState* destroyedRoute = this->route(id);
                    if (!destroyedRoute) {
                        return;
                    }
                    if (destroyedRoute->visibilityAction) {
                        destroyedRoute->visibilityAction->setEnabled(false);
                    }
                    if (destroyedRoute->navigationAction) {
                        destroyedRoute->navigationAction->setEnabled(false);
                    }
                    if (m_pendingActivation
                        && m_pendingActivation->id == id) {
                        m_pendingActivation.reset();
                    }
                });
    }

    m_focusTimer.setSingleShot(true);
    m_focusTimer.setInterval(std::chrono::milliseconds::zero());
    connect(&m_focusTimer, &QTimer::timeout,
            this, &WorkbenchPanelNavigator::dispatchFocus);
}

QAction* WorkbenchPanelNavigator::visibilityAction(
    WorkbenchPanelId id) {
    RouteState* state = route(id);
    return state ? state->visibilityAction.data() : nullptr;
}

QAction* WorkbenchPanelNavigator::navigationAction(
    WorkbenchPanelId id) {
    RouteState* state = route(id);
    return state ? state->navigationAction.data() : nullptr;
}

QDockWidget* WorkbenchPanelNavigator::dock(WorkbenchPanelId id) {
    RouteState* state = route(id);
    return state ? state->dock.data() : nullptr;
}

void WorkbenchPanelNavigator::addVisibilityActions(QMenu& menu) const {
    for (const std::optional<RouteState>& state : m_routes) {
        if (state && state->visibilityAction) {
            menu.addAction(state->visibilityAction);
        }
    }
}

void WorkbenchPanelNavigator::addNavigationActions(QMenu& menu) const {
    for (const std::optional<RouteState>& state : m_routes) {
        if (state && state->navigationAction) {
            menu.addAction(state->navigationAction);
        }
    }
}

void WorkbenchPanelNavigator::activate(
    WorkbenchPanelId id,
    WorkbenchPanelIntent intent) {
    RouteState* state = route(id);
    if (!state || !state->dock) {
        return;
    }
    m_pendingActivation = PendingActivation{
        id, intent, focusDispatchRetryCount};
    emit panelActivationRequested(id, intent);
    scheduleFocusDispatch();
}

WorkbenchPanelNavigator::RouteState* WorkbenchPanelNavigator::route(
    WorkbenchPanelId id) {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= m_routes.size() || !m_routes[index]) {
        return nullptr;
    }
    return &*m_routes[index];
}

bool WorkbenchPanelNavigator::focusIsUsable(QWidget* focus) const {
    return focus && focus->isEnabled()
        && focus->focusPolicy() != Qt::NoFocus && focus->isVisible();
}

bool WorkbenchPanelNavigator::focusReachedTarget(QWidget* target) const {
    QWidget* focus = QApplication::focusWidget();
    return target && focusIsUsable(focus)
        && (focus == target || target->isAncestorOf(focus));
}

bool WorkbenchPanelNavigator::panelIsExposed(
    const QDockWidget* dock) const {
    return dock && dock->isVisible()
        && !dock->visibleRegion().isEmpty();
}

void WorkbenchPanelNavigator::requestFocusRepair(QDockWidget* dock) {
    QWidget* focus = QApplication::focusWidget();
    QWidget* candidate = focus ? focus : m_lastNonNullFocus.data();
    if (!dock || !candidate
        || (candidate != dock && !dock->isAncestorOf(candidate))) {
        return;
    }
    m_focusRepairPending = true;
    scheduleFocusDispatch();
}

void WorkbenchPanelNavigator::scheduleFocusDispatch() {
    m_focusTimer.start(std::chrono::milliseconds::zero());
}

void WorkbenchPanelNavigator::dispatchFocus() {
    if (m_pendingActivation) {
        PendingActivation& pending = *m_pendingActivation;
        RouteState* state = route(pending.id);
        if (state && panelIsExposed(state->dock)) {
            QPointer<QWidget> target = state->focusTarget
                ? state->focusTarget(pending.intent) : nullptr;
            if (target && target->isEnabled()
                && target->focusPolicy() != Qt::NoFocus
                && target->isVisibleTo(state->dock)) {
                target->setFocus(Qt::ShortcutFocusReason);
                if (focusReachedTarget(target)) {
                    m_pendingActivation.reset();
                    m_focusRepairPending = false;
                    return;
                }
            }
        }

        if (state && state->dock && pending.remainingRetries > 0) {
            --pending.remainingRetries;
            m_focusTimer.start(focusDispatchRetryInterval);
            return;
        }
        const WorkbenchPanelId failedId = pending.id;
        const WorkbenchPanelIntent failedIntent = pending.intent;
        m_pendingActivation.reset();
        emit panelActivationFailed(failedId, failedIntent);
        m_focusRepairPending = false;
        if (!focusIsUsable(QApplication::focusWidget())) {
            emit workspaceFocusRequested();
        }
        return;
    }

    if (std::exchange(m_focusRepairPending, false)
        && !focusIsUsable(QApplication::focusWidget())) {
        emit workspaceFocusRequested();
    }
}

} // namespace finepaper::ui
