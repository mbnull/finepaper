#include "ipcraft/migration.h"

#include "ipcraft/schemaids.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <algorithm>

namespace {

constexpr qint64 kMaxProjectFileBytes = 16 * 1024 * 1024;

ipcraft::DiagnosticLocation documentLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ipcraft::DiagnosticLocation fileLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("file");
    location.file = path;
    return location;
}

void appendDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                      const QString& ruleId,
                      const QString& message,
                      const QString& path) {
    diagnostics.records.append(ipcraft::migrationDiagnostic(ruleId, message, path));
}

void appendFileDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                          const QString& ruleId,
                          const QString& message,
                          const QString& path) {
    ipcraft::Diagnostic diagnostic = ipcraft::migrationDiagnostic(ruleId, message);
    diagnostic.locations.clear();
    diagnostic.locations.append(fileLocation(path));
    diagnostics.records.append(diagnostic);
}

QString scopeKey(const QString& packageId, const QString& instanceId) {
    return packageId + QLatin1Char('/') + instanceId;
}

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

QJsonArray arrayValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray{};
}

QJsonObject layoutNodeObject(const QJsonObject& parameters) {
    QJsonObject node;
    const QJsonValue x = parameters.value(QStringLiteral("x"));
    if (x.isDouble()) {
        node.insert(QStringLiteral("x"), x);
    }
    const QJsonValue y = parameters.value(QStringLiteral("y"));
    if (y.isDouble()) {
        node.insert(QStringLiteral("y"), y);
    }
    const QJsonValue collapsed = parameters.value(QStringLiteral("collapsed"));
    if (collapsed.isBool()) {
        node.insert(QStringLiteral("collapsed"), collapsed);
    }
    return node;
}

QJsonObject nonLayoutParameters(QJsonObject parameters) {
    parameters.remove(QStringLiteral("x"));
    parameters.remove(QStringLiteral("y"));
    parameters.remove(QStringLiteral("collapsed"));
    return parameters;
}

bool insertParameter(QJsonObject* parameters,
                     const QString& key,
                     const QJsonValue& value,
                     ipcraft::DiagnosticStore& diagnostics,
                     const QString& path) {
    if (parameters->contains(key) && parameters->value(key) != value) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy parameter collision cannot be represented deterministically."),
                         path);
        return false;
    }
    parameters->insert(key, value);
    return true;
}

QString projectNameFromLegacy(const QJsonObject& root) {
    const QJsonObject project = objectValue(root, QStringLiteral("project"));
    const QString name = project.value(QStringLiteral("name")).toString().trimmed();
    return name.isEmpty() ? QStringLiteral("Untitled") : name;
}

QString projectVersionFromLegacy(const QJsonObject& root) {
    return objectValue(root, QStringLiteral("project"))
        .value(QStringLiteral("version"))
        .toString();
}

bool readPackageVersions(const QJsonObject& root,
                         QHash<QString, QString>* versions,
                         ipcraft::DiagnosticStore& diagnostics) {
    const QJsonValue ipcoresValue = root.value(QStringLiteral("ipcores"));
    if (ipcoresValue.isUndefined()) {
        return true;
    }
    if (!ipcoresValue.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy project ipcores must be an array."),
                         QStringLiteral("$.ipcores"));
        return false;
    }

    QSet<QString> ids;
    const QJsonArray ipcores = ipcoresValue.toArray();
    for (qsizetype index = 0; index < ipcores.size(); ++index) {
        if (!ipcores.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy ipcores entries must be objects."),
                             QStringLiteral("$.ipcores[%1]").arg(index));
            return false;
        }
        const QJsonObject ipcore = ipcores.at(index).toObject();
        if (!isNonEmptyString(ipcore.value(QStringLiteral("id"))) ||
            !isNonEmptyString(ipcore.value(QStringLiteral("version")))) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy ipcore id and version are required for exact package resolution."),
                             QStringLiteral("$.ipcores[%1]").arg(index));
            return false;
        }
        const QString id = ipcore.value(QStringLiteral("id")).toString();
        if (ids.contains(id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Duplicate legacy ipcore id cannot be migrated."),
                             QStringLiteral("$.ipcores[%1].id").arg(index));
            return false;
        }
        ids.insert(id);
        versions->insert(id, ipcore.value(QStringLiteral("version")).toString());
    }
    return true;
}

