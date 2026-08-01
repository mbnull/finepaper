#include "ui/theme/workbench_style.h"

#include "ui/theme/ui_tokens.h"

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QPalette>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

namespace finepaper::ui {
namespace {

constexpr auto kStyleObserverProperty =
    "_finepaper_workbench_style_observer";
constexpr auto kStylePaletteKeyProperty =
    "_finepaper_workbench_style_palette_key";

QString cssColor(const QColor& color) {
    return color.name(QColor::HexRgb);
}

void applyPaletteStyle(QApplication& application,
                       const QPalette& palette,
                       bool force = false) {
    const qulonglong paletteKey = palette.cacheKey();
    if (!force
        && application.property(kStylePaletteKeyProperty).toULongLong()
            == paletteKey
        && !application.styleSheet().isEmpty()) {
        return;
    }
    // Store the key before setting the stylesheet. If a platform style emits
    // paletteChanged while it is being repolished, the callback is idempotent.
    application.setProperty(kStylePaletteKeyProperty, paletteKey);
    application.setStyleSheet(workbenchStyleSheet(palette));
}

class WorkbenchPaletteObserver final : public QObject {
public:
    explicit WorkbenchPaletteObserver(QApplication& application)
        : QObject(&application),
          m_application(application) {
        application.installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == &m_application && event
            && event->type() == QEvent::ApplicationPaletteChange
            && !m_refreshPending) {
            m_refreshPending = true;
            QTimer::singleShot(0, this, [this] {
                m_refreshPending = false;
                applyPaletteStyle(m_application, m_application.palette());
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QApplication& m_application;
    bool m_refreshPending = false;
};

} // namespace

QString workbenchStyleSheet(const QPalette& palette) {
    const UiColors token = colors(palette);
    const QString surface = cssColor(token.surface);
    const QString raised = cssColor(token.surfaceRaised);
    const QString sunken = cssColor(token.surfaceSunken);
    const QString outline = cssColor(token.outline);
    const QString outlineStrong = cssColor(token.outlineStrong);
    const QString text = cssColor(token.text);
    const QString muted = cssColor(token.mutedText);
    const QString accent = cssColor(token.accent);
    const QString onAccent = cssColor(token.onAccent);
    const QString accentHover = cssColor(token.accentHover);
    const QString accentPressed = cssColor(token.accentPressed);
    const QString accentSubtle = cssColor(token.accentSubtle);
    const QString error = cssColor(token.error);
    const QString canvas = cssColor(token.canvas);

    QString sheet;
    QTextStream out(&sheet);

    out << "QMainWindow, QDialog {"
        << " background-color: " << surface << ";"
        << " color: " << text << ";"
        << "}\n"
        << "QToolTip {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outlineStrong << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: " << UiMetrics::spacing4 << "px "
        << UiMetrics::spacing8 << "px;"
        << "}\n";

    out << "QMenuBar {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border-bottom: 1px solid " << outline << ";"
        << " spacing: " << UiMetrics::spacing4 << "px;"
        << "}\n"
        << "QMenuBar::item {"
        << " background: transparent;"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: " << UiMetrics::spacing4 << "px "
        << UiMetrics::spacing8 << "px;"
        << "}\n"
        << "QMenuBar::item:selected, QMenuBar::item:pressed {"
        << " background-color: " << accentSubtle << ";"
        << "}\n"
        << "QMenu {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outlineStrong << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: " << UiMetrics::spacing4 << "px;"
        << "}\n"
        << "QMenu::item {"
        << " min-height: " << UiMetrics::controlCompactHeight << "px;"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: 0 " << UiMetrics::spacing24 << "px 0 "
        << UiMetrics::spacing8 << "px;"
        << "}\n"
        << "QMenu::item:selected {"
        << " background-color: " << accentSubtle << ";"
        << " color: " << text << ";"
        << "}\n"
        << "QMenu::item:disabled { color: " << muted << "; }\n"
        << "QMenu::separator {"
        << " background-color: " << outline << ";"
        << " height: 1px; margin: " << UiMetrics::spacing4 << "px "
        << UiMetrics::spacing8 << "px;"
        << "}\n";

    out << "QToolBar {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 0;"
        << " border-bottom: 1px solid " << outline << ";"
        << " spacing: " << UiMetrics::spacing4 << "px;"
        << " padding: " << UiMetrics::spacing4 << "px "
        << UiMetrics::spacing8 << "px;"
        << "}\n"
        << "QToolBar:vertical {"
        << " border-bottom: 0;"
        << " border-right: 1px solid " << outline << ";"
        << "}\n"
        << "QToolBar::separator {"
        << " background-color: " << outline << ";"
        << " width: 1px; height: 1px;"
        << " margin: " << UiMetrics::spacing8 << "px "
        << UiMetrics::spacing4 << "px;"
        << "}\n";

    out << "QDockWidget { color: " << text << "; }\n"
        << "QDockWidget::title {"
        << " background-color: " << raised << ";"
        << " border-bottom: 1px solid " << outline << ";"
        << " padding: " << UiMetrics::spacing8 << "px "
        << UiMetrics::spacing12 << "px;"
        << " font-weight: 600;"
        << "}\n"
        << "QDockWidget::close-button, QDockWidget::float-button {"
        << " background: transparent; border: 0;"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: " << UiMetrics::spacing4 << "px;"
        << "}\n"
        << "QDockWidget::close-button:hover, QDockWidget::float-button:hover {"
        << " background-color: " << accentSubtle << ";"
        << "}\n";

    out << "QTabWidget::pane {"
        << " background-color: " << surface << ";"
        << " border: 1px solid " << outline << ";"
        << "}\n"
        << "QTabBar::tab {"
        << " background: transparent;"
        << " color: " << muted << ";"
        << " border: 0;"
        << " border-bottom: 2px solid transparent;"
        << " min-height: " << UiMetrics::controlCompactHeight << "px;"
        << " padding: 0 " << UiMetrics::spacing12 << "px;"
        << " margin-right: " << UiMetrics::spacing4 << "px;"
        << "}\n"
        << "QTabBar::tab:selected {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border-bottom-color: " << accent << ";"
        << "}\n"
        << "QTabBar::tab:!selected:hover {"
        << " background-color: " << accentSubtle << ";"
        << " color: " << text << ";"
        << "}\n";

    out << "QGroupBox {"
        << " color: " << text << ";"
        << " background: transparent;"
        << " border: 1px solid " << outline << ";"
        << " border-radius: " << UiMetrics::radiusLarge << "px;"
        << " margin-top: " << UiMetrics::spacing12 << "px;"
        << " padding-top: " << UiMetrics::spacing8 << "px;"
        << " font-weight: 600;"
        << "}\n"
        << "QGroupBox::title {"
        << " subcontrol-origin: margin;"
        << " subcontrol-position: top left;"
        << " left: " << UiMetrics::spacing12 << "px;"
        << " padding: 0 " << UiMetrics::spacing4 << "px;"
        << " background-color: " << surface << ";"
        << " color: " << text << ";"
        << "}\n";

    out << "QPushButton, QToolBar QToolButton {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outlineStrong << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: 0 " << UiMetrics::spacing12 << "px;"
        << "}\n"
        << "QPushButton {"
        << " min-height: " << UiMetrics::controlHeight << "px;"
        << "}\n"
        << "QToolBar QToolButton {"
        << " min-width: " << UiMetrics::controlCompactHeight << "px;"
        << " min-height: " << UiMetrics::controlCompactHeight << "px;"
        << " padding-left: " << UiMetrics::spacing8 << "px;"
        << " padding-right: " << UiMetrics::spacing8 << "px;"
        << "}\n"
        << "QPushButton:hover, QToolBar QToolButton:hover {"
        << " background-color: " << accentSubtle << ";"
        << " border-color: " << accent << ";"
        << "}\n"
        << "QPushButton:pressed, QToolBar QToolButton:pressed,"
        << " QToolBar QToolButton:checked {"
        << " background-color: " << sunken << ";"
        << " border-color: " << accent << ";"
        << "}\n"
        << "QPushButton:disabled, QToolBar QToolButton:disabled {"
        << " background-color: " << sunken << ";"
        << " color: " << muted << ";"
        << " border-color: " << outline << ";"
        << "}\n";

    out << "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outlineStrong << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " min-height: " << UiMetrics::controlCompactHeight << "px;"
        << " padding: 0 " << UiMetrics::spacing8 << "px;"
        << " selection-background-color: " << accent << ";"
        << " selection-color: " << onAccent << ";"
        << "}\n"
        << "QPlainTextEdit, QTextEdit {"
        << " background-color: " << raised << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outlineStrong << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " padding: " << UiMetrics::spacing8 << "px;"
        << " selection-background-color: " << accent << ";"
        << " selection-color: " << onAccent << ";"
        << "}\n"
        << "QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled,"
        << " QDoubleSpinBox:disabled, QPlainTextEdit:disabled, QTextEdit:disabled {"
        << " background-color: " << sunken << ";"
        << " color: " << muted << ";"
        << " border-color: " << outline << ";"
        << "}\n"
        << "QCheckBox, QRadioButton {"
        << " color: " << text << ";"
        << " spacing: " << UiMetrics::spacing8 << "px;"
        << "}\n";

    out << "QAbstractItemView {"
        << " background-color: " << raised << ";"
        << " alternate-background-color: " << sunken << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outline << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " outline: 0;"
        << " selection-background-color: " << accent << ";"
        << " selection-color: " << onAccent << ";"
        << "}\n"
        << "QAbstractItemView::item {"
        << " min-height: " << UiMetrics::controlCompactHeight << "px;"
        << " padding: " << UiMetrics::spacing4 << "px "
        << UiMetrics::spacing8 << "px;"
        << "}\n"
        << "QAbstractItemView::item:hover {"
        << " background-color: " << accentSubtle << ";"
        << " color: " << text << ";"
        << "}\n"
        << "QAbstractItemView::item:selected {"
        << " background-color: " << accent << ";"
        << " color: " << onAccent << ";"
        << "}\n"
        << "QHeaderView::section {"
        << " background-color: " << sunken << ";"
        << " color: " << text << ";"
        << " border: 0;"
        << " border-right: 1px solid " << outline << ";"
        << " border-bottom: 1px solid " << outline << ";"
        << " padding: " << UiMetrics::spacing8 << "px;"
        << " font-weight: 600;"
        << "}\n";

    out << "QScrollBar:vertical {"
        << " background: " << sunken << ";"
        << " width: " << UiMetrics::spacing12 << "px;"
        << " margin: 0;"
        << "}\n"
        << "QScrollBar::handle:vertical {"
        << " background: " << outlineStrong << ";"
        << " min-height: " << UiMetrics::spacing24 << "px;"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " margin: 2px;"
        << "}\n"
        << "QScrollBar:horizontal {"
        << " background: " << sunken << ";"
        << " height: " << UiMetrics::spacing12 << "px;"
        << " margin: 0;"
        << "}\n"
        << "QScrollBar::handle:horizontal {"
        << " background: " << outlineStrong << ";"
        << " min-width: " << UiMetrics::spacing24 << "px;"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " margin: 2px;"
        << "}\n"
        << "QScrollBar::add-line, QScrollBar::sub-line {"
        << " width: 0; height: 0; border: 0;"
        << "}\n"
        << "QScrollBar::add-page, QScrollBar::sub-page {"
        << " background: transparent;"
        << "}\n";

    out << "QStatusBar {"
        << " background-color: " << raised << ";"
        << " color: " << muted << ";"
        << " border-top: 1px solid " << outline << ";"
        << "}\n"
        << "QStatusBar::item { border: 0; }\n"
        << "QProgressBar {"
        << " background-color: " << sunken << ";"
        << " color: " << text << ";"
        << " border: 1px solid " << outline << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << " text-align: center;"
        << "}\n"
        << "QProgressBar::chunk {"
        << " background-color: " << accent << ";"
        << " border-radius: " << UiMetrics::radiusSmall << "px;"
        << "}\n"
        << "QGraphicsView {"
        << " background-color: " << canvas << ";"
        << " border: 0;"
        << "}\n"
        << "QSplitter::handle { background-color: " << outline << "; }\n"
        << "QSplitter::handle:horizontal { width: 1px; }\n"
        << "QSplitter::handle:vertical { height: 1px; }\n"
        << "QSplitter::handle:hover { background-color: " << accent << "; }\n";

    // Semantic roles are deliberately property-based. Components can opt in
    // without tying the theme to object names or feature-specific classes.
    out << "QWidget[finepaperRole=\"card\"] {"
        << " background-color: " << raised << ";"
        << " border: 1px solid " << outline << ";"
        << " border-radius: " << UiMetrics::radiusLarge << "px;"
        << "}\n"
        << "QLabel[finepaperRole=\"title\"] {"
        << " color: " << text << "; font-weight: 600;"
        << "}\n"
        << "QLabel[finepaperRole=\"subtitle\"] {"
        << " color: " << text << "; font-weight: 600;"
        << "}\n"
        << "QLabel[finepaperRole=\"muted\"] {"
        << " color: " << muted << ";"
        << "}\n";

    out << "QPushButton[finepaperRole=\"primary\"],"
        << " QToolButton[finepaperRole=\"primary\"] {"
        << " background-color: " << accent << ";"
        << " color: " << onAccent << ";"
        << " border-color: " << accent << ";"
        << " font-weight: 600;"
        << "}\n"
        << "QPushButton[finepaperRole=\"primary\"]:hover,"
        << " QToolButton[finepaperRole=\"primary\"]:hover {"
        << " background-color: " << accentHover << ";"
        << " border-color: " << accentHover << ";"
        << "}\n"
        << "QPushButton[finepaperRole=\"primary\"]:pressed,"
        << " QToolButton[finepaperRole=\"primary\"]:pressed {"
        << " background-color: " << accentPressed << ";"
        << " border-color: " << accentPressed << ";"
        << "}\n"
        << "QPushButton[finepaperRole=\"primary\"]:disabled,"
        << " QToolButton[finepaperRole=\"primary\"]:disabled {"
        << " background-color: " << sunken << ";"
        << " color: " << muted << ";"
        << " border-color: " << outline << ";"
        << "}\n";

    out << "QPushButton[finepaperRole=\"quiet\"],"
        << " QToolButton[finepaperRole=\"quiet\"] {"
        << " background: transparent; border-color: transparent;"
        << "}\n"
        << "QPushButton[finepaperRole=\"quiet\"]:hover,"
        << " QToolButton[finepaperRole=\"quiet\"]:hover {"
        << " background-color: " << accentSubtle << ";"
        << " border-color: transparent;"
        << "}\n"
        << "QPushButton[finepaperRole=\"danger\"],"
        << " QToolButton[finepaperRole=\"danger\"] {"
        << " background: transparent;"
        << " color: " << error << ";"
        << " border-color: " << error << ";"
        << "}\n"
        << "QPushButton[finepaperRole=\"danger\"]:hover,"
        << " QToolButton[finepaperRole=\"danger\"]:hover,"
        << " QPushButton[finepaperRole=\"danger\"]:pressed,"
        << " QToolButton[finepaperRole=\"danger\"]:pressed {"
        << " background-color: " << sunken << ";"
        << " color: " << error << ";"
        << " border-color: " << error << ";"
        << "}\n"
        << "QPushButton[finepaperRole=\"danger\"]:disabled,"
        << " QToolButton[finepaperRole=\"danger\"]:disabled {"
        << " background-color: " << sunken << ";"
        << " color: " << muted << ";"
        << " border-color: " << outline << ";"
        << "}\n";

    out << "QToolButton[finepaperRole=\"canvasMode\"] {"
        << " background: transparent;"
        << " border-color: transparent;"
        << "}\n"
        << "QToolButton[finepaperRole=\"canvasMode\"]:hover {"
        << " background-color: " << accentSubtle << ";"
        << "}\n"
        << "QToolButton[finepaperRole=\"canvasMode\"]:checked {"
        << " background-color: " << accentSubtle << ";"
        << " color: " << text << ";"
        << " border-color: " << accent << ";"
        << " font-weight: 600;"
        << "}\n";

    out << "QPushButton:focus, QToolBar QToolButton:focus,"
        << " QToolButton[finepaperRole]:focus, QLineEdit:focus,"
        << " QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,"
        << " QPlainTextEdit:focus, QTextEdit:focus, QAbstractItemView:focus,"
        << " QGraphicsView:focus {"
        << " border: 2px solid " << accent << ";"
        << "}\n"
        << "QPushButton[finepaperRole=\"primary\"]:focus,"
        << " QToolButton[finepaperRole=\"primary\"]:focus {"
        << " border: 2px solid " << onAccent << ";"
        << "}\n"
        << "QTabBar::tab:focus {"
        << " border: 1px solid " << accent << ";"
        << " border-bottom: 2px solid " << accent << ";"
        << "}\n";

    return sheet;
}

void applyWorkbenchStyle(QApplication& application) {
    applyPaletteStyle(application, application.palette(), true);
    if (application.property(kStyleObserverProperty).value<QObject*>()) {
        return;
    }
    auto* observer = new WorkbenchPaletteObserver(application);
    application.setProperty(
        kStyleObserverProperty,
        QVariant::fromValue(static_cast<QObject*>(observer)));
}

} // namespace finepaper::ui
