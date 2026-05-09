// CompositeCommand executes child commands atomically and undoes in reverse order.
#include "commands/compositecommand.h"

void CompositeCommand::addCommand(std::unique_ptr<Command> command) {
    if (command) {
        m_commands.push_back(std::move(command));
    }
}

void CompositeCommand::execute() {
    m_executed = false;
    m_executedCount = 0;
    for (auto& command : m_commands) {
        command->execute();
        if (!command->wasExecuted()) {
            for (int index = m_executedCount - 1; index >= 0; --index) {
                m_commands.at(static_cast<std::size_t>(index))->undo();
            }
            m_executedCount = 0;
            return;
        }
        ++m_executedCount;
    }
    m_executed = m_executedCount > 0;
}

void CompositeCommand::undo() {
    for (int index = m_executedCount - 1; index >= 0; --index) {
        m_commands.at(static_cast<std::size_t>(index))->undo();
    }
    m_executedCount = 0;
    m_executed = false;
}
