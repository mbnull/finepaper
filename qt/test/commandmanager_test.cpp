// CommandManager unit tests for execute/undo/redo stack semantics.
#include "commands/compositecommand.h"
#include "commands/commandmanager.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class RecordingCommand final : public Command {
public:
    RecordingCommand(std::vector<std::string>& events, std::string name, bool executeSucceeds = true)
        : m_events(events), m_name(std::move(name)), m_executeSucceeds(executeSucceeds) {
    }

    void execute() override {
        m_events.push_back("execute:" + m_name);
        m_undone = false;
        m_executed = m_executeSucceeds;
    }

    void undo() override {
        m_events.push_back("undo:" + m_name);
        m_undone = true;
    }

private:
    std::vector<std::string>& m_events;
    std::string m_name;
    bool m_executeSucceeds;
};

class FailsOnRedoCommand final : public Command {
public:
    explicit FailsOnRedoCommand(std::vector<std::string>& events)
        : m_events(events) {
    }

    void execute() override {
        ++m_attempts;
        m_undone = false;
        if (m_attempts == 1) {
            m_events.push_back("execute:first");
            m_executed = true;
            return;
        }

        m_events.push_back("execute:rejected");
        m_executed = false;
    }

    void undo() override {
        m_events.push_back("undo");
        m_undone = true;
        m_executed = false;
    }

private:
    std::vector<std::string>& m_events;
    int m_attempts = 0;
};

class FailsOnUndoCommand final : public Command {
public:
    explicit FailsOnUndoCommand(std::vector<std::string>& events)
        : m_events(events) {
    }

    void execute() override {
        m_events.push_back("execute");
        m_undone = false;
        m_executed = true;
    }

    void undo() override {
        m_events.push_back("undo:rejected");
        m_undone = false;
    }

private:
    std::vector<std::string>& m_events;
};

class FailsFirstUndoThenSucceedsCommand final : public Command {
public:
    explicit FailsFirstUndoThenSucceedsCommand(std::vector<std::string>& events)
        : m_events(events) {
    }

    void execute() override {
        m_events.push_back("execute:retryable");
        m_undone = false;
        m_executed = true;
    }

    void undo() override {
        ++m_undoAttempts;
        if (m_undoAttempts == 1) {
            m_events.push_back("undo:retryable:rejected");
            m_undone = false;
            return;
        }

        m_events.push_back("undo:retryable");
        m_undone = true;
        m_executed = false;
    }

private:
    std::vector<std::string>& m_events;
    int m_undoAttempts = 0;
};

class FailsReexecuteAfterUndoCommand final : public Command {
public:
    explicit FailsReexecuteAfterUndoCommand(std::vector<std::string>& events)
        : m_events(events) {
    }

    void execute() override {
        ++m_executeAttempts;
        m_undone = false;
        if (m_executeAttempts == 1) {
            m_events.push_back("execute:rollback");
            m_executed = true;
            return;
        }

        m_events.push_back("execute:rollback:rejected");
        m_executed = false;
    }

    void undo() override {
        m_events.push_back("undo:rollback");
        m_undone = true;
        m_executed = false;
    }

private:
    std::vector<std::string>& m_events;
    int m_executeAttempts = 0;
};

class PartialUndoFailureCommand final : public Command {
public:
    explicit PartialUndoFailureCommand(std::vector<std::string>& events)
        : m_events(events) {
    }

    void execute() override {
        m_events.push_back("execute:partial");
        m_undone = false;
        m_undoFailureChangedState = false;
        m_executed = true;
    }

    void undo() override {
        m_events.push_back("undo:partial:rejected");
        m_undone = false;
        m_undoFailureChangedState = true;
    }

private:
    std::vector<std::string>& m_events;
};

