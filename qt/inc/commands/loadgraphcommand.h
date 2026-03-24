// LoadGraphCommand loads a graph from JSON with undo support
#pragma once

#include "command.h"
#include "graph.h"
#include "module.h"
#include "connection.h"
#include <QString>
#include <vector>
#include <memory>

class LoadGraphCommand : public Command {
public:
    LoadGraphCommand(Graph* graph, const QString& jsonPath);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    QString m_jsonPath;
    std::vector<std::unique_ptr<Module>> m_previousModules;
    std::vector<std::unique_ptr<Connection>> m_previousConnections;
};
