// Project IP service implementation.
#include "project/projectipservice.h"

#include "project/projectdocument.h"
#include "project/projectstateservice.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <variant>

namespace {

bool sameInstance(const ProjectIpInstanceRef& left, const ProjectIpInstanceRef& right) {
    return left.ipcoreId == right.ipcoreId && left.instanceId == right.instanceId;
}

QString instanceIdToken(const QString& ipcoreId) {
    QString token = ipcoreId.section(QLatin1Char('.'), -1).trimmed().toLower();
    token.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    token.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (token.isEmpty()) {
        token = QStringLiteral("ip");
    }
    return token;
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

ProjectIpInstanceRecord defaultRecordForEntry(const IpCatalogEntry& entry, const QString& instanceId) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = entry.id;
    record.instanceId = instanceId;
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

void ProjectIpService::loadFromDocument(const ProjectDocument& document) {
    if (!m_stateService) {
        setSelectedInstance(std::nullopt);
        return;
    }

    m_stateService->loadFromDocument(document);
    if (m_stateService->ipInstanceRecords().isEmpty()) {
        setSelectedInstance(std::nullopt);
        return;
    }

    const ProjectIpInstanceRecord& first = m_stateService->ipInstanceRecords().first();
    setSelectedInstance(ProjectIpInstanceRef{first.ipcoreId, first.instanceId});
}

void ProjectIpService::clear() {
    if (!m_stateService) {
        setSelectedInstance(std::nullopt);
        return;
    }

    const bool hadRecords = !m_stateService->ipInstanceRecords().isEmpty();
    m_stateService->clear();
    if (hadRecords) {
        emit ipInstancesChanged();
    }
    setSelectedInstance(std::nullopt);
}

ProjectIpServiceResult ProjectIpService::createInstanceForIpcore(const IpCatalogEntry& entry) {
    ProjectIpServiceResult result;
    if (!m_stateService) {
        result.error = QStringLiteral("Project state service is not available.");
        return result;
    }
    if (entry.id.trimmed().isEmpty()) {
        result.error = QStringLiteral("IP core id is required.");
        return result;
    }

    ProjectIpInstanceRecord record = defaultRecordForEntry(entry, nextInstanceIdForIpcore(entry.id));
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

std::optional<ProjectIpInstanceRef> ProjectIpService::selectedIpInstance() const {
    return m_selectedIpInstance;
}

std::optional<ProjectIpInstanceRecord> ProjectIpService::selectedIpInstanceRecord() const {
    if (!m_selectedIpInstance.has_value()) {
        return std::nullopt;
    }

    const ProjectIpInstanceRecord* record =
        findRecord(m_selectedIpInstance->ipcoreId, m_selectedIpInstance->instanceId);
    if (!record) {
        return std::nullopt;
    }
    return *record;
}

void ProjectIpService::handleIpInstanceRecordsMutated(
    std::optional<ProjectIpInstanceRef> preferredSelection,
    SelectionFallbackPolicy fallbackPolicy) {
    if (!m_stateService) {
        setSelectedInstance(std::nullopt);
        emit ipInstancesChanged();
        return;
    }

    std::optional<ProjectIpInstanceRef> nextSelection;
    if (preferredSelection.has_value()
        && findRecord(preferredSelection->ipcoreId, preferredSelection->instanceId)) {
        nextSelection = preferredSelection;
    } else if (fallbackPolicy == SelectionFallbackPolicy::ExactOrClear) {
        nextSelection = std::nullopt;
    } else if (m_selectedIpInstance.has_value()
               && findRecord(m_selectedIpInstance->ipcoreId, m_selectedIpInstance->instanceId)) {
        nextSelection = m_selectedIpInstance;
    } else if (!m_stateService->ipInstanceRecords().isEmpty()) {
        const ProjectIpInstanceRecord& first = m_stateService->ipInstanceRecords().first();
        nextSelection = ProjectIpInstanceRef{first.ipcoreId, first.instanceId};
    }

    setSelectedInstance(nextSelection);
    emit ipInstancesChanged();
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

QString ProjectIpService::nextInstanceIdForIpcore(const QString& ipcoreId) const {
    const QString token = instanceIdToken(ipcoreId);
    const QRegularExpression pattern(
        QStringLiteral("^%1_(\\d+)$").arg(QRegularExpression::escape(token)));
    int nextIndex = 0;

    if (!m_stateService) {
        return token + QStringLiteral("_0");
    }

    for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
        const QRegularExpressionMatch match = pattern.match(record.instanceId);
        if (match.hasMatch()) {
            nextIndex = std::max(nextIndex, match.captured(1).toInt() + 1);
        }
    }
    return QStringLiteral("%1_%2").arg(token).arg(nextIndex);
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