void testExecuteUndoRedoLifecycle() {
    std::vector<std::string> events;
    CommandManager manager;
    require(manager.currentStateId() == 0, "fresh history should start at state 0");

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));

    require(manager.canUndo(), "executed command should be undoable");
    require(!manager.canRedo(), "redo stack should be empty after execute");
    require(manager.currentStateId() != 0, "execute should advance state id");
    require(events.size() == 1 && events[0] == "execute:alpha", "execute should run immediately");

    const int executedStateId = manager.currentStateId();

    manager.undo();

    require(!manager.canUndo(), "undo should empty the undo stack");
    require(manager.canRedo(), "undo should populate redo stack");
    require(manager.currentStateId() == 0, "undo should restore the original state id");
    require(events.size() == 2 && events[1] == "undo:alpha", "undo should run on the command");

    manager.redo();

    require(manager.canUndo(), "redo should restore undo availability");
    require(!manager.canRedo(), "redo should consume the redo stack");
    require(manager.currentStateId() == executedStateId, "redo should restore the executed state id");
    require(events.size() == 3 && events[2] == "execute:alpha", "redo should execute the command again");
}

void testRedoHistoryClearsAfterNewExecute() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    const int alphaStateId = manager.currentStateId();
    manager.undo();
    require(manager.canRedo(), "redo should be available after undo");

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "beta"));

    require(manager.canUndo(), "new command should still be undoable");
    require(!manager.canRedo(), "new execute should clear redo history");
    require(manager.currentStateId() != alphaStateId,
            "new execute after undo should create a distinct history state");
}

void testFailedExecuteDoesNotEnterUndoStack() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha", false));

    require(!manager.canUndo(), "command that never executed should not be undoable");
    require(!manager.canRedo(), "failed execute should not populate redo stack");
    require(manager.currentStateId() == 0, "failed execute should leave state id unchanged");
    require(events.size() == 1 && events[0] == "execute:alpha", "execute should still have been attempted");
}

void testClearHistoryDropsUndoAndRedoStacks() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    manager.undo();
    require(manager.canRedo(), "redo should exist before clearHistory");

    manager.clearHistory();

    require(!manager.canUndo(), "clearHistory should empty undo stack");
    require(!manager.canRedo(), "clearHistory should empty redo stack");
    require(manager.currentStateId() == 0, "clearHistory should reset the current state id");
}

void testRedoFailureDoesNotAdvanceHistory() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<FailsOnRedoCommand>(events));
    manager.undo();
    require(manager.canRedo(), "undo should make command redoable");
    require(manager.currentStateId() == 0, "undo should restore initial state");

    manager.redo();

    require(manager.currentStateId() == 0, "failed redo should not advance current state");
    require(!manager.canUndo(), "failed redo should not push command back to undo");
    require(manager.canRedo(), "failed redo should keep command available for retry");
    require(events.size() == 3 && events[2] == "execute:rejected",
            "redo should attempt execution and observe rejection");
}

void testExecuteCommandReturnsRejectedCommandOnFailure() {
    std::vector<std::string> events;
    CommandManager manager;

    std::unique_ptr<Command> rejected =
        manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha", false));

    require(rejected != nullptr, "failed command should be returned to caller");
    require(!rejected->wasExecuted(), "returned failed command should expose failed status");
    require(!manager.canUndo(), "failed command should not enter undo history");
    require(manager.currentStateId() == 0, "failed command should not advance state");
}

void testExecuteCommandReturnsNullOnAcceptedCommand() {
    std::vector<std::string> events;
    CommandManager manager;

    std::unique_ptr<Command> rejected =
        manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));

    require(rejected == nullptr, "accepted command should not be returned");
    require(manager.canUndo(), "accepted command should enter undo history");
    require(manager.currentStateId() != 0, "accepted command should advance state");
}

void testUndoFailureLeavesHistoryAndStateUnchanged() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<FailsOnUndoCommand>(events));
    const int executedStateId = manager.currentStateId();
    require(manager.canUndo(), "accepted command should be undoable before failed undo");
    require(!manager.canRedo(), "redo should be empty before failed undo");

    manager.undo();

    require(manager.canUndo(), "failed undo should leave command on undo stack");
    require(!manager.canRedo(), "failed undo should not move command to redo stack");
    require(manager.currentStateId() == executedStateId,
            "failed undo should leave current state id unchanged");
    require(events.size() == 2 && events[1] == "undo:rejected",
            "failed undo should still attempt the undo operation");
}