bool readLegacyInstances(const QJsonObject& root,
                         const QHash<QString, QString>& packageVersions,
                         ProjectDocument* document,
                         QHash<QString, qsizetype>* instanceIndexByScope,
                         ipcraft::DiagnosticStore& diagnostics) {
    const QJsonValue stateValue = root.value(QStringLiteral("ipcore_state"));
    if (stateValue.isUndefined()) {
        return true;
    }
    if (!stateValue.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy ipcore_state must be an array."),
                         QStringLiteral("$.ipcore_state"));
        return false;
    }

    QSet<QString> instanceIds;
    const QJsonArray states = stateValue.toArray();
    for (qsizetype index = 0; index < states.size(); ++index) {
        if (!states.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy ipcore_state entries must be objects."),
                             QStringLiteral("$.ipcore_state[%1]").arg(index));
            return false;
        }
        const QJsonObject stateEntry = states.at(index).toObject();
        if (!isNonEmptyString(stateEntry.value(QStringLiteral("schema")))) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy ipcore_state entries require schema."),
                             QStringLiteral("$.ipcore_state[%1]").arg(index));
            return false;
        }
        if (!isNonEmptyString(stateEntry.value(QStringLiteral("ipcore"))) ||
            !isNonEmptyString(stateEntry.value(QStringLiteral("instance"))) ||
            !stateEntry.value(QStringLiteral("state")).isObject()) {
            continue;
        }

        const QString packageId = stateEntry.value(QStringLiteral("ipcore")).toString();
        const QString instanceId = stateEntry.value(QStringLiteral("instance")).toString();
        if (!packageVersions.contains(packageId) && !packageVersions.isEmpty()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy instance package is not declared in ipcores."),
                             QStringLiteral("$.ipcore_state[%1].ipcore").arg(index));
            return false;
        }
        if (instanceIds.contains(instanceId)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Duplicate legacy instance id cannot be migrated."),
                             QStringLiteral("$.ipcore_state[%1].instance").arg(index));
            return false;
        }
        instanceIds.insert(instanceId);

        const QJsonObject state = stateEntry.value(QStringLiteral("state")).toObject();
        QJsonObject parameters;
        const QJsonValue globalParameters = state.value(QStringLiteral("global_parameters"));
        if (globalParameters.isObject()) {
            parameters = globalParameters.toObject();
        } else if (!globalParameters.isUndefined()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy global_parameters must be an object."),
                             QStringLiteral("$.ipcore_state[%1].state.global_parameters").arg(index));
            return false;
        }

        ProjectIpInstanceRecord instance;
        instance.id = instanceId;
        instance.displayName = state.value(QStringLiteral("type")).toString();
        instance.package.id = packageId;
        instance.package.version = packageVersions.value(packageId);
        if (instance.package.version.isEmpty()) {
            instance.package.version = stateEntry.value(QStringLiteral("version")).toString().trimmed();
        }
        if (instance.package.version.isEmpty()) {
            instance.package.version = QStringLiteral("unknown");
        }
        if (!parameters.isEmpty()) {
            instance.config.insert(QStringLiteral("parameters"), parameters);
        }
        instance.native.insert(QStringLiteral("legacy_schema"),
                               stateEntry.value(QStringLiteral("schema")).toString());

        instanceIndexByScope->insert(scopeKey(packageId, instanceId),
                                     document->instances.size());
        document->instances.append(instance);
    }
    return true;
}

