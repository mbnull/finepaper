// RemoveConnectionCommand removes one connection and restores it on undo.
#include "commands/removeconnectioncommand.h"

RemoveConnectionCommand::RemoveConnectionCommand(Graph* graph, const QString& connectionId)
    : m_graph(graph), m_connectionId(connectionId) {}

// Remove connection from graph
void RemoveConnectionCommand::execute() {
    m_executed = false;
    m_undone = false;
    m_connection = m_graph->takeConnection(m_connectionId);
    if (m_connection) {
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
    m_graph->insertConnection(std::make_unique<Connection>(
        m_connection->id(),
        m_connection->source(),
        m_connection->target()));
    const qsizetype after = static_cast<qsizetype>(m_graph->connections().size());
    if (after == before + 1) {
        m_connection.reset();
        m_undone = true;
    }
}
