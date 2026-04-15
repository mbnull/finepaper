// RemoveModuleCommand removes a module and its connections with undo support
#pragma once

#include "command.h"
#include "graph.h"
#include <memory>

class RemoveModuleCommand : public Command {
public:
    // Targets a specific module ID for deletion.
    RemoveModuleCommand(Graph* graph, const QString& moduleId);
    // Removes the module and all incident connections, storing them for undo.
    void execute() override;
    // Re-inserts removed module and its captured connections.
    void undo() override;

private:
    Graph* m_graph;
    QString m_moduleId;
    std::unique_ptr<Module> m_module;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
