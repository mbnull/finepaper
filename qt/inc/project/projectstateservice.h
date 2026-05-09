// ProjectStateService owns editable IP instance project state outside Graph.
#pragma once

#include "project/ipinstancestate.h"
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
    const QVector<ProjectIpInstanceRecord>& ipInstanceRecords() const { return m_ipInstanceRecords; }
    bool ensureIpInstanceRecord(const ProjectIpInstanceRecord& record);
    bool removeIpInstanceRecord(const QString& ipcoreId, const QString& instanceId);

    bool setParameter(const QString& ipcoreId,
                      const QString& instanceId,
                      const QString& section,
                      const QString& name,
                      const QJsonValue& value);
    QJsonValue parameter(const QString& ipcoreId,
                         const QString& instanceId,
                         const QString& section,
                         const QString& name) const;

signals:
    void parameterChanged(const QString& ipcoreId,
                          const QString& instanceId,
                          const QString& section,
                          const QString& name);
    void ipInstanceRecordsChanged();

private:
    QVector<ProjectIpInstanceRecord> m_ipInstanceRecords;
};
