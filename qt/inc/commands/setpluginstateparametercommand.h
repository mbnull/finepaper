// SetPluginStateParameterCommand changes one plugin-owned parameter with undo support.
#pragma once

#include "commands/command.h"
#include "project/projectstateservice.h"

#include <QJsonValue>
#include <QString>

class SetPluginStateParameterCommand final : public Command {
public:
    SetPluginStateParameterCommand(ProjectStateService* stateService,
                                   QString pluginId,
                                   QString instanceId,
                                   QString section,
                                   QString name,
                                   QJsonValue newValue);
    void execute() override;
    void undo() override;

private:
    ProjectStateService* m_stateService;
    QString m_pluginId;
    QString m_instanceId;
    QString m_section;
    QString m_name;
    QJsonValue m_newValue;
    QJsonValue m_oldValue;
};
