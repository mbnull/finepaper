#include "commands/removemodulecommand.h"

RemoveModuleCommand::RemoveModuleCommand(Graph* graph, const QString& moduleId)
    : m_graph(graph), m_moduleId(moduleId) {}

void RemoveModuleCommand::execute() {
    for (const auto& conn : m_graph->connections()) {
        if (conn->source().moduleId == m_moduleId || conn->target().moduleId == m_moduleId) {
            m_connections.push_back(m_graph->takeConnection(conn->id()));
        }
    }
    m_module = m_graph->takeModule(m_moduleId);
}

void RemoveModuleCommand::undo() {
    m_graph->insertModule(std::move(m_module));
    for (auto& conn : m_connections) {
        m_graph->insertConnection(std::move(conn));
    }
    m_connections.clear();
}
