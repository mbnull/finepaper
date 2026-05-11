// ProjectStateService stores IP-instance project state outside Graph.
#include "project/projectstateservice.h"

#include <algorithm>
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

int ProjectStateService::indexOfIpInstanceRecord(const QString& ipcoreId,
                                                 const QString& instanceId) const {
    for (qsizetype index = 0; index < m_ipInstanceRecords.size(); ++index) {
        const ProjectIpInstanceRecord& record = m_ipInstanceRecords.at(index);
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::optional<ProjectIpInstanceRecord> ProjectStateService::ipInstanceRecord(
    const QString& ipcoreId,
    const QString& instanceId) const {
    const int index = indexOfIpInstanceRecord(ipcoreId, instanceId);
    if (index < 0) {
        return std::nullopt;
    }
    return m_ipInstanceRecords.at(index);
}

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
    return insertIpInstanceRecord(static_cast<int>(m_ipInstanceRecords.size()), record);
}

bool ProjectStateService::insertIpInstanceRecord(int index, const ProjectIpInstanceRecord& record) {
    if (indexOfIpInstanceRecord(record.ipcoreId, record.instanceId) >= 0) {
        return false;
    }

    const int size = static_cast<int>(m_ipInstanceRecords.size());
    const int insertIndex = std::clamp(index, 0, size);
    m_ipInstanceRecords.insert(insertIndex, record);
    emit ipInstanceRecordsChanged();
    return true;
}

std::optional<ProjectIpInstanceRecord> ProjectStateService::takeIpInstanceRecord(
    const QString& ipcoreId,
    const QString& instanceId) {
    const int index = indexOfIpInstanceRecord(ipcoreId, instanceId);
    if (index < 0) {
        return std::nullopt;
    }

    ProjectIpInstanceRecord record = m_ipInstanceRecords.at(index);
    m_ipInstanceRecords.removeAt(index);
    emit ipInstanceRecordsChanged();
    return record;
}

bool ProjectStateService::removeIpInstanceRecord(const QString& ipcoreId,
                                                 const QString& instanceId) {
    return takeIpInstanceRecord(ipcoreId, instanceId).has_value();
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
