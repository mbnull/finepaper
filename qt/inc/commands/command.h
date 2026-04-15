// Command interface for undo/redo pattern
#pragma once

class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    bool wasExecuted() const { return m_executed; }

protected:
    bool m_executed = false;
};
