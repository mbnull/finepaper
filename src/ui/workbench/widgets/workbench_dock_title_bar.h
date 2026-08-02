#pragma once

#include <QString>
#include <QWidget>

class QDockWidget;
class QEvent;
class QLabel;
class QResizeEvent;
class QToolButton;

namespace finepaper::ui {

// Text-first Dock controls keep floating and closing discoverable without
// relying on platform-specific title-bar glyphs.
class WorkbenchDockTitleBar final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WorkbenchDockTitleBar)

public:
    explicit WorkbenchDockTitleBar(QDockWidget& dock);

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePresentation();
    void updateResponsivePresentation();

    QDockWidget& m_dock;
    QLabel* m_title = nullptr;
    QToolButton* m_floatButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QString m_fullTitle;
};

void installWorkbenchDockTitleBar(QDockWidget* dock);

} // namespace finepaper::ui