void testCompositeCommandUndoRunsChildrenInReverseOrder() {
    std::vector<std::string> events;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta"));

    composite->execute();
    require(composite->wasExecuted(), "composite command should execute when all children execute");
    composite->undo();

    require(events.size() == 4, "composite should record two executes and two undos");
    require(events[0] == "execute:alpha", "first child should execute first");
    require(events[1] == "execute:beta", "second child should execute second");
    require(events[2] == "undo:beta", "second child should undo first");
    require(events[3] == "undo:alpha", "first child should undo last");
}

void testCompositeUndoFailureLeavesHistoryAndStateUnchanged() {
    std::vector<std::string> events;
    CommandManager manager;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<FailsOnUndoCommand>(events));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta"));

    manager.executeCommand(std::move(composite));
    const int executedStateId = manager.currentStateId();
    require(manager.canUndo(), "executed composite should be undoable before a failed child undo");
    require(!manager.canRedo(), "redo should be empty before a failed composite undo");

    manager.undo();

    require(manager.canUndo(), "failed composite undo should leave the command on the undo stack");
    require(!manager.canRedo(), "failed composite undo should not populate redo");
    require(manager.currentStateId() == executedStateId,
            "failed composite undo should leave current state id unchanged");
    require(events.size() == 5,
            "composite undo failure should record child undo, rejection, and rollback execute");
    require(events[2] == "undo:beta", "successful later child should still undo first");
    require(events[3] == "undo:rejected", "failed earlier child should report undo rejection");
    require(events[4] == "execute:beta", "successful later child should be re-executed during rollback");
}

void testCompositeUndoFailureRollsBackAndRetryUndoesLaterChildAgain() {
    std::vector<std::string> events;
    CommandManager manager;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<FailsFirstUndoThenSucceedsCommand>(events));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta"));

    manager.executeCommand(std::move(composite));
    const int executedStateId = manager.currentStateId();

    manager.undo();

    require(manager.canUndo(), "first failed composite undo should leave undo available");
    require(!manager.canRedo(), "first failed composite undo should not populate redo");
    require(manager.currentStateId() == executedStateId,
            "first failed composite undo should leave history state unchanged");
    require(events.size() == 5,
            "first failed composite undo should run later child, reject earlier child, and roll back");
    require(events[2] == "undo:beta", "later child should undo during the first attempt");
    require(events[3] == "undo:retryable:rejected",
            "earlier child should reject the first undo attempt");
    require(events[4] == "execute:beta",
            "later child should be re-executed to restore the pre-undo state");

    manager.undo();

    require(!manager.canUndo(), "second undo attempt should finish the composite undo");
    require(manager.canRedo(), "successful retry should move the composite command to redo");
    require(manager.currentStateId() == 0, "successful retry should rewind history");
    require(events.size() == 7, "successful retry should undo both children after rollback restored state");
    require(events[5] == "undo:beta", "retry should undo the later child again");
    require(events[6] == "undo:retryable", "retry should undo the earlier child after the later child");
}

void testCompositeExecuteRollbackFailureEntersHistoryForRetry() {
    std::vector<std::string> events;
    CommandManager manager;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<FailsFirstUndoThenSucceedsCommand>(events));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta", false));

    std::unique_ptr<Command> rejected = manager.executeCommand(std::move(composite));

    require(rejected == nullptr, "partial rollback failure should keep the composite accepted");
    require(manager.canUndo(), "partial rollback failure should leave the composite undoable");
    require(!manager.canRedo(), "partial rollback failure should not create redo history");
    require(manager.currentStateId() != 0, "partial rollback failure should advance history because state remains applied");
    require(events.size() == 3, "execute rollback should attempt the child undo once");
    require(events[0] == "execute:retryable", "first child should execute before the later rejection");
    require(events[1] == "execute:beta", "later child should attempt execute and reject");
    require(events[2] == "undo:retryable:rejected",
            "rollback should record the rejected child undo");

    manager.undo();

    require(!manager.canUndo(), "retrying undo after execute rollback failure should finish cleanup");
    require(manager.canRedo(), "successful retry should move the composite to redo");
    require(manager.currentStateId() == 0, "successful retry should rewind history");
    require(events.size() == 4, "successful retry should only need one more child undo");
    require(events[3] == "undo:retryable", "retry should resume at the still-executed child only");
}

