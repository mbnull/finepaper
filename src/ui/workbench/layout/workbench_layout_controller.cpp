#include "ui/workbench/layout/workbench_layout_controller.h"

#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QMainWindow>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace finepaper::ui {
namespace {

constexpr int panelVisibilitySettingsVersion = 1;
// Qt UI Design's minimum responsive desktop layout width. The larger bound
// below is measured from the complete, unelided workspace navigation.
constexpr int minimumResponsiveWorkspaceWidth = 240;

} // namespace

WorkbenchWidthMode classifyWorkbenchWidth(
    const WorkbenchWidthRequirements& requirements,
    WorkbenchWidthMode currentMode) {
    const int available = (std::max)(0, requirements.availableWidth);
    const int allPanels = (std::max)(0, requirements.allPanelsWidth);
    const int onePanel = (std::min)(
        allPanels, (std::max)(0, requirements.onePanelWidth));
    const int hysteresis = (std::max)(0, requirements.hysteresis);

    if (allPanels == 0 || available >= allPanels +
            (currentMode == WorkbenchWidthMode::Wide ? 0 : hysteresis)) {
        return WorkbenchWidthMode::Wide;
    }

    if (onePanel == 0) {
        return WorkbenchWidthMode::Compact;
    }

    if (currentMode == WorkbenchWidthMode::CanvasOnly) {
        return available >= onePanel + hysteresis
            ? WorkbenchWidthMode::Compact
            : WorkbenchWidthMode::CanvasOnly;
    }
    return available < onePanel
        ? WorkbenchWidthMode::CanvasOnly
        : WorkbenchWidthMode::Compact;
}

WorkbenchLayoutController::WorkbenchLayoutController(
    QMainWindow* window,
    QWidget* centerWorkspace,
    QList<WorkbenchPanelBinding> sidePanels,
    QList<QDockWidget*> secondaryPanels,
    PresentationCallback presentationCallback,
    QWidget* compactNavigation)
    : QObject(window),
      m_window(window),
      m_centerWorkspace(centerWorkspace),
      m_compactNavigation(compactNavigation),
      m_sidePanels([&sidePanels] {
          QList<PanelState> states;
          states.reserve(sidePanels.size());
          for (const WorkbenchPanelBinding& binding :
               std::as_const(sidePanels)) {
              if (!binding.dock) {
                  continue;
              }
              PanelState state;
              state.binding = binding;
              state.userVisible = !binding.dock->isHidden();
              if (binding.preferenceAction) {
                  state.preferenceLabel =
                      binding.preferenceAction->text();
                  state.preferenceStatusTip =
                      binding.preferenceAction->statusTip();
              }
              states.push_back(std::move(state));
          }
          return states;
      }()),
      m_secondaryPanels(std::move(secondaryPanels)),
      m_presentationCallback(std::move(presentationCallback)) {
    for (PanelState& state : m_sidePanels) {
        WorkbenchPanelBinding& binding = state.binding;
        binding.dock->installEventFilter(this);
        connect(binding.dock, &QDockWidget::topLevelChanged,
                this, [this](bool) { scheduleReevaluation(); });
        connect(binding.dock, &QDockWidget::dockLocationChanged,
                this, [this](Qt::DockWidgetArea) {
                    scheduleReevaluation();
                });
        if (binding.preferenceAction) {
            binding.preferenceAction->setCheckable(true);
            connect(binding.preferenceAction, &QAction::toggled,
                    this, [this, role = binding.role](bool visible) {
                        if (!m_applyingLayout) {
                            setUserPanelVisible(role, visible);
                        }
                    });
        }
        if (QAction* nativeToggle = binding.dock->toggleViewAction();
            nativeToggle && nativeToggle != binding.preferenceAction) {
            connect(nativeToggle, &QAction::triggered,
                    this, [this, role = binding.role](bool visible) {
                        if (!m_applyingLayout) {
                            setUserPanelVisible(role, visible);
                        }
                    });
        }
    }
    for (QDockWidget* secondaryPanel : std::as_const(m_secondaryPanels)) {
        const bool alreadyObserved = std::any_of(
            m_sidePanels.cbegin(), m_sidePanels.cend(),
            [secondaryPanel](const PanelState& state) {
                return state.binding.dock == secondaryPanel;
            });
        if (secondaryPanel && !alreadyObserved) {
            secondaryPanel->installEventFilter(this);
        }
    }

    if (m_window) {
        m_window->installEventFilter(this);
    }
    if (m_centerWorkspace) {
        m_centerWorkspace->installEventFilter(this);
    }
    if (m_compactNavigation) {
        m_compactNavigation->installEventFilter(this);
    }
    m_reflowTimer.setSingleShot(true);
    m_reflowTimer.setInterval(0);
    connect(&m_reflowTimer, &QTimer::timeout,
            this, &WorkbenchLayoutController::reevaluateNow);
}

