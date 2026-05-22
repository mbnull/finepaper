// ProjectStateService stores IP-instance project state outside Graph.
#include "project/projectstateservice.h"

#include <algorithm>
#include <QHash>
#include <QJsonArray>
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

QString instanceScopeKey(const QString& ipcoreId, const QString& instanceId) {
    return ipcoreId + QLatin1Char('\n') + instanceId;
}

bool isLayoutParameter(const QString& key) {
    return key == QStringLiteral("x") ||
           key == QStringLiteral("y") ||
           key == QStringLiteral("collapsed");
}

QJsonObject graphObjectProperties(const QJsonObject& parameters) {
    QJsonObject properties;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (!isLayoutParameter(it.key())) {
            properties.insert(it.key(), it.value());
        }
    }
    return properties;
}

QJsonObject layoutNodeProperties(const QJsonObject& parameters) {
    QJsonObject properties;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (isLayoutParameter(it.key())) {
            properties.insert(it.key(), it.value());
        }
    }
    return properties;
}

QJsonArray stringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

void attachGraphConfigAndLayout(ProjectDocument& document) {
    if (document.modules.isEmpty()) {
        return;
    }

    QHash<QString, qsizetype> instanceIndexes;
    for (qsizetype index = 0; index < document.instances.size(); ++index) {
        const ProjectIpInstanceRecord& instance = document.instances.at(index);
        instanceIndexes.insert(instanceScopeKey(instance.package.id, instance.id), index);
    }

    QHash<QString, qsizetype> moduleInstanceIndexes;
    QHash<qsizetype, QJsonArray> objectsByInstance;
    QJsonObject layoutNodes;
    for (const ProjectModuleRecord& module : document.modules) {
        const QString scope = instanceScopeKey(module.ipcoreId, module.instanceId);
        if (!instanceIndexes.contains(scope)) {
            continue;
        }
        const qsizetype instanceIndex = instanceIndexes.value(scope);
        moduleInstanceIndexes.insert(module.id, instanceIndex);

        QJsonObject object;
        object.insert(QStringLiteral("id"), module.id);
        object.insert(QStringLiteral("type"), module.type);
        const QJsonObject properties = graphObjectProperties(module.parameters);
        if (!properties.isEmpty()) {
            object.insert(QStringLiteral("properties"), properties);
        }
        objectsByInstance[instanceIndex].append(object);

        const QJsonObject layout = layoutNodeProperties(module.parameters);
        if (!layout.isEmpty()) {
            layoutNodes.insert(module.id, layout);
        }
    }

    QHash<qsizetype, QJsonArray> relationshipsByInstance;
    for (const ProjectConnectionRecord& connection : document.connections) {
        QJsonArray endpoints;
        qsizetype ownerIndex = -1;
        bool sameOwner = true;
        const QVector<ProjectConnectionInterfaceRef> interfaces = !connection.interfaces.isEmpty()
            ? connection.interfaces
            : QVector<ProjectConnectionInterfaceRef>{
                ProjectConnectionInterfaceRef{connection.source.moduleId, connection.source.portId},
                ProjectConnectionInterfaceRef{connection.target.moduleId, connection.target.portId}
            };

        for (const ProjectConnectionInterfaceRef& interfaceRef : interfaces) {
            if (!moduleInstanceIndexes.contains(interfaceRef.instanceId)) {
                sameOwner = false;
                break;
            }
            const qsizetype endpointOwner = moduleInstanceIndexes.value(interfaceRef.instanceId);
            if (ownerIndex < 0) {
                ownerIndex = endpointOwner;
            } else if (ownerIndex != endpointOwner) {
                sameOwner = false;
                break;
            }

            QJsonObject endpoint;
            endpoint.insert(QStringLiteral("object"), interfaceRef.instanceId);
            endpoint.insert(QStringLiteral("role"), interfaceRef.interfaceId);
            endpoints.append(endpoint);
        }

        if (!sameOwner || ownerIndex < 0 || endpoints.size() < 2) {
            continue;
        }

        QJsonObject relationship;
        relationship.insert(QStringLiteral("id"), connection.id);
        relationship.insert(QStringLiteral("type"),
                            connection.connectionClassId.trimmed().isEmpty()
                                ? QStringLiteral("connection")
                                : connection.connectionClassId);
        relationship.insert(QStringLiteral("endpoints"), endpoints);
        QJsonObject properties;
        if (!connection.status.trimmed().isEmpty()) {
            properties.insert(QStringLiteral("status"), connection.status);
        }
        if (!connection.alternatives.isEmpty()) {
            properties.insert(QStringLiteral("alternatives"), stringArray(connection.alternatives));
        }
        if (!properties.isEmpty()) {
            relationship.insert(QStringLiteral("properties"), properties);
        }
        relationshipsByInstance[ownerIndex].append(relationship);
    }

    for (auto it = objectsByInstance.constBegin(); it != objectsByInstance.constEnd(); ++it) {
        ProjectIpInstanceRecord& instance = document.instances[it.key()];
        QJsonObject graphConfig;
        graphConfig.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.graph-config.v1"));
        graphConfig.insert(QStringLiteral("objects"), it.value());
        graphConfig.insert(QStringLiteral("relationships"), relationshipsByInstance.value(it.key()));
        graphConfig.insert(QStringLiteral("properties"), QJsonObject{});
        graphConfig.insert(QStringLiteral("native"), QJsonObject{});
        instance.hasGraphConfig = true;
        instance.graphConfigIsNull = false;
        instance.graphConfig = graphConfig;
    }

    if (!layoutNodes.isEmpty()) {
        QJsonObject canvas;
        canvas.insert(QStringLiteral("nodes"), layoutNodes);
        canvas.insert(QStringLiteral("connections"), QJsonObject{});
        QJsonObject view;
        view.insert(QStringLiteral("id"), QStringLiteral("graph"));
        view.insert(QStringLiteral("kind"), QStringLiteral("canvas"));
        view.insert(QStringLiteral("canvas"), canvas);
        document.layout.insert(QStringLiteral("views"), QJsonArray{view});
    }
}

