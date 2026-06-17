#include "ipcraft/core/project_patch.h"

#include "ipcraft/patchops.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>

namespace ipcraft::core {
namespace {

ValidationIssue issue(const QString& code, const QString& message, const QString& path) {
    return ValidationIssue{code, message, path};
}

void insertStringIfNonEmpty(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

bool isLayoutConfigKey(const QString& key) {
    return key == QStringLiteral("x") ||
           key == QStringLiteral("y") ||
           key == QStringLiteral("node_width") ||
           key == QStringLiteral("node_height") ||
           key == QStringLiteral("collapsed") ||
           key == QStringLiteral("waypoints") ||
           key == QStringLiteral("zoom") ||
           key == QStringLiteral("pan");
}

bool containsLayoutConfigKey(const QJsonValue& value);

bool containsLayoutConfigKey(const QJsonArray& array) {
    for (const QJsonValue& child : array) {
        if (containsLayoutConfigKey(child)) {
            return true;
        }
    }

    return false;
}

bool containsLayoutConfigKey(const QJsonObject& object) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isLayoutConfigKey(it.key()) || containsLayoutConfigKey(it.value())) {
            return true;
        }
    }

    return false;
}

bool containsLayoutConfigKey(const QJsonValue& value) {
    if (value.isObject()) {
        return containsLayoutConfigKey(value.toObject());
    }

    if (value.isArray()) {
        return containsLayoutConfigKey(value.toArray());
    }

    return false;
}

bool isAllowedTopLevelKey(const QString& key) {
    return key == QStringLiteral("schema") ||
           key == QStringLiteral("id") ||
           key == QStringLiteral("description") ||
           key == QStringLiteral("author") ||
           key == QStringLiteral("metadata") ||
           key == QStringLiteral("ops");
}

void appendUnknownTopLevelFieldIssues(QVector<ValidationIssue>& issues,
                                      const QJsonObject& object) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!isAllowedTopLevelKey(it.key())) {
            issues.append(issue(QStringLiteral("patch.unknown_field"),
                                QStringLiteral("Top-level patch field is not supported."),
                                QStringLiteral("/%1").arg(it.key())));
        }
    }
}

bool idFromTarget(const QString& target, const QString& prefix, QString& id) {
    if (!target.startsWith(prefix)) {
        return false;
    }

    id = target.mid(prefix.size());
    return !id.trimmed().isEmpty() && !id.contains(QLatin1Char('/'));
}

bool componentIdFromTarget(const QString& target, QString& componentId) {
    return idFromTarget(target, QStringLiteral("component:"), componentId);
}

bool connectionIdFromTarget(const QString& target, QString& connectionId) {
    return idFromTarget(target, QStringLiteral("connection:"), connectionId);
}

bool viewIdFromTarget(const QString& target, QString& viewId) {
    return idFromTarget(target, QStringLiteral("view:"), viewId);
}

bool topologyIdFromTarget(const QString& target, QString& topologyId) {
    return idFromTarget(target, QStringLiteral("topology:"), topologyId);
}

bool decodeJsonPointerSegments(const QString& path, QVector<QString>& segments) {
    if (!path.startsWith(QLatin1Char('/')) || path.size() == 1) {
        return false;
    }

    segments.clear();
    QString segment;
    segment.reserve(path.size() - 1);
    for (qsizetype index = 1; index < path.size(); ++index) {
        if (path.at(index) == QLatin1Char('/')) {
            if (segment.isEmpty()) {
                segments.clear();
                return false;
            }
            segments.append(segment);
            segment.clear();
            continue;
        }

        if (path.at(index) != QLatin1Char('~')) {
            segment.append(path.at(index));
            continue;
        }

        ++index;
        if (index >= path.size()) {
            segments.clear();
            return false;
        }

        if (path.at(index) == QLatin1Char('0')) {
            segment.append(QLatin1Char('~'));
        } else if (path.at(index) == QLatin1Char('1')) {
            segment.append(QLatin1Char('/'));
        } else {
            segments.clear();
            return false;
        }
    }

    if (segment.isEmpty()) {
        segments.clear();
        return false;
    }
    segments.append(segment);
    return true;
}

bool decodeJsonPointerSegment(const QString& path, QString& key) {
    QVector<QString> segments;
    if (!decodeJsonPointerSegments(path, segments) || segments.size() != 1) {
        key.clear();
        return false;
    }

    key = segments.first();
    return true;
}

bool decodeJsonPointerTwoSegments(const QString& path, QString& first, QString& second) {
    QVector<QString> segments;
    if (!decodeJsonPointerSegments(path, segments) || segments.size() != 2) {
        first.clear();
        second.clear();
        return false;
    }

    first = segments.at(0);
    second = segments.at(1);
    return true;
}

qsizetype componentIndexById(const ProjectDesign& project, const QString& componentId) {
    for (qsizetype index = 0; index < project.components.size(); ++index) {
        if (project.components.at(index).id == componentId) {
            return index;
        }
    }

    return -1;
}

qsizetype connectionIndexById(const ProjectDesign& project, const QString& connectionId) {
    for (qsizetype index = 0; index < project.connections.size(); ++index) {
        if (project.connections.at(index).id == connectionId) {
            return index;
        }
    }

    return -1;
}

qsizetype viewIndexById(const ProjectDesign& project, const QString& viewId) {
    for (qsizetype index = 0; index < project.views.size(); ++index) {
        if (project.views.at(index).id == viewId) {
            return index;
        }
    }

    return -1;
}

qsizetype topologyIndexById(const ProjectDesign& project, const QString& topologyId) {
    for (qsizetype index = 0; index < project.topologies.size(); ++index) {
        if (project.topologies.at(index).id == topologyId) {
            return index;
        }
    }

    return -1;
}

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}

