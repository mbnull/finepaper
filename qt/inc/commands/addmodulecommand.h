// AddModuleCommand adds a module to the graph with undo support
#pragma once

#include "commands/command.h"
#include "graph/graph.h"
#include <memory>

class AddModuleCommand : public Command {
public:
    // Takes ownership of a prepared module to be inserted into the graph.
    AddModuleCommand(Graph* graph, std::unique_ptr<Module> module);
    // Inserts the module if ID and graph constraints are satisfied.
    void execute() override;
    // Removes the inserted module and restores local ownership.
    void undo() override;

private:
    Graph* m_graph;
    std::unique_ptr<Module> m_module;
    QString m_moduleId;
};
