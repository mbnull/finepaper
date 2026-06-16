// TopologyPresetCommand wraps preset graph mutations in command history.
#include "commands/topologypresetcommand.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "project/editormutationtarget.h"

#include <utility>

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
    if (m_executed && m_editorMutationTarget) {
        for (const QString& moduleId : m_result.moduleIds) {
            const Module* module = m_graph->getModule(moduleId);
            if (!module || !m_editorMutationTarget->upsertEditorModuleRecord(*module)) {
                undo();
                return;
            }
        }
        for (const QString& connectionId : m_result.connectionIds) {
            const Connection* connection = m_graph->getConnection(connectionId);
            if (!connection || !m_editorMutationTarget->upsertEditorConnectionRecord(*connection)) {
                undo();
                return;
            }
        }
    }
}

void TopologyPresetCommand::undo() {
    m_undone = false;
    if (!m_graph || !m_result.success) {
        return;
    }
    for (const QString& connectionId : m_result.connectionIds) {
        m_graph->removeConnection(connectionId);
    }
    for (int index = m_result.moduleIds.size() - 1; index >= 0; --index) {
        m_graph->removeModule(m_result.moduleIds.at(index));
    }
    if (m_editorMutationTarget) {
        for (const QString& connectionId : m_result.connectionIds) {
            m_editorMutationTarget->removeEditorConnectionRecord(connectionId);
        }
        for (const QString& moduleId : m_result.moduleIds) {
            m_editorMutationTarget->removeEditorModuleRecord(moduleId);
        }
    }
    m_executed = false;
    m_undone = true;
}

const TopologyPresetResult& TopologyPresetCommand::result() const {
    return m_result;
}
