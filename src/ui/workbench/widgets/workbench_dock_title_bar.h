#pragma once

#include <QWidget>

class QDockWidget;
class QLabel;
class QToolButton;

namespace finepaper::ui {

// Text-first Dock controls keep floating and closing discoverable without
// relying on platform-specific title-bar glyphs.
class WorkbenchDockTitleBar final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WorkbenchDockTitleBar)

public:
    explicit WorkbenchDockTitleBar(QDockWidget& dock);

private:
    void updatePresentation();

    QDockWidget& m_dock;
    QLabel* m_title = nullptr;
    QToolButton* m_floatButton = nullptr;
    QToolButton* m_closeButton = nullptr;
};

void installWorkbenchDockTitleBar(QDockWidget* dock);

} // namespace finepaper::ui
