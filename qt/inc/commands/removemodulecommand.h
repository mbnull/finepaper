// RemoveModuleCommand removes a module and its connections with undo support
#pragma once

#include "command.h"
#include "graph.h"
#include <memory>

class RemoveModuleCommand : public Command {
public:
    RemoveModuleCommand(Graph* graph, const QString& moduleId);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    QString m_moduleId;
    std::unique_ptr<Module> m_module;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