void WorkbenchLayoutController::start() {
    if (m_started) {
        scheduleReevaluation();
        return;
    }
    m_started = true;
    for (PanelState& state : m_sidePanels) {
        syncPreferenceAction(state);
    }
    scheduleReevaluation();
}

void WorkbenchLayoutController::reevaluateNow() {
    if (!m_started || !m_window || m_shuttingDown) {
        return;
    }

    const WorkbenchWidthRequirements requirements = widthRequirements();
    const WorkbenchWidthMode previousMode = m_widthMode;
    const WorkbenchWidthMode nextMode = classifyWorkbenchWidth(
        requirements, m_widthMode);
    if (nextMode != previousMode) {
        m_widthMode = nextMode;
    }
    if (nextMode == WorkbenchWidthMode::Wide) {
        setCompactReveal(std::nullopt);
    } else if (nextMode != previousMode
               && nextMode == WorkbenchWidthMode::CanvasOnly
               && !m_compactRevealTransitionPending) {
        setCompactReveal(std::nullopt);
    }
    m_compactRevealTransitionPending = false;

    if (m_presentationCallback
        && (!m_presentationInitialized || nextMode != previousMode)) {
        m_presentationCallback(m_widthMode);
        m_presentationInitialized = true;
    }
    const QString modeName = m_widthMode == WorkbenchWidthMode::Wide
        ? QStringLiteral("wide")
        : m_widthMode == WorkbenchWidthMode::Compact
            ? QStringLiteral("compact")
            : QStringLiteral("canvas-only");
    m_window->setProperty(workbench::workbenchWidthModeProperty, modeName);
    m_window->setProperty(
        workbench::workbenchAllPanelsWidthProperty,
        requirements.allPanelsWidth);
    m_window->setProperty(
        workbench::workbenchOnePanelWidthProperty,
        requirements.onePanelWidth);
    if (!m_canvasFocusActive && panelTaskFocusActive()) {
        applyPanelTaskFocusVisibility();
    } else if (!m_canvasFocusActive) {
        applyResponsivePanelVisibility();
    }
}

WorkbenchWidthMode WorkbenchLayoutController::widthMode() const {
    return m_widthMode;
}

bool WorkbenchLayoutController::panelAutoSuppressed(
    WorkbenchPanelRole role) const {
    const PanelState* state = panel(role);
    return state && state->autoSuppressed;
}

bool WorkbenchLayoutController::userPanelVisible(
    WorkbenchPanelRole role) const {
    const PanelState* state = panel(role);
    return state && state->userVisible;
}

void WorkbenchLayoutController::setCompactPreferredPanel(
    WorkbenchPanelRole role) {
    if (m_compactPreferredPanel == role) {
        return;
    }
    m_compactPreferredPanel = role;
    setCompactReveal(std::nullopt);
    scheduleReevaluation();
    if (m_started && !m_canvasFocusActive && !panelTaskFocusActive()) {
        applyResponsivePanelVisibility();
    }
}

void WorkbenchLayoutController::setUserPanelVisible(
    WorkbenchPanelRole role,
    bool visible) {
    PanelState* state = panel(role);
    if (!state) {
        return;
    }
    state->userVisible = visible;
    if (!visible && m_compactReveal == role) {
        setCompactReveal(std::nullopt);
    } else if (visible) {
        if (m_widthMode == WorkbenchWidthMode::Wide) {
            setCompactReveal(std::nullopt);
        } else {
            setCompactReveal(role);
            m_compactRevealTransitionPending = true;
        }
    }
    syncPreferenceAction(*state);
    if (!m_canvasFocusActive && !panelTaskFocusActive()) {
        applyResponsivePanelVisibility();
    }
}

