#include "commandmanager.h"

void CommandManager::executeCommand(std::unique_ptr<Command> command) {
    command->execute();
    m_undoStack.push(std::move(command));
    while (!m_redoStack.empty()) {
        m_redoStack.pop();
    }
}

void CommandManager::undo() {
    if (!canUndo()) return;
    auto command = std::move(m_undoStack.top());
    m_undoStack.pop();
    if (command->wasExecuted()) {
        command->undo();
    }
    m_redoStack.push(std::move(command));
}

void CommandManager::redo() {
    if (!canRedo()) return;
    auto command = std::move(m_redoStack.top());
    m_redoStack.pop();
    if (command->wasExecuted()) {
        command->execute();
    }
    m_undoStack.push(std::move(command));
}
