#include "features/operations/design_run_state.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

using namespace finepaper::operations;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
}

DesignRunState stateWithCurrentDesign() {
    DesignRunState state;
    state.beginSession(QStringLiteral("design-a"));
    state.advanceDesignRevision(QStringLiteral("design-a"));
    return state;
}

void currentRunIsAccepted() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Validation);

    check(state.disposition(ticket) == CompletionDisposition::Current,
          QStringLiteral("an active run for the current stamp is Current"));
    check(state.finishRun(ticket),
          QStringLiteral("the active current ticket can finish"));
    check(!state.hasActiveRun(),
          QStringLiteral("finishing the active ticket clears the active run"));
}

void changedRevisionIsStale() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Validation);
    state.advanceDesignRevision(QStringLiteral("design-a"));

    check(state.disposition(ticket) == CompletionDisposition::StaleRevision,
          QStringLiteral("a run from an earlier design revision is stale"));
    check(state.finishRun(ticket),
          QStringLiteral("a stale but still-active ticket can release the run"));
}

void replacementDesignHasDifferentSession() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Validation);
    state.beginSession(QStringLiteral("design-b"));
    state.advanceDesignRevision(QStringLiteral("design-b"));

    check(state.disposition(ticket) == CompletionDisposition::DifferentSession,
          QStringLiteral("a run from a replaced design has a different session"));
    check(state.hasActiveRun(),
          QStringLiteral("changing sessions does not pretend the worker stopped"));
    check(state.finishRun(ticket),
          QStringLiteral("the active prior-session ticket can release its run"));
    check(!state.hasActiveRun(),
          QStringLiteral("finishing the prior-session worker returns to idle"));
}

void changedCatalogIsStale() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Generation);
    state.advanceCatalogRevision();

    check(state.disposition(ticket) == CompletionDisposition::StaleCatalog,
          QStringLiteral("a run from an earlier Package catalog is stale"));
    check(state.finishRun(ticket),
          QStringLiteral("a catalog-stale active ticket can release the run"));
}

void newerRunSupersedesOlderRun() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket older = state.beginRun(RunKind::Validation);
    const RunTicket newer = state.beginRun(
        RunKind::Generation, QStringLiteral("/tmp/finepaper-output"));

    check(state.disposition(older) == CompletionDisposition::Superseded,
          QStringLiteral("starting a newer run supersedes the older ticket"));
    check(state.disposition(newer) == CompletionDisposition::Current,
          QStringLiteral("the newest ticket remains current"));
}

void onlyExactActiveTicketCanFinish() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket older = state.beginRun(RunKind::Validation);
    const RunTicket active = state.beginRun(
        RunKind::Generation, QStringLiteral("/tmp/finepaper-output"));

    check(!state.finishRun(older),
          QStringLiteral("a superseded ticket cannot finish the active run"));
    check(state.hasActiveRun()
              && state.disposition(active) == CompletionDisposition::Current,
          QStringLiteral("rejecting an old ticket preserves the active run"));

    RunTicket forged = active;
    forged.outputRoot = QStringLiteral("/tmp/forged-output");
    check(!state.finishRun(forged),
          QStringLiteral("matching only the active run id is insufficient"));
    check(state.hasActiveRun(),
          QStringLiteral("rejecting a forged ticket preserves the active run"));

    check(state.finishRun(active),
          QStringLiteral("the exact active ticket can finish"));
    check(!state.finishRun(active),
          QStringLiteral("the same ticket cannot finish twice"));
    check(!state.hasActiveRun(),
          QStringLiteral("duplicate completion leaves the state idle"));
    check(state.disposition(active) == CompletionDisposition::Superseded,
          QStringLiteral("a completed ticket is no longer current"));
}

void cancellationRequestSuppressesPublicationUntilFinish() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Validation);

    check(state.requestCancel(ticket),
          QStringLiteral("the exact active ticket accepts its first cancellation request"));
    check(state.disposition(ticket)
              == CompletionDisposition::CancelRequested,
          QStringLiteral("a cancellation request suppresses publication before the worker finishes"));
    check(state.hasActiveRun(),
          QStringLiteral("requesting cancellation does not pretend the worker has stopped"));
    check(state.finishRun(ticket),
          QStringLiteral("the cancelled active ticket can release the run when its worker finishes"));
    check(!state.hasActiveRun(),
          QStringLiteral("finishing the cancelled worker returns the state to idle"));
}

