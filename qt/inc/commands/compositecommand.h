// CompositeCommand groups child commands into one undoable user action.
#pragma once

#include "commands/command.h"

#include <memory>
#include <vector>

class CompositeCommand : public Command {
public:
    void addCommand(std::unique_ptr<Command> command);
    void execute() override;
    void undo() override;

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    int m_executedCount = 0;
};
