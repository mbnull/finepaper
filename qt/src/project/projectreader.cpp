// ProjectReader parses Ipcraft V1 project JSON into ProjectDocument records.
#include "project/projectreader.h"

#include "ipcraft/compositionmodel.h"
#include "ipcraft/schemaids.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

namespace {

constexpr qint64 kMaxProjectFileBytes = 16 * 1024 * 1024;

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}

ipcraft::DiagnosticLocation documentPathLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ProjectReadResult failure(const QString& ruleId,
                          const QString& message,
                          const QString& path = QStringLiteral("$")) {
    ProjectReadResult result;
    result.error = message;

    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("project.reader");
    diagnostic.ruleId = ruleId;
    diagnostic.category = QStringLiteral("project");
    diagnostic.message = message;
    diagnostic.locations.append(documentPathLocation(path));
    result.diagnostics.records.append(diagnostic);
    return result;
}

ProjectReadResult diagnosticsFailure(ipcraft::DiagnosticStore diagnostics,
                                     const QString& message,
                                     const QString& pathPrefix) {
    ProjectReadResult result;
    result.error = message;
    for (ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        for (ipcraft::DiagnosticLocation& location : diagnostic.locations) {
            if (location.kind == QStringLiteral("document_path") &&
                location.path.startsWith(QLatin1Char('$'))) {
                location.path = pathPrefix + location.path.mid(1);
            }
        }
        result.diagnostics.records.append(diagnostic);
    }
    return result;
}

ProjectReadResult ioFailure(const QString& ruleId,
                            const QString& message,
                            const QString& filePath) {
    ProjectReadResult result = failure(ruleId, message);
    result.diagnostics.records.first().locations.clear();
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("file");
    location.file = filePath;
    result.diagnostics.records.first().locations.append(location);
    return result;
}

bool hasOnlyKeys(const QJsonObject& object,
                 const QSet<QString>& allowedKeys,
                 QString* unknownKey) {
    for (const QString& key : object.keys()) {
        if (!allowedKeys.contains(key)) {
            if (unknownKey != nullptr) {
                *unknownKey = key;
            }
            return false;
        }
    }
    return true;
}

QString objectPath(const QString& base, const QString& key) {
    return base + QStringLiteral(".") + key;
}

ProjectReadResult optionalObjectValue(const QJsonObject& object,
                                      const QString& key,
                                      const QString& path,
                                      QJsonObject* output) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        *output = {};
        return {};
    }
    if (!value.isObject()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Project field %1 must be an object").arg(path),
                       path);
    }
    *output = value.toObject();
    return {};
}

ProjectReadResult optionalStringValue(const QJsonObject& object,
                                      const QString& key,
                                      const QString& path,
                                      bool requireNonEmpty,
                                      QString* output) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        *output = {};
        return {};
    }
    if (!value.isString()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Project field %1 must be a string").arg(path),
                       path);
    }
    const QString stringValue = value.toString();
    if (requireNonEmpty && stringValue.trimmed().isEmpty()) {
        return failure(QStringLiteral("project.invalid_value"),
                       QStringLiteral("Project field %1 must not be empty").arg(path),
                       path);
    }
    *output = stringValue;
    return {};
}

QJsonArray arrayValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray{};
}

ProjectPackageRef packageRefFromObject(const QJsonObject& object) {
    ProjectPackageRef package;
    package.id = object.value(QStringLiteral("id")).toString();
    package.version = object.value(QStringLiteral("version")).toString();
    return package;
}

ProjectReadResult validateKeys(const QJsonObject& object,
                               const QSet<QString>& allowedKeys,
                               const QString& path) {
    QString unknownKey;
    if (!hasOnlyKeys(object, allowedKeys, &unknownKey)) {
        return failure(QStringLiteral("project.unknown_field"),
                       QStringLiteral("Unknown project field: %1").arg(unknownKey),
                       objectPath(path, unknownKey));
    }
    return {};
}

