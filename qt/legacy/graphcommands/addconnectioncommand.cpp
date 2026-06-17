// AddConnectionCommand adds one connection with undo support.
#include "legacy/graphcommands/addconnectioncommand.h"

#include "project/editormutationtarget.h"

AddConnectionCommand::AddConnectionCommand(Graph* graph,
                                           std::unique_ptr<Connection> connection,
                                           PackageManifestsProvider packageManifestsProvider,
                                           EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_packageManifestsProvider(std::move(packageManifestsProvider)),
      m_connection(std::move(connection)),
      m_editorMutationTarget(editorMutationTarget) {
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
    Connection* insertedConnection = m_connection.get();
    m_graph->insertConnection(std::move(m_connection));
    if (m_editorMutationTarget) {
        if (Connection* graphConnection = m_graph->getConnection(m_connectionId)) {
            insertedConnection = graphConnection;
        }
        if (!insertedConnection ||
            !m_editorMutationTarget->upsertEditorConnectionRecord(*insertedConnection)) {
            m_connection = m_graph->takeConnection(m_connectionId);
            return;
        }
    }
    m_executed = true;
}

// Remove connection and restore ownership
void AddConnectionCommand::undo() {
    m_undone = false;
    if (!m_graph || m_connectionId.isEmpty() || !m_graph->getConnection(m_connectionId)) {
        return;
    }
    if (m_editorMutationTarget &&
        !m_editorMutationTarget->removeEditorConnectionRecord(m_connectionId)) {
        return;
    }

    m_connection = m_graph->takeConnection(m_connectionId);
    if (m_connection) {
        m_undone = true;
    }
}
