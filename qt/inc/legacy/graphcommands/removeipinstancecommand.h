// RemoveIpInstanceCommand removes one IP instance's project state and owned graph entities.
#pragma once

#include "commands/command.h"
#include "graph/connection.h"
#include "graph/module.h"
#include "project/ipinstancestate.h"
#include "project/projectipservice.h"

#include <QString>
#include <memory>
#include <optional>
#include <vector>

class Graph;
class ProjectIpService;
class ProjectStateService;

class RemoveIpInstanceCommand : public Command {
public:
    RemoveIpInstanceCommand(Graph* graph,
                            ProjectStateService* stateService,
                            ProjectIpService* ipService,
                            QString ipcoreId,
                            QString instanceId);

    void execute() override;
    void undo() override;

private:
    Graph* m_graph = nullptr;
    ProjectStateService* m_stateService = nullptr;
    ProjectIpService* m_ipService = nullptr;
    QString m_ipcoreId;
    QString m_instanceId;
    int m_recordIndex = -1;
    std::optional<ProjectIpInstanceRecord> m_removedRecord;
    std::optional<ProjectIpInstanceRef> m_previousSelection;
    std::vector<std::unique_ptr<Module>> m_removedModules;
    std::vector<std::unique_ptr<Connection>> m_removedConnections;
};
