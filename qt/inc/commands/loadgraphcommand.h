// LoadGraphCommand loads a graph from JSON with undo support
#pragma once

#include "commands/command.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/connection.h"
#include <QString>
#include <vector>
#include <memory>

class LoadGraphCommand : public Command {
public:
    // Captures path to import; previous graph snapshot is captured on first execute.
    LoadGraphCommand(Graph* graph, const QString& jsonPath);
    // Replaces current graph with imported JSON payload.
    void execute() override;
    // Restores full previous module/connection snapshot.
    void undo() override;

private:
    Graph* m_graph;
    QString m_jsonPath;
    std::vector<std::unique_ptr<Module>> m_previousModules;
    std::vector<std::unique_ptr<Connection>> m_previousConnections;
};
