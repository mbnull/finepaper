#include "ui/workbench/widgets/workbench_dock_title_bar.h"

#include "ui/theme/ui_tokens.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QToolButton>

#include <algorithm>

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
    m_fullTitle = m_dock.windowTitle();
    const bool floating = m_dock.isFloating();
    const QString floatVerb = floating
        ? titleBarText("Dock")
        : titleBarText("Float");
    m_title->setToolTip(m_fullTitle);
    m_title->setAccessibleName(m_fullTitle);
    setAccessibleName(
        titleBarText("%1 panel controls").arg(m_fullTitle));

    m_floatButton->setText(floatVerb);
    m_floatButton->setAccessibleName(
        titleBarText("%1 %2 panel").arg(floatVerb).arg(m_fullTitle));
    m_floatButton->setToolTip(
        floating
            ? titleBarText("Return %1 to the workbench").arg(m_fullTitle)
            : titleBarText("Float %1 in its own window").arg(m_fullTitle));

    m_closeButton->setAccessibleName(
        titleBarText("Close %1 panel").arg(m_fullTitle));
    m_closeButton->setToolTip(
        titleBarText("Close %1 panel").arg(m_fullTitle));
    m_closeButton->setVisible(
        m_dock.features().testFlag(QDockWidget::DockWidgetClosable));

    updateResponsivePresentation();
}

void WorkbenchDockTitleBar::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }

    switch (event->type()) {
    case QEvent::FontChange:
    case QEvent::StyleChange:
    case QEvent::LayoutDirectionChange:
        updateResponsivePresentation();
        break;
    default:
        break;
    }
}

void WorkbenchDockTitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsivePresentation();
}

void WorkbenchDockTitleBar::updateResponsivePresentation() {
    if (!m_title || !m_floatButton || !m_closeButton || !layout()) {
        return;
    }

    const QMargins margins = layout()->contentsMargins();
    const int spacing = (std::max)(0, layout()->spacing());
    const bool floating = m_dock.isFloating();
    const bool floatable = m_dock.features().testFlag(
        QDockWidget::DockWidgetFloatable);
    const bool closable = m_dock.features().testFlag(
        QDockWidget::DockWidgetClosable);
    const int fullTitleWidth = m_title->fontMetrics().horizontalAdvance(
        m_fullTitle);

    const auto requiredWidth = [&](bool includeFloat) {
        int result = margins.left() + margins.right() + fullTitleWidth;
        int actionCount = 0;
        if (includeFloat) {
            result += m_floatButton->sizeHint().width();
            ++actionCount;
        }
        if (closable) {
            result += m_closeButton->sizeHint().width();
            ++actionCount;
        }
        return result + (actionCount * spacing);
    };

    // While docked, Float is secondary to an intact panel title and Close.
    // Once floating, Dock remains available even in a very narrow window so
    // there is always an obvious route back to the workbench.
    const bool showFloat = floating
        || (floatable && width() >= requiredWidth(true));
    m_floatButton->setVisible(showFloat);

    int availableTitleWidth = width() - margins.left() - margins.right();
    int visibleActionCount = 0;
    if (showFloat) {
        availableTitleWidth -= m_floatButton->sizeHint().width();
        ++visibleActionCount;
    }
    if (closable) {
        availableTitleWidth -= m_closeButton->sizeHint().width();
        ++visibleActionCount;
    }
    availableTitleWidth -= visibleActionCount * spacing;

    m_title->setText(m_title->fontMetrics().elidedText(
        m_fullTitle,
        Qt::ElideRight,
        (std::max)(0, availableTitleWidth)));
}

void installWorkbenchDockTitleBar(QDockWidget* dock) {
    if (dock) {
        dock->setTitleBarWidget(new WorkbenchDockTitleBar(*dock));
    }
}

} // namespace finepaper::ui