void testCompositeUndoRollbackFailureMarksPartialState() {
    std::vector<std::string> events;
    CommandManager manager;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<FailsFirstUndoThenSucceedsCommand>(events));
    composite->addCommand(std::make_unique<FailsReexecuteAfterUndoCommand>(events));

    manager.executeCommand(std::move(composite));
    const int executedStateId = manager.currentStateId();

    manager.undo();

    require(manager.canUndo(), "partial composite undo should remain undoable");
    require(!manager.canRedo(), "partial composite undo should not populate redo");
    require(manager.currentStateId() != executedStateId,
            "partial composite undo should move to a distinct partial-state id");
    require(manager.currentStateId() != 0,
            "partial composite undo should not look like the before state");
    require(events.size() == 5, "partial composite undo should record undo failure and rollback failure");
    require(events[2] == "undo:rollback", "later child should undo before the earlier failure");
    require(events[3] == "undo:retryable:rejected",
            "earlier child should reject the first undo attempt");
    require(events[4] == "execute:rollback:rejected",
            "rollback should try and fail to re-execute the later child");

    manager.undo();

    require(!manager.canUndo(), "retry should finish undoing the remaining partial state");
    require(manager.canRedo(), "successful retry should move command to redo");
    require(manager.currentStateId() == 0, "successful retry should restore the before state id");
    require(events.size() == 6, "retry should only undo the still-executed earlier child");
    require(events[5] == "undo:retryable",
            "retry should resume at the earlier child after rollback re-execute failed");
}

void testCompositeUndoPropagatesChildPartialStateAfterSuccessfulRollback() {
    std::vector<std::string> events;
    CommandManager manager;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<PartialUndoFailureCommand>(events));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "rollback"));

    manager.executeCommand(std::move(composite));
    const int executedStateId = manager.currentStateId();

    manager.undo();

    require(manager.canUndo(), "failed composite undo should stay undoable");
    require(!manager.canRedo(), "failed composite undo should not populate redo");
    require(manager.currentStateId() != executedStateId,
            "child partial-state failure should advance to a distinct state id");
    require(manager.currentStateId() != 0,
            "child partial-state failure should not look like the original state");
    require(events.size() == 5,
            "composite should undo later child, observe partial failure, and roll back later child");
    require(events[2] == "undo:rollback", "later child should undo first");
    require(events[3] == "undo:partial:rejected",
            "earlier child should report a partial-state undo failure");
    require(events[4] == "execute:rollback",
            "successful rollback should re-execute the later child");
}

void testCompositeCommandRollsBackExecutedChildrenOnFailure() {
    std::vector<std::string> events;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta", false));

    composite->execute();

    require(!composite->wasExecuted(), "composite should fail when a child fails");
    require(events.size() == 3, "composite should roll back the executed child");
    require(events[0] == "execute:alpha", "first child should execute");
    require(events[1] == "execute:beta", "second child should attempt execute");
    require(events[2] == "undo:alpha", "first child should roll back");
}

} // namespace

int main() {
    try {
        testExecuteUndoRedoLifecycle();
        testRedoHistoryClearsAfterNewExecute();
        testFailedExecuteDoesNotEnterUndoStack();
        testClearHistoryDropsUndoAndRedoStacks();
        testRedoFailureDoesNotAdvanceHistory();
        testExecuteCommandReturnsRejectedCommandOnFailure();
        testExecuteCommandReturnsNullOnAcceptedCommand();
        testUndoFailureLeavesHistoryAndStateUnchanged();
        testCompositeCommandUndoRunsChildrenInReverseOrder();
        testCompositeUndoFailureLeavesHistoryAndStateUnchanged();
        testCompositeUndoFailureRollsBackAndRetryUndoesLaterChildAgain();
        testCompositeExecuteRollbackFailureEntersHistoryForRetry();
        testCompositeUndoRollbackFailureMarksPartialState();
        testCompositeUndoPropagatesChildPartialStateAfterSuccessfulRollback();
        testCompositeCommandRollsBackExecutedChildrenOnFailure();
    } catch (const std::exception& error) {
        std::cerr << "commandmanager_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "commandmanager_test passed\n";
    return 0;
}
