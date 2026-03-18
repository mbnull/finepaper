#include "commands/removeconnectioncommand.h"

RemoveConnectionCommand::RemoveConnectionCommand(Graph* graph, const QString& connectionId)
    : m_graph(graph), m_connectionId(connectionId) {}

void RemoveConnectionCommand::execute() {
    m_graph->removeConnection(m_connectionId);
}

void RemoveConnectionCommand::undo() {
    m_graph->addConnection(std::move(m_connection));
}
