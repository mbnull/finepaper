// AddIpInstanceCommand creates one project IP instance with undo support.
#pragma once

#include "commands/command.h"
#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "project/projectipservice.h"

#include <optional>

class ProjectStateService;

class AddIpInstanceCommand : public Command {
public:
    AddIpInstanceCommand(ProjectStateService* stateService,
                         ProjectIpService* ipService,
                         IpCatalogEntry entry);

    void execute() override;
    void undo() override;

private:
    ProjectStateService* m_stateService = nullptr;
    ProjectIpService* m_ipService = nullptr;
    IpCatalogEntry m_entry;
    std::optional<ProjectIpInstanceRecord> m_createdRecord;
    std::optional<ProjectIpInstanceRef> m_previousSelection;
    int m_recordIndex = -1;
};