ProjectReadResult readEndpoint(const QJsonObject& object,
                               const QString& path,
                               ProjectEndpointRef* output) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("instance"),
        QStringLiteral("interface"),
        QStringLiteral("port"),
        QStringLiteral("role"),
        QStringLiteral("properties")
    };

    ProjectReadResult keyResult = validateKeys(object, allowedKeys, path);
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (!isNonEmptyString(object.value(QStringLiteral("instance")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Connection endpoint instance is required"),
                       objectPath(path, QStringLiteral("instance")));
    }
    if (!isNonEmptyString(object.value(QStringLiteral("interface")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Connection endpoint interface is required"),
                       objectPath(path, QStringLiteral("interface")));
    }

    QJsonObject properties;
    ProjectReadResult propertiesResult = optionalObjectValue(object,
                                                            QStringLiteral("properties"),
                                                            objectPath(path, QStringLiteral("properties")),
                                                            &properties);
    if (!propertiesResult.diagnostics.records.isEmpty()) {
        return propertiesResult;
    }

    ProjectEndpointRef endpoint;
    endpoint.instanceId = object.value(QStringLiteral("instance")).toString();
    endpoint.interfaceId = object.value(QStringLiteral("interface")).toString();
    ProjectReadResult portResult = optionalStringValue(object,
                                                       QStringLiteral("port"),
                                                       objectPath(path, QStringLiteral("port")),
                                                       true,
                                                       &endpoint.portId);
    if (!portResult.diagnostics.records.isEmpty()) {
        return portResult;
    }
    ProjectReadResult roleResult = optionalStringValue(object,
                                                       QStringLiteral("role"),
                                                       objectPath(path, QStringLiteral("role")),
                                                       true,
                                                       &endpoint.role);
    if (!roleResult.diagnostics.records.isEmpty()) {
        return roleResult;
    }
    endpoint.properties = properties;
    *output = endpoint;
    return {};
}

ProjectReadResult readConnection(const QJsonObject& object,
                                 const QString& path,
                                 ProjectConnectionRecord* output) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("type"),
        QStringLiteral("endpoints"),
        QStringLiteral("source"),
        QStringLiteral("properties"),
        QStringLiteral("native")
    };

    ProjectReadResult keyResult = validateKeys(object, allowedKeys, path);
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (!isNonEmptyString(object.value(QStringLiteral("id")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Connection id is required"),
                       objectPath(path, QStringLiteral("id")));
    }
    if (!object.value(QStringLiteral("endpoints")).isArray()) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Connection endpoints array is required"),
                       objectPath(path, QStringLiteral("endpoints")));
    }

    ProjectConnectionRecord connection;
    connection.id = object.value(QStringLiteral("id")).toString();
    ProjectReadResult typeResult = optionalStringValue(object,
                                                       QStringLiteral("type"),
                                                       objectPath(path, QStringLiteral("type")),
                                                       true,
                                                       &connection.type);
    if (!typeResult.diagnostics.records.isEmpty()) {
        return typeResult;
    }
    ProjectReadResult sourceResult = optionalStringValue(object,
                                                         QStringLiteral("source"),
                                                         objectPath(path, QStringLiteral("source")),
                                                         true,
                                                         &connection.sourceKind);
    if (!sourceResult.diagnostics.records.isEmpty()) {
        return sourceResult;
    }
    if (!connection.sourceKind.isEmpty() &&
        connection.sourceKind != QStringLiteral("user") &&
        connection.sourceKind != QStringLiteral("generated") &&
        connection.sourceKind != QStringLiteral("imported")) {
        return failure(QStringLiteral("project.invalid_value"),
                       QStringLiteral("Connection source must be user, generated, or imported"),
                       objectPath(path, QStringLiteral("source")));
    }

    ProjectReadResult propertiesResult = optionalObjectValue(object,
                                                            QStringLiteral("properties"),
                                                            objectPath(path, QStringLiteral("properties")),
                                                            &connection.properties);
    if (!propertiesResult.diagnostics.records.isEmpty()) {
        return propertiesResult;
    }
    ProjectReadResult nativeResult = optionalObjectValue(object,
                                                        QStringLiteral("native"),
                                                        objectPath(path, QStringLiteral("native")),
                                                        &connection.native);
    if (!nativeResult.diagnostics.records.isEmpty()) {
        return nativeResult;
    }

    for (const QJsonValue& endpointValue : arrayValue(object, QStringLiteral("endpoints"))) {
        if (!endpointValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Connection endpoints entries must be objects"),
                           objectPath(path, QStringLiteral("endpoints[]")));
        }

        ProjectEndpointRef endpoint;
        ProjectReadResult endpointResult = readEndpoint(endpointValue.toObject(),
                                                        objectPath(path, QStringLiteral("endpoints[]")),
                                                        &endpoint);
        if (!endpointResult.diagnostics.records.isEmpty()) {
            return endpointResult;
        }
        connection.endpoints.append(endpoint);
    }
    if (connection.endpoints.size() < 2) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Connection endpoints must contain at least two endpoints"),
                       objectPath(path, QStringLiteral("endpoints")));
    }

    *output = connection;
    return {};
}

ProjectReadResult validateEndpointInstances(const QVector<ProjectEndpointRef>& endpoints,
                                            const QSet<QString>& instanceIds,
                                            const QString& path) {
    for (const ProjectEndpointRef& endpoint : endpoints) {
        if (!instanceIds.contains(endpoint.instanceId)) {
            return failure(QStringLiteral("project.unknown_instance"),
                           QStringLiteral("Unknown endpoint instance: %1").arg(endpoint.instanceId),
                           path);
        }
    }
    return {};
}

ProjectReadResult readExternalPort(const QJsonObject& object,
                                   const QString& path,
                                   ProjectExternalPortRecord* output) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("interface"),
        QStringLiteral("properties"),
        QStringLiteral("native")
    };

    ProjectReadResult keyResult = validateKeys(object, allowedKeys, path);
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (!isNonEmptyString(object.value(QStringLiteral("id")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("External port id is required"),
                       objectPath(path, QStringLiteral("id")));
    }

    ProjectExternalPortRecord port;
    port.id = object.value(QStringLiteral("id")).toString();
    ProjectReadResult nameResult = optionalStringValue(object,
                                                       QStringLiteral("name"),
                                                       objectPath(path, QStringLiteral("name")),
                                                       false,
                                                       &port.name);
    if (!nameResult.diagnostics.records.isEmpty()) {
        return nameResult;
    }
    const QJsonValue interfaceValue = object.value(QStringLiteral("interface"));
    if (interfaceValue.isObject()) {
        port.hasInterface = true;
        ProjectReadResult endpointResult = readEndpoint(interfaceValue.toObject(),
                                                        objectPath(path, QStringLiteral("interface")),
                                                        &port.interfaceRef);
        if (!endpointResult.diagnostics.records.isEmpty()) {
            return endpointResult;
        }
    } else if (!interfaceValue.isUndefined()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("External port interface must be an object"),
                       objectPath(path, QStringLiteral("interface")));
    }

    ProjectReadResult propertiesResult = optionalObjectValue(object,
                                                            QStringLiteral("properties"),
                                                            objectPath(path, QStringLiteral("properties")),
                                                            &port.properties);
    if (!propertiesResult.diagnostics.records.isEmpty()) {
        return propertiesResult;
    }
    ProjectReadResult nativeResult = optionalObjectValue(object,
                                                        QStringLiteral("native"),
                                                        objectPath(path, QStringLiteral("native")),
                                                        &port.native);
    if (!nativeResult.diagnostics.records.isEmpty()) {
        return nativeResult;
    }
    *output = port;
    return {};
}

