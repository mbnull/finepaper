#pragma once

#include "command.h"
#include <memory>
#include <stack>

class CommandManager {
public:
    void executeCommand(std::unique_ptr<Command> command);
    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

private:
    std::stack<std::unique_ptr<Command>> m_undoStack;
    std::stack<std::unique_ptr<Command>> m_redoStack;
};