QString packageRefKey(const PackageRef& package) {
    return package.id + QLatin1Char('@') + package.version;
}

QString resolveComponentPackageRef(const ProjectDesign& project, const QString& requestedRef) {
    if (requestedRef.contains(QLatin1Char('@'))) {
        return requestedRef;
    }

    QString resolved;
    for (const PackageRef& package : project.packages) {
        if (package.id != requestedRef || package.version.trimmed().isEmpty()) {
            continue;
        }

        if (!resolved.isEmpty()) {
            return requestedRef;
        }
        resolved = packageRefKey(package);
    }

    return resolved.isEmpty() ? requestedRef : resolved;
}

QJsonObject addComponentPayload(const PatchOperation& operation) {
    if (operation.payload.value(QStringLiteral("payload")).isObject()) {
        return operation.payload.value(QStringLiteral("payload")).toObject();
    }

    if (operation.value.isObject()) {
        return operation.value.toObject();
    }

    return operation.payload;
}

bool operationObjectArgument(const PatchOperation& operation, QJsonObject& object) {
    if (operation.payload.value(QStringLiteral("payload")).isObject()) {
        object = operation.payload.value(QStringLiteral("payload")).toObject();
        return true;
    }

    if (!operation.value.isUndefined()) {
        if (!operation.value.isObject()) {
            object = {};
            return false;
        }
        object = operation.value.toObject();
        return true;
    }

    object = operation.payload;
    const bool looksLikeSerializedOperation =
        object.contains(QStringLiteral("op")) ||
        object.contains(QStringLiteral("target")) ||
        object.contains(QStringLiteral("path")) ||
        object.contains(QStringLiteral("value")) ||
        object.contains(QStringLiteral("payload"));
    object.remove(QStringLiteral("op"));
    object.remove(QStringLiteral("target"));
    object.remove(QStringLiteral("path"));
    object.remove(QStringLiteral("value"));
    object.remove(QStringLiteral("payload"));

    if (looksLikeSerializedOperation && object.isEmpty()) {
        return false;
    }

    return true;
}

bool optionalObjectField(const QJsonObject& payload,
                         const QString& key,
                         const QString& code,
                         const QString& path,
                         QVector<ValidationIssue>& issues,
                         QJsonObject& output) {
    if (!payload.contains(key)) {
        output = {};
        return true;
    }

    if (!payload.value(key).isObject()) {
        issues.append(issue(code, QStringLiteral("Patch field must be an object."), path));
        return false;
    }

    output = payload.value(key).toObject();
    return true;
}

