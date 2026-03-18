#include "commands/addmodulecommand.h"

AddModuleCommand::AddModuleCommand(Graph* graph, std::unique_ptr<Module> module)
    : m_graph(graph), m_module(std::move(module)) {
    m_moduleId = m_module->id();
}

void AddModuleCommand::execute() {
    if (m_moduleId.isEmpty() || m_graph->getModule(m_moduleId)) {
        return;
    }
    if (m_graph->insertModule(std::move(m_module))) {
        m_executed = true;
    }
}

void AddModuleCommand::undo() {
    m_module = m_graph->takeModule(m_moduleId);
}