QHash<QString, int> moduleCountByScope(const QJsonArray& modules) {
    QHash<QString, int> counts;
    for (const QJsonValue& value : modules) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject module = value.toObject();
        counts[scopeKey(module.value(QStringLiteral("ipcore")).toString(),
                        module.value(QStringLiteral("instance")).toString())] += 1;
    }
    return counts;
}

void appendObject(QHash<QString, QJsonArray>* arrays,
                  const QString& key,
                  const QJsonObject& object) {
    (*arrays)[key].append(object);
}

QJsonObject graphObjectFromModule(const QJsonObject& module) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), module.value(QStringLiteral("id")).toString());
    object.insert(QStringLiteral("type"),
                  module.value(QStringLiteral("type")).toString().trimmed().isEmpty()
                      ? QStringLiteral("legacy.module")
                      : module.value(QStringLiteral("type")).toString());
    QJsonObject properties;
    properties.insert(QStringLiteral("legacy_ipcore"),
                      module.value(QStringLiteral("ipcore")).toString());
    object.insert(QStringLiteral("properties"), properties);
    return object;
}

bool addRelationship(QHash<QString, QJsonArray>* relationshipsByScope,
                     const QString& scope,
                     const QJsonObject& relationship) {
    appendObject(relationshipsByScope, scope, relationship);
    return true;
}

bool connectionProperties(const QJsonObject& connection,
                          QJsonObject* properties,
                          ipcraft::DiagnosticStore& diagnostics,
                          const QString& path) {
    const QJsonValue classValue = connection.value(QStringLiteral("class"));
    if (!classValue.isUndefined()) {
        if (!classValue.isString()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy connection class must be a string."),
                             path + QStringLiteral(".class"));
            return false;
        }
        properties->insert(QStringLiteral("class"), classValue);
    }
    const QJsonValue statusValue = connection.value(QStringLiteral("status"));
    if (!statusValue.isUndefined()) {
        if (!statusValue.isString()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy connection status must be a string."),
                             path + QStringLiteral(".status"));
            return false;
        }
        properties->insert(QStringLiteral("status"), statusValue);
    }
    const QJsonValue alternativesValue = connection.value(QStringLiteral("alternatives"));
    if (!alternativesValue.isUndefined()) {
        if (!alternativesValue.isArray()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy connection alternatives must be an array."),
                             path + QStringLiteral(".alternatives"));
            return false;
        }
        properties->insert(QStringLiteral("alternatives"), alternativesValue);
    }
    return true;
}

bool appendSourceTargetRelationship(const QJsonObject& connection,
                                    qsizetype index,
                                    const QHash<QString, QString>& moduleScopeById,
                                    QHash<QString, QJsonArray>* relationshipsByScope,
                                    ipcraft::DiagnosticStore& diagnostics) {
    const QString path = QStringLiteral("$.graph.connections[%1]").arg(index);
    const QJsonObject source = connection.value(QStringLiteral("source")).toObject();
    const QJsonObject target = connection.value(QStringLiteral("target")).toObject();
    if (!isNonEmptyString(source.value(QStringLiteral("module"))) ||
        !isNonEmptyString(target.value(QStringLiteral("module")))) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy source/target connection requires module references."),
                         path);
        return false;
    }
    const QString sourceModule = source.value(QStringLiteral("module")).toString();
    const QString targetModule = target.value(QStringLiteral("module")).toString();
    if (!moduleScopeById.contains(sourceModule) || !moduleScopeById.contains(targetModule)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy connection references an unknown module."),
                         path);
        return false;
    }
    const QString scope = moduleScopeById.value(sourceModule);
    if (scope != moduleScopeById.value(targetModule)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Cross-instance legacy graph connections must be rebuilt as project composition."),
                         path);
        return false;
    }

    QJsonObject properties;
    if (!connectionProperties(connection, &properties, diagnostics, path)) {
        return false;
    }

    auto endpoint = [](const QString& objectId, const QString& role, const QJsonObject& endpoint) {
        QJsonObject endpointObject;
        endpointObject.insert(QStringLiteral("object"), objectId);
        endpointObject.insert(QStringLiteral("role"), role);
        if (isNonEmptyString(endpoint.value(QStringLiteral("port")))) {
            endpointObject.insert(QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("port"), endpoint.value(QStringLiteral("port")).toString()}
            });
        }
        return endpointObject;
    };

    QJsonObject relationship;
    relationship.insert(QStringLiteral("id"),
                        connection.value(QStringLiteral("id")).toString().trimmed().isEmpty()
                            ? QStringLiteral("connection_%1").arg(index)
                            : connection.value(QStringLiteral("id")).toString());
    relationship.insert(QStringLiteral("type"),
                        properties.value(QStringLiteral("class")).toString().trimmed().isEmpty()
                            ? QStringLiteral("legacy.connection")
                            : properties.value(QStringLiteral("class")).toString());
    relationship.insert(QStringLiteral("endpoints"), QJsonArray{
        endpoint(sourceModule, QStringLiteral("source"), source),
        endpoint(targetModule, QStringLiteral("target"), target)
    });
    if (!properties.isEmpty()) {
        relationship.insert(QStringLiteral("properties"), properties);
    }
    return addRelationship(relationshipsByScope, scope, relationship);
}

