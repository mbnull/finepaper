// AddConnectionCommand adds one connection with undo support.
#include "commands/addconnectioncommand.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph,
                                           IpInstanceRecordsProvider ipInstanceRecordsProvider,
                                           std::unique_ptr<Connection> connection)
    : m_graph(graph),
      m_ipInstanceRecordsProvider(std::move(ipInstanceRecordsProvider)),
      m_connection(std::move(connection)) {
    m_connectionId = m_connection->id();
}

// Add connection if valid
void AddConnectionCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_graph || !m_connection) {
        return;
    }

    ConnectionRuleService ruleService(
        m_graph,
        m_ipInstanceRecordsProvider ? m_ipInstanceRecordsProvider()
                                    : QVector<ProjectIpInstanceRecord>{});
    const ConnectionCheckResult result = ruleService.check(
        ConnectionRequest::portToPort(m_connection->source(),
                                      m_connection->target(),
                                      ConnectionRequestKind::Programmatic));
    if (!result.hasSingleOption()) {
        return;
    }

    if (!m_graph->isValidConnection(m_connection->source(), m_connection->target())) {
        return;
    }
    m_graph->insertConnection(std::move(m_connection));
    m_executed = true;
}

// Remove connection and restore ownership
void AddConnectionCommand::undo() {
    m_undone = false;
    if (!m_graph) {
        return;
    }
    m_connection = m_graph->takeConnection(m_connectionId);
    if (m_connection) {
        m_undone = true;
    }
}
