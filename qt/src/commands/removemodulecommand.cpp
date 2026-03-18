#include "commands/removemodulecommand.h"

RemoveModuleCommand::RemoveModuleCommand(Graph* graph, const QString& moduleId)
    : m_graph(graph), m_moduleId(moduleId) {}

void RemoveModuleCommand::execute() {
    m_module = std::unique_ptr<Module>(m_graph->getModule(m_moduleId));
    m_graph->removeModule(m_moduleId);
}

void RemoveModuleCommand::undo() {
    m_graph->addModule(std::move(m_module));
}