ProjectIpInstanceRecord withCanonicalInstanceFields(ProjectIpInstanceRecord record) {
    if (record.id.trimmed().isEmpty()) {
        record.id = record.instanceId;
    }
    if (record.package.id.trimmed().isEmpty()) {
        record.package.id = record.ipcoreId;
    }
    if (record.package.version.trimmed().isEmpty()) {
        record.package.version = QStringLiteral("1.0");
    }
    if (record.displayName.trimmed().isEmpty()) {
        record.displayName = record.id;
    }

    const QJsonValue globalParameters = record.state.value(QStringLiteral("global_parameters"));
    if (globalParameters.isObject()) {
        record.config.insert(QStringLiteral("parameters"), globalParameters.toObject());
    }
    return record;
}

ProjectIpInstanceRecord withLegacyAliasFields(ProjectIpInstanceRecord record) {
    if (record.instanceId.trimmed().isEmpty()) {
        record.instanceId = record.id;
    }
    if (record.ipcoreId.trimmed().isEmpty()) {
        record.ipcoreId = record.package.id;
    }
    if (record.schema.trimmed().isEmpty()) {
        record.schema = QStringLiteral("ipcraft.noc.instance-state.v1");
    }
    if (record.state.isEmpty()) {
        QJsonObject state;
        const QJsonValue parameters = record.config.value(QStringLiteral("parameters"));
        if (parameters.isObject()) {
            state.insert(QStringLiteral("global_parameters"), parameters.toObject());
        }
        record.state = state;
    }
    return withCanonicalInstanceFields(record);
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
    m_ipInstanceRecords.clear();
    if (!document.instances.isEmpty()) {
        m_ipInstanceRecords.reserve(document.instances.size());
        for (ProjectIpInstanceRecord record : document.instances) {
            m_ipInstanceRecords.append(withLegacyAliasFields(std::move(record)));
        }
    } else {
        m_ipInstanceRecords.reserve(document.ipcoreState.size());
        for (ProjectIpInstanceRecord record : document.ipcoreState) {
            m_ipInstanceRecords.append(withCanonicalInstanceFields(std::move(record)));
        }
    }
    emit ipInstanceRecordsChanged();
}

void ProjectStateService::writeToDocument(ProjectDocument& document) const {
    document.ipcoreState = m_ipInstanceRecords;
    document.instances.clear();
    document.instances.reserve(m_ipInstanceRecords.size());
    for (ProjectIpInstanceRecord record : m_ipInstanceRecords) {
        document.instances.append(withCanonicalInstanceFields(std::move(record)));
    }
    attachGraphConfigAndLayout(document);
    addIpcoreStateDependencies(document, m_ipInstanceRecords);
}

bool ProjectStateService::ensureIpInstanceRecord(const ProjectIpInstanceRecord& record) {
    return insertIpInstanceRecord(static_cast<int>(m_ipInstanceRecords.size()),
                                  withLegacyAliasFields(record));
}

bool ProjectStateService::insertIpInstanceRecord(int index, const ProjectIpInstanceRecord& record) {
    const ProjectIpInstanceRecord normalized = withLegacyAliasFields(record);
    if (indexOfIpInstanceRecord(normalized.ipcoreId, normalized.instanceId) >= 0) {
        return false;
    }

    const int size = static_cast<int>(m_ipInstanceRecords.size());
    const int insertIndex = std::clamp(index, 0, size);
    m_ipInstanceRecords.insert(insertIndex, normalized);
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
        if (section == QStringLiteral("global_parameters")) {
            record.config.insert(QStringLiteral("parameters"), sectionObject);
        }
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
