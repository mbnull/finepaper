// ProjectStateService owns editable plugin project state outside Graph.
#pragma once

#include "project/pluginstate.h"
#include "project/projectdocument.h"

#include <QJsonValue>
#include <QString>
#include <QVector>

class ProjectStateService {
public:
    void clear();
    void loadFromDocument(const ProjectDocument& document);
    void writeToDocument(ProjectDocument& document) const;
    const QVector<ProjectPluginStateRecord>& pluginStates() const { return m_pluginStates; }

    bool setParameter(const QString& pluginId,
                      const QString& instanceId,
                      const QString& section,
                      const QString& name,
                      const QJsonValue& value);
    QJsonValue parameter(const QString& pluginId,
                         const QString& instanceId,
                         const QString& section,
                         const QString& name) const;

private:
    QVector<ProjectPluginStateRecord> m_pluginStates;
};