void WorkbenchLayoutController::revealPanel(WorkbenchPanelRole role) {
    PanelState* state = panel(role);
    if (!state) {
        return;
    }
    state->userVisible = true;
    if (m_widthMode == WorkbenchWidthMode::Wide) {
        setCompactReveal(std::nullopt);
    } else {
        setCompactReveal(role);
        m_compactRevealTransitionPending = true;
    }
    syncPreferenceAction(*state);
    if (!m_canvasFocusActive && !panelTaskFocusActive()) {
        applyResponsivePanelVisibility();
    }
    if (state->binding.dock) {
        state->binding.dock->show();
        state->binding.dock->raise();
    }
}

void WorkbenchLayoutController::captureUserPanelVisibilityFromLayout() {
    for (PanelState& state : m_sidePanels) {
        if (!state.binding.dock) {
            continue;
        }
        state.userVisible = !state.binding.dock->isHidden();
        state.autoSuppressed = false;
        state.readableExtent = state.binding.dock->width();
        syncPreferenceAction(state);
    }
    setCompactReveal(std::nullopt);
    scheduleReevaluation();
}

void WorkbenchLayoutController::restoreUserPanelVisibility(
    const QVariantMap& settings) {
    const bool supported =
        settings.value(QStringLiteral("version")).toInt()
        == panelVisibilitySettingsVersion;
    const QVariantMap panels = supported
        ? settings.value(QStringLiteral("panels")).toMap()
        : QVariantMap{};

    for (PanelState& state : m_sidePanels) {
        const QString key = state.binding.dock
            ? state.binding.dock->objectName() : QString{};
        state.userVisible = panels.contains(key)
            ? panels.value(key).toBool()
            : state.binding.dock && !state.binding.dock->isHidden();
        state.autoSuppressed = false;
        state.readableExtent = state.binding.dock
            ? state.binding.dock->width() : 0;
        syncPreferenceAction(state);
    }
    setCompactReveal(std::nullopt);
}

QVariantMap WorkbenchLayoutController::persistentUserPanelVisibility() const {
    QVariantMap panels;
    for (const PanelState& state : m_sidePanels) {
        if (state.binding.dock
            && !state.binding.dock->objectName().isEmpty()) {
            panels.insert(
                state.binding.dock->objectName(), state.userVisible);
        }
    }
    return {
        {QStringLiteral("version"), panelVisibilitySettingsVersion},
        {QStringLiteral("panels"), panels},
    };
}

bool WorkbenchLayoutController::canvasFocusActive() const {
    return m_canvasFocusActive;
}

bool WorkbenchLayoutController::enterCanvasFocus() {
    if (m_canvasFocusActive) {
        return true;
    }
    if (!m_window || panelTaskFocusActive()) {
        return false;
    }

    const QByteArray restoreState = m_window->saveState();
    if (restoreState.isEmpty()) {
        return false;
    }

    m_canvasFocusRestoreState = restoreState;
    m_canvasFocusActive = true;
    const QScopedValueRollback applying(m_applyingLayout, true);
    for (QDockWidget* panel : std::as_const(m_secondaryPanels)) {
        if (panel) {
            panel->hide();
        }
    }
    return true;
}

bool WorkbenchLayoutController::leaveCanvasFocus() {
    if (!m_canvasFocusActive) {
        return true;
    }

    const QByteArray restoreState = m_canvasFocusRestoreState;
    m_canvasFocusRestoreState.clear();
    m_canvasFocusActive = false;
    if (!m_window || restoreState.isEmpty()) {
        return false;
    }

    bool restored = false;
    {
        const QScopedValueRollback applying(m_applyingLayout, true);
        restored = m_window->restoreState(restoreState);
    }
    if (restored) {
        reevaluateNow();
    }
    return restored;
}

bool WorkbenchLayoutController::panelTaskFocusActive() const {
    return m_panelTaskFocusRole.has_value()
        && !m_panelTaskFocusRestoreState.isEmpty();
}

std::optional<WorkbenchPanelRole>
WorkbenchLayoutController::panelTaskFocusRole() const {
    return panelTaskFocusActive()
        ? m_panelTaskFocusRole : std::nullopt;
}

