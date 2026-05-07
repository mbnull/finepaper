// ProjectStateService owns editable plugin project state outside Graph.
#pragma once

#include "project/pluginstate.h"
#include "project/projectdocument.h"

#include <QObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

class ProjectStateService : public QObject {
    Q_OBJECT

public:
    explicit ProjectStateService(QObject* parent = nullptr);
    void clear();
    void loadFromDocument(const ProjectDocument& document);
    void writeToDocument(ProjectDocument& document) const;
    const QVector<ProjectPluginStateRecord>& pluginStates() const { return m_pluginStates; }
    bool ensurePluginStateRecord(const ProjectPluginStateRecord& record);

    bool setParameter(const QString& pluginId,
                      const QString& instanceId,
                      const QString& section,
                      const QString& name,
                      const QJsonValue& value);
    QJsonValue parameter(const QString& pluginId,
                         const QString& instanceId,
                         const QString& section,
                         const QString& name) const;

signals:
    void parameterChanged(const QString& pluginId,
                          const QString& instanceId,
                          const QString& section,
                          const QString& name);

private:
    QVector<ProjectPluginStateRecord> m_pluginStates;
};
