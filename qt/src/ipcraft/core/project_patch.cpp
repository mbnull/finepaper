#include "ipcraft/core/project_patch.h"

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

bool componentIdFromTarget(const QString& target, QString& componentId) {
    const QString prefix = QStringLiteral("component:");
    if (!target.startsWith(prefix)) {
        return false;
    }

    componentId = target.mid(prefix.size());
    return !componentId.trimmed().isEmpty() && !componentId.contains(QLatin1Char('/'));
}

bool decodeJsonPointerSegment(const QString& path, QString& key) {
    if (!path.startsWith(QLatin1Char('/')) || path.size() == 1) {
        return false;
    }

    key.clear();
    key.reserve(path.size() - 1);
    for (qsizetype index = 1; index < path.size(); ++index) {
        if (path.at(index) == QLatin1Char('/')) {
            key.clear();
            return false;
        }

        if (path.at(index) != QLatin1Char('~')) {
            key.append(path.at(index));
            continue;
        }

        ++index;
        if (index >= path.size()) {
            key.clear();
            return false;
        }

        if (path.at(index) == QLatin1Char('0')) {
            key.append(QLatin1Char('~'));
        } else if (path.at(index) == QLatin1Char('1')) {
            key.append(QLatin1Char('/'));
        } else {
            key.clear();
            return false;
        }
    }

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
        issues.append(issue(code, QStringLiteral("Component field must be an object."), path));
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

        if (operation.op == QStringLiteral("add")) {
            if (!applyAddComponentOperation(candidate, operation, opIndex, result.issues)) {
                return result;
            }
            continue;
        }

        if (operation.op != QStringLiteral("set_config")) {
            result.issues.append(issue(QStringLiteral("patch.unsupported_op"),
                                       QStringLiteral("Patch operation is not supported."),
                                       QStringLiteral("/ops/%1/op").arg(opIndex)));
            return result;
        }

        QString componentId;
        if (!componentIdFromTarget(operation.target, componentId)) {
            result.issues.append(issue(QStringLiteral("patch.invalid_target"),
                                       QStringLiteral("Patch target is not supported."),
                                       QStringLiteral("/ops/%1/target").arg(opIndex)));
            return result;
        }

        const qsizetype componentIndex = componentIndexById(candidate, componentId);
        if (componentIndex < 0) {
            result.issues.append(issue(QStringLiteral("patch.target_not_found"),
                                       QStringLiteral("Patch target was not found."),
                                       QStringLiteral("/ops/%1/target").arg(opIndex)));
            return result;
        }

        QString configKey;
        if (!decodeJsonPointerSegment(operation.path, configKey)) {
            result.issues.append(issue(QStringLiteral("patch.unsupported_path"),
                                       QStringLiteral("Patch path is not supported."),
                                       QStringLiteral("/ops/%1/path").arg(opIndex)));
            return result;
        }

        if (operation.value.isUndefined()) {
            result.issues.append(issue(QStringLiteral("patch.invalid_op"),
                                       QStringLiteral("set_config requires a value."),
                                       QStringLiteral("/ops/%1/value").arg(opIndex)));
            return result;
        }

        if (isLayoutConfigKey(configKey)) {
            result.issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                                       QStringLiteral("Layout fields cannot be set in component config."),
                                       QStringLiteral("/ops/%1/path").arg(opIndex)));
            return result;
        }

        if (containsLayoutConfigKey(operation.value)) {
            result.issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                                       QStringLiteral("Layout fields cannot be inserted into component config."),
                                       QStringLiteral("/ops/%1/value").arg(opIndex)));
            return result;
        }

        candidate.components[componentIndex].config.insert(configKey, operation.value);
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