bool WorkbenchLayoutController::enterPanelTaskFocus(
    WorkbenchPanelRole role) {
    PanelState* target = panel(role);
    if (!m_window || !target || !target->binding.dock
        || m_canvasFocusActive) {
        return false;
    }
    if (panelTaskFocusActive()) {
        return m_panelTaskFocusRole == role;
    }

    const QByteArray restoreState = m_window->saveState();
    if (restoreState.isEmpty()) {
        return false;
    }

    m_panelTaskFocusRestoreState = restoreState;
    m_panelTaskFocusRole = role;
    m_panelTaskFocusRestoreCompactReveal = m_compactReveal;
    setCompactReveal(role);
    m_panelTaskFocusIsolated = false;
    applyPanelTaskFocusVisibility();
    return true;
}

bool WorkbenchLayoutController::leavePanelTaskFocus() {
    if (!panelTaskFocusActive()) {
        releasePanelTaskFocusOwnership();
        return true;
    }

    const QByteArray restoreState = m_panelTaskFocusRestoreState;
    const std::optional<WorkbenchPanelRole> restoreCompactReveal =
        m_panelTaskFocusRestoreCompactReveal;
    releasePanelTaskFocusOwnership();
    setCompactReveal(restoreCompactReveal);
    if (!m_window || restoreState.isEmpty()) {
        return false;
    }

    bool restored = false;
    {
        const QScopedValueRollback applying(m_applyingLayout, true);
        restored = m_window->restoreState(restoreState);
    }
    if (restored) {
        reevaluateNow();
    }
    return restored;
}

void WorkbenchLayoutController::releasePanelTaskFocusOwnership() {
    m_panelTaskFocusRestoreState.clear();
    m_panelTaskFocusRole = std::nullopt;
    m_panelTaskFocusRestoreCompactReveal = std::nullopt;
    m_panelTaskFocusIsolated = false;
    m_panelTaskFocusClosePending = false;
}

QByteArray WorkbenchLayoutController::persistentWindowState() const {
    if (m_canvasFocusActive && !m_canvasFocusRestoreState.isEmpty()) {
        return m_canvasFocusRestoreState;
    }
    if (panelTaskFocusActive()) {
        return m_panelTaskFocusRestoreState;
    }
    return m_window ? m_window->saveState() : QByteArray{};
}

void WorkbenchLayoutController::beginShutdown() {
    m_shuttingDown = true;
    m_reflowTimer.stop();
}

