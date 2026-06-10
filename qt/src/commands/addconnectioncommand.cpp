// AddConnectionCommand adds one connection with undo support.
#include "commands/addconnectioncommand.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph,
                                           std::unique_ptr<Connection> connection,
                                           PackageManifestsProvider packageManifestsProvider)
    : m_graph(graph),
      m_packageManifestsProvider(std::move(packageManifestsProvider)),
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
        m_packageManifestsProvider ? m_packageManifestsProvider()
                                   : QVector<IpcraftPackageManifest>{});
    ConnectionRequest request = ConnectionRequest::portToPort(m_connection->source(),
                                                              m_connection->target(),
                                                              ConnectionRequestKind::Programmatic);
    request.connectionClassId = m_connection->connectionClassId();
    const ConnectionCheckResult result = ruleService.check(request);
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
