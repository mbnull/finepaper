// CommandManager handles undo/redo stack for graph operations
#pragma once

#include "commands/command.h"
#include <memory>
#include <stack>

class CommandManager {
public:
    // Executes a command; if it changes state, it is pushed to undo and redo history is cleared.
    void executeCommand(std::unique_ptr<Command> command);
    // Reverts the most recent executed command.
    void undo();
    // Re-applies the most recently undone command.
    void redo();
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

private:
    std::stack<std::unique_ptr<Command>> m_undoStack;
    std::stack<std::unique_ptr<Command>> m_redoStack;
};
