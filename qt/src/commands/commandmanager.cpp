// CommandManager executes commands and maintains undo/redo history.
#include "commands/commandmanager.h"
#include "qdebug.h"
#include <QDebug>

// Execute command and push to undo stack, clearing redo history
std::unique_ptr<Command> CommandManager::executeCommand(std::unique_ptr<Command> command) {
    qDebug() << "Executing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size()
             << "stateId" << m_currentStateId;
    command->execute();
    if (command->wasExecuted()) {
        HistoryEntry entry;
        entry.command = std::move(command);
        entry.beforeStateId = m_currentStateId;
        entry.afterStateId = m_nextStateId++;
        m_currentStateId = entry.afterStateId;
        m_undoStack.push_back(std::move(entry));
        trimUndoStack();
        m_redoStack.clear();
        qDebug() << "Command executed"
                << "undoDepth" << m_undoStack.size()
                << "redoDepth" << m_redoStack.size()
                << "stateId" << m_currentStateId;
        return nullptr;
    } else {
        qDebug() << "Command execution produced no state change";
        return command;
    }
}

void CommandManager::undo() {
    if (!canUndo()) {
        qDebug() << "Undo requested with empty undo stack";
        return;
    }

    qDebug() << "Undoing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size()
             << "stateId" << m_currentStateId;
    auto entry = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    if (entry.command->wasExecuted()) {
        entry.command->undo();
    }
    if (!entry.command->wasUndone()) {
        if (entry.command->undoFailureChangedState()) {
            m_currentStateId = m_nextStateId++;
        }
        m_undoStack.push_back(std::move(entry));
        qDebug() << "Undo rejected or failed"
                 << "undoDepth" << m_undoStack.size()
                 << "redoDepth" << m_redoStack.size()
                 << "stateId" << m_currentStateId;
        return;
    }
    m_currentStateId = entry.beforeStateId;
    m_redoStack.push_back(std::move(entry));
    qInfo() << "Undo complete"
            << "undoDepth" << m_undoStack.size()
            << "redoDepth" << m_redoStack.size()
            << "stateId" << m_currentStateId;
}

void CommandManager::redo() {
    if (!canRedo()) {
        qDebug() << "Redo requested with empty redo stack";
        return;
    }

    qDebug() << "Redoing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size()
             << "stateId" << m_currentStateId;
    auto entry = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    entry.command->execute();
    if (!entry.command->wasExecuted()) {
        m_redoStack.push_back(std::move(entry));
        qDebug() << "Redo produced no state change"
                 << "undoDepth" << m_undoStack.size()
                 << "redoDepth" << m_redoStack.size()
                 << "stateId" << m_currentStateId;
        return;
    }
    m_currentStateId = entry.afterStateId;
    m_undoStack.push_back(std::move(entry));
    trimUndoStack();
    qInfo() << "Redo complete"
            << "undoDepth" << m_undoStack.size()
            << "redoDepth" << m_redoStack.size()
            << "stateId" << m_currentStateId;
}

void CommandManager::clearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
    m_currentStateId = 0;
    m_nextStateId = 1;

    qInfo() << "Cleared command history";
}

void CommandManager::trimUndoStack() {
    while (m_undoStack.size() > kMaxUndoDepth) {
        m_undoStack.pop_front();
    }
}
