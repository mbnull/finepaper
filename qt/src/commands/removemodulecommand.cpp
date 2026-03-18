#include "commands/removemodulecommand.h"

RemoveModuleCommand::RemoveModuleCommand(Graph* graph, const QString& moduleId)
    : m_graph(graph), m_moduleId(moduleId) {}

void RemoveModuleCommand::execute() {
    m_module = m_graph->takeModule(m_moduleId);
}

void RemoveModuleCommand::undo() {
    m_graph->insertModule(std::move(m_module));
}
