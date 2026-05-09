// AddModuleCommand inserts one module with undo support.
#include "commands/addmodulecommand.h"

AddModuleCommand::AddModuleCommand(Graph* graph,
                                   std::unique_ptr<Module> module,
                                   QString expectedIpcoreId)
    : m_graph(graph),
      m_module(std::move(module)),
      m_expectedIpcoreId(std::move(expectedIpcoreId)) {
    if (m_module) {
        m_moduleId = m_module->id();
    }
}

// Insert module into graph if valid
void AddModuleCommand::execute() {
    m_executed = false;
    if (!m_graph || !m_module || m_moduleId.isEmpty() || m_graph->getModule(m_moduleId)) {
        return;
    }
    if (!m_expectedIpcoreId.isEmpty() && m_module->ipcoreId() != m_expectedIpcoreId) {
        return;
    }
    if (m_graph->insertModule(std::move(m_module))) {
        m_executed = true;
    }
}

// Remove module from graph and restore ownership
void AddModuleCommand::undo() {
    m_module = m_graph->takeModule(m_moduleId);
}
