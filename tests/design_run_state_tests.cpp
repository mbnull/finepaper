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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    currentRunIsAccepted();
    changedRevisionIsStale();
    replacementDesignHasDifferentSession();
    changedCatalogIsStale();
    newerRunSupersedesOlderRun();
    onlyExactActiveTicketCanFinish();

    if (failures == 0) {
        QTextStream(stdout) << "All design run state tests passed." << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
