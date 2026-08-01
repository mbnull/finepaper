#include "ui/workbench/inspector_design_settings.h"
#include "ui/workbench/inspector_summary_panel.h"
#include "ui/workbench/workbench_config.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
}

void summaryUsesStablePlainTextWidgets() {
    finepaper::ui::InspectorSummaryPanel panel;
    panel.resize(240, 480);
    panel.show();
    QApplication::processEvents();

    auto* selection = panel.findChild<QWidget*>(
        finepaper::workbench::selectionInspectorName);
    auto* designTitle = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.designOverview"));
    auto* availability = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.designAvailability"));
    check(selection && !selection->isVisible(),
          QStringLiteral("selection summary starts hidden"));

    const QString hostileTitle = QStringLiteral("NoC %2 <mesh> & clocks");
    panel.setDesignSummary({
        hostileTitle,
        QStringLiteral("pkg@example · 2 × 2"),
        QStringLiteral("Runtime unavailable")});
    panel.setSelectionSummary(finepaper::ui::InspectorSelectionSummary{
        QStringLiteral("Endpoint ep/<0>"),
        QStringLiteral("master & coherent · Router r-0-0"),
        QStringLiteral("Reconnect the EP port.\nDomain: power<&>core")});
    QApplication::processEvents();

    auto* selectionTitle = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.inspectorSelectionTitle"));
    auto* selectionDetail = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.inspectorSelectionDetail"));
    check(designTitle && designTitle->text() == hostileTitle
              && designTitle->textFormat() == Qt::PlainText,
          QStringLiteral("design content is preserved as literal plain text"));
    check(availability && availability->isVisible()
              && availability->property("finepaperRole").toString()
                  == QStringLiteral("warning"),
          QStringLiteral("availability warning is explicit and semantic"));
    check(selection && selection->isVisible()
              && selectionTitle
              && selectionTitle->text() == QStringLiteral("Endpoint ep/<0>")
              && selectionDetail
              && selectionDetail->text().contains(
                  QStringLiteral("power<&>core"))
              && selectionDetail->textFormat() == Qt::PlainText,
          QStringLiteral("selection content reuses fixed wrapping labels without HTML interpretation"));
    check(selection && selection->findChildren<QPushButton*>().isEmpty()
              && selection->findChildren<QToolButton*>().isEmpty(),
          QStringLiteral("selection summary contains no hidden icon or wiring actions"));
    check(panel.minimumSizeHint().width() <= panel.width(),
          QStringLiteral("long summary text does not force a wider Inspector"));

    QFont enlarged = panel.font();
    enlarged.setPointSizeF(enlarged.pointSizeF() * 1.5);
    panel.setFont(enlarged);
    panel.setDesignSummary({hostileTitle, QStringLiteral("metadata"), {}});
    panel.setSelectionSummary(std::nullopt);
    QApplication::processEvents();
    check(availability && !availability->isVisible()
              && selection && !selection->isVisible()
              && selectionTitle && selectionTitle->text().isEmpty()
              && panel.minimumSizeHint().width() <= panel.width(),
          QStringLiteral("clearing verbose state leaves no stale selection at large font size"));
}

void designSettingsUseTextDisclosureAndExposeDrafts() {
    finepaper::ui::InspectorDesignSettings settings;
    settings.resize(240, 320);
    auto* contentLabel = new QLabel(QStringLiteral("Design controls"));
    settings.addSection(contentLabel);
    settings.show();
    QApplication::processEvents();

    auto* toggle = settings.findChild<QToolButton*>(
        QStringLiteral("finepaper.inspectorDesignSettingsToggle"));
    auto* content = settings.findChild<QWidget*>(
        QStringLiteral("finepaper.inspectorDesignSettingsContent"));
    auto* draft = settings.findChild<QLabel*>(
        QStringLiteral("finepaper.parameterDraftStatus"));
    check(toggle && toggle->icon().isNull()
              && toggle->toolButtonStyle() == Qt::ToolButtonTextOnly
              && toggle->text() == QStringLiteral("Hide design settings")
              && content && content->isVisible(),
          QStringLiteral("expanded design settings use a text-only disclosure"));

    toggle->click();
    settings.setDraftNotice(QStringLiteral("Unapplied NoC parameter changes."));
    QApplication::processEvents();
    check(!settings.isExpanded() && content && !content->isVisible()
              && draft && draft->isVisible()
              && toggle->text() == QStringLiteral("Review parameter draft"),
          QStringLiteral("collapsed settings keep an unapplied draft visible in text"));

    toggle->click();
    check(settings.isExpanded() && content && content->isVisible()
              && toggle->text() == QStringLiteral("Hide design settings"),
          QStringLiteral("reviewing a draft expands the design settings"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    summaryUsesStablePlainTextWidgets();
    designSettingsUseTextDisclosureAndExposeDrafts();
    if (failures == 0) {
        QTextStream(stdout) << "Inspector workbench tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
