// RemoveModuleCommand removes one module plus incident edges and restores them on undo.
#include "commands/removemodulecommand.h"

#include "project/editormutationtarget.h"

namespace {

const Module* findRestoredOrExistingModule(const Graph* graph,
                                           const Module* restoredModule,
                                           const QString& moduleId) {
    if (restoredModule && restoredModule->id() == moduleId) {
        return restoredModule;
    }
    return graph ? graph->getModule(moduleId) : nullptr;
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }

    for (const auto& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

bool graphHasConnection(const Graph* graph, const PortRef& source, const PortRef& target) {
    return std::any_of(graph->connections().begin(), graph->connections().end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return connection &&
                   connection->source().moduleId == source.moduleId &&
                   connection->source().portId == source.portId &&
                   connection->target().moduleId == target.moduleId &&
                   connection->target().portId == target.portId;
        });
}

bool canRestoreConnection(const Graph* graph,
                          const Module* restoredModule,
                          const Connection* connection) {
    if (!graph || !restoredModule || !connection) {
        return false;
    }

    const PortRef source = connection->source();
    const PortRef target = connection->target();
    if (source.moduleId == target.moduleId) {
        return false;
    }

    const Module* sourceModule = findRestoredOrExistingModule(graph, restoredModule, source.moduleId);
    const Module* targetModule = findRestoredOrExistingModule(graph, restoredModule, target.moduleId);
    if (!sourceModule || !targetModule) {
        return false;
    }
    if (!findPort(sourceModule, source.portId) || !findPort(targetModule, target.portId)) {
        return false;
    }

    return !graphHasConnection(graph, source, target);
}

bool canRestoreModuleAndConnections(const Graph* graph,
                                    const Module* restoredModule,
                                    const std::vector<std::unique_ptr<Connection>>& connections) {
    if (!graph || !restoredModule || restoredModule->id().isEmpty()) {
        return false;
    }
    if (graph->getModule(restoredModule->id())) {
        return false;
    }

    return std::all_of(connections.begin(), connections.end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return canRestoreConnection(graph, restoredModule, connection.get());
        });
}

std::unique_ptr<Connection> cloneConnection(const Connection& connection) {
    return connection.clone();
}

void restoreConnectionRecords(EditorMutationTarget* target,
                              const Graph* graph,
                              const std::vector<QString>& connectionIds) {
    if (!target || !graph) {
        return;
    }
    for (const QString& connectionId : connectionIds) {
        if (const Connection* connection = graph->getConnection(connectionId)) {
            const bool restored = target->upsertEditorConnectionRecord(*connection);
            Q_UNUSED(restored);
        }
    }
}

void removeConnectionRecords(EditorMutationTarget* target,
                             const std::vector<QString>& connectionIds) {
    if (!target) {
        return;
    }
    for (const QString& connectionId : connectionIds) {
        const bool removed = target->removeEditorConnectionRecord(connectionId);
        Q_UNUSED(removed);
    }
}

} // namespace

RemoveModuleCommand::RemoveModuleCommand(Graph* graph,
                                         const QString& moduleId,
                                         EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_moduleId(moduleId),
      m_editorMutationTarget(editorMutationTarget) {}

// Remove module and all connected connections
void RemoveModuleCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_graph || !m_graph->getModule(m_moduleId)) return;

    std::vector<QString> connIds;
    for (const auto& conn : m_graph->connections()) {
        if (conn->source().moduleId == m_moduleId || conn->target().moduleId == m_moduleId) {
            connIds.push_back(conn->id());
        }
    }
    if (m_editorMutationTarget) {
        std::vector<QString> removedConnectionRecords;
        for (const QString& id : connIds) {
            if (!m_editorMutationTarget->removeEditorConnectionRecord(id)) {
                restoreConnectionRecords(m_editorMutationTarget, m_graph, removedConnectionRecords);
                return;
            }
            removedConnectionRecords.push_back(id);
        }
        if (!m_editorMutationTarget->removeEditorModuleRecord(m_moduleId)) {
            restoreConnectionRecords(m_editorMutationTarget, m_graph, removedConnectionRecords);
            return;
        }
    }

    for (const auto& id : connIds) {
        if (auto conn = m_graph->takeConnection(id)) {
            m_connections.push_back(std::move(conn));
        }
    }
    m_module = m_graph->takeModule(m_moduleId);
    m_executed = (m_module != nullptr);
}

// Restore module and its connections
void RemoveModuleCommand::undo() {
    m_undone = false;
    if (!m_graph || !m_module) {
        return;
    }
    if (!canRestoreModuleAndConnections(m_graph, m_module.get(), m_connections)) {
        return;
    }

    const QString moduleId = m_module->id();
    std::vector<QString> persistedConnectionIds;
    if (m_editorMutationTarget) {
        if (!m_editorMutationTarget->upsertEditorModuleRecord(*m_module)) {
            return;
        }
        for (const auto& conn : m_connections) {
            if (!conn || !m_editorMutationTarget->upsertEditorConnectionRecord(*conn)) {
                removeConnectionRecords(m_editorMutationTarget, persistedConnectionIds);
                const bool removed = m_editorMutationTarget->removeEditorModuleRecord(moduleId);
                Q_UNUSED(removed);
                return;
            }
            persistedConnectionIds.push_back(conn->id());
        }
    }

    if (!m_graph->insertModule(m_module->clone())) {
        if (m_editorMutationTarget) {
            removeConnectionRecords(m_editorMutationTarget, persistedConnectionIds);
            const bool removed = m_editorMutationTarget->removeEditorModuleRecord(moduleId);
            Q_UNUSED(removed);
        }
        return;
    }

    for (const auto& conn : m_connections) {
        if (!conn) {
            m_graph->removeModule(moduleId);
            if (m_editorMutationTarget) {
                removeConnectionRecords(m_editorMutationTarget, persistedConnectionIds);
                const bool removed = m_editorMutationTarget->removeEditorModuleRecord(moduleId);
                Q_UNUSED(removed);
            }
            return;
        }

        const qsizetype before = static_cast<qsizetype>(m_graph->connections().size());
        m_graph->insertConnection(cloneConnection(*conn));
        const qsizetype after = static_cast<qsizetype>(m_graph->connections().size());
        if (after != before + 1) {
            m_graph->removeModule(moduleId);
            if (m_editorMutationTarget) {
                removeConnectionRecords(m_editorMutationTarget, persistedConnectionIds);
                const bool removed = m_editorMutationTarget->removeEditorModuleRecord(moduleId);
                Q_UNUSED(removed);
            }
            return;
        }
    }

    m_module.reset();
    m_connections.clear();
    m_undone = true;
}
