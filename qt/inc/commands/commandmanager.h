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
    // Clears undo/redo history, typically after replacing the current document.
    void clearHistory();
    // Returns the current logical history state id for clean/dirty tracking.
    int currentStateId() const { return m_currentStateId; }
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

private:
    struct HistoryEntry {
        std::unique_ptr<Command> command;
        int beforeStateId = 0;
        int afterStateId = 0;
    };

    std::stack<HistoryEntry> m_undoStack;
    std::stack<HistoryEntry> m_redoStack;
    int m_currentStateId = 0;
    int m_nextStateId = 1;
};
