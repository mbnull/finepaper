// SetPluginStateParameterCommand applies undoable plugin state edits.
#include "commands/setpluginstateparametercommand.h"

#include <utility>

SetPluginStateParameterCommand::SetPluginStateParameterCommand(ProjectStateService* stateService,
                                                               QString pluginId,
                                                               QString instanceId,
                                                               QString section,
                                                               QString name,
                                                               QJsonValue newValue)
    : m_stateService(stateService),
      m_pluginId(std::move(pluginId)),
      m_instanceId(std::move(instanceId)),
      m_section(std::move(section)),
      m_name(std::move(name)),
      m_newValue(std::move(newValue)) {}

void SetPluginStateParameterCommand::execute() {
    m_executed = false;
    if (!m_stateService) {
        return;
    }

    m_oldValue = m_stateService->parameter(m_pluginId, m_instanceId, m_section, m_name);
    if (m_oldValue == m_newValue) {
        return;
    }
    if (m_stateService->setParameter(m_pluginId, m_instanceId, m_section, m_name, m_newValue)) {
        m_executed = true;
    }
}

void SetPluginStateParameterCommand::undo() {
    if (!m_stateService) {
        return;
    }

    m_stateService->setParameter(m_pluginId, m_instanceId, m_section, m_name, m_oldValue);
}
