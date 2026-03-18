#include "commands/addconnectioncommand.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph, std::unique_ptr<Connection> connection)
    : m_graph(graph), m_connection(std::move(connection)) {
    m_connectionId = m_connection->id();
}

void AddConnectionCommand::execute() {
    m_graph->addConnection(std::move(m_connection));
}

void AddConnectionCommand::undo() {
    m_graph->removeConnection(m_connectionId);
}