ProjectReadResult readMigration(const QJsonObject& object, ProjectMigration* output) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("from_schema"),
        QStringLiteral("from_version"),
        QStringLiteral("preserved"),
        QStringLiteral("metadata"),
        QStringLiteral("native")
    };
    ProjectReadResult keyResult = validateKeys(object,
                                               allowedKeys,
                                               QStringLiteral("$.migration"));
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }

    ProjectMigration migration;
    ProjectReadResult schemaResult = optionalStringValue(object,
                                                        QStringLiteral("from_schema"),
                                                        QStringLiteral("$.migration.from_schema"),
                                                        false,
                                                        &migration.fromSchema);
    if (!schemaResult.diagnostics.records.isEmpty()) {
        return schemaResult;
    }
    ProjectReadResult versionResult = optionalStringValue(object,
                                                         QStringLiteral("from_version"),
                                                         QStringLiteral("$.migration.from_version"),
                                                         false,
                                                         &migration.fromVersion);
    if (!versionResult.diagnostics.records.isEmpty()) {
        return versionResult;
    }
    ProjectReadResult preservedResult = optionalObjectValue(object,
                                                           QStringLiteral("preserved"),
                                                           QStringLiteral("$.migration.preserved"),
                                                           &migration.preserved);
    if (!preservedResult.diagnostics.records.isEmpty()) {
        return preservedResult;
    }
    ProjectReadResult metadataResult = optionalObjectValue(object,
                                                          QStringLiteral("metadata"),
                                                          QStringLiteral("$.migration.metadata"),
                                                          &migration.metadata);
    if (!metadataResult.diagnostics.records.isEmpty()) {
        return metadataResult;
    }
    ProjectReadResult nativeResult = optionalObjectValue(object,
                                                        QStringLiteral("native"),
                                                        QStringLiteral("$.migration.native"),
                                                        &migration.native);
    if (!nativeResult.diagnostics.records.isEmpty()) {
        return nativeResult;
    }
    *output = migration;
    return {};
}

ProjectReadResult validateRootKeys(const QJsonObject& root) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("schema"),
        QStringLiteral("project"),
        QStringLiteral("instances"),
        QStringLiteral("composition"),
        QStringLiteral("layout"),
        QStringLiteral("diagnostics"),
        QStringLiteral("artifacts"),
        QStringLiteral("migration"),
        QStringLiteral("native")
    };

    QString unknownKey;
    if (!hasOnlyKeys(root, allowedKeys, &unknownKey)) {
        return failure(QStringLiteral("project.unknown_field"),
                       QStringLiteral("Unknown project field: %1").arg(unknownKey),
                       objectPath(QStringLiteral("$"), unknownKey));
    }
    return {};
}

ProjectReadResult validateProjectObject(const QJsonObject& project) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("description"),
        QStringLiteral("display"),
        QStringLiteral("metadata"),
        QStringLiteral("native")
    };

    QString unknownKey;
    if (!hasOnlyKeys(project, allowedKeys, &unknownKey)) {
        return failure(QStringLiteral("project.unknown_field"),
                       QStringLiteral("Unknown project metadata field: %1").arg(unknownKey),
                       objectPath(QStringLiteral("$.project"), unknownKey));
    }
    if (!isNonEmptyString(project.value(QStringLiteral("id")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Project project.id is required"),
                       QStringLiteral("$.project.id"));
    }
    if (!isNonEmptyString(project.value(QStringLiteral("name")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Project project.name is required"),
                       QStringLiteral("$.project.name"));
    }
    return {};
}

ProjectReadResult validatePackageRef(const QJsonObject& package, const QString& instanceId) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("version")
    };
    ProjectReadResult keyResult = validateKeys(package,
                                               allowedKeys,
                                               QStringLiteral("$.instances[].package"));
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (!isNonEmptyString(package.value(QStringLiteral("id")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Instance %1 package.id is required").arg(instanceId),
                       QStringLiteral("$.instances[].package.id"));
    }
    if (!isNonEmptyString(package.value(QStringLiteral("version")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Instance %1 package.version is required").arg(instanceId),
                       QStringLiteral("$.instances[].package.version"));
    }
    return {};
}

ProjectReadResult validateConfigBundle(const QJsonObject& config) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("parameters"),
        QStringLiteral("tables"),
        QStringLiteral("documents"),
        QStringLiteral("files"),
        QStringLiteral("preserved")
    };
    ProjectReadResult keyResult = validateKeys(config,
                                               allowedKeys,
                                               QStringLiteral("$.instances[].config"));
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    for (const QString& key : allowedKeys) {
        const QJsonValue value = config.value(key);
        if (!value.isUndefined() && !value.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Config bundle %1 must be an object").arg(key),
                           objectPath(QStringLiteral("$.instances[].config"), key));
        }
    }
    return {};
}

