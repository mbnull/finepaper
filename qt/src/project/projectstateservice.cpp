// ProjectStateService stores IP-instance project state outside Graph.
#include "project/projectstateservice.h"

#include <QSet>

namespace {

void addIpcoreStateDependencies(ProjectDocument& document,
                                const QVector<ProjectIpInstanceRecord>& records) {
    QSet<QString> ipcoreIds;
    for (const ProjectIpcoreRecord& ipcore : document.ipcores) {
        ipcoreIds.insert(ipcore.id);
    }
    for (const ProjectIpInstanceRecord& state : records) {
        if (state.ipcoreId.isEmpty() || ipcoreIds.contains(state.ipcoreId)) {
            continue;
        }
        document.ipcores.push_back(ProjectIpcoreRecord{state.ipcoreId, QStringLiteral("1.0")});
        ipcoreIds.insert(state.ipcoreId);
    }
}

} // namespace

ProjectStateService::ProjectStateService(QObject* parent)
    : QObject(parent) {}

void ProjectStateService::clear() {
    if (m_ipInstanceRecords.isEmpty()) {
        return;
    }
    m_ipInstanceRecords.clear();
    emit ipInstanceRecordsChanged();
}

void ProjectStateService::loadFromDocument(const ProjectDocument& document) {
    m_ipInstanceRecords = document.ipcoreState;
    emit ipInstanceRecordsChanged();
}

void ProjectStateService::writeToDocument(ProjectDocument& document) const {
    document.ipcoreState = m_ipInstanceRecords;
    addIpcoreStateDependencies(document, m_ipInstanceRecords);
}

bool ProjectStateService::ensureIpInstanceRecord(const ProjectIpInstanceRecord& record) {
    for (const ProjectIpInstanceRecord& existing : m_ipInstanceRecords) {
        if (existing.ipcoreId == record.ipcoreId && existing.instanceId == record.instanceId) {
            return false;
        }
    }

    m_ipInstanceRecords.push_back(record);
    emit ipInstanceRecordsChanged();
    return true;
}

bool ProjectStateService::removeIpInstanceRecord(const QString& ipcoreId,
                                                 const QString& instanceId) {
    for (qsizetype i = 0; i < m_ipInstanceRecords.size(); ++i) {
        const ProjectIpInstanceRecord& record = m_ipInstanceRecords.at(i);
        if (record.ipcoreId != ipcoreId || record.instanceId != instanceId) {
            continue;
        }
        m_ipInstanceRecords.removeAt(i);
        emit ipInstanceRecordsChanged();
        return true;
    }
    return false;
}

bool ProjectStateService::setParameter(const QString& ipcoreId,
                                       const QString& instanceId,
                                       const QString& section,
                                       const QString& name,
                                       const QJsonValue& value) {
    for (ProjectIpInstanceRecord& record : m_ipInstanceRecords) {
        if (record.ipcoreId != ipcoreId || record.instanceId != instanceId) {
            continue;
        }

        const QJsonValue sectionValue = record.state.value(section);
        if (!sectionValue.isObject()) {
            return false;
        }

        QJsonObject sectionObject = sectionValue.toObject();
        sectionObject.insert(name, value);
        record.state.insert(section, sectionObject);
        emit parameterChanged(ipcoreId, instanceId, section, name);
        return true;
    }
    return false;
}

QJsonValue ProjectStateService::parameter(const QString& ipcoreId,
                                          const QString& instanceId,
                                          const QString& section,
                                          const QString& name) const {
    for (const ProjectIpInstanceRecord& record : m_ipInstanceRecords) {
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            const QJsonValue sectionValue = record.state.value(section);
            if (!sectionValue.isObject()) {
                return QJsonValue(QJsonValue::Undefined);
            }
            return sectionValue.toObject().value(name);
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}
