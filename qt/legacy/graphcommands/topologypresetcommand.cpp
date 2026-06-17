// TopologyPresetCommand wraps preset graph mutations in command history.
#include "legacy/graphcommands/topologypresetcommand.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "legacy/graphcommands/editormutationtarget.h"

#include <utility>
#include <vector>

namespace {

void removeGraphPresetState(Graph* graph, const TopologyPresetResult& result) {
    if (!graph) {
        return;
    }
    for (const QString& connectionId : result.connectionIds) {
        graph->removeConnection(connectionId);
    }
    for (int index = result.moduleIds.size() - 1; index >= 0; --index) {
        graph->removeModule(result.moduleIds.at(index));
    }
}

void removeDurablePresetRecords(EditorMutationTarget* target,
                                const std::vector<QString>& moduleIds,
                                const std::vector<QString>& connectionIds) {
    if (!target) {
        return;
    }
    for (const QString& connectionId : connectionIds) {
        const bool removed = target->removeEditorConnectionRecord(connectionId);
        Q_UNUSED(removed);
    }
    for (const QString& moduleId : moduleIds) {
        const bool removed = target->removeEditorModuleRecord(moduleId);
        Q_UNUSED(removed);
    }
}

void restoreDurablePresetRecordsFromGraph(EditorMutationTarget* target,
                                          const Graph* graph,
                                          const std::vector<QString>& moduleIds,
                                          const std::vector<QString>& connectionIds) {
    if (!target || !graph) {
        return;
    }
    for (const QString& moduleId : moduleIds) {
        if (const Module* module = graph->getModule(moduleId)) {
            const bool restored = target->upsertEditorModuleRecord(*module);
            Q_UNUSED(restored);
        }
    }
    for (const QString& connectionId : connectionIds) {
        if (const Connection* connection = graph->getConnection(connectionId)) {
            const bool restored = target->upsertEditorConnectionRecord(*connection);
            Q_UNUSED(restored);
        }
    }
}

bool persistCreatedPresetRecords(EditorMutationTarget* target,
                                 const Graph* graph,
                                 const TopologyPresetResult& result) {
    if (!target) {
        return true;
    }

    std::vector<QString> persistedModuleIds;
    std::vector<QString> persistedConnectionIds;
    for (const QString& moduleId : result.moduleIds) {
        const Module* module = graph ? graph->getModule(moduleId) : nullptr;
        if (!module || !target->upsertEditorModuleRecord(*module)) {
            removeDurablePresetRecords(target, persistedModuleIds, persistedConnectionIds);
            return false;
        }
        persistedModuleIds.push_back(moduleId);
    }
    for (const QString& connectionId : result.connectionIds) {
        const Connection* connection = graph ? graph->getConnection(connectionId) : nullptr;
        if (!connection || !target->upsertEditorConnectionRecord(*connection)) {
            removeDurablePresetRecords(target, persistedModuleIds, persistedConnectionIds);
            return false;
        }
        persistedConnectionIds.push_back(connectionId);
    }
    return true;
}

bool removePresetRecordsForUndo(EditorMutationTarget* target,
                                const Graph* graph,
                                const TopologyPresetResult& result) {
    if (!target) {
        return true;
    }

    std::vector<QString> removedModuleIds;
    std::vector<QString> removedConnectionIds;
    for (const QString& connectionId : result.connectionIds) {
        if (!target->removeEditorConnectionRecord(connectionId)) {
            restoreDurablePresetRecordsFromGraph(target, graph, removedModuleIds, removedConnectionIds);
            return false;
        }
        removedConnectionIds.push_back(connectionId);
    }
    for (const QString& moduleId : result.moduleIds) {
        if (!target->removeEditorModuleRecord(moduleId)) {
            restoreDurablePresetRecordsFromGraph(target, graph, removedModuleIds, removedConnectionIds);
            return false;
        }
        removedModuleIds.push_back(moduleId);
    }
    return true;
}

} // namespace

TopologyPresetCommand::TopologyPresetCommand(Graph* graph,
                                             const ModuleRegistry* registry,
                                             TopologyPresetRequest request,
                                             EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_registry(registry),
      m_request(std::move(request)),
      m_editorMutationTarget(editorMutationTarget) {}

void TopologyPresetCommand::execute() {
    m_executed = false;
    m_undone = false;
    m_result = {};
    if (!m_graph || !m_registry) {
        return;
    }
    m_result = TopologyPresetBuilder::apply(m_graph, *m_registry, m_request);
    m_executed = m_result.success;
    if (m_executed &&
        !persistCreatedPresetRecords(m_editorMutationTarget, m_graph, m_result)) {
        removeGraphPresetState(m_graph, m_result);
        m_result = {};
        m_executed = false;
    }
}

void TopologyPresetCommand::undo() {
    m_undone = false;
    if (!m_graph || !m_result.success) {
        return;
    }
    if (!removePresetRecordsForUndo(m_editorMutationTarget, m_graph, m_result)) {
        return;
    }

    removeGraphPresetState(m_graph, m_result);
    m_executed = false;
    m_undone = true;
}

const TopologyPresetResult& TopologyPresetCommand::result() const {
    return m_result;
}
