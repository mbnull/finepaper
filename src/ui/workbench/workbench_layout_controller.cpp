#include "ui/workbench/workbench_layout_controller.h"

#include <QDockWidget>
#include <QMainWindow>

#include <utility>

namespace finepaper::ui {

WorkbenchLayoutController::WorkbenchLayoutController(
    QMainWindow* window,
    QList<QDockWidget*> secondaryPanels)
    : m_window(window),
      m_secondaryPanels(std::move(secondaryPanels)) {}

bool WorkbenchLayoutController::canvasFocusActive() const {
    return m_canvasFocusActive;
}

bool WorkbenchLayoutController::enterCanvasFocus() {
    if (m_canvasFocusActive) {
        return true;
    }
    if (!m_window) {
        return false;
    }

    const QByteArray restoreState = m_window->saveState();
    if (restoreState.isEmpty()) {
        return false;
    }

    m_canvasFocusRestoreState = restoreState;
    m_canvasFocusActive = true;
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
    return m_window && !restoreState.isEmpty()
        && m_window->restoreState(restoreState);
}

QByteArray WorkbenchLayoutController::persistentWindowState() const {
    if (m_canvasFocusActive && !m_canvasFocusRestoreState.isEmpty()) {
        return m_canvasFocusRestoreState;
    }
    return m_window ? m_window->saveState() : QByteArray{};
}

} // namespace finepaper::ui
