#include "commands/loadgraphcommand.h"

LoadGraphCommand::LoadGraphCommand(Graph* graph, const QString& jsonPath)
    : m_graph(graph), m_jsonPath(jsonPath) {
}

void LoadGraphCommand::execute() {
    if (!m_executed) {
        for (const auto& module : m_graph->modules()) {
            m_previousModules.push_back(module->clone());
        }
        for (const auto& connection : m_graph->connections()) {
            m_previousConnections.push_back(std::make_unique<Connection>(*connection));
        }
    }

    if (m_graph->loadFromJson(m_jsonPath)) {
        m_executed = true;
    }
}

void LoadGraphCommand::undo() {
    while (!m_graph->modules().empty()) {
        m_graph->removeModule(m_graph->modules().front()->id());
    }

    for (auto& module : m_previousModules) {
        m_graph->insertModule(module->clone());
    }
    for (auto& connection : m_previousConnections) {
        m_graph->insertConnection(std::make_unique<Connection>(*connection));
    }
}
