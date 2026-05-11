// AddIpInstanceCommand creates one project IP instance with undo/redo support.
#include "commands/addipinstancecommand.h"

#include "project/projectstateservice.h"

#include <utility>

AddIpInstanceCommand::AddIpInstanceCommand(ProjectStateService* stateService,
                                           ProjectIpService* ipService,
                                           IpCatalogEntry entry)
    : m_stateService(stateService),
      m_ipService(ipService),
      m_entry(std::move(entry)) {}

void AddIpInstanceCommand::execute() {
    m_executed = false;
    m_undone = false;

    if (!m_stateService || !m_ipService) {
        return;
    }

    if (m_createdRecord.has_value()) {
        if (!m_stateService->insertIpInstanceRecord(m_recordIndex, *m_createdRecord)) {
            return;
        }
        m_ipService->handleIpInstanceRecordsMutated(
            ProjectIpInstanceRef{m_createdRecord->ipcoreId, m_createdRecord->instanceId},
            ProjectIpService::SelectionFallbackPolicy::ExactOrClear);
        m_executed = true;
        return;
    }

    m_previousSelection = m_ipService->selectedIpInstance();
    const ProjectIpServiceResult result = m_ipService->createInstanceForIpcore(m_entry);
    if (!result.success) {
        return;
    }

    m_createdRecord = result.record;
    m_recordIndex = m_stateService->indexOfIpInstanceRecord(result.record.ipcoreId,
                                                           result.record.instanceId);
    m_executed = m_recordIndex >= 0;
}

void AddIpInstanceCommand::undo() {
    m_undone = false;
    if (!m_stateService || !m_ipService || !m_createdRecord.has_value()) {
        return;
    }

    const std::optional<ProjectIpInstanceRecord> removedRecord =
        m_stateService->takeIpInstanceRecord(m_createdRecord->ipcoreId,
                                             m_createdRecord->instanceId);
    if (!removedRecord.has_value()) {
        return;
    }

    m_ipService->handleIpInstanceRecordsMutated(
        m_previousSelection,
        ProjectIpService::SelectionFallbackPolicy::ExactOrClear);
    m_undone = true;
}
