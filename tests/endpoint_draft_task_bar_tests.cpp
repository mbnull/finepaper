#include "features/topology/drafts/endpoint_canvas_draft_state.h"
#include "features/topology/endpoint_draft_task_bar.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <QtTest/QTest>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
}

template <typename Widget>
Widget* requiredChild(EndpointDraftTaskBar& bar, const QString& name) {
    Widget* child = bar.findChild<Widget*>(name);
    check(child != nullptr, QStringLiteral("task bar exposes %1").arg(name));
    return child;
}

EndpointCanvasDraftInfo pendingDraft(const QString& id) {
    EndpointCanvasDraftInfo draft;
    draft.id.value = id;
    draft.lifecycle = EndpointCanvasDraftLifecycle::PendingNew;
    draft.endpointType = QStringLiteral("cpu");
    draft.endpointTypeLabel = QStringLiteral("CPU Endpoint");
    return draft;
}

EndpointCanvasDraftInfo detachedDraft(const QString& draftId,
                                      const QString& endpointId) {
    EndpointCanvasDraftInfo draft;
    draft.id.value = draftId;
    draft.lifecycle = EndpointCanvasDraftLifecycle::Detached;
    draft.endpointType = QStringLiteral("memory");
    draft.endpointTypeLabel = QStringLiteral("Memory Endpoint");
    draft.endpointId = endpointId;
    draft.previousAttachment = EndpointAttachment{
        RouterPosition{1, 2}, QStringLiteral("memory-0")};
    return draft;
}

QRect mappedRect(QWidget& ancestor, QWidget* widget) {
    return widget
        ? QRect(widget->mapTo(&ancestor, QPoint{}), widget->size())
        : QRect{};
}

bool fullyVisibleWithin(QWidget& ancestor, QWidget* widget) {
    return widget && widget->isVisibleTo(&ancestor)
        && ancestor.rect().contains(mappedRect(ancestor, widget));
}

void draftTextDistinguishesLifecycleImpact() {
    using namespace endpoint_canvas_draft_text;

    const EndpointCanvasDraftState pending({
        pendingDraft(QStringLiteral("pending-0"))});
    const QString pendingNotice = notice(pending);
    const QString pendingConfirmation = taskDiscardConfirmation(pending);
    check(taskTitle(pending) == QStringLiteral("Resolve 1 new Endpoint draft")
              && reviewAction(pending) == QStringLiteral("Review draft")
              && discardAction(pending) == QStringLiteral("Discard draft")
              && pendingNotice.contains(QStringLiteral("not in the design"))
              && pendingNotice.contains(QStringLiteral("Connect the new draft"))
              && pendingConfirmation.contains(
                     QStringLiteral("has not been added to the design"))
              && pendingConfirmation.contains(
                     QStringLiteral("removes it from the canvas"))
              && !pendingConfirmation.contains(
                     QStringLiteral("permanently deletes")),
          QStringLiteral(
              "pending copy explains that the draft is not durable and Discard only removes canvas work"));

    const EndpointCanvasDraftState detached({
        detachedDraft(
            QStringLiteral("detached-0"), QStringLiteral("memory_0"))});
    const QString detachedNotice = notice(detached);
    const QString detachedConfirmation = taskDiscardConfirmation(detached);
    check(taskTitle(detached)
                  == QStringLiteral("Resolve 1 disconnected Endpoint")
              && reviewAction(detached) == QStringLiteral("Review Endpoint")
              && discardAction(detached)
                     == QStringLiteral("Delete Endpoint…")
              && detachedNotice.contains(
                     QStringLiteral("preserved for this session"))
              && detachedNotice.contains(QStringLiteral("memory_0"))
              && detachedNotice.contains(
                     QStringLiteral("delete it permanently"))
              && detachedConfirmation.contains(
                     QStringLiteral("recoverable in this session"))
              && detachedConfirmation.contains(
                     QStringLiteral(
                         "permanently deletes it from the current design"))
              && detachedConfirmation.contains(
                     QStringLiteral(
                         "preserved Domain assignments, attachment settings, and configuration will be lost")),
          QStringLiteral(
              "detached copy identifies session recovery and the permanent current-design data loss before Delete"));

    const EndpointCanvasDraftState mixed({
        detachedDraft(
            QStringLiteral("detached-1"), QStringLiteral("memory_1")),
        pendingDraft(QStringLiteral("pending-1"))});
    const QString mixedNotice = notice(mixed);
    const QString mixedConfirmation = taskDiscardConfirmation(mixed);
    check(taskTitle(mixed)
                  == QStringLiteral(
                      "Resolve 1 new Endpoint draft and 1 disconnected Endpoint")
              && reviewAction(mixed) == QStringLiteral("Review drafts")
              && discardAction(mixed)
                     == QStringLiteral("Discard / Delete…")
              && mixedNotice.contains(QStringLiteral("need attention"))
              && mixedNotice.contains(QStringLiteral("Connect the new draft"))
              && mixedNotice.contains(
                     QStringLiteral("Reconnect the disconnected Endpoint"))
              && mixedConfirmation.contains(
                     QStringLiteral("has not been added to the design"))
              && mixedConfirmation.contains(
                     QStringLiteral(
                         "permanently deletes it from the current design")),
          QStringLiteral(
              "mixed copy preserves the distinct Discard and permanent Delete consequences"));

    check(operationUnavailableHint(pending, QStringLiteral("Save"))
                  .contains(QStringLiteral("Save is unavailable"))
              && operationUnavailableHint(detached, QStringLiteral("Generate"))
                     .contains(QStringLiteral("Reconnect")),
          QStringLiteral(
              "persistent-operation hints name the blocked action and its lifecycle-specific recovery"));
}

