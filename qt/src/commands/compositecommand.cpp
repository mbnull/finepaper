// CompositeCommand executes child commands atomically and undoes in reverse order.
#include "commands/compositecommand.h"

void CompositeCommand::addCommand(std::unique_ptr<Command> command) {
    if (command) {
        m_commands.push_back(std::move(command));
    }
}

void CompositeCommand::execute() {
    m_executed = false;
    m_undone = false;
    m_undoFailureChangedState = false;
    m_executedCount = 0;
    for (auto& command : m_commands) {
        command->execute();
        if (!command->wasExecuted()) {
            for (int index = m_executedCount - 1; index >= 0; --index) {
                auto& rollbackCommand = m_commands.at(static_cast<std::size_t>(index));
                rollbackCommand->undo();
                if (!rollbackCommand->wasUndone()) {
                    m_executed = true;
                    return;
                }
                m_executedCount = index;
            }
            m_executedCount = 0;
            return;
        }
        ++m_executedCount;
    }
    m_executed = m_executedCount > 0;
}

void CompositeCommand::undo() {
    const int originalExecutedCount = m_executedCount;
    const bool wasExecutedBeforeUndo = m_executed;
    m_undone = false;
    m_undoFailureChangedState = false;
    for (int index = originalExecutedCount - 1; index >= 0; --index) {
        auto& command = m_commands.at(static_cast<std::size_t>(index));
        command->undo();
        if (!command->wasUndone()) {
            if (command->undoFailureChangedState()) {
                m_undoFailureChangedState = true;
            }
            for (int rollbackIndex = index + 1; rollbackIndex < originalExecutedCount; ++rollbackIndex) {
                auto& rollbackCommand = m_commands.at(static_cast<std::size_t>(rollbackIndex));
                rollbackCommand->execute();
                if (!rollbackCommand->wasExecuted()) {
                    m_executedCount = rollbackIndex;
                    m_executed = m_executedCount > 0;
                    m_undoFailureChangedState = true;
                    return;
                }
            }

            m_executedCount = originalExecutedCount;
            m_executed = wasExecutedBeforeUndo;
            return;
        }
    }
    m_executedCount = 0;
    m_executed = false;
    m_undone = true;
}