ProjectReadResult validateDiagnosticsObject(const QJsonObject& diagnostics,
                                            bool fieldPresent,
                                            const QString& path) {
    if (!fieldPresent && diagnostics.isEmpty()) {
        return {};
    }
    static const QSet<QString> allowedKeys = {
        QStringLiteral("schema"),
        QStringLiteral("records")
    };
    ProjectReadResult keyResult = validateKeys(diagnostics, allowedKeys, path);
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (diagnostics.value(QStringLiteral("schema")).toString() !=
        ipcraft::schemaids::diagnosticsV1) {
        return failure(QStringLiteral("project.invalid_value"),
                       QStringLiteral("Diagnostics schema must be ipcraft.diagnostics.v1"),
                       objectPath(path, QStringLiteral("schema")));
    }
    if (!diagnostics.value(QStringLiteral("records")).isArray()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Diagnostics records must be an array"),
                       objectPath(path, QStringLiteral("records")));
    }
    const QJsonArray records = diagnostics.value(QStringLiteral("records")).toArray();
    for (const QJsonValue& recordValue : records) {
        if (!recordValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Diagnostics records entries must be objects"),
                           objectPath(path, QStringLiteral("records[]")));
        }
        const QJsonObject record = recordValue.toObject();
        static const QSet<QString> recordKeys = {
            QStringLiteral("severity"),
            QStringLiteral("source"),
            QStringLiteral("rule_id"),
            QStringLiteral("category"),
            QStringLiteral("message"),
            QStringLiteral("details"),
            QStringLiteral("locations")
        };
        ProjectReadResult recordKeyResult = validateKeys(record,
                                                         recordKeys,
                                                         objectPath(path, QStringLiteral("records[]")));
        if (!recordKeyResult.diagnostics.records.isEmpty()) {
            return recordKeyResult;
        }
        if (!isNonEmptyString(record.value(QStringLiteral("severity"))) ||
            !isNonEmptyString(record.value(QStringLiteral("source"))) ||
            !isNonEmptyString(record.value(QStringLiteral("rule_id"))) ||
            !record.value(QStringLiteral("message")).isString() ||
            !record.value(QStringLiteral("locations")).isArray()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Diagnostics record is missing required fields"),
                           objectPath(path, QStringLiteral("records[]")));
        }
        const QString severity = record.value(QStringLiteral("severity")).toString();
        if (severity != QStringLiteral("error") &&
            severity != QStringLiteral("warning") &&
            severity != QStringLiteral("info")) {
            return failure(QStringLiteral("project.invalid_value"),
                           QStringLiteral("Diagnostics severity is invalid"),
                           objectPath(path, QStringLiteral("records[].severity")));
        }
        const QJsonValue detailsValue = record.value(QStringLiteral("details"));
        if (!detailsValue.isUndefined() && !detailsValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Diagnostics details must be an object"),
                           objectPath(path, QStringLiteral("records[].details")));
        }
        for (const QJsonValue& locationValue : record.value(QStringLiteral("locations")).toArray()) {
            if (!locationValue.isObject() ||
                !isNonEmptyString(locationValue.toObject().value(QStringLiteral("kind")))) {
                return failure(QStringLiteral("project.type_mismatch"),
                               QStringLiteral("Diagnostics location must be an object with a kind"),
                               objectPath(path, QStringLiteral("records[].locations[]")));
            }
        }
    }
    return {};
}

