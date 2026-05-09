// TopologyPresetCommand applies a topology preset as one undoable command.
#pragma once

#include "commands/command.h"
#include "topology/topologypresetbuilder.h"

class Graph;
class ModuleRegistry;

class TopologyPresetCommand : public Command {
public:
    TopologyPresetCommand(Graph* graph,
                          const ModuleRegistry* registry,
                          TopologyPresetRequest request);

    void execute() override;
    void undo() override;
    const TopologyPresetResult& result() const;

private:
    Graph* m_graph = nullptr;
    const ModuleRegistry* m_registry = nullptr;
    TopologyPresetRequest m_request;
    TopologyPresetResult m_result;
};