bool WorkbenchLayoutController::eventFilter(QObject* watched, QEvent* event) {
    if (!event) {
        return QObject::eventFilter(watched, event);
    }

    QDockWidget* watchedDock = qobject_cast<QDockWidget*>(watched);
    const bool watchedSecondaryPanel = watchedDock
        && m_secondaryPanels.contains(watchedDock);
    if (!m_applyingLayout && !m_shuttingDown
        && watchedSecondaryPanel && event->type() == QEvent::Close) {
        const PanelState* focusedPanel = panelTaskFocusActive()
            ? panel(*m_panelTaskFocusRole) : nullptr;
        const bool closingFocusedTaskPanel = focusedPanel
            && watchedDock == focusedPanel->binding.dock;
        if (panelTaskFocusActive()) {
            if (closingFocusedTaskPanel) {
                m_panelTaskFocusClosePending = true;
                QTimer::singleShot(0, this, [this] {
                    m_panelTaskFocusClosePending = false;
                });
            } else {
                releasePanelTaskFocusOwnership();
            }
        }
        for (PanelState& state : m_sidePanels) {
            if (watched == state.binding.dock) {
                state.userVisible = false;
                state.autoSuppressed = false;
                syncPreferenceAction(state);
                break;
            }
        }
    }

    if (!m_applyingLayout && !m_shuttingDown
        && watchedSecondaryPanel && panelTaskFocusActive()
        && !m_panelTaskFocusClosePending
        && event->type() == QEvent::Show) {
        const PanelState* focusedPanel = panel(*m_panelTaskFocusRole);
        if (!focusedPanel || watchedDock != focusedPanel->binding.dock) {
            releasePanelTaskFocusOwnership();
        }
    }

    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::StyleChange:
    case QEvent::LayoutDirectionChange:
    case QEvent::LayoutRequest:
        scheduleReevaluation();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

WorkbenchLayoutController::PanelState* WorkbenchLayoutController::panel(
    WorkbenchPanelRole role) {
    const auto found = std::find_if(
        m_sidePanels.begin(), m_sidePanels.end(),
        [role](const PanelState& state) {
            return state.binding.role == role;
        });
    return found == m_sidePanels.end() ? nullptr : &*found;
}

const WorkbenchLayoutController::PanelState*
WorkbenchLayoutController::panel(WorkbenchPanelRole role) const {
    const auto found = std::find_if(
        m_sidePanels.cbegin(), m_sidePanels.cend(),
        [role](const PanelState& state) {
            return state.binding.role == role;
        });
    return found == m_sidePanels.cend() ? nullptr : &*found;
}

WorkbenchWidthRequirements WorkbenchLayoutController::widthRequirements() {
    if (!m_window) {
        return {};
    }

    const int wideCenterWidth = readableWideCenterWidth();
    const int compactCenterWidth = readableCompactCenterWidth();
    int packageWidth = 0;
    int contextWidth = 0;
    int contextTitleWidth = 0;
    for (PanelState& state : m_sidePanels) {
        if (!state.userVisible || !state.binding.dock
            || state.binding.dock->isFloating()) {
            continue;
        }
        const int extent = readablePanelExtent(state);
        if (state.binding.role == WorkbenchPanelRole::Package) {
            packageWidth = extent;
        } else if (isContextRole(state.binding.role)) {
            contextWidth = (std::max)(contextWidth, extent);
            contextTitleWidth +=
                state.binding.dock->fontMetrics().horizontalAdvance(
                    state.binding.dock->windowTitle())
                + 2 * state.binding.dock->fontMetrics().lineSpacing();
        }
    }
    contextWidth = (std::max)(contextWidth, contextTitleWidth);

    const int separator = m_window->style()->pixelMetric(
        QStyle::PM_DockWidgetSeparatorExtent, nullptr, m_window);
    const int allSeparators =
        (packageWidth > 0 ? separator : 0)
        + (contextWidth > 0 ? separator : 0);
    const bool preferPackage =
        m_compactPreferredPanel == WorkbenchPanelRole::Package
        && packageWidth > 0;
    const int preferredOnePanel = preferPackage
        ? packageWidth
        : contextWidth > 0 ? contextWidth : packageWidth;
    const int oneSeparator = preferredOnePanel > 0 ? separator : 0;
    const int averageCharacter = m_window->fontMetrics().averageCharWidth();
    const int hysteresis = (std::max)(
        2 * separator,
        4 * averageCharacter);

    return {
        m_window->contentsRect().width(),
        wideCenterWidth + packageWidth + contextWidth + allSeparators,
        compactCenterWidth + preferredOnePanel + oneSeparator,
        hysteresis,
    };
}

int WorkbenchLayoutController::readableWideCenterWidth() const {
    if (!m_window) {
        return 0;
    }
    int completeNavigationWidth = 0;
    if (const auto* tabs = qobject_cast<const QTabWidget*>(
            m_centerWorkspace)) {
        completeNavigationWidth = tabs->tabBar()->sizeHint().width()
            + 2 * tabs->style()->pixelMetric(
                QStyle::PM_DefaultFrameWidth, nullptr, tabs);
    }
    return (std::max)({
        minimumResponsiveWorkspaceWidth,
        completeNavigationWidth});
}

int WorkbenchLayoutController::readableCompactCenterWidth() const {
    const int navigationWidth = m_compactNavigation
        ? m_compactNavigation->sizeHint().width()
        : 0;
    return (std::max)(
        minimumResponsiveWorkspaceWidth, navigationWidth);
}

int WorkbenchLayoutController::readableCenterWidthForMode() const {
    return m_widthMode == WorkbenchWidthMode::Wide
        ? readableWideCenterWidth()
        : readableCompactCenterWidth();
}

int WorkbenchLayoutController::readablePanelExtent(PanelState& state) const {
    if (!state.binding.dock) {
        return 0;
    }
    const QDockWidget* dock = state.binding.dock;
    const int measuredMinimum = (std::max)(
        dock->minimumSizeHint().width(),
        dock->fontMetrics().horizontalAdvance(dock->windowTitle())
            + 4 * dock->fontMetrics().lineSpacing());
    if (state.readableExtent <= 0) {
        state.readableExtent = dock->width();
    }
    return (std::max)(state.readableExtent, measuredMinimum);
}

bool WorkbenchLayoutController::isContextRole(
    WorkbenchPanelRole role) const {
    return role == WorkbenchPanelRole::Inspector
        || role == WorkbenchPanelRole::Domain;
}

void WorkbenchLayoutController::scheduleReevaluation() {
    if (m_started && !m_shuttingDown && !m_reflowTimer.isActive()) {
        m_reflowTimer.start();
    }
}

void WorkbenchLayoutController::applyPanelTaskFocusVisibility() {
    if (!panelTaskFocusActive() || !m_window
        || m_applyingLayout || m_shuttingDown) {
        return;
    }
    PanelState* target = panel(*m_panelTaskFocusRole);
    if (!target || !target->binding.dock) {
        return;
    }

    const QScopedValueRollback applying(m_applyingLayout, true);
    if (m_widthMode == WorkbenchWidthMode::Wide) {
        if (m_panelTaskFocusIsolated
            && m_window->restoreState(m_panelTaskFocusRestoreState)) {
            m_panelTaskFocusIsolated = false;
        }
        target->binding.dock->show();
        target->binding.dock->raise();
        return;
    }

    if (!m_panelTaskFocusIsolated) {
        for (QDockWidget* secondaryPanel : std::as_const(m_secondaryPanels)) {
            if (!secondaryPanel || secondaryPanel == target->binding.dock
                || secondaryPanel->isFloating()) {
                continue;
            }
            secondaryPanel->hide();
        }
        m_panelTaskFocusIsolated = true;
    }
    target->binding.dock->show();
    target->binding.dock->raise();
}

void WorkbenchLayoutController::applyResponsivePanelVisibility() {
    if (m_applyingLayout || m_canvasFocusActive || panelTaskFocusActive()
        || m_shuttingDown) {
        return;
    }

    const QScopedValueRollback applying(m_applyingLayout, true);
    const bool revealPackage =
        m_compactReveal == WorkbenchPanelRole::Package;
    const bool revealContext = m_compactReveal
        && isContextRole(*m_compactReveal);
    const bool anyContextRequested = std::any_of(
        m_sidePanels.cbegin(), m_sidePanels.cend(),
        [this](const PanelState& state) {
            return isContextRole(state.binding.role)
                && state.userVisible && state.binding.dock
                && !state.binding.dock->isFloating();
        });
    const bool preferPackage =
        m_compactPreferredPanel == WorkbenchPanelRole::Package
        && std::any_of(
            m_sidePanels.cbegin(), m_sidePanels.cend(),
            [](const PanelState& state) {
                return state.binding.role
                           == WorkbenchPanelRole::Package
                    && state.userVisible && state.binding.dock
                    && !state.binding.dock->isFloating();
            });

    for (PanelState& state : m_sidePanels) {
        QDockWidget* dock = state.binding.dock;
        bool suppressed = false;
        if (dock && !dock->isFloating()) {
            switch (m_widthMode) {
            case WorkbenchWidthMode::Wide:
                break;
            case WorkbenchWidthMode::Compact:
                if (revealPackage) {
                    suppressed = isContextRole(state.binding.role);
                } else if (revealContext || anyContextRequested) {
                    suppressed = preferPackage && !revealContext
                        ? isContextRole(state.binding.role)
                        : state.binding.role
                            == WorkbenchPanelRole::Package;
                }
                break;
            case WorkbenchWidthMode::CanvasOnly:
                if (revealPackage) {
                    suppressed = isContextRole(state.binding.role);
                } else if (revealContext) {
                    suppressed =
                        state.binding.role == WorkbenchPanelRole::Package;
                } else {
                    suppressed = true;
                }
                break;
            }
        }
        state.autoSuppressed = state.userVisible && suppressed;
        applyPanelVisibility(
            state, state.userVisible && !state.autoSuppressed);
        syncPreferenceAction(state);
    }

    QDockWidget* visiblePackage = nullptr;
    QDockWidget* visibleContext = nullptr;
    int packageExtent = 0;
    int contextExtent = 0;
    int contextTitleExtent = 0;
    for (PanelState& state : m_sidePanels) {
        QDockWidget* dock = state.binding.dock;
        if (!state.userVisible || state.autoSuppressed || !dock
            || dock->isFloating() || dock->isHidden()) {
            continue;
        }
        if (state.binding.role == WorkbenchPanelRole::Package) {
            visiblePackage = dock;
            packageExtent = readablePanelExtent(state);
        } else if (isContextRole(state.binding.role)) {
            if (!visibleContext
                || (m_compactReveal
                    && state.binding.role == *m_compactReveal)) {
                visibleContext = dock;
            }
            contextExtent = (std::max)(
                contextExtent, readablePanelExtent(state));
            contextTitleExtent +=
                dock->fontMetrics().horizontalAdvance(dock->windowTitle())
                + 2 * dock->fontMetrics().lineSpacing();
        }
    }
    contextExtent = (std::max)(contextExtent, contextTitleExtent);
    const int separator = m_window->style()->pixelMetric(
        QStyle::PM_DockWidgetSeparatorExtent, nullptr, m_window);
    const int available = m_window->contentsRect().width();
    const int centerExtent = readableCenterWidthForMode();
    if (visiblePackage && packageExtent > visiblePackage->width()
        && available >= centerExtent + packageExtent + separator) {
        m_window->resizeDocks(
            {visiblePackage}, {packageExtent}, Qt::Horizontal);
    }
    if (visibleContext && contextExtent > visibleContext->width()
        && available >= centerExtent + contextExtent + separator) {
        m_window->resizeDocks(
            {visibleContext}, {contextExtent}, Qt::Horizontal);
    }

    std::optional<WorkbenchPanelRole> raisedRole = m_compactReveal;
    if (!raisedRole && m_widthMode == WorkbenchWidthMode::Compact) {
        raisedRole = m_compactPreferredPanel;
    }
    if (raisedRole) {
        PanelState* revealed = panel(*raisedRole);
        if ((!revealed || !revealed->userVisible
             || revealed->autoSuppressed)
            && isContextRole(*raisedRole)) {
            const auto fallback = std::find_if(
                m_sidePanels.begin(), m_sidePanels.end(),
                [this](const PanelState& state) {
                    return isContextRole(state.binding.role)
                        && state.userVisible && !state.autoSuppressed;
                });
            revealed = fallback == m_sidePanels.end()
                ? nullptr : &*fallback;
        }
        if (revealed && revealed->binding.dock
            && !revealed->binding.dock->isHidden()) {
            revealed->binding.dock->raise();
        }
    }
}

void WorkbenchLayoutController::applyPanelVisibility(
    PanelState& state,
    bool visible) {
    if (!state.binding.dock) {
        return;
    }
    if (visible) {
        state.binding.dock->show();
    } else {
        state.binding.dock->hide();
    }
}

void WorkbenchLayoutController::syncPreferenceAction(PanelState& state) {
    if (!state.binding.preferenceAction) {
        return;
    }
    const QScopedValueRollback applying(m_applyingLayout, true);
    const QSignalBlocker blocker(state.binding.preferenceAction);
    state.binding.preferenceAction->setChecked(state.userVisible);
    const bool hiddenToFit = state.userVisible && state.autoSuppressed;
    state.binding.preferenceAction->setText(
        hiddenToFit
            ? QCoreApplication::translate(
                  "WorkbenchLayoutController", "%1 — hidden to fit")
                  .arg(state.preferenceLabel)
            : state.preferenceLabel);
    state.binding.preferenceAction->setStatusTip(
        hiddenToFit
            ? QCoreApplication::translate(
                  "WorkbenchLayoutController",
                  "%1 The panel will return when space allows.")
                  .arg(state.preferenceStatusTip)
            : state.preferenceStatusTip);
    state.binding.preferenceAction->setWhatsThis(
        hiddenToFit
            ? QCoreApplication::translate(
                  "WorkbenchLayoutController",
                  "Selected as a layout preference; currently hidden to fit.")
            : QCoreApplication::translate(
                  "WorkbenchLayoutController",
                  "Checked panels are restored whenever the layout has space."));
}

void WorkbenchLayoutController::setCompactReveal(
    std::optional<WorkbenchPanelRole> role) {
    m_compactReveal = role;
    if (!role) {
        m_compactRevealTransitionPending = false;
    }
}

} // namespace finepaper::ui