ProjectReadResult readInstance(const QJsonObject& object,
                               QSet<QString>* instanceIds,
                               ProjectIpInstanceRecord* output) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("id"),
        QStringLiteral("display_name"),
        QStringLiteral("package"),
        QStringLiteral("config"),
        QStringLiteral("graph_config"),
        QStringLiteral("native"),
        QStringLiteral("last_runs"),
        QStringLiteral("artifacts"),
        QStringLiteral("diagnostics"),
        QStringLiteral("view")
    };
    ProjectReadResult keyResult = validateKeys(object,
                                               allowedKeys,
                                               QStringLiteral("$.instances[]"));
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }

    if (!isNonEmptyString(object.value(QStringLiteral("id")))) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Instance id is required"),
                       QStringLiteral("$.instances[].id"));
    }

    const QString instanceId = object.value(QStringLiteral("id")).toString();
    if (instanceIds->contains(instanceId)) {
        return failure(QStringLiteral("project.duplicate_id"),
                       QStringLiteral("Duplicate instance id: %1").arg(instanceId),
                       QStringLiteral("$.instances"));
    }
    instanceIds->insert(instanceId);

    const QJsonValue packageValue = object.value(QStringLiteral("package"));
    if (!packageValue.isObject()) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Instance %1 package is required").arg(instanceId),
                       QStringLiteral("$.instances[].package"));
    }

    const QJsonObject package = packageValue.toObject();
    ProjectReadResult packageResult = validatePackageRef(package, instanceId);
    if (!packageResult.diagnostics.records.isEmpty()) {
        return packageResult;
    }

    ProjectIpInstanceRecord instance;
    instance.id = instanceId;
    ProjectReadResult displayNameResult = optionalStringValue(object,
                                                             QStringLiteral("display_name"),
                                                             QStringLiteral("$.instances[].display_name"),
                                                             false,
                                                             &instance.displayName);
    if (!displayNameResult.diagnostics.records.isEmpty()) {
        return displayNameResult;
    }
    instance.package = packageRefFromObject(package);
    ProjectReadResult configObjectResult = optionalObjectValue(object,
                                                              QStringLiteral("config"),
                                                              QStringLiteral("$.instances[].config"),
                                                              &instance.config);
    if (!configObjectResult.diagnostics.records.isEmpty()) {
        return configObjectResult;
    }
    ProjectReadResult configResult = validateConfigBundle(instance.config);
    if (!configResult.diagnostics.records.isEmpty()) {
        return configResult;
    }
    ProjectReadResult nativeResult = optionalObjectValue(object,
                                                        QStringLiteral("native"),
                                                        QStringLiteral("$.instances[].native"),
                                                        &instance.native);
    if (!nativeResult.diagnostics.records.isEmpty()) {
        return nativeResult;
    }
    ProjectReadResult lastRunsResult = optionalObjectValue(object,
                                                          QStringLiteral("last_runs"),
                                                          QStringLiteral("$.instances[].last_runs"),
                                                          &instance.lastRuns);
    if (!lastRunsResult.diagnostics.records.isEmpty()) {
        return lastRunsResult;
    }
    ProjectReadResult artifactsResult = optionalObjectValue(object,
                                                           QStringLiteral("artifacts"),
                                                           QStringLiteral("$.instances[].artifacts"),
                                                           &instance.artifacts);
    if (!artifactsResult.diagnostics.records.isEmpty()) {
        return artifactsResult;
    }
    ProjectReadResult diagnosticsResult = optionalObjectValue(object,
                                                             QStringLiteral("diagnostics"),
                                                             QStringLiteral("$.instances[].diagnostics"),
                                                             &instance.diagnostics);
    if (!diagnosticsResult.diagnostics.records.isEmpty()) {
        return diagnosticsResult;
    }
    ProjectReadResult diagnosticsShapeResult = validateDiagnosticsObject(
        instance.diagnostics,
        object.contains(QStringLiteral("diagnostics")),
        QStringLiteral("$.instances[].diagnostics"));
    if (!diagnosticsShapeResult.diagnostics.records.isEmpty()) {
        return diagnosticsShapeResult;
    }
    ProjectReadResult viewResult = optionalObjectValue(object,
                                                      QStringLiteral("view"),
                                                      QStringLiteral("$.instances[].view"),
                                                      &instance.view);
    if (!viewResult.diagnostics.records.isEmpty()) {
        return viewResult;
    }
    const QJsonValue graphConfig = object.value(QStringLiteral("graph_config"));
    if (graphConfig.isObject()) {
        const ipcraft::GraphConfigReadResult graphConfigResult =
            ipcraft::GraphConfig::fromJson(graphConfig.toObject());
        if (!graphConfigResult.diagnostics.records.isEmpty()) {
            return diagnosticsFailure(graphConfigResult.diagnostics,
                                      QStringLiteral("Instance graph_config is invalid"),
                                      QStringLiteral("$.instances[].graph_config"));
        }
        ipcraft::DiagnosticStore graphConfigDiagnostics =
            ipcraft::validateGraphConfig(graphConfigResult.config);
        if (!graphConfigDiagnostics.records.isEmpty()) {
            return diagnosticsFailure(graphConfigDiagnostics,
                                      QStringLiteral("Instance graph_config is invalid"),
                                      QStringLiteral("$.instances[].graph_config"));
        }
        instance.hasGraphConfig = true;
        instance.graphConfig = graphConfig.toObject();
    } else if (graphConfig.isNull()) {
        instance.hasGraphConfig = true;
        instance.graphConfigIsNull = true;
    } else if (!graphConfig.isUndefined()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Instance graph_config must be an object or null"),
                       QStringLiteral("$.instances[].graph_config"));
    }

    // Transitional aliases let existing editor adapters keep working while the
    // persisted project shape remains canonical ProjectDocument V1.
    instance.instanceId = instance.id;
    instance.ipcoreId = instance.package.id;
    const QJsonValue parameters = instance.config.value(QStringLiteral("parameters"));
    if (parameters.isObject()) {
        instance.state.insert(QStringLiteral("global_parameters"), parameters.toObject());
    }

    *output = instance;
    return {};
}

