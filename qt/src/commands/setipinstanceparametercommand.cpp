// SetIpInstanceParameterCommand applies undoable IP-instance state edits.
#include "commands/setipinstanceparametercommand.h"

#include <utility>

SetIpInstanceParameterCommand::SetIpInstanceParameterCommand(ProjectStateService* stateService,
                                                             QString ipcoreId,
                                                             QString instanceId,
                                                             QString section,
                                                             QString name,
                                                             QJsonValue newValue)
    : m_stateService(stateService),
      m_ipcoreId(std::move(ipcoreId)),
      m_instanceId(std::move(instanceId)),
      m_section(std::move(section)),
      m_name(std::move(name)),
      m_newValue(std::move(newValue)) {}

void SetIpInstanceParameterCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_stateService) {
        return;
    }

    m_oldValue = m_stateService->parameter(m_ipcoreId, m_instanceId, m_section, m_name);
    if (m_oldValue == m_newValue) {
        return;
    }
    if (m_stateService->setParameter(m_ipcoreId, m_instanceId, m_section, m_name, m_newValue)) {
        m_executed = true;
    }
}

void SetIpInstanceParameterCommand::undo() {
    m_undone = false;
    if (!m_stateService) {
        return;
    }

    if (m_stateService->setParameter(m_ipcoreId, m_instanceId, m_section, m_name, m_oldValue)) {
        m_undone = true;
    }
}
