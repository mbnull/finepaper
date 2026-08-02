#include "ui/workbench/inspector_design_settings.h"
#include "ui/workbench/inspector_summary_panel.h"
#include "ui/workbench/workbench_config.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QRect>
#include <QTextStream>
#include <QToolButton>
#include <QVector>
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
    auto* selectionDetailToggle = panel.findChild<QToolButton*>(
        QStringLiteral("finepaper.inspectorSelectionDetailToggle"));
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
    auto* editDomainAssignments = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.inspectorEditDomainAssignments"));
    auto* reviewDiagnostics = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.inspectorReviewDiagnostics"));
    auto* disconnectEndpoint = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.inspectorDisconnectEndpoint"));
    check(selection && selectionDetailToggle
              && editDomainAssignments && reviewDiagnostics && disconnectEndpoint
              && !editDomainAssignments->isVisible()
              && !reviewDiagnostics->isVisible()
              && !disconnectEndpoint->isVisible()
              && !selectionDetailToggle->isVisible()
              && editDomainAssignments->icon().isNull()
              && reviewDiagnostics->icon().isNull()
              && selectionDetailToggle->icon().isNull()
              && selectionDetailToggle->toolButtonStyle()
                  == Qt::ToolButtonTextOnly,
          QStringLiteral(
              "selection summary keeps only explicit text task routes hidden by default"));

    panel.setSelectionTaskFocused(true);
    QApplication::processEvents();
    check(designTitle && !designTitle->isVisible()
              && selectionDetail && !selectionDetail->isVisible()
              && selectionDetailToggle->isVisible()
              && selectionDetailToggle->text()
                  == QStringLiteral("Show selection details"),
          QStringLiteral(
              "selection task mode hides design metadata and collapses guidance behind text disclosure"));
    selectionDetailToggle->click();
    QApplication::processEvents();
    check(selectionDetail->isVisible()
              && selectionDetailToggle->text()
                  == QStringLiteral("Hide selection details"),
          QStringLiteral(
              "selection guidance remains explicitly available without an icon"));

    int editDomainRequests = 0;
    int reviewDiagnosticRequests = 0;
    int disconnectRequests = 0;
    panel.editDomainAssignmentsRequested = [&editDomainRequests] {
        ++editDomainRequests;
    };
    panel.reviewDiagnosticsRequested = [&reviewDiagnosticRequests] {
        ++reviewDiagnosticRequests;
    };
    panel.disconnectEndpointAttachmentRequested = [&disconnectRequests] {
        ++disconnectRequests;
    };
    panel.setContextActions({true, true, true});
    QApplication::processEvents();
    editDomainAssignments->click();
    reviewDiagnostics->click();
    disconnectEndpoint->click();
    check(editDomainAssignments->isVisible()
              && editDomainAssignments->text()
                  == QStringLiteral("Edit Domain assignments")
              && reviewDiagnostics->isVisible()
              && reviewDiagnostics->text()
                  == QStringLiteral("Review diagnostics")
              && disconnectEndpoint->isVisible()
              && disconnectEndpoint->text()
                  == QStringLiteral("Disconnect Endpoint")
              && panel.preferredFocusTarget() == disconnectEndpoint
              && editDomainAssignments->mapTo(&panel, QPoint{}).y()
                  < selectionTitle->mapTo(&panel, QPoint{}).y()
              && editDomainRequests == 1 && reviewDiagnosticRequests == 1
              && disconnectRequests == 1,
          QStringLiteral(
              "selection summary keeps text task routes before descriptive metadata with stable callbacks"));
    QFont actionFont = panel.font();
    actionFont.setPointSizeF(actionFont.pointSizeF() * 2.0);
    panel.setFont(actionFont);
    panel.resize(240, 720);
    QApplication::processEvents();
    const QVector<QPushButton*> taskActions{
        disconnectEndpoint, editDomainAssignments, reviewDiagnostics};
    QVector<QRect> taskActionRects;
    bool taskActionsContained = true;
    bool taskActionsDistinct = true;
    for (QPushButton* action : taskActions) {
        if (!action || !action->isVisibleTo(&panel)) {
            taskActionsContained = false;
            continue;
        }
        const QRect actionRect(action->mapTo(&panel, QPoint{}), action->size());
        taskActionsContained = taskActionsContained
            && panel.rect().contains(actionRect);
        for (const QRect& existing : taskActionRects) {
            taskActionsDistinct = taskActionsDistinct
                && !existing.intersects(actionRect);
        }
        taskActionRects.append(actionRect);
    }
    check(taskActionsContained && taskActionsDistinct,
          QStringLiteral(
              "240 px Inspector at 2x font keeps text task actions contained and non-overlapping"));
    check(panel.minimumSizeHint().width() <= panel.width(),
          QStringLiteral("long summary text does not force a wider Inspector"));

    panel.setSelectionTaskFocused(false);
    panel.setDesignSummary({hostileTitle, QStringLiteral("metadata"), {}});
    panel.setSelectionSummary(std::nullopt);
    panel.setContextActions({false, true});
    QApplication::processEvents();
    check(availability && !availability->isVisible()
              && selection && !selection->isVisible()
              && selectionTitle && selectionTitle->text().isEmpty()
              && editDomainAssignments && !editDomainAssignments->isVisible()
              && reviewDiagnostics && reviewDiagnostics->isVisible()
              && disconnectEndpoint && !disconnectEndpoint->isVisible()
              && panel.minimumSizeHint().width() <= panel.width(),
          QStringLiteral(
              "clearing verbose state leaves no stale selection while design-level diagnostics remain reachable"));
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
