// TopologyPresetCommand wraps preset graph mutations in command history.
#include "commands/topologypresetcommand.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"

#include <utility>

TopologyPresetCommand::TopologyPresetCommand(Graph* graph,
                                             const ModuleRegistry* registry,
                                             TopologyPresetRequest request)
    : m_graph(graph),
      m_registry(registry),
      m_request(std::move(request)) {}

void TopologyPresetCommand::execute() {
    m_executed = false;
    m_result = {};
    if (!m_graph || !m_registry) {
        return;
    }
    m_result = TopologyPresetBuilder::apply(m_graph, *m_registry, m_request);
    m_executed = m_result.success;
}

void TopologyPresetCommand::undo() {
    if (!m_graph || !m_result.success) {
        return;
    }
    for (const QString& connectionId : m_result.connectionIds) {
        m_graph->removeConnection(connectionId);
    }
    for (int index = m_result.moduleIds.size() - 1; index >= 0; --index) {
        m_graph->removeModule(m_result.moduleIds.at(index));
    }
    m_executed = false;
}

const TopologyPresetResult& TopologyPresetCommand::result() const {
    return m_result;
}
