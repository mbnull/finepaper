// Project IP service implementation.
#include "project/projectipservice.h"

#include "project/projectstateservice.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <variant>

namespace {

bool sameInstance(const ProjectIpInstanceRef& left, const ProjectIpInstanceRef& right) {
    return left.ipcoreId == right.ipcoreId && left.instanceId == right.instanceId;
}

bool isNocKind(const QString& kind) {
    return kind.compare(QStringLiteral("noc"), Qt::CaseInsensitive) == 0;
}

QString defaultIpInstanceId(const QString& ipcoreId) {
    QString token = ipcoreId.section(QLatin1Char('.'), -1).trimmed().toLower();
    token.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    token.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (token.isEmpty()) {
        token = QStringLiteral("ip");
    }
    return token + QStringLiteral("_0");
}

QJsonValue parameterDefaultValueToJson(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) {
        return *stringValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return *intValue;
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonObject globalParameterDefaults(const IpCatalogEntry& entry) {
    QJsonObject defaults;
    QStringList names = entry.instanceParameters.keys();
    names.sort();
    for (const QString& name : names) {
        defaults.insert(name, parameterDefaultValueToJson(entry.instanceParameters.value(name).defaultValue));
    }
    return defaults;
}

ProjectIpInstanceRecord defaultRecordForEntry(const IpCatalogEntry& entry) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = entry.id;
    record.instanceId = defaultIpInstanceId(entry.id);
    record.schema = entry.id + QStringLiteral("-project-state-v1");
    record.state.insert(QStringLiteral("kind"), entry.kind);
    record.state.insert(QStringLiteral("type"), entry.name);
    record.state.insert(QStringLiteral("global_parameters"), globalParameterDefaults(entry));
    return record;
}

} // namespace

ProjectIpService::ProjectIpService(ProjectStateService* stateService, QObject* parent)
    : QObject(parent),
      m_stateService(stateService) {}

ProjectIpServiceResult ProjectIpService::ensureInstanceForIpcore(const IpCatalogEntry& entry) {
    ProjectIpServiceResult result;
    if (!m_stateService) {
        result.error = QStringLiteral("Project state service is not available.");
        return result;
    }
    if (entry.id.trimmed().isEmpty()) {
        result.error = QStringLiteral("IP core id is required.");
        return result;
    }

    if (const ProjectIpInstanceRecord* existing = firstRecordForIpcore(entry.id)) {
        result.success = selectInstance(existing->ipcoreId, existing->instanceId);
        result.record = *existing;
        if (!result.success) {
            result.error = QStringLiteral("Existing IP instance could not be selected.");
        }
        return result;
    }

    if (isNocKind(entry.kind)) {
        for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
            if (record.ipcoreId != entry.id
                && isNocKind(record.state.value(QStringLiteral("kind")).toString())) {
                result.error = QStringLiteral("Project already contains a NoC IP instance.");
                return result;
            }
        }
    }

    ProjectIpInstanceRecord record = defaultRecordForEntry(entry);
    if (!m_stateService->ensureIpInstanceRecord(record)) {
        result.error = QStringLiteral("IP instance record already exists.");
        return result;
    }

    emit ipInstancesChanged();
    result.success = selectInstance(record.ipcoreId, record.instanceId);
    result.record = record;
    if (!result.success) {
        result.error = QStringLiteral("Created IP instance could not be selected.");
    }
    return result;
}

bool ProjectIpService::selectInstance(const QString& ipcoreId, const QString& instanceId) {
    if (!findRecord(ipcoreId, instanceId)) {
        return false;
    }

    setSelectedInstance(ProjectIpInstanceRef{ipcoreId, instanceId});
    return true;
}

bool ProjectIpService::removeInstance(const QString& ipcoreId, const QString& instanceId) {
    if (!m_stateService || !findRecord(ipcoreId, instanceId)) {
        return false;
    }

    const bool removingSelection =
        m_selectedIpInstance.has_value()
        && m_selectedIpInstance->ipcoreId == ipcoreId
        && m_selectedIpInstance->instanceId == instanceId;
    if (!m_stateService->removeIpInstanceRecord(ipcoreId, instanceId)) {
        return false;
    }

    emit ipInstancesChanged();
    if (removingSelection) {
        if (m_stateService->ipInstanceRecords().isEmpty()) {
            setSelectedInstance(std::nullopt);
        } else {
            const ProjectIpInstanceRecord& first = m_stateService->ipInstanceRecords().first();
            setSelectedInstance(ProjectIpInstanceRef{first.ipcoreId, first.instanceId});
        }
    }
    return true;
}

std::optional<ProjectIpInstanceRef> ProjectIpService::selectedIpInstance() const {
    return m_selectedIpInstance;
}

const ProjectIpInstanceRecord* ProjectIpService::findRecord(const QString& ipcoreId,
                                                            const QString& instanceId) const {
    if (!m_stateService) {
        return nullptr;
    }

    for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
        if (record.ipcoreId == ipcoreId && record.instanceId == instanceId) {
            return &record;
        }
    }
    return nullptr;
}

const ProjectIpInstanceRecord* ProjectIpService::firstRecordForIpcore(const QString& ipcoreId) const {
    if (!m_stateService) {
        return nullptr;
    }

    for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
        if (record.ipcoreId == ipcoreId) {
            return &record;
        }
    }
    return nullptr;
}

void ProjectIpService::setSelectedInstance(std::optional<ProjectIpInstanceRef> selection) {
    if (m_selectedIpInstance.has_value() && selection.has_value()
        && sameInstance(*m_selectedIpInstance, *selection)) {
        return;
    }
    if (!m_selectedIpInstance.has_value() && !selection.has_value()) {
        return;
    }

    m_selectedIpInstance = std::move(selection);
    emit selectedIpInstanceChanged();
}
