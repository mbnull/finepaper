// CommandManager unit tests for execute/undo/redo stack semantics.
#include "commandmanager.h"

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

void testExecuteUndoRedoLifecycle() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));

    require(manager.canUndo(), "executed command should be undoable");
    require(!manager.canRedo(), "redo stack should be empty after execute");
    require(events.size() == 1 && events[0] == "execute:alpha", "execute should run immediately");

    manager.undo();

    require(!manager.canUndo(), "undo should empty the undo stack");
    require(manager.canRedo(), "undo should populate redo stack");
    require(events.size() == 2 && events[1] == "undo:alpha", "undo should run on the command");

    manager.redo();

    require(manager.canUndo(), "redo should restore undo availability");
    require(!manager.canRedo(), "redo should consume the redo stack");
    require(events.size() == 3 && events[2] == "execute:alpha", "redo should execute the command again");
}

void testRedoHistoryClearsAfterNewExecute() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    manager.undo();
    require(manager.canRedo(), "redo should be available after undo");

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "beta"));

    require(manager.canUndo(), "new command should still be undoable");
    require(!manager.canRedo(), "new execute should clear redo history");
}

void testFailedExecuteDoesNotEnterUndoStack() {
    std::vector<std::string> events;
    CommandManager manager;

    manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha", false));

    require(!manager.canUndo(), "command that never executed should not be undoable");
    require(!manager.canRedo(), "failed execute should not populate redo stack");
    require(events.size() == 1 && events[0] == "execute:alpha", "execute should still have been attempted");
}

} // namespace

int main() {
    try {
        testExecuteUndoRedoLifecycle();
        testRedoHistoryClearsAfterNewExecute();
        testFailedExecuteDoesNotEnterUndoStack();
    } catch (const std::exception& error) {
        std::cerr << "commandmanager_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "commandmanager_test passed\n";
    return 0;
}
