#include "ui/workbench/widgets/workbench_dock_title_bar.h"

#include "ui/theme/ui_tokens.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>

namespace finepaper::ui {
namespace {

QString titleBarText(const char* sourceText) {
    return QCoreApplication::translate(
        "WorkbenchDockTitleBar", sourceText);
}

} // namespace

WorkbenchDockTitleBar::WorkbenchDockTitleBar(QDockWidget& dock)
    : QWidget(&dock),
      m_dock(dock) {
    setObjectName(QStringLiteral("finepaper.dockTitleBar"));
    setProperty("finepaperRole", QStringLiteral("dockTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        UiMetrics::spacing12,
        UiMetrics::spacing4,
        UiMetrics::spacing4,
        UiMetrics::spacing4);
    layout->setSpacing(UiMetrics::spacing4);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("finepaper.dockTitleBar.title"));
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMinimumWidth(0);
    layout->addWidget(m_title, 1);

    m_floatButton = new QToolButton(this);
    m_floatButton->setObjectName(
        QStringLiteral("finepaper.dockTitleBar.floatButton"));
    m_floatButton->setProperty("finepaperRole", QStringLiteral("quiet"));
    m_floatButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_floatButton->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_floatButton);

    m_closeButton = new QToolButton(this);
    m_closeButton->setObjectName(
        QStringLiteral("finepaper.dockTitleBar.closeButton"));
    m_closeButton->setProperty("finepaperRole", QStringLiteral("quiet"));
    m_closeButton->setText(titleBarText("Close"));
    m_closeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_closeButton->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_closeButton);

    connect(&m_dock, &QDockWidget::windowTitleChanged,
            this, [this](const QString&) { updatePresentation(); });
    connect(&m_dock, &QDockWidget::topLevelChanged,
            this, [this](bool) { updatePresentation(); });
    connect(&m_dock, &QDockWidget::featuresChanged,
            this, [this](QDockWidget::DockWidgetFeatures) {
                updatePresentation();
            });
    connect(m_floatButton, &QToolButton::clicked,
            &m_dock, [this] {
                m_dock.setFloating(!m_dock.isFloating());
            });
    connect(m_closeButton, &QToolButton::clicked,
            &m_dock, &QDockWidget::close);
    updatePresentation();
}

void WorkbenchDockTitleBar::updatePresentation() {
    const QString title = m_dock.windowTitle();
    const bool floating = m_dock.isFloating();
    const QString floatVerb = floating
        ? titleBarText("Dock")
        : titleBarText("Float");
    m_title->setText(title);
    m_title->setToolTip(title);
    setAccessibleName(titleBarText("%1 panel controls").arg(title));

    m_floatButton->setText(floatVerb);
    m_floatButton->setAccessibleName(
        titleBarText("%1 %2 panel").arg(floatVerb).arg(title));
    m_floatButton->setToolTip(
        floating
            ? titleBarText("Return %1 to the workbench").arg(title)
            : titleBarText("Float %1 in its own window").arg(title));
    m_floatButton->setVisible(
        m_dock.features().testFlag(QDockWidget::DockWidgetFloatable));

    m_closeButton->setAccessibleName(
        titleBarText("Close %1 panel").arg(title));
    m_closeButton->setToolTip(
        titleBarText("Close %1 panel").arg(title));
    m_closeButton->setVisible(
        m_dock.features().testFlag(QDockWidget::DockWidgetClosable));
}

void installWorkbenchDockTitleBar(QDockWidget* dock) {
    if (dock) {
        dock->setTitleBarWidget(new WorkbenchDockTitleBar(*dock));
    }
}

} // namespace finepaper::ui
