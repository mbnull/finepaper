#include "commands/addconnectioncommand.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph, std::unique_ptr<Connection> connection)
    : m_graph(graph), m_connection(std::move(connection)) {
    m_connectionId = m_connection->id();
}

void AddConnectionCommand::execute() {
    if (!m_graph->isValidConnection(m_connection->source(), m_connection->target())) {
        return;
    }
    m_graph->insertConnection(std::move(m_connection));
    m_executed = true;
}

void AddConnectionCommand::undo() {
    m_connection = m_graph->takeConnection(m_connectionId);
}
