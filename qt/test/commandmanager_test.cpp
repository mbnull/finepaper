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
        m_executed = m_executeSucceeds;
    }

    void undo() override {
        m_events.push_back("undo:" + m_name);
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
        m_executed = false;
    }

private:
    std::vector<std::string>& m_events;
    int m_attempts = 0;
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
        testCompositeCommandUndoRunsChildrenInReverseOrder();
        testCompositeCommandRollsBackExecutedChildrenOnFailure();
    } catch (const std::exception& error) {
        std::cerr << "commandmanager_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "commandmanager_test passed\n";
    return 0;
}