void narrowTaskBarKeepsActionsVisibleAndKeyboardReachable() {
    EndpointDraftTaskBar bar;
    QMenu connectMenu(&bar);
    connectMenu.setObjectName(
        QStringLiteral("finepaper.endpointDraftRouterMenu.test"));
    connectMenu.addAction(QStringLiteral("Router r-0-0"));
    bar.setConnectMenu(&connectMenu);

    const EndpointCanvasDraftState draftState({
        pendingDraft(QStringLiteral("pending-layout"))});
    EndpointDraftTaskBarState state;
    state.visible = true;
    state.title = endpoint_canvas_draft_text::taskTitle(draftState);
    state.guidance = endpoint_canvas_draft_text::notice(draftState);
    state.reviewText = endpoint_canvas_draft_text::reviewAction(draftState);
    state.connectText = QStringLiteral("Connect…");
    state.discardText = endpoint_canvas_draft_text::discardAction(draftState);
    state.reviewEnabled = true;
    state.connectEnabled = true;
    state.discardEnabled = true;
    bar.setState(state);
    bar.show();
    QApplication::processEvents();
    bar.resize(798, 360);
    QApplication::processEvents();

    auto* title = requiredChild<QLabel>(
        bar, QStringLiteral("finepaper.endpointDraftTaskTitle"));
    auto* guidance = requiredChild<QLabel>(
        bar, QStringLiteral("finepaper.endpointCanvasDraftNotice"));
    auto* review = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.endpointDraftReview"));
    auto* connect = requiredChild<QToolButton>(
        bar, QStringLiteral("finepaper.endpointDraftConnect"));
    auto* discard = requiredChild<QPushButton>(
        bar, QStringLiteral("finepaper.endpointDraftDiscard"));
    if (!title || !guidance || !review || !connect || !discard) {
        return;
    }

    const QVector<QWidget*> content{
        title, guidance, review, connect, discard};
    bool allHaveGeometry = bar.size() == QSize(798, 360);
    bool allFullyVisible = true;
    bool noOverlap = true;
    for (qsizetype index = 0; index < content.size(); ++index) {
        QWidget* widget = content.at(index);
        allHaveGeometry = allHaveGeometry
            && widget->width() > 0 && widget->height() > 0;
        allFullyVisible = allFullyVisible
            && fullyVisibleWithin(bar, widget);
        const QRect current = mappedRect(bar, widget);
        for (qsizetype other = index + 1; other < content.size(); ++other) {
            noOverlap = noOverlap
                && !current.intersects(mappedRect(bar, content.at(other)));
        }
    }
    check(allHaveGeometry && allFullyVisible && noOverlap,
          QStringLiteral(
              "the 798x360 task bar keeps production guidance and three text actions non-zero, separate, and fully visible"));
    check(review->text() == state.reviewText
              && connect->text() == state.connectText
              && discard->text() == state.discardText
              && review->accessibleName() == state.reviewText
              && connect->accessibleName() == state.connectText
              && discard->accessibleName() == state.discardText,
          QStringLiteral(
              "visible text and accessible names retain each complete task action"));
    check(bar.preferredFocusTarget() == connect
              && connect->focusPolicy() == Qt::StrongFocus,
          QStringLiteral(
              "task focus prefers the available Connect route"));

    QSignalSpy menuShown(&connectMenu, &QMenu::aboutToShow);
    connect->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    const bool connectReceivedFocus = connect->hasFocus();
    bool connectMenuBecameVisible = false;
    QTimer::singleShot(0, [&] {
        connectMenuBecameVisible = connectMenu.isVisible();
        connectMenu.close();
    });
    QTest::keyClick(connect, Qt::Key_Space);
    QApplication::processEvents();
    check(connectReceivedFocus && menuShown.size() == 1
              && connectMenuBecameVisible,
          QStringLiteral(
              "Space opens the Connect Router menu from the keyboard"));

    bar.setState(EndpointDraftTaskBarState{});
    QApplication::processEvents();
    check(!bar.isVisible() && !title->isVisible()
              && !guidance->isVisible()
              && !review->isVisible()
              && !connect->isVisible()
              && !discard->isVisible()
              && bar.preferredFocusTarget() == nullptr,
          QStringLiteral(
              "an empty task state hides the route and exposes no stale focus target"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    bool fontScaleOk = false;
    const double fontScale = qEnvironmentVariable(
        "FINEPAPER_ENDPOINT_DRAFT_TASK_BAR_FONT_SCALE")
                                 .toDouble(&fontScaleOk);
    if (fontScaleOk && fontScale > 0.0) {
        QFont font = application.font();
        if (font.pointSizeF() > 0.0) {
            font.setPointSizeF(font.pointSizeF() * fontScale);
        } else if (font.pixelSize() > 0) {
            font.setPixelSize(qRound(font.pixelSize() * fontScale));
        }
        application.setFont(font);
    }

    draftTextDistinguishesLifecycleImpact();
    narrowTaskBarKeepsActionsVisibleAndKeyboardReachable();

    QTextStream(stdout)
        << (failures == 0
                ? "endpoint-draft-task-bar tests passed"
                : "endpoint-draft-task-bar tests failed")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
