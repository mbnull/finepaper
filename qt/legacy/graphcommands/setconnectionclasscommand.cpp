// SetConnectionClassCommand updates connection class metadata with undo support.
#include "legacy/graphcommands/setconnectionclasscommand.h"

#include "graph/graph.h"
#include "project/editormutationtarget.h"

#include <utility>

SetConnectionClassCommand::SetConnectionClassCommand(Graph* graph,
                                                     QString connectionId,
                                                     QString connectionClassId,
                                                     EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_connectionId(std::move(connectionId)),
      m_newConnectionClassId(std::move(connectionClassId)),
      m_editorMutationTarget(editorMutationTarget) {}

void SetConnectionClassCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_graph || m_connectionId.isEmpty() || m_newConnectionClassId.isEmpty()) {
        return;
    }

    Connection* connection = m_graph->getConnection(m_connectionId);
    if (!connection) {
        return;
    }

    const QStringList alternatives = connection->alternatives();
    if (!alternatives.contains(m_newConnectionClassId)) {
        return;
    }

    m_oldConnectionClassId = connection->connectionClassId();
    m_oldStatus = connection->status();
    m_oldAlternatives = alternatives;
    if (m_oldConnectionClassId == m_newConnectionClassId &&
        m_oldStatus == QStringLiteral("valid") &&
        m_oldAlternatives.isEmpty()) {
        return;
    }

    if (m_editorMutationTarget) {
        std::unique_ptr<Connection> durableConnection = connection->clone();
        durableConnection->setConnectionMetadata(m_newConnectionClassId,
                                                 QStringLiteral("valid"),
                                                 {});
        if (!m_editorMutationTarget->upsertEditorConnectionRecord(*durableConnection)) {
            return;
        }
    }

    m_executed = m_graph->setConnectionMetadata(m_connectionId,
                                                m_newConnectionClassId,
                                                QStringLiteral("valid"),
                                                {});
    if (!m_executed && m_editorMutationTarget) {
        const bool restored = m_editorMutationTarget->upsertEditorConnectionRecord(*connection);
        Q_UNUSED(restored);
    }
}

void SetConnectionClassCommand::undo() {
    m_undone = false;
    if (!m_graph || m_connectionId.isEmpty()) {
        return;
    }

    Connection* connection = m_graph->getConnection(m_connectionId);
    if (!connection) {
        return;
    }
    if (m_editorMutationTarget) {
        std::unique_ptr<Connection> durableConnection = connection->clone();
        durableConnection->setConnectionMetadata(m_oldConnectionClassId,
                                                 m_oldStatus,
                                                 m_oldAlternatives);
        if (!m_editorMutationTarget->upsertEditorConnectionRecord(*durableConnection)) {
            return;
        }
    }

    m_undone = m_graph->setConnectionMetadata(m_connectionId,
                                              m_oldConnectionClassId,
                                              m_oldStatus,
                                              m_oldAlternatives);
    if (!m_undone && m_editorMutationTarget) {
        const bool restored = m_editorMutationTarget->upsertEditorConnectionRecord(*connection);
        Q_UNUSED(restored);
    }
}
