// Command interface for undo/redo pattern
#pragma once

class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    bool wasExecuted() const { return m_executed; }
    bool wasUndone() const { return m_undone; }
    bool undoFailureChangedState() const { return m_undoFailureChangedState; }

protected:
    bool m_executed = false;
    bool m_undone = false;
    bool m_undoFailureChangedState = false;
};
