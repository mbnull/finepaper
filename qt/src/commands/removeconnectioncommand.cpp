// RemoveConnectionCommand removes one connection and restores it on undo.
#include "commands/removeconnectioncommand.h"

#include "project/editormutationtarget.h"

RemoveConnectionCommand::RemoveConnectionCommand(Graph* graph,
                                                 const QString& connectionId,
                                                 EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_connectionId(connectionId),
      m_editorMutationTarget(editorMutationTarget) {}

// Remove connection from graph
void RemoveConnectionCommand::execute() {
    m_executed = false;
    m_undone = false;
    m_connection = m_graph->takeConnection(m_connectionId);
    if (m_connection) {
        if (m_editorMutationTarget) {
            m_editorMutationTarget->removeEditorConnectionRecord(m_connectionId);
        }
        m_executed = true;
    }
}

// Restore connection to graph
void RemoveConnectionCommand::undo() {
    m_undone = false;
    if (!m_graph || !m_connection) {
        return;
    }
    if (!m_graph->isValidConnection(m_connection->source(), m_connection->target())) {
        return;
    }

    const qsizetype before = static_cast<qsizetype>(m_graph->connections().size());
    m_graph->insertConnection(m_connection->clone());
    const qsizetype after = static_cast<qsizetype>(m_graph->connections().size());
    if (after == before + 1) {
        m_connection.reset();
        if (m_editorMutationTarget) {
            if (const Connection* connection = m_graph->getConnection(m_connectionId)) {
                m_editorMutationTarget->upsertEditorConnectionRecord(*connection);
            }
        }
        m_undone = true;
    }
}