bool appendInterfaceRelationship(const QJsonObject& connection,
                                 qsizetype index,
                                 const QHash<QString, QString>& moduleScopeById,
                                 QHash<QString, QJsonArray>* relationshipsByScope,
                                 ipcraft::DiagnosticStore& diagnostics) {
    const QString path = QStringLiteral("$.graph.connections[%1]").arg(index);
    const QJsonValue interfacesValue = connection.value(QStringLiteral("interfaces"));
    if (!interfacesValue.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy interface connection requires an interfaces array."),
                         path + QStringLiteral(".interfaces"));
        return false;
    }

    QJsonArray endpoints;
    QString scope;
    const QJsonArray interfaces = interfacesValue.toArray();
    if (interfaces.size() < 2) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy interface connection requires at least two participants."),
                         path + QStringLiteral(".interfaces"));
        return false;
    }
    for (qsizetype endpointIndex = 0; endpointIndex < interfaces.size(); ++endpointIndex) {
        if (!interfaces.at(endpointIndex).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy interface participant must be an object."),
                             QStringLiteral("%1.interfaces[%2]").arg(path).arg(endpointIndex));
            return false;
        }
        const QJsonObject participant = interfaces.at(endpointIndex).toObject();
        if (!isNonEmptyString(participant.value(QStringLiteral("instance"))) ||
            !isNonEmptyString(participant.value(QStringLiteral("interface")))) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy interface participant requires instance and interface."),
                             QStringLiteral("%1.interfaces[%2]").arg(path).arg(endpointIndex));
            return false;
        }
        const QString moduleId = participant.value(QStringLiteral("instance")).toString();
        if (!moduleScopeById.contains(moduleId)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy interface connection references an unknown module."),
                             QStringLiteral("%1.interfaces[%2].instance").arg(path).arg(endpointIndex));
            return false;
        }
        if (scope.isEmpty()) {
            scope = moduleScopeById.value(moduleId);
        } else if (scope != moduleScopeById.value(moduleId)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Cross-instance legacy interface connections must be rebuilt as project composition."),
                             path);
            return false;
        }

        const QString interfaceId = participant.value(QStringLiteral("interface")).toString();
        endpoints.append(QJsonObject{
            {QStringLiteral("object"), moduleId},
            {QStringLiteral("role"), interfaceId},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("interface"), interfaceId}
            }}
        });
    }

    QJsonObject properties;
    if (!connectionProperties(connection, &properties, diagnostics, path)) {
        return false;
    }
    QJsonObject relationship;
    relationship.insert(QStringLiteral("id"),
                        connection.value(QStringLiteral("id")).toString().trimmed().isEmpty()
                            ? QStringLiteral("connection_%1").arg(index)
                            : connection.value(QStringLiteral("id")).toString());
    relationship.insert(QStringLiteral("type"),
                        properties.value(QStringLiteral("class")).toString().trimmed().isEmpty()
                            ? QStringLiteral("legacy.interface_connection")
                            : properties.value(QStringLiteral("class")).toString());
    relationship.insert(QStringLiteral("endpoints"), endpoints);
    if (!properties.isEmpty()) {
        relationship.insert(QStringLiteral("properties"), properties);
    }
    return addRelationship(relationshipsByScope, scope, relationship);
}

