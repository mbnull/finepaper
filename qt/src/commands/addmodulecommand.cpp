#include "commands/addmodulecommand.h"

AddModuleCommand::AddModuleCommand(Graph* graph, std::unique_ptr<Module> module)
    : m_graph(graph), m_module(std::move(module)) {
    m_moduleId = m_module->id();
}

void AddModuleCommand::execute() {
    m_graph->insertModule(std::move(m_module));
}

void AddModuleCommand::undo() {
    m_module = m_graph->takeModule(m_moduleId);
}
