#pragma once

#include <QByteArray>
#include <QList>

class QDockWidget;
class QMainWindow;

namespace finepaper::ui {

// Owns transient workbench layout modes without making them persistent user
// preferences. The MainWindow remains responsible for commands and focus.
class WorkbenchLayoutController final {
public:
    WorkbenchLayoutController(QMainWindow* window,
                              QList<QDockWidget*> secondaryPanels);

    [[nodiscard]] bool canvasFocusActive() const;
    bool enterCanvasFocus();
    bool leaveCanvasFocus();
    [[nodiscard]] QByteArray persistentWindowState() const;

private:
    QMainWindow* m_window = nullptr;
    QList<QDockWidget*> m_secondaryPanels;
    QByteArray m_canvasFocusRestoreState;
    bool m_canvasFocusActive = false;
};

} // namespace finepaper::ui
