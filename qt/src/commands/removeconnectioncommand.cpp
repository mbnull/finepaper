#include "commands/removeconnectioncommand.h"

RemoveConnectionCommand::RemoveConnectionCommand(Graph* graph, const QString& connectionId)
    : m_graph(graph), m_connectionId(connectionId) {}

void RemoveConnectionCommand::execute() {
    m_connection = m_graph->takeConnection(m_connectionId);
}

void RemoveConnectionCommand::undo() {
    m_graph->insertConnection(std::move(m_connection));
}
