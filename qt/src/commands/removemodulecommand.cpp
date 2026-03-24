#include "commands/removemodulecommand.h"

RemoveModuleCommand::RemoveModuleCommand(Graph* graph, const QString& moduleId)
    : m_graph(graph), m_moduleId(moduleId) {}

// Remove module and all connected connections
void RemoveModuleCommand::execute() {
    if (!m_graph->getModule(m_moduleId)) return;

    std::vector<QString> connIds;
    for (const auto& conn : m_graph->connections()) {
        if (conn->source().moduleId == m_moduleId || conn->target().moduleId == m_moduleId) {
            connIds.push_back(conn->id());
        }
    }
    for (const auto& id : connIds) {
        if (auto conn = m_graph->takeConnection(id)) {
            m_connections.push_back(std::move(conn));
        }
    }
    m_module = m_graph->takeModule(m_moduleId);
    m_executed = true;
}

// Restore module and its connections
void RemoveModuleCommand::undo() {
    if (!m_module) return;
    m_graph->insertModule(std::move(m_module));
    for (auto& conn : m_connections) {
        m_graph->insertConnection(std::move(conn));
    }
    m_connections.clear();
}