void cancellationRequestIsIdempotent() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket ticket = state.beginRun(RunKind::Generation);

    check(state.requestCancel(ticket),
          QStringLiteral("the first cancellation request changes active-run state"));
    check(!state.requestCancel(ticket),
          QStringLiteral("a duplicate cancellation request is rejected"));
    check(state.disposition(ticket)
              == CompletionDisposition::CancelRequested,
          QStringLiteral("a duplicate request leaves the run cancellation-requested"));
}

void oldOrForgedTicketCannotCancelActiveRun() {
    DesignRunState state = stateWithCurrentDesign();
    const RunTicket older = state.beginRun(RunKind::Validation);
    const RunTicket active = state.beginRun(
        RunKind::Generation, QStringLiteral("/tmp/finepaper-output"));

    check(!state.requestCancel(older),
          QStringLiteral("a superseded ticket cannot cancel the newer active run"));

    RunTicket forged = active;
    forged.outputRoot = QStringLiteral("/tmp/forged-output");
    check(!state.requestCancel(forged),
          QStringLiteral("a ticket that only shares the active run ID cannot request cancellation"));
    check(state.disposition(active) == CompletionDisposition::Current,
          QStringLiteral("rejected old and forged requests leave the active run current"));
}

void cancellationAndCompletionRaceIsDeterministic() {
    DesignRunState cancelFirst = stateWithCurrentDesign();
    const RunTicket cancelled = cancelFirst.beginRun(RunKind::Validation);
    check(cancelFirst.requestCancel(cancelled)
              && cancelFirst.disposition(cancelled)
                     == CompletionDisposition::CancelRequested
              && cancelFirst.finishRun(cancelled),
          QStringLiteral("when cancellation is handled first, completion remains suppressed and releases the run"));

    DesignRunState finishFirst = stateWithCurrentDesign();
    const RunTicket finished = finishFirst.beginRun(RunKind::Validation);
    check(finishFirst.finishRun(finished),
          QStringLiteral("the worker can finish before a queued cancellation request"));
    check(!finishFirst.requestCancel(finished)
              && finishFirst.disposition(finished)
                     == CompletionDisposition::Superseded,
          QStringLiteral("a cancellation arriving after completion cannot retroactively cancel or publish the run"));
}

void cancellationDispositionUsesDocumentAndActiveRunPrecedence() {
    DesignRunState differentSession = stateWithCurrentDesign();
    const RunTicket previousSession = differentSession.beginRun(
        RunKind::Validation);
    check(differentSession.requestCancel(previousSession),
          QStringLiteral("previous-session fixture accepts cancellation"));
    differentSession.beginSession(QStringLiteral("design-b"));
    check(differentSession.disposition(previousSession)
              == CompletionDisposition::DifferentSession,
          QStringLiteral("a different document session takes precedence over cancellation"));

    DesignRunState changedCatalog = stateWithCurrentDesign();
    const RunTicket cancelledCatalogRun = changedCatalog.beginRun(
        RunKind::Generation);
    check(changedCatalog.requestCancel(cancelledCatalogRun),
          QStringLiteral("catalog fixture accepts cancellation"));
    changedCatalog.advanceCatalogRevision();
    check(changedCatalog.disposition(cancelledCatalogRun)
              == CompletionDisposition::CancelRequested,
          QStringLiteral("an exact active cancellation takes precedence over later catalog staleness"));

    DesignRunState changedRevision = stateWithCurrentDesign();
    const RunTicket cancelledRevisionRun = changedRevision.beginRun(
        RunKind::Validation);
    check(changedRevision.requestCancel(cancelledRevisionRun),
          QStringLiteral("revision fixture accepts cancellation"));
    changedRevision.advanceDesignRevision(QStringLiteral("design-a"));
    check(changedRevision.disposition(cancelledRevisionRun)
              == CompletionDisposition::CancelRequested,
          QStringLiteral("an exact active cancellation takes precedence over later revision staleness"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    currentRunIsAccepted();
    changedRevisionIsStale();
    replacementDesignHasDifferentSession();
    changedCatalogIsStale();
    newerRunSupersedesOlderRun();
    onlyExactActiveTicketCanFinish();
    cancellationRequestSuppressesPublicationUntilFinish();
    cancellationRequestIsIdempotent();
    oldOrForgedTicketCannotCancelActiveRun();
    cancellationAndCompletionRaceIsDeterministic();
    cancellationDispositionUsesDocumentAndActiveRunPrecedence();

    if (failures == 0) {
        QTextStream(stdout) << "All design run state tests passed." << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
