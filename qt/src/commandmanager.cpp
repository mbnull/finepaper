// CommandManager executes commands and maintains undo/redo history.
#include "commandmanager.h"
#include "qdebug.h"
#include <QDebug>

// Execute command and push to undo stack, clearing redo history
void CommandManager::executeCommand(std::unique_ptr<Command> command) {
    qDebug() << "Executing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size();
    command->execute();
    if (command->wasExecuted()) {
        m_undoStack.push(std::move(command));
        while (!m_redoStack.empty()) {
            m_redoStack.pop();
        }
        qDebug() << "Command executed"
                << "undoDepth" << m_undoStack.size()
                << "redoDepth" << m_redoStack.size();
    } else {
        qDebug() << "Command execution produced no state change";
    }
}

void CommandManager::undo() {
    if (!canUndo()) {
        qDebug() << "Undo requested with empty undo stack";
        return;
    }

    qDebug() << "Undoing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size();
    auto command = std::move(m_undoStack.top());
    m_undoStack.pop();
    if (command->wasExecuted()) {
        command->undo();
    }
    m_redoStack.push(std::move(command));
    qInfo() << "Undo complete"
            << "undoDepth" << m_undoStack.size()
            << "redoDepth" << m_redoStack.size();
}

void CommandManager::redo() {
    if (!canRedo()) {
        qDebug() << "Redo requested with empty redo stack";
        return;
    }

    qDebug() << "Redoing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size();
    auto command = std::move(m_redoStack.top());
    m_redoStack.pop();
    if (command->wasExecuted()) {
        command->execute();
    }
    m_undoStack.push(std::move(command));
    qInfo() << "Redo complete"
            << "undoDepth" << m_undoStack.size()
            << "redoDepth" << m_redoStack.size();
}
