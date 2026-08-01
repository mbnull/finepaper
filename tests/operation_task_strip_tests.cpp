#include "ui/components/operation_task_strip.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextStream>

#include <QtTest/QTest>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    finepaper::ui::OperationTaskStrip strip;
    strip.resize(900, strip.sizeHint().height());
    QApplication::processEvents();
    check(!strip.isVisible() && !strip.isRunning(),
          QStringLiteral("the task strip starts idle and hidden"));
    check(strip.focusPolicy() == Qt::NoFocus,
          QStringLiteral(
              "the presentation container is not an empty keyboard tab stop"));

    auto* state = strip.findChild<QLabel*>(
        QStringLiteral("finepaper.operationTaskState"));
    auto* operation = strip.findChild<QLabel*>(
        QStringLiteral("finepaper.operationTaskName"));
    auto* progress = strip.findChild<QProgressBar*>(
        QStringLiteral("finepaper.operationProgress"));
    auto* cancel = strip.findChild<QPushButton*>(
        QStringLiteral("finepaper.cancelOperation"));
    check(state && operation && progress && cancel,
          QStringLiteral("the task strip exposes stable text and cancellation controls"));

    int cancellationRequests = 0;
    QObject::connect(
        &strip, &finepaper::ui::OperationTaskStrip::cancelRequested,
        &strip, [&] {
            ++cancellationRequests;
            strip.setCancellationRequested();
        });

    const QString completeName = QStringLiteral(
        "Validating a deliberately long design name, revision 27");
    strip.setReducedMotion(false);
    strip.begin(completeName, QStringLiteral("Cancel design validation"));
    QApplication::processEvents();
    check(strip.isVisible() && strip.isRunning()
              && state && state->text() == QStringLiteral("Running")
              && operation && operation->accessibleName() == completeName
              && cancel && cancel->isEnabled()
              && cancel->text() == QStringLiteral("Cancel")
              && cancel->accessibleName()
                     == QStringLiteral("Cancel design validation"),
          QStringLiteral("begin presents the operation in text with an accessible Cancel action"));
    check(progress && !progress->isVisibleTo(&strip),
          QStringLiteral("brief operations do not flash an indeterminate progress animation"));
    QTest::qWait(400);
    check(progress && progress->isVisibleTo(&strip),
          QStringLiteral("a continuing operation reveals delayed progress feedback"));

    if (cancel) {
        cancel->setFocus(Qt::OtherFocusReason);
        cancel->click();
    }
    QApplication::processEvents();
    check(cancellationRequests == 1
              && strip.cancellationRequested()
              && state
              && state->text() == QStringLiteral("Cancel requested")
              && cancel && !cancel->isEnabled()
              && cancel->text() == QString::fromUtf8("Cancelling…"),
          QStringLiteral("Cancel becomes an idempotent visible request while work continues"));
    check(!strip.hasFocus() && cancel && !cancel->hasFocus(),
          QStringLiteral(
              "disabling Cancel does not strand focus on the non-interactive strip"));
    if (cancel) {
        cancel->click();
    }
    check(cancellationRequests == 1,
          QStringLiteral("a disabled cancellation control cannot emit a duplicate request"));

    strip.finish();
    QApplication::processEvents();
    check(!strip.isVisible() && !strip.isRunning()
              && !strip.cancellationRequested(),
          QStringLiteral("finish returns the component to a reusable idle state"));

    strip.setReducedMotion(true);
    strip.begin(QStringLiteral("Generating RTL"));
    QTest::qWait(400);
    check(progress && !progress->isVisibleTo(&strip),
          QStringLiteral("Reduce Motion keeps textual feedback while suppressing progress animation"));
    check(state && state->isVisibleTo(&strip)
              && cancel && cancel->isVisibleTo(&strip),
          QStringLiteral("Reduce Motion does not remove state or cancellation controls"));
    strip.finish();

    QFont largeFont = strip.font();
    if (largeFont.pointSizeF() > 0.0) {
        largeFont.setPointSizeF(largeFont.pointSizeF() * 2.0);
    } else if (largeFont.pixelSize() > 0) {
        largeFont.setPixelSize(largeFont.pixelSize() * 2);
    }
    strip.setFont(largeFont);
    strip.resize(520, strip.sizeHint().height());
    strip.setReducedMotion(false);
    strip.begin(completeName);
    QTest::qWait(400);
    const bool controlsDoNotOverlap = state && operation && progress && cancel
        && !state->geometry().intersects(operation->geometry())
        && !operation->geometry().intersects(progress->geometry())
        && !progress->geometry().intersects(cancel->geometry());
    check(controlsDoNotOverlap
              && state && state->isVisibleTo(&strip)
              && progress && !progress->isVisibleTo(&strip)
              && cancel && cancel->isVisibleTo(&strip)
              && operation && operation->accessibleName() == completeName,
          QStringLiteral(
              "a narrow 200% font layout prioritizes task identity and Cancel without overlap or lost semantics"));
    strip.finish();

    QTextStream(stdout)
        << (failures == 0
                ? "All operation task strip tests passed."
                : "Operation task strip tests failed.")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
