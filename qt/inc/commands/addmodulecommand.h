#pragma once

#include "command.h"
#include "graph.h"
#include <memory>

class AddModuleCommand : public Command {
public:
    AddModuleCommand(Graph* graph, std::unique_ptr<Module> module);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    std::unique_ptr<Module> m_module;
    QString m_moduleId;
};
