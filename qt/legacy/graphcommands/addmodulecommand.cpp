// AddModuleCommand inserts one module with undo support.
#include "legacy/graphcommands/addmodulecommand.h"

#include "project/editormutationtarget.h"

AddModuleCommand::AddModuleCommand(Graph* graph,
                                   std::unique_ptr<Module> module,
                                   QString expectedIpcoreId,
                                   QString expectedInstanceId,
                                   EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_module(std::move(module)),
      m_expectedIpcoreId(std::move(expectedIpcoreId)),
      m_expectedInstanceId(std::move(expectedInstanceId)),
      m_editorMutationTarget(editorMutationTarget) {
    if (m_module) {
        m_moduleId = m_module->id();
    }
}

// Insert module into graph if valid
void AddModuleCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_graph || !m_module || m_moduleId.isEmpty() || m_graph->getModule(m_moduleId)) {
        return;
    }
    if (!m_expectedIpcoreId.isEmpty() && m_module->ipcoreId() != m_expectedIpcoreId) {
        return;
    }
    if (m_module->instanceId().trimmed().isEmpty()) {
        return;
    }
    if (!m_expectedInstanceId.isEmpty() && m_module->instanceId() != m_expectedInstanceId) {
        return;
    }
    Module* insertedModule = m_module.get();
    if (!m_graph->insertModule(std::move(m_module))) {
        return;
    }
    if (m_editorMutationTarget && !m_editorMutationTarget->upsertEditorModuleRecord(*insertedModule)) {
        m_module = m_graph->takeModule(m_moduleId);
        return;
    }
    m_executed = true;
}

// Remove module from graph and restore ownership
void AddModuleCommand::undo() {
    m_undone = false;
    if (!m_graph || m_moduleId.isEmpty() || !m_graph->getModule(m_moduleId)) {
        return;
    }
    if (m_editorMutationTarget &&
        !m_editorMutationTarget->removeEditorModuleRecord(m_moduleId)) {
        return;
    }

    m_module = m_graph->takeModule(m_moduleId);
    if (m_module) {
        m_undone = true;
    }
}
