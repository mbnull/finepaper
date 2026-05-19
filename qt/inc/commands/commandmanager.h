// CommandManager handles undo/redo stack for graph operations
#pragma once

#include "commands/command.h"
#include <cstddef>
#include <deque>
#include <memory>

class CommandManager {
public:
    // Executes a command; accepted commands enter history, rejected commands are returned for inspection.
    std::unique_ptr<Command> executeCommand(std::unique_ptr<Command> command);
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
    static constexpr std::size_t kMaxUndoDepth = 256;

    struct HistoryEntry {
        std::unique_ptr<Command> command;
        int beforeStateId = 0;
        int afterStateId = 0;
    };

    void trimUndoStack();

    std::deque<HistoryEntry> m_undoStack;
    std::deque<HistoryEntry> m_redoStack;
    int m_currentStateId = 0;
    int m_nextStateId = 1;
};