void appendGraphConfigProjection(const ProjectIpInstanceRecord& instance,
                                 ProjectDocument* document) {
    if (!instance.hasGraphConfig || instance.graphConfigIsNull) {
        return;
    }

    const QJsonArray objects = instance.graphConfig.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue& objectValue : objects) {
        if (!objectValue.isObject()) {
            continue;
        }
        const QJsonObject object = objectValue.toObject();
        ProjectModuleRecord module;
        module.id = object.value(QStringLiteral("id")).toString();
        module.ipcoreId = instance.package.id;
        module.instanceId = instance.id;
        module.type = object.value(QStringLiteral("type")).toString();
        module.parameters = object.value(QStringLiteral("properties")).toObject();
        if (!module.id.trimmed().isEmpty() && !module.type.trimmed().isEmpty()) {
            document->modules.append(module);
        }
    }

    const QJsonArray relationships =
        instance.graphConfig.value(QStringLiteral("relationships")).toArray();
    for (const QJsonValue& relationshipValue : relationships) {
        if (!relationshipValue.isObject()) {
            continue;
        }
        const QJsonObject relationship = relationshipValue.toObject();
        ProjectConnectionRecord connection;
        connection.id = relationship.value(QStringLiteral("id")).toString();
        connection.connectionClassId = relationship.value(QStringLiteral("type")).toString();
        const QJsonObject properties = relationship.value(QStringLiteral("properties")).toObject();
        connection.properties = properties;
        connection.status = properties.value(QStringLiteral("status"))
            .toString(QStringLiteral("valid"));
        const QJsonArray alternatives = properties.value(QStringLiteral("alternatives")).toArray();
        for (const QJsonValue& alternative : alternatives) {
            if (alternative.isString()) {
                connection.alternatives.append(alternative.toString());
            }
        }

        const QJsonArray endpoints = relationship.value(QStringLiteral("endpoints")).toArray();
        for (const QJsonValue& endpointValue : endpoints) {
            if (!endpointValue.isObject()) {
                continue;
            }
            const QJsonObject endpoint = endpointValue.toObject();
            ProjectConnectionInterfaceRef interfaceRef;
            interfaceRef.instanceId = endpoint.value(QStringLiteral("object")).toString();
            interfaceRef.interfaceId = endpoint.value(QStringLiteral("role")).toString();
            interfaceRef.properties = endpoint.value(QStringLiteral("properties")).toObject();
            connection.interfaces.append(interfaceRef);
        }
        if (!connection.id.trimmed().isEmpty() && connection.interfaces.size() >= 2) {
            document->connections.append(connection);
        }
    }
}

bool isLayoutParameter(const QString& key) {
    return key == QStringLiteral("x") ||
           key == QStringLiteral("y") ||
           key == QStringLiteral("collapsed");
}

bool isValidLayoutParameterValue(const QString& key, const QJsonValue& value) {
    if (key == QStringLiteral("collapsed")) {
        return value.isBool();
    }
    if (key == QStringLiteral("x") || key == QStringLiteral("y")) {
        return value.isDouble();
    }
    return false;
}

QHash<QString, QJsonObject> layoutNodesById(const QJsonObject& layout) {
    QHash<QString, QJsonObject> nodesById;
    const QJsonArray views = layout.value(QStringLiteral("views")).toArray();
    for (const QJsonValue& viewValue : views) {
        if (!viewValue.isObject()) {
            continue;
        }
        const QJsonObject view = viewValue.toObject();
        if (view.value(QStringLiteral("id")).toString() != QStringLiteral("graph")) {
            continue;
        }
        const QJsonObject nodes = view
            .value(QStringLiteral("canvas"))
            .toObject()
            .value(QStringLiteral("nodes"))
            .toObject();
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            if (it.value().isObject()) {
                nodesById.insert(it.key(), it.value().toObject());
            }
        }
    }
    return nodesById;
}

void applyLayoutProjection(ProjectDocument* document) {
    const QHash<QString, QJsonObject> nodes = layoutNodesById(document->layout);
    if (nodes.isEmpty()) {
        return;
    }

    for (ProjectModuleRecord& module : document->modules) {
        const QJsonObject node = nodes.value(module.id);
        for (auto it = node.constBegin(); it != node.constEnd(); ++it) {
            if (isLayoutParameter(it.key()) && isValidLayoutParameterValue(it.key(), it.value())) {
                module.parameters.insert(it.key(), it.value());
            }
        }
    }
}

ProjectReadResult validateCompositionObject(const QJsonObject& composition) {
    static const QSet<QString> allowedKeys = {
        QStringLiteral("connections"),
        QStringLiteral("external_ports"),
        QStringLiteral("groups"),
        QStringLiteral("properties"),
        QStringLiteral("native")
    };
    ProjectReadResult keyResult = validateKeys(composition,
                                               allowedKeys,
                                               QStringLiteral("$.composition"));
    if (!keyResult.diagnostics.records.isEmpty()) {
        return keyResult;
    }
    if (!composition.value(QStringLiteral("connections")).isUndefined() &&
        !composition.value(QStringLiteral("connections")).isArray()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Composition connections must be an array"),
                       QStringLiteral("$.composition.connections"));
    }
    if (!composition.value(QStringLiteral("external_ports")).isUndefined() &&
        !composition.value(QStringLiteral("external_ports")).isArray()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Composition external_ports must be an array"),
                       QStringLiteral("$.composition.external_ports"));
    }
    if (!composition.value(QStringLiteral("groups")).isUndefined() &&
        !composition.value(QStringLiteral("groups")).isArray()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Composition groups must be an array"),
                       QStringLiteral("$.composition.groups"));
    }
    const QJsonArray groups = composition.value(QStringLiteral("groups")).toArray();
    for (qsizetype index = 0; index < groups.size(); ++index) {
        if (!groups.at(index).isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Composition groups entries must be objects"),
                           QStringLiteral("$.composition.groups[%1]").arg(index));
        }
    }
    return {};
}

} // namespace