bool migrateLegacyGraph(const QJsonObject& root,
                        ProjectDocument* document,
                        const QHash<QString, qsizetype>& instanceIndexByScope,
                        ipcraft::DiagnosticStore& diagnostics) {
    const QJsonValue graphValue = root.value(QStringLiteral("graph"));
    if (graphValue.isUndefined()) {
        return true;
    }
    if (!graphValue.isObject()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy graph must be an object."),
                         QStringLiteral("$.graph"));
        return false;
    }
    const QJsonObject graph = graphValue.toObject();
    if (!graph.value(QStringLiteral("modules")).isArray() ||
        !graph.value(QStringLiteral("connections")).isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("migration.unsupported_legacy_content"),
                         QStringLiteral("Legacy graph modules and connections must be arrays."),
                         QStringLiteral("$.graph"));
        return false;
    }
    QJsonObject nodes;
    QHash<QString, QString> moduleScopeById;
    QHash<QString, QJsonArray> graphObjectsByScope;
    QHash<QString, QJsonArray> graphRelationshipsByScope;
    const QJsonArray modules = graph.value(QStringLiteral("modules")).toArray();
    const QHash<QString, int> moduleCounts = moduleCountByScope(modules);
    QSet<QString> moduleIds;
    for (qsizetype index = 0; index < modules.size(); ++index) {
        if (!modules.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy graph modules entries must be objects."),
                             QStringLiteral("$.graph.modules[%1]").arg(index));
            return false;
        }
        const QJsonObject module = modules.at(index).toObject();
        if (!isNonEmptyString(module.value(QStringLiteral("id"))) ||
            !isNonEmptyString(module.value(QStringLiteral("ipcore"))) ||
            !isNonEmptyString(module.value(QStringLiteral("instance"))) ||
            !module.value(QStringLiteral("parameters")).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy graph module requires id, ipcore, instance, and object parameters."),
                             QStringLiteral("$.graph.modules[%1]").arg(index));
            return false;
        }

        const QString moduleId = module.value(QStringLiteral("id")).toString();
        if (moduleIds.contains(moduleId)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Duplicate legacy module id cannot be migrated."),
                             QStringLiteral("$.graph.modules[%1].id").arg(index));
            return false;
        }
        moduleIds.insert(moduleId);

        const QString scope = scopeKey(module.value(QStringLiteral("ipcore")).toString(),
                                       module.value(QStringLiteral("instance")).toString());
        if (!instanceIndexByScope.contains(scope)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy module owner does not match a migrated instance."),
                             QStringLiteral("$.graph.modules[%1]").arg(index));
            return false;
        }
        moduleScopeById.insert(moduleId, scope);
        appendObject(&graphObjectsByScope, scope, graphObjectFromModule(module));

        const QJsonObject parameters =
            module.value(QStringLiteral("parameters")).toObject();
        const QJsonObject layoutNode = layoutNodeObject(parameters);
        if (!layoutNode.isEmpty()) {
            nodes.insert(moduleId, layoutNode);
        }

        const QJsonObject nonLayout = nonLayoutParameters(parameters);
        if (!nonLayout.isEmpty()) {
            if (moduleCounts.value(scope) != 1) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("migration.unsupported_legacy_content"),
                                 QStringLiteral("Module parameters are ambiguous when multiple legacy modules map to one instance."),
                                 QStringLiteral("$.graph.modules[%1].parameters").arg(index));
                return false;
            }
            ProjectIpInstanceRecord& instance =
                document->instances[instanceIndexByScope.value(scope)];
            QJsonObject configParameters =
                instance.config.value(QStringLiteral("parameters")).toObject();
            for (auto it = nonLayout.constBegin(); it != nonLayout.constEnd(); ++it) {
                if (!insertParameter(&configParameters,
                                     it.key(),
                                     it.value(),
                                     diagnostics,
                                     QStringLiteral("$.graph.modules[%1].parameters.%2")
                                         .arg(index)
                                         .arg(it.key()))) {
                    return false;
                }
            }
            instance.config.insert(QStringLiteral("parameters"), configParameters);
        }
    }

    const QJsonArray connections = graph.value(QStringLiteral("connections")).toArray();
    QSet<QString> connectionIds;
    for (qsizetype index = 0; index < connections.size(); ++index) {
        if (!connections.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy graph connections entries must be objects."),
                             QStringLiteral("$.graph.connections[%1]").arg(index));
            return false;
        }
        const QJsonObject connection = connections.at(index).toObject();
        const QString connectionId = connection.value(QStringLiteral("id")).toString();
        if (!connectionId.trimmed().isEmpty()) {
            if (connectionIds.contains(connectionId)) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("migration.unsupported_legacy_content"),
                                 QStringLiteral("Duplicate legacy connection id cannot be migrated."),
                                 QStringLiteral("$.graph.connections[%1].id").arg(index));
                return false;
            }
            connectionIds.insert(connectionId);
        }

        const bool hasSourceTarget =
            connection.value(QStringLiteral("source")).isObject() &&
            connection.value(QStringLiteral("target")).isObject();
        const bool hasInterfaces = connection.contains(QStringLiteral("interfaces"));
        if (hasSourceTarget) {
            if (!appendSourceTargetRelationship(connection,
                                                index,
                                                moduleScopeById,
                                                &graphRelationshipsByScope,
                                                diagnostics)) {
                return false;
            }
        } else if (hasInterfaces) {
            if (!appendInterfaceRelationship(connection,
                                             index,
                                             moduleScopeById,
                                             &graphRelationshipsByScope,
                                             diagnostics)) {
                return false;
            }
        } else {
            appendDiagnostic(diagnostics,
                             QStringLiteral("migration.unsupported_legacy_content"),
                             QStringLiteral("Legacy graph connection shape is not supported."),
                             QStringLiteral("$.graph.connections[%1]").arg(index));
            return false;
        }
    }

    if (!nodes.isEmpty()) {
        document->layout.insert(QStringLiteral("views"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("graph")},
                {QStringLiteral("kind"), QStringLiteral("canvas")},
                {QStringLiteral("canvas"), QJsonObject{
                    {QStringLiteral("nodes"), nodes},
                    {QStringLiteral("connections"), QJsonObject{}}
                }}
            }
        });
    }
    for (auto it = graphObjectsByScope.constBegin(); it != graphObjectsByScope.constEnd(); ++it) {
        const qsizetype instanceIndex = instanceIndexByScope.value(it.key(), -1);
        if (instanceIndex < 0 || instanceIndex >= document->instances.size()) {
            continue;
        }
        ProjectIpInstanceRecord& instance = document->instances[instanceIndex];
        instance.hasGraphConfig = true;
        instance.graphConfigIsNull = false;
        instance.graphConfig = QJsonObject{
            {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
            {QStringLiteral("objects"), it.value()},
            {QStringLiteral("relationships"), graphRelationshipsByScope.value(it.key())},
            {QStringLiteral("properties"), QJsonObject{}},
            {QStringLiteral("native"), QJsonObject{}}
        };
    }
    return true;
}

} // namespace

