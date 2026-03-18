#include "commands/addmodulecommand.h"

AddModuleCommand::AddModuleCommand(Graph* graph, std::unique_ptr<Module> module)
    : m_graph(graph), m_module(std::move(module)) {
    m_moduleId = m_module->id();
}

void AddModuleCommand::execute() {
    m_graph->addModule(std::move(m_module));
}

void AddModuleCommand::undo() {
    m_module = std::unique_ptr<Module>(m_graph->getModule(m_moduleId));
    m_graph->removeModule(m_moduleId);
}
