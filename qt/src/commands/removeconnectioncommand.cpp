#include "commands/removeconnectioncommand.h"

RemoveConnectionCommand::RemoveConnectionCommand(Graph* graph, const QString& connectionId)
    : m_graph(graph), m_connectionId(connectionId) {}

// Remove connection from graph
void RemoveConnectionCommand::execute() {
    m_connection = m_graph->takeConnection(m_connectionId);
    if (m_connection) {
        m_executed = true;
    }
}

// Restore connection to graph
void RemoveConnectionCommand::undo() {
    if (m_connection) {
        m_graph->insertConnection(std::move(m_connection));
    }
}