namespace ipcraft {

Diagnostic migrationDiagnostic(const QString& ruleId,
                               const QString& message,
                               const QString& path) {
    Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("migration");
    diagnostic.ruleId = ruleId;
    diagnostic.category = QStringLiteral("migration");
    diagnostic.message = message;
    diagnostic.locations.append(documentLocation(path));
    return diagnostic;
}

ProjectMigrationResult ProjectMigrator::migrateFile(const QString& path,
                                                    const QString& targetSchema) {
    ProjectMigrationResult result;
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.input_missing"),
                             QStringLiteral("Could not open project file for migration."),
                             path);
        return result;
    }
    if (!fileInfo.isFile()) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.invalid_input"),
                             QStringLiteral("Migration input must be a regular file."),
                             path);
        return result;
    }
    if (fileInfo.size() > kMaxProjectFileBytes) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.invalid_input"),
                             QStringLiteral("Project file is too large for migration."),
                             path);
        return result;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.input_missing"),
                             QStringLiteral("Could not open project file for migration."),
                             path);
        return result;
    }

    const QByteArray bytes = file.read(kMaxProjectFileBytes + 1);
    if (bytes.size() > kMaxProjectFileBytes) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.invalid_input"),
                             QStringLiteral("Project file is too large for migration."),
                             path);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        appendFileDiagnostic(result.diagnostics,
                             QStringLiteral("migration.invalid_input"),
                             QStringLiteral("Project file is not a JSON object."),
                             path);
        return result;
    }
    return migrateJson(json.object(), targetSchema);
}