bool applyAddComponentOperation(ProjectDesign& candidate,
                                const PatchOperation& operation,
                                qsizetype opIndex,
                                QVector<ValidationIssue>& issues) {
    if (operation.target != QStringLiteral("component")) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (operation.path != QStringLiteral("/components/-")) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    const QJsonObject payload = addComponentPayload(operation);
    if (payload.isEmpty()) {
        issues.append(issue(QStringLiteral("patch.invalid_component_payload"),
                            QStringLiteral("add component requires a payload object."),
                            QStringLiteral("/ops/%1/payload").arg(opIndex)));
        return false;
    }

    if (!isNonEmptyString(payload.value(QStringLiteral("id")))) {
        issues.append(issue(QStringLiteral("patch.component_missing_id"),
                            QStringLiteral("Component id is required."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    if (!isNonEmptyString(payload.value(QStringLiteral("type")))) {
        issues.append(issue(QStringLiteral("patch.component_missing_type"),
                            QStringLiteral("Component type is required."),
                            QStringLiteral("/ops/%1/payload/type").arg(opIndex)));
        return false;
    }

    if (!isNonEmptyString(payload.value(QStringLiteral("packageRef")))) {
        issues.append(issue(QStringLiteral("patch.component_missing_package_ref"),
                            QStringLiteral("Component packageRef is required."),
                            QStringLiteral("/ops/%1/payload/packageRef").arg(opIndex)));
        return false;
    }

    const QString componentId = payload.value(QStringLiteral("id")).toString();
    if (componentIndexById(candidate, componentId) >= 0) {
        issues.append(issue(QStringLiteral("patch.duplicate_component_id"),
                            QStringLiteral("Component id is duplicated."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    ComponentInstance component;
    component.id = componentId;
    component.type = payload.value(QStringLiteral("type")).toString();
    component.packageRef = resolveComponentPackageRef(
        candidate,
        payload.value(QStringLiteral("packageRef")).toString());

    if (!optionalObjectField(payload,
                             QStringLiteral("config"),
                             QStringLiteral("patch.invalid_component_config_shape"),
                             QStringLiteral("/ops/%1/payload/config").arg(opIndex),
                             issues,
                             component.config)) {
        return false;
    }
    if (containsLayoutConfigKey(component.config)) {
        issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                            QStringLiteral("Layout fields cannot be inserted into component config."),
                            QStringLiteral("/ops/%1/payload/config").arg(opIndex)));
        return false;
    }
    if (!optionalObjectField(payload,
                             QStringLiteral("identity"),
                             QStringLiteral("patch.invalid_component_identity_shape"),
                             QStringLiteral("/ops/%1/payload/identity").arg(opIndex),
                             issues,
                             component.identity)) {
        return false;
    }
    if (!optionalObjectField(payload,
                             QStringLiteral("metadata"),
                             QStringLiteral("patch.invalid_component_metadata_shape"),
                             QStringLiteral("/ops/%1/payload/metadata").arg(opIndex),
                             issues,
                             component.metadata)) {
        return false;
    }
    if (!optionalObjectField(payload,
                             QStringLiteral("extensionData"),
                             QStringLiteral("patch.invalid_component_extension_data_shape"),
                             QStringLiteral("/ops/%1/payload/extensionData").arg(opIndex),
                             issues,
                             component.extensionData)) {
        return false;
    }

    candidate.components.append(component);
    return true;
}

bool endpointFromObject(const QJsonObject& object, EndpointRef& endpoint) {
    endpoint.component = object.value(QStringLiteral("component")).toString();
    if (endpoint.component.trimmed().isEmpty()) {
        endpoint.component = object.value(QStringLiteral("componentId")).toString();
    }

    endpoint.interface = object.value(QStringLiteral("interface")).toString();
    if (endpoint.interface.trimmed().isEmpty()) {
        endpoint.interface = object.value(QStringLiteral("interfaceId")).toString();
    }

    return !endpoint.component.trimmed().isEmpty() &&
           !endpoint.interface.trimmed().isEmpty();
}

bool endpointsFromConnectionPayload(const QJsonObject& payload,
                                    qsizetype opIndex,
                                    QVector<ValidationIssue>& issues,
                                    EndpointRef& from,
                                    EndpointRef& to) {
    if (payload.value(QStringLiteral("from")).isObject() &&
        payload.value(QStringLiteral("to")).isObject()) {
        if (!endpointFromObject(payload.value(QStringLiteral("from")).toObject(), from)) {
            issues.append(issue(QStringLiteral("patch.connection_missing_endpoint"),
                                QStringLiteral("Connection from endpoint is required."),
                                QStringLiteral("/ops/%1/payload/from").arg(opIndex)));
            return false;
        }
        if (!endpointFromObject(payload.value(QStringLiteral("to")).toObject(), to)) {
            issues.append(issue(QStringLiteral("patch.connection_missing_endpoint"),
                                QStringLiteral("Connection to endpoint is required."),
                                QStringLiteral("/ops/%1/payload/to").arg(opIndex)));
            return false;
        }
        return true;
    }

    const QJsonArray endpoints = payload.value(QStringLiteral("endpoints")).toArray();
    if (endpoints.size() >= 2 && endpoints.at(0).isObject() && endpoints.at(1).isObject()) {
        if (endpointFromObject(endpoints.at(0).toObject(), from) &&
            endpointFromObject(endpoints.at(1).toObject(), to)) {
            return true;
        }
    }

    issues.append(issue(QStringLiteral("patch.connection_missing_endpoint"),
                        QStringLiteral("Connection endpoints are required."),
                        QStringLiteral("/ops/%1/payload/endpoints").arg(opIndex)));
    return false;
}

bool applyRemoveComponentOperation(ProjectDesign& candidate,
                                   const PatchOperation& operation,
                                   qsizetype opIndex,
                                   QVector<ValidationIssue>& issues) {
    QString componentId;
    if (!componentIdFromTarget(operation.target, componentId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (!operation.path.isEmpty()) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    const qsizetype componentIndex = componentIndexById(candidate, componentId);
    if (componentIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    candidate.components.removeAt(componentIndex);
    return true;
}

bool applyComponentConfigSetOperation(ProjectDesign& candidate,
                                      const PatchOperation& operation,
                                      qsizetype opIndex,
                                      QVector<ValidationIssue>& issues) {
    QString componentId;
    if (!componentIdFromTarget(operation.target, componentId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype componentIndex = componentIndexById(candidate, componentId);
    if (componentIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    QString configKey;
    if (!decodeJsonPointerSegment(operation.path, configKey)) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    if (operation.value.isUndefined()) {
        issues.append(issue(QStringLiteral("patch.invalid_op"),
                            QStringLiteral("component.config.set requires a value."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    if (isLayoutConfigKey(configKey)) {
        issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                            QStringLiteral("Layout fields cannot be set in component config."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    if (containsLayoutConfigKey(operation.value)) {
        issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                            QStringLiteral("Layout fields cannot be inserted into component config."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    candidate.components[componentIndex].config.insert(configKey, operation.value);
    return true;
}

bool applyComponentConfigUnsetOperation(ProjectDesign& candidate,
                                        const PatchOperation& operation,
                                        qsizetype opIndex,
                                        QVector<ValidationIssue>& issues) {
    QString componentId;
    if (!componentIdFromTarget(operation.target, componentId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype componentIndex = componentIndexById(candidate, componentId);
    if (componentIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    QString configKey;
    if (!decodeJsonPointerSegment(operation.path, configKey)) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    if (isLayoutConfigKey(configKey)) {
        issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                            QStringLiteral("Layout fields cannot be unset through component config."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    candidate.components[componentIndex].config.remove(configKey);
    return true;
}

bool applyAddConnectionOperation(ProjectDesign& candidate,
                                 const PatchOperation& operation,
                                 qsizetype opIndex,
                                 QVector<ValidationIssue>& issues) {
    if (operation.target != QStringLiteral("connection")) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (!operation.path.isEmpty() && operation.path != QStringLiteral("/connections/-")) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    const QJsonObject payload = addComponentPayload(operation);
    if (!isNonEmptyString(payload.value(QStringLiteral("id")))) {
        issues.append(issue(QStringLiteral("patch.connection_missing_id"),
                            QStringLiteral("Connection id is required."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    const QString connectionId = payload.value(QStringLiteral("id")).toString();
    if (connectionIndexById(candidate, connectionId) >= 0) {
        issues.append(issue(QStringLiteral("patch.duplicate_connection_id"),
                            QStringLiteral("Connection id is duplicated."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    Connection connection;
    connection.id = connectionId;
    if (!endpointsFromConnectionPayload(payload, opIndex, issues, connection.from, connection.to)) {
        return false;
    }

    if (isNonEmptyString(payload.value(QStringLiteral("kind")))) {
        connection.kind = payload.value(QStringLiteral("kind")).toString();
    }

    if (!optionalObjectField(payload,
                             QStringLiteral("config"),
                             QStringLiteral("patch.invalid_connection_config_shape"),
                             QStringLiteral("/ops/%1/payload/config").arg(opIndex),
                             issues,
                             connection.config)) {
        return false;
    }
    if (!optionalObjectField(payload,
                             QStringLiteral("constraints"),
                             QStringLiteral("patch.invalid_connection_constraints_shape"),
                             QStringLiteral("/ops/%1/payload/constraints").arg(opIndex),
                             issues,
                             connection.constraints)) {
        return false;
    }
    if (!optionalObjectField(payload,
                             QStringLiteral("metadata"),
                             QStringLiteral("patch.invalid_connection_metadata_shape"),
                             QStringLiteral("/ops/%1/payload/metadata").arg(opIndex),
                             issues,
                             connection.metadata)) {
        return false;
    }

    if (payload.contains(QStringLiteral("class"))) {
        if (!isNonEmptyString(payload.value(QStringLiteral("class")))) {
            issues.append(issue(QStringLiteral("patch.invalid_connection_class_shape"),
                                QStringLiteral("Connection class must be a non-empty string."),
                                QStringLiteral("/ops/%1/payload/class").arg(opIndex)));
            return false;
        }
        connection.metadata.insert(QStringLiteral("class"),
                                   payload.value(QStringLiteral("class")).toString());
    }
    if (payload.contains(QStringLiteral("status"))) {
        if (!isNonEmptyString(payload.value(QStringLiteral("status")))) {
            issues.append(issue(QStringLiteral("patch.invalid_connection_status_shape"),
                                QStringLiteral("Connection status must be a non-empty string."),
                                QStringLiteral("/ops/%1/payload/status").arg(opIndex)));
            return false;
        }
        connection.metadata.insert(QStringLiteral("status"),
                                   payload.value(QStringLiteral("status")).toString());
    }
    if (payload.contains(QStringLiteral("interfaces"))) {
        if (!payload.value(QStringLiteral("interfaces")).isArray()) {
            issues.append(issue(QStringLiteral("patch.invalid_connection_interfaces_shape"),
                                QStringLiteral("Connection interfaces must be an array."),
                                QStringLiteral("/ops/%1/payload/interfaces").arg(opIndex)));
            return false;
        }
        connection.metadata.insert(QStringLiteral("interfaces"),
                                   payload.value(QStringLiteral("interfaces")));
    }
    if (payload.contains(QStringLiteral("extensionData"))) {
        QJsonObject extensionData;
        if (!optionalObjectField(payload,
                                 QStringLiteral("extensionData"),
                                 QStringLiteral("patch.invalid_connection_extension_data_shape"),
                                 QStringLiteral("/ops/%1/payload/extensionData").arg(opIndex),
                                 issues,
                                 extensionData)) {
            return false;
        }
        connection.metadata.insert(QStringLiteral("extensionData"), extensionData);
    }

    candidate.connections.append(connection);
    return true;
}

bool applyRemoveConnectionOperation(ProjectDesign& candidate,
                                    const PatchOperation& operation,
                                    qsizetype opIndex,
                                    QVector<ValidationIssue>& issues) {
    QString connectionId;
    if (!connectionIdFromTarget(operation.target, connectionId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (!operation.path.isEmpty()) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    const qsizetype connectionIndex = connectionIndexById(candidate, connectionId);
    if (connectionIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    candidate.connections.removeAt(connectionIndex);
    return true;
}

bool applyConnectionConfigSetOperation(ProjectDesign& candidate,
                                       const PatchOperation& operation,
                                       qsizetype opIndex,
                                       QVector<ValidationIssue>& issues) {
    QString connectionId;
    if (!connectionIdFromTarget(operation.target, connectionId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype connectionIndex = connectionIndexById(candidate, connectionId);
    if (connectionIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    QString configKey;
    if (!decodeJsonPointerSegment(operation.path, configKey)) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    if (operation.value.isUndefined()) {
        issues.append(issue(QStringLiteral("patch.invalid_op"),
                            QStringLiteral("connection.config.set requires a value."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    candidate.connections[connectionIndex].config.insert(configKey, operation.value);
    return true;
}

bool applyConnectionMetadataSetOperation(ProjectDesign& candidate,
                                         const PatchOperation& operation,
                                         qsizetype opIndex,
                                         QVector<ValidationIssue>& issues) {
    QString connectionId;
    if (!connectionIdFromTarget(operation.target, connectionId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype connectionIndex = connectionIndexById(candidate, connectionId);
    if (connectionIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    QString metadataKey;
    if (!decodeJsonPointerSegment(operation.path, metadataKey)) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    if (operation.value.isUndefined()) {
        issues.append(issue(QStringLiteral("patch.invalid_op"),
                            QStringLiteral("connection.metadata.set requires a value."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    candidate.connections[connectionIndex].metadata.insert(metadataKey, operation.value);
    return true;
}

bool applyConnectionClassSetOperation(ProjectDesign& candidate,
                                      const PatchOperation& operation,
                                      qsizetype opIndex,
                                      QVector<ValidationIssue>& issues) {
    QString connectionId;
    if (!connectionIdFromTarget(operation.target, connectionId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype connectionIndex = connectionIndexById(candidate, connectionId);
    if (connectionIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (!operation.path.isEmpty()) {
        QString pathKey;
        if (!decodeJsonPointerSegment(operation.path, pathKey) ||
            pathKey != QStringLiteral("class")) {
            issues.append(issue(QStringLiteral("patch.unsupported_path"),
                                QStringLiteral("Patch path is not supported."),
                                QStringLiteral("/ops/%1/path").arg(opIndex)));
            return false;
        }
    }

    if (!isNonEmptyString(operation.value)) {
        issues.append(issue(QStringLiteral("patch.invalid_op"),
                            QStringLiteral("connection.class.set requires a class string."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    candidate.connections[connectionIndex].metadata.insert(QStringLiteral("class"),
                                                           operation.value.toString());
    return true;
}

bool viewFromPayload(const QJsonObject& payload,
                     qsizetype opIndex,
                     QVector<ValidationIssue>& issues,
                     ViewDocument& view) {
    if (!isNonEmptyString(payload.value(QStringLiteral("id")))) {
        issues.append(issue(QStringLiteral("patch.view_missing_id"),
                            QStringLiteral("View id is required."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    view.id = payload.value(QStringLiteral("id")).toString();
    view.schema = payload.value(QStringLiteral("schema")).toString();
    view.kind = payload.value(QStringLiteral("kind")).toString();
    view.targetRef = payload.value(QStringLiteral("targetRef")).toString();
    view.providerRef = payload.value(QStringLiteral("providerRef")).toString();
    view.sourceRef = payload.value(QStringLiteral("sourceRef")).toString();

    if (!optionalObjectField(payload, QStringLiteral("templates"),
                             QStringLiteral("patch.invalid_view_templates_shape"),
                             QStringLiteral("/ops/%1/payload/templates").arg(opIndex),
                             issues, view.templates) ||
        !optionalObjectField(payload, QStringLiteral("portGrouping"),
                             QStringLiteral("patch.invalid_view_port_grouping_shape"),
                             QStringLiteral("/ops/%1/payload/portGrouping").arg(opIndex),
                             issues, view.portGrouping) ||
        !optionalObjectField(payload, QStringLiteral("labels"),
                             QStringLiteral("patch.invalid_view_labels_shape"),
                             QStringLiteral("/ops/%1/payload/labels").arg(opIndex),
                             issues, view.labels) ||
        !optionalObjectField(payload, QStringLiteral("badges"),
                             QStringLiteral("patch.invalid_view_badges_shape"),
                             QStringLiteral("/ops/%1/payload/badges").arg(opIndex),
                             issues, view.badges) ||
        !optionalObjectField(payload, QStringLiteral("propertyGroups"),
                             QStringLiteral("patch.invalid_view_property_groups_shape"),
                             QStringLiteral("/ops/%1/payload/propertyGroups").arg(opIndex),
                             issues, view.propertyGroups) ||
        !optionalObjectField(payload, QStringLiteral("layoutPreference"),
                             QStringLiteral("patch.invalid_view_layout_preference_shape"),
                             QStringLiteral("/ops/%1/payload/layoutPreference").arg(opIndex),
                             issues, view.layoutPreference) ||
        !optionalObjectField(payload, QStringLiteral("interactionAffordances"),
                             QStringLiteral("patch.invalid_view_interaction_affordances_shape"),
                             QStringLiteral("/ops/%1/payload/interactionAffordances").arg(opIndex),
                             issues, view.interactionAffordances) ||
        !optionalObjectField(payload, QStringLiteral("diagnosticsOverlay"),
                             QStringLiteral("patch.invalid_view_diagnostics_overlay_shape"),
                             QStringLiteral("/ops/%1/payload/diagnosticsOverlay").arg(opIndex),
                             issues, view.diagnosticsOverlay) ||
        !optionalObjectField(payload, QStringLiteral("icons"),
                             QStringLiteral("patch.invalid_view_icons_shape"),
                             QStringLiteral("/ops/%1/payload/icons").arg(opIndex),
                             issues, view.icons) ||
        !optionalObjectField(payload, QStringLiteral("layout"),
                             QStringLiteral("patch.invalid_view_layout_shape"),
                             QStringLiteral("/ops/%1/payload/layout").arg(opIndex),
                             issues, view.layout) ||
        !optionalObjectField(payload, QStringLiteral("presentationState"),
                             QStringLiteral("patch.invalid_view_presentation_state_shape"),
                             QStringLiteral("/ops/%1/payload/presentationState").arg(opIndex),
                             issues, view.presentationState) ||
        !optionalObjectField(payload, QStringLiteral("metadata"),
                             QStringLiteral("patch.invalid_view_metadata_shape"),
                             QStringLiteral("/ops/%1/payload/metadata").arg(opIndex),
                             issues, view.metadata)) {
        return false;
    }

    return true;
}

bool applyViewLayoutSetOperation(ProjectDesign& candidate,
                                 const PatchOperation& operation,
                                 qsizetype opIndex,
                                 QVector<ValidationIssue>& issues) {
    QJsonObject payload;
    if (!operationObjectArgument(operation, payload)) {
        issues.append(issue(QStringLiteral("patch.invalid_view_layout_payload"),
                            QStringLiteral("view.layout.set requires an object payload or value."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    QString viewId;
    if (viewIdFromTarget(operation.target, viewId)) {
        if (!operation.path.isEmpty() && operation.path != QStringLiteral("/layout")) {
            issues.append(issue(QStringLiteral("patch.unsupported_path"),
                                QStringLiteral("Patch path is not supported."),
                                QStringLiteral("/ops/%1/path").arg(opIndex)));
            return false;
        }

        const qsizetype viewIndex = viewIndexById(candidate, viewId);
        if (viewIndex < 0) {
            issues.append(issue(QStringLiteral("patch.target_not_found"),
                                QStringLiteral("Patch target was not found."),
                                QStringLiteral("/ops/%1/target").arg(opIndex)));
            return false;
        }

        candidate.views[viewIndex].layout = payload;
        return true;
    }

    if (operation.target == QStringLiteral("view")) {
        ViewDocument view;
        if (!viewFromPayload(payload, opIndex, issues, view)) {
            return false;
        }

        const qsizetype viewIndex = viewIndexById(candidate, view.id);
        if (viewIndex >= 0) {
            candidate.views[viewIndex] = view;
        } else {
            candidate.views.append(view);
        }
        return true;
    }

    issues.append(issue(QStringLiteral("patch.invalid_target"),
                        QStringLiteral("Patch target is not supported."),
                        QStringLiteral("/ops/%1/target").arg(opIndex)));
    return false;
}

bool applyViewNodePositionSetOperation(ProjectDesign& candidate,
                                       const PatchOperation& operation,
                                       qsizetype opIndex,
                                       QVector<ValidationIssue>& issues) {
    QString viewId;
    if (!viewIdFromTarget(operation.target, viewId)) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    const qsizetype viewIndex = viewIndexById(candidate, viewId);
    if (viewIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    QString collection;
    QString componentId;
    if (!decodeJsonPointerTwoSegments(operation.path, collection, componentId) ||
        collection != QStringLiteral("nodes")) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    QJsonObject position;
    if (!operationObjectArgument(operation, position)) {
        issues.append(issue(QStringLiteral("patch.invalid_view_node_position_payload"),
                            QStringLiteral("view.node_position.set requires an object value."),
                            QStringLiteral("/ops/%1/value").arg(opIndex)));
        return false;
    }

    QJsonObject layout = candidate.views.at(viewIndex).layout;
    QJsonObject nodes = layout.value(QStringLiteral("nodes")).toObject();
    nodes.insert(componentId, position);
    layout.insert(QStringLiteral("nodes"), nodes);
    candidate.views[viewIndex].layout = layout;
    return true;
}

bool optionalObjectArrayField(const QJsonObject& payload,
                              const QString& key,
                              const QString& code,
                              const QString& path,
                              QVector<ValidationIssue>& issues,
                              QVector<QJsonObject>& output) {
    output = {};
    if (!payload.contains(key)) {
        return true;
    }

    if (!payload.value(key).isArray()) {
        issues.append(issue(code, QStringLiteral("Patch field must be an array."), path));
        return false;
    }

    const QJsonArray array = payload.value(key).toArray();
    output.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            issues.append(issue(code, QStringLiteral("Patch field array items must be objects."), path));
            output = {};
            return false;
        }
        output.append(value.toObject());
    }
    return true;
}

TopologyAttachment topologyAttachmentFromObject(const QJsonObject& object) {
    TopologyAttachment attachment;
    attachment.id = object.value(QStringLiteral("id")).toString();
    attachment.topologyId = object.value(QStringLiteral("topologyId")).toString();
    attachment.attachmentPoint = object.value(QStringLiteral("attachmentPoint")).toObject();
    attachment.componentRef = object.value(QStringLiteral("componentRef")).toString();
    attachment.interfaceRef = object.value(QStringLiteral("interfaceRef")).toString();
    attachment.adapterRef = object.value(QStringLiteral("adapterRef")).toString();
    attachment.config = object.value(QStringLiteral("config")).toObject();
    return attachment;
}

bool optionalAttachmentArrayField(const QJsonObject& payload,
                                  const QString& key,
                                  const QString& path,
                                  QVector<ValidationIssue>& issues,
                                  QVector<TopologyAttachment>& output) {
    output = {};
    if (!payload.contains(key)) {
        return true;
    }

    if (!payload.value(key).isArray()) {
        issues.append(issue(QStringLiteral("patch.invalid_topology_attachments_shape"),
                            QStringLiteral("Topology attachments must be an array."),
                            path));
        return false;
    }

    const QJsonArray array = payload.value(key).toArray();
    output.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            issues.append(issue(QStringLiteral("patch.invalid_topology_attachments_shape"),
                                QStringLiteral("Topology attachments must be objects."),
                                path));
            return false;
        }
        output.append(topologyAttachmentFromObject(value.toObject()));
    }
    return true;
}

bool topologyFromPayload(const QJsonObject& payload,
                         qsizetype opIndex,
                         QVector<ValidationIssue>& issues,
                         TopologyGraph& topology) {
    topology.id = payload.value(QStringLiteral("id")).toString();
    topology.schema = payload.value(QStringLiteral("schema")).toString();
    topology.ownerComponentId = payload.value(QStringLiteral("ownerComponentId")).toString();
    topology.kind = payload.value(QStringLiteral("kind")).toString();
    topology.family = payload.value(QStringLiteral("family")).toString();
    topology.providerRef = payload.value(QStringLiteral("providerRef")).toString();

    if (!optionalObjectField(payload, QStringLiteral("parameters"),
                             QStringLiteral("patch.invalid_topology_parameters_shape"),
                             QStringLiteral("/ops/%1/payload/parameters").arg(opIndex),
                             issues, topology.parameters) ||
        !optionalObjectField(payload, QStringLiteral("constraints"),
                             QStringLiteral("patch.invalid_topology_constraints_shape"),
                             QStringLiteral("/ops/%1/payload/constraints").arg(opIndex),
                             issues, topology.constraints) ||
        !optionalObjectArrayField(payload, QStringLiteral("nodes"),
                                  QStringLiteral("patch.invalid_topology_nodes_shape"),
                                  QStringLiteral("/ops/%1/payload/nodes").arg(opIndex),
                                  issues, topology.nodes) ||
        !optionalObjectArrayField(payload, QStringLiteral("links"),
                                  QStringLiteral("patch.invalid_topology_links_shape"),
                                  QStringLiteral("/ops/%1/payload/links").arg(opIndex),
                                  issues, topology.links) ||
        !optionalAttachmentArrayField(payload, QStringLiteral("attachments"),
                                      QStringLiteral("/ops/%1/payload/attachments").arg(opIndex),
                                      issues, topology.attachments) ||
        !optionalObjectField(payload, QStringLiteral("routing"),
                             QStringLiteral("patch.invalid_topology_routing_shape"),
                             QStringLiteral("/ops/%1/payload/routing").arg(opIndex),
                             issues, topology.routing) ||
        !optionalObjectField(payload, QStringLiteral("metadata"),
                             QStringLiteral("patch.invalid_topology_metadata_shape"),
                             QStringLiteral("/ops/%1/payload/metadata").arg(opIndex),
                             issues, topology.metadata)) {
        return false;
    }

    return true;
}

bool applyTopologyAddOrUpdateOperation(ProjectDesign& candidate,
                                       const PatchOperation& operation,
                                       qsizetype opIndex,
                                       QVector<ValidationIssue>& issues) {
    QString targetTopologyId;
    const bool hasTargetId = topologyIdFromTarget(operation.target, targetTopologyId);
    if (!hasTargetId && operation.target != QStringLiteral("topology")) {
        issues.append(issue(QStringLiteral("patch.invalid_target"),
                            QStringLiteral("Patch target is not supported."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    if (!operation.path.isEmpty() && operation.path != QStringLiteral("/topologies/-")) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    QJsonObject payload;
    if (!operationObjectArgument(operation, payload)) {
        issues.append(issue(QStringLiteral("patch.invalid_topology_payload"),
                            QStringLiteral("topology.add_or_update requires an object payload or value."),
                            QStringLiteral("/ops/%1/payload").arg(opIndex)));
        return false;
    }

    if (hasTargetId) {
        const QString payloadId = payload.value(QStringLiteral("id")).toString();
        if (!payloadId.trimmed().isEmpty() && payloadId != targetTopologyId) {
            issues.append(issue(QStringLiteral("patch.invalid_target"),
                                QStringLiteral("Topology target id does not match payload id."),
                                QStringLiteral("/ops/%1/target").arg(opIndex)));
            return false;
        }
        payload.insert(QStringLiteral("id"), targetTopologyId);
    }

    if (!isNonEmptyString(payload.value(QStringLiteral("id")))) {
        issues.append(issue(QStringLiteral("patch.topology_missing_id"),
                            QStringLiteral("Topology id is required."),
                            QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
        return false;
    }

    TopologyGraph topology;
    if (!topologyFromPayload(payload, opIndex, issues, topology)) {
        return false;
    }

    const qsizetype topologyIndex = topologyIndexById(candidate, topology.id);
    if (topologyIndex >= 0) {
        candidate.topologies[topologyIndex] = topology;
    } else {
        candidate.topologies.append(topology);
    }
    return true;
}

bool applyTopologyRemoveOperation(ProjectDesign& candidate,
                                  const PatchOperation& operation,
                                  qsizetype opIndex,
                                  QVector<ValidationIssue>& issues) {
    QString topologyId;
    if (!topologyIdFromTarget(operation.target, topologyId)) {
        if (operation.target != QStringLiteral("topology")) {
            issues.append(issue(QStringLiteral("patch.invalid_target"),
                                QStringLiteral("Patch target is not supported."),
                                QStringLiteral("/ops/%1/target").arg(opIndex)));
            return false;
        }

        QJsonObject payload;
        if (!operationObjectArgument(operation, payload) ||
            !isNonEmptyString(payload.value(QStringLiteral("id")))) {
            issues.append(issue(QStringLiteral("patch.topology_missing_id"),
                                QStringLiteral("Topology id is required."),
                                QStringLiteral("/ops/%1/payload/id").arg(opIndex)));
            return false;
        }
        topologyId = payload.value(QStringLiteral("id")).toString();
    }

    if (!operation.path.isEmpty()) {
        issues.append(issue(QStringLiteral("patch.unsupported_path"),
                            QStringLiteral("Patch path is not supported."),
                            QStringLiteral("/ops/%1/path").arg(opIndex)));
        return false;
    }

    const qsizetype topologyIndex = topologyIndexById(candidate, topologyId);
    if (topologyIndex < 0) {
        issues.append(issue(QStringLiteral("patch.target_not_found"),
                            QStringLiteral("Patch target was not found."),
                            QStringLiteral("/ops/%1/target").arg(opIndex)));
        return false;
    }

    candidate.topologies.removeAt(topologyIndex);
    return true;
}

bool applyOperation(ProjectDesign& candidate,
                    const PatchOperation& operation,
                    qsizetype opIndex,
                    QVector<ValidationIssue>& issues) {
    if (operation.op == ipcraft::patchops::componentAdd ||
        operation.op == QStringLiteral("add")) {
        return applyAddComponentOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::componentRemove) {
        return applyRemoveComponentOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::componentConfigSet ||
        operation.op == QStringLiteral("set_config")) {
        return applyComponentConfigSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::componentConfigUnset) {
        return applyComponentConfigUnsetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::connectionAdd) {
        return applyAddConnectionOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::connectionRemove) {
        return applyRemoveConnectionOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::connectionConfigSet) {
        return applyConnectionConfigSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::connectionMetadataSet) {
        return applyConnectionMetadataSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::connectionClassSet) {
        return applyConnectionClassSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::viewLayoutSet) {
        return applyViewLayoutSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::viewNodePositionSet) {
        return applyViewNodePositionSetOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::topologyAddOrUpdate) {
        return applyTopologyAddOrUpdateOperation(candidate, operation, opIndex, issues);
    }
    if (operation.op == ipcraft::patchops::topologyRemove) {
        return applyTopologyRemoveOperation(candidate, operation, opIndex, issues);
    }

    issues.append(issue(QStringLiteral("patch.unsupported_op"),
                        QStringLiteral("Patch operation is not supported."),
                        QStringLiteral("/ops/%1/op").arg(opIndex)));
    return false;
}

QJsonObject operationToJson(const PatchOperation& operation) {
    QJsonObject object = operation.payload;
    object.remove(QStringLiteral("op"));
    object.remove(QStringLiteral("target"));
    object.remove(QStringLiteral("path"));
    object.remove(QStringLiteral("value"));

    insertStringIfNonEmpty(object, QStringLiteral("op"), operation.op);
    insertStringIfNonEmpty(object, QStringLiteral("target"), operation.target);
    insertStringIfNonEmpty(object, QStringLiteral("path"), operation.path);
    if (!operation.value.isUndefined()) {
        object.insert(QStringLiteral("value"), operation.value);
    }

    return object;
}

PatchOperation operationFromJson(const QJsonObject& object) {
    PatchOperation operation;
    operation.op = object.value(QStringLiteral("op")).toString();
    operation.target = object.value(QStringLiteral("target")).toString();
    operation.path = object.value(QStringLiteral("path")).toString();
    operation.value = object.value(QStringLiteral("value"));
    operation.payload = object;
    return operation;
}

} // namespace

ProjectPatchReadResult ProjectPatchCodec::readObject(const QJsonObject& object) {
    ProjectPatchReadResult result;
    ProjectPatch patch;

    patch.schema = object.value(QStringLiteral("schema")).toString();
    patch.id = object.value(QStringLiteral("id")).toString();
    patch.description = object.value(QStringLiteral("description")).toString();
    patch.author = object.value(QStringLiteral("author")).toString();
    if (object.value(QStringLiteral("metadata")).isObject()) {
        patch.metadata = object.value(QStringLiteral("metadata")).toObject();
    } else if (object.contains(QStringLiteral("metadata"))) {
        result.issues.append(issue(QStringLiteral("patch.invalid_metadata_shape"),
                                   QStringLiteral("Patch metadata must be an object."),
                                   QStringLiteral("/metadata")));
    }

    appendUnknownTopLevelFieldIssues(result.issues, object);

    if (patch.schema != schemaids::patchV1) {
        result.patch = patch;
        result.issues.append(issue(QStringLiteral("patch.unsupported_schema"),
                                   QStringLiteral("Patch schema is not supported."),
                                   QStringLiteral("/schema")));
        return result;
    }

    const QJsonValue opsValue = object.value(QStringLiteral("ops"));
    if (!opsValue.isArray() || opsValue.toArray().isEmpty()) {
        result.issues.append(issue(QStringLiteral("patch.empty_ops"),
                                   QStringLiteral("Patch ops must be a non-empty array."),
                                   QStringLiteral("/ops")));
    } else {
        const QJsonArray ops = opsValue.toArray();
        patch.ops.reserve(ops.size());
        for (qsizetype index = 0; index < ops.size(); ++index) {
            const QJsonValue opValue = ops.at(index);
            if (!opValue.isObject()) {
                result.issues.append(issue(QStringLiteral("patch.invalid_op"),
                                           QStringLiteral("Patch operation must be an object."),
                                           QStringLiteral("/ops/%1").arg(index)));
                continue;
            }

            patch.ops.append(operationFromJson(opValue.toObject()));
        }
    }

    result.patch = patch;
    result.success = result.issues.isEmpty();
    return result;
}

QJsonObject ProjectPatchCodec::writeObject(const ProjectPatch& patch) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::patchV1);
    insertStringIfNonEmpty(object, QStringLiteral("id"), patch.id);
    insertStringIfNonEmpty(object, QStringLiteral("description"), patch.description);
    insertStringIfNonEmpty(object, QStringLiteral("author"), patch.author);
    if (!patch.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), patch.metadata);
    }

    QJsonArray ops;
    for (const PatchOperation& operation : patch.ops) {
        ops.append(operationToJson(operation));
    }
    object.insert(QStringLiteral("ops"), ops);

    return object;
}

ProjectPatchReadResult ProjectPatchApi::readObject(const QJsonObject& object) {
    return ProjectPatchCodec::readObject(object);
}

QJsonObject ProjectPatchApi::writeObject(const ProjectPatch& patch) {
    return ProjectPatchCodec::writeObject(patch);
}

PatchApplyResult applyPatch(const ProjectDesign& project, const ProjectPatch& patch) {
    PatchApplyResult result;
    result.project = project;

    if (patch.schema != schemaids::patchV1) {
        result.issues.append(issue(QStringLiteral("patch.unsupported_schema"),
                                   QStringLiteral("Patch schema is not supported."),
                                   QStringLiteral("/schema")));
        return result;
    }

    if (patch.ops.isEmpty()) {
        result.issues.append(issue(QStringLiteral("patch.empty_ops"),
                                   QStringLiteral("Patch ops must be a non-empty array."),
                                   QStringLiteral("/ops")));
        return result;
    }

    ProjectDesign candidate = project;
    for (qsizetype opIndex = 0; opIndex < patch.ops.size(); ++opIndex) {
        const PatchOperation& operation = patch.ops.at(opIndex);

        if (operation.op.isEmpty()) {
            result.issues.append(issue(QStringLiteral("patch.invalid_op"),
                                       QStringLiteral("Patch operation op is required."),
                                       QStringLiteral("/ops/%1/op").arg(opIndex)));
            return result;
        }

        if (!applyOperation(candidate, operation, opIndex, result.issues)) {
            return result;
        }
    }

    const QVector<ValidationIssue> validationIssues = validateProjectDesign(candidate);
    if (!validationIssues.isEmpty()) {
        result.issues = validationIssues;
        return result;
    }

    result.success = true;
    result.project = candidate;
    return result;
}

} // namespace ipcraft::core
