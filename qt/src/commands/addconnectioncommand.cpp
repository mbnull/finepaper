// AddConnectionCommand adds one connection with undo support.
#include "commands/addconnectioncommand.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph,
                                           ConnectionRuleService* ruleService,
                                           std::unique_ptr<Connection> connection)
    : m_graph(graph),
      m_ruleService(ruleService),
      m_connection(std::move(connection)) {
    m_connectionId = m_connection->id();
}

// Add connection if valid
void AddConnectionCommand::execute() {
    if (!m_graph || !m_connection) {
        return;
    }

    if (m_ruleService) {
        const ConnectionCheckResult result = m_ruleService->check(
            ConnectionRequest::portToPort(m_connection->source(),
                                          m_connection->target(),
                                          ConnectionRequestKind::Programmatic));
        if (!result.hasSingleOption()) {
            return;
        }
    }

    if (!m_graph->isValidConnection(m_connection->source(), m_connection->target())) {
        return;
    }
    m_graph->insertConnection(std::move(m_connection));
    m_executed = true;
}

// Remove connection and restore ownership
void AddConnectionCommand::undo() {
    if (!m_graph) {
        return;
    }
    m_connection = m_graph->takeConnection(m_connectionId);
}