ProjectMigrationResult ProjectMigrator::migrateJson(const QJsonObject& root,
                                                    const QString& targetSchema) {
    ProjectMigrationResult result;
    if (targetSchema.trimmed().isEmpty()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("migration.target_required"),
                         QStringLiteral("Migration target schema is required."),
                         QStringLiteral("$.to"));
        return result;
    }
    if (targetSchema != schemaids::projectV1) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("migration.unsupported_target"),
                         QStringLiteral("Migration target schema is not supported."),
                         QStringLiteral("$.to"));
        return result;
    }
    if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("v1") ||
        (!root.value(QStringLiteral("kind")).isUndefined() &&
         root.value(QStringLiteral("kind")).toString() != QStringLiteral("finepaper-project"))) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("migration.unsupported_schema"),
                         QStringLiteral("Input is not a supported legacy Finepaper project."),
                         QStringLiteral("$.schema"));
        return result;
    }

    ProjectDocument document;
    document.projectId = QStringLiteral("project_0");
    document.projectName = projectNameFromLegacy(root);
    document.migration.fromSchema = root.value(QStringLiteral("schema")).toString();
    document.migration.fromVersion = projectVersionFromLegacy(root);

    QJsonObject preserved;
    preserved.insert(QStringLiteral("ipcore_state"),
                     root.value(QStringLiteral("ipcore_state")).isArray()
                         ? root.value(QStringLiteral("ipcore_state")).toArray()
                         : QJsonArray{});
    if (root.value(QStringLiteral("graph")).isObject()) {
        preserved.insert(QStringLiteral("graph"), root.value(QStringLiteral("graph")).toObject());
    }
    document.migration.preserved.insert(QStringLiteral("legacy_state"), preserved);

    QHash<QString, QString> packageVersions;
    QHash<QString, qsizetype> instanceIndexByScope;
    bool ok = readPackageVersions(root, &packageVersions, result.diagnostics);
    if (ok) {
        ok = readLegacyInstances(root,
                                 packageVersions,
                                 &document,
                                 &instanceIndexByScope,
                                 result.diagnostics);
    }
    if (ok) {
        ok = migrateLegacyGraph(root, &document, instanceIndexByScope, result.diagnostics);
    }
    if (!ok) {
        return result;
    }

    result.ok = true;
    result.document = document;
    return result;
}

} // namespace ipcraft