ProjectFileKind ProjectReader::detectKind(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ProjectFileKind::Unknown;
    }
    if (file.size() > kMaxProjectFileBytes) {
        return ProjectFileKind::Unknown;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return ProjectFileKind::Unknown;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1) {
        return ProjectFileKind::Project;
    }

    return ProjectFileKind::Unknown;
}

ProjectReadResult ProjectReader::readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ioFailure(QStringLiteral("project.read_failed"),
                         QStringLiteral("Could not open project file: %1").arg(path),
                         path);
    }
    if (file.size() > kMaxProjectFileBytes) {
        return ioFailure(QStringLiteral("project.file_too_large"),
                         QStringLiteral("Project file is too large: ") +
                             QFileInfo(path).fileName() +
                             QStringLiteral(" (max ") +
                             QString::number(kMaxProjectFileBytes / (1024 * 1024)) +
                             QStringLiteral(" MiB)"),
                         path);
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return ioFailure(QStringLiteral("project.invalid_json"),
                         QStringLiteral("Invalid project JSON: %1").arg(parseError.errorString()),
                         path);
    }
    if (!json.isObject()) {
        return ioFailure(QStringLiteral("project.invalid_json"),
                         QStringLiteral("Project JSON root must be an object"),
                         path);
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("schema")).toString() != ipcraft::schemaids::projectV1) {
        return failure(QStringLiteral("project.unsupported_schema"),
                       QStringLiteral("Unsupported project schema: %1")
                           .arg(root.value(QStringLiteral("schema")).toString()),
                       QStringLiteral("$.schema"));
    }

    ProjectReadResult rootKeysResult = validateRootKeys(root);
    if (!rootKeysResult.diagnostics.records.isEmpty()) {
        return rootKeysResult;
    }

    const QJsonValue projectValue = root.value(QStringLiteral("project"));
    if (!projectValue.isObject()) {
        return failure(QStringLiteral("project.missing_required"),
                       QStringLiteral("Project project object is required"),
                       QStringLiteral("$.project"));
    }

    const QJsonObject project = projectValue.toObject();
    ProjectReadResult projectResult = validateProjectObject(project);
    if (!projectResult.diagnostics.records.isEmpty()) {
        return projectResult;
    }

    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = project.value(QStringLiteral("id")).toString();
    document.projectName = project.value(QStringLiteral("name")).toString();
    ProjectReadResult descriptionResult = optionalStringValue(project,
                                                             QStringLiteral("description"),
                                                             QStringLiteral("$.project.description"),
                                                             false,
                                                             &document.projectDescription);
    if (!descriptionResult.diagnostics.records.isEmpty()) {
        return descriptionResult;
    }
    ProjectReadResult displayResult = optionalObjectValue(project,
                                                         QStringLiteral("display"),
                                                         QStringLiteral("$.project.display"),
                                                         &document.projectDisplay);
    if (!displayResult.diagnostics.records.isEmpty()) {
        return displayResult;
    }
    ProjectReadResult metadataResult = optionalObjectValue(project,
                                                          QStringLiteral("metadata"),
                                                          QStringLiteral("$.project.metadata"),
                                                          &document.projectMetadata);
    if (!metadataResult.diagnostics.records.isEmpty()) {
        return metadataResult;
    }
    ProjectReadResult projectNativeResult = optionalObjectValue(project,
                                                               QStringLiteral("native"),
                                                               QStringLiteral("$.project.native"),
                                                               &document.projectNative);
    if (!projectNativeResult.diagnostics.records.isEmpty()) {
        return projectNativeResult;
    }
    const QJsonValue instancesValue = root.value(QStringLiteral("instances"));
    if (!instancesValue.isUndefined() && !instancesValue.isArray()) {
        return failure(QStringLiteral("project.type_mismatch"),
                       QStringLiteral("Project instances must be an array"),
                       QStringLiteral("$.instances"));
    }

    QSet<QString> instanceIds;
    for (const QJsonValue& instanceValue : instancesValue.toArray()) {
        if (!instanceValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Project instances entries must be objects"),
                           QStringLiteral("$.instances[]"));
        }

        ProjectIpInstanceRecord instance;
        ProjectReadResult instanceResult = readInstance(instanceValue.toObject(),
                                                        &instanceIds,
                                                        &instance);
        if (!instanceResult.diagnostics.records.isEmpty()) {
            return instanceResult;
        }
        document.instances.append(instance);
        document.ipcoreState.append(instance);
        appendGraphConfigProjection(instance, &document);
    }

    QJsonObject composition;
    ProjectReadResult compositionObjectResult = optionalObjectValue(root,
                                                                    QStringLiteral("composition"),
                                                                    QStringLiteral("$.composition"),
                                                                    &composition);
    if (!compositionObjectResult.diagnostics.records.isEmpty()) {
        return compositionObjectResult;
    }
    ProjectReadResult compositionResult = validateCompositionObject(composition);
    if (!compositionResult.diagnostics.records.isEmpty()) {
        return compositionResult;
    }
    QSet<QString> connectionIds;
    for (const QJsonValue& connectionValue : arrayValue(composition,
                                                        QStringLiteral("connections"))) {
        if (!connectionValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Composition connections entries must be objects"),
                           QStringLiteral("$.composition.connections[]"));
        }

        ProjectConnectionRecord connection;
        ProjectReadResult connectionResult = readConnection(connectionValue.toObject(),
                                                            QStringLiteral("$.composition.connections[]"),
                                                            &connection);
        if (!connectionResult.diagnostics.records.isEmpty()) {
            return connectionResult;
        }
        ProjectReadResult endpointInstanceResult = validateEndpointInstances(
            connection.endpoints,
            instanceIds,
            QStringLiteral("$.composition.connections[].endpoints[]"));
        if (!endpointInstanceResult.diagnostics.records.isEmpty()) {
            return endpointInstanceResult;
        }
        if (connectionIds.contains(connection.id)) {
            return failure(QStringLiteral("project.duplicate_id"),
                           QStringLiteral("Duplicate connection id: %1").arg(connection.id),
                           QStringLiteral("$.composition.connections"));
        }
        connectionIds.insert(connection.id);
        document.composition.connections.append(connection);
    }
    QSet<QString> externalPortIds;
    for (const QJsonValue& portValue : arrayValue(composition,
                                                  QStringLiteral("external_ports"))) {
        if (!portValue.isObject()) {
            return failure(QStringLiteral("project.type_mismatch"),
                           QStringLiteral("Composition external_ports entries must be objects"),
                           QStringLiteral("$.composition.external_ports[]"));
        }

        ProjectExternalPortRecord port;
        ProjectReadResult portResult = readExternalPort(portValue.toObject(),
                                                        QStringLiteral("$.composition.external_ports[]"),
                                                        &port);
        if (!portResult.diagnostics.records.isEmpty()) {
            return portResult;
        }
        if (port.hasInterface) {
            ProjectReadResult endpointInstanceResult = validateEndpointInstances(
                QVector<ProjectEndpointRef>{port.interfaceRef},
                instanceIds,
                QStringLiteral("$.composition.external_ports[].interface"));
            if (!endpointInstanceResult.diagnostics.records.isEmpty()) {
                return endpointInstanceResult;
            }
        }
        if (externalPortIds.contains(port.id)) {
            return failure(QStringLiteral("project.duplicate_id"),
                           QStringLiteral("Duplicate external port id: %1").arg(port.id),
                           QStringLiteral("$.composition.external_ports"));
        }
        externalPortIds.insert(port.id);
        document.composition.externalPorts.append(port);
    }
    document.composition.groups = arrayValue(composition, QStringLiteral("groups"));
    ProjectReadResult compositionPropertiesResult = optionalObjectValue(
        composition,
        QStringLiteral("properties"),
        QStringLiteral("$.composition.properties"),
        &document.composition.properties);
    if (!compositionPropertiesResult.diagnostics.records.isEmpty()) {
        return compositionPropertiesResult;
    }
    ProjectReadResult compositionNativeResult = optionalObjectValue(
        composition,
        QStringLiteral("native"),
        QStringLiteral("$.composition.native"),
        &document.composition.native);
    if (!compositionNativeResult.diagnostics.records.isEmpty()) {
        return compositionNativeResult;
    }

    ProjectReadResult layoutResult = optionalObjectValue(root,
                                                        QStringLiteral("layout"),
                                                        QStringLiteral("$.layout"),
                                                        &document.layout);
    if (!layoutResult.diagnostics.records.isEmpty()) {
        return layoutResult;
    }
    applyLayoutProjection(&document);
    QJsonObject diagnostics;
    ProjectReadResult diagnosticsResult = optionalObjectValue(root,
                                                             QStringLiteral("diagnostics"),
                                                             QStringLiteral("$.diagnostics"),
                                                             &diagnostics);
    if (!diagnosticsResult.diagnostics.records.isEmpty()) {
        return diagnosticsResult;
    }
    ProjectReadResult diagnosticsShapeResult = validateDiagnosticsObject(
        diagnostics,
        root.contains(QStringLiteral("diagnostics")),
        QStringLiteral("$.diagnostics"));
    if (!diagnosticsShapeResult.diagnostics.records.isEmpty()) {
        return diagnosticsShapeResult;
    }
    document.diagnostics = ipcraft::DiagnosticStore::fromJson(diagnostics);
    ProjectReadResult artifactsResult = optionalObjectValue(root,
                                                           QStringLiteral("artifacts"),
                                                           QStringLiteral("$.artifacts"),
                                                           &document.artifacts);
    if (!artifactsResult.diagnostics.records.isEmpty()) {
        return artifactsResult;
    }
    QJsonObject migration;
    ProjectReadResult migrationResult = optionalObjectValue(root,
                                                           QStringLiteral("migration"),
                                                           QStringLiteral("$.migration"),
                                                           &migration);
    if (!migrationResult.diagnostics.records.isEmpty()) {
        return migrationResult;
    }
    ProjectReadResult migrationReadResult = readMigration(migration, &document.migration);
    if (!migrationReadResult.diagnostics.records.isEmpty()) {
        return migrationReadResult;
    }
    ProjectReadResult nativeResult = optionalObjectValue(root,
                                                        QStringLiteral("native"),
                                                        QStringLiteral("$.native"),
                                                        &document.native);
    if (!nativeResult.diagnostics.records.isEmpty()) {
        return nativeResult;
    }

    ProjectReadResult result;
    result.success = true;
    result.document = document;
    return result;
}
