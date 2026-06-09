// ProjectWriter serializes Ipcraft V1 project documents as stable JSON.
#include "project/projectwriter.h"

#include "ipcraft/compositionmodel.h"
#include "ipcraft/jsonhelpers.h"
#include "ipcraft/schemaids.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace {

void insertObject(QJsonObject& object, const QString& key, const QJsonObject& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

void insertString(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

QJsonObject packageRefObject(const ProjectPackageRef& package) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), package.id);
    object.insert(QStringLiteral("version"), package.version);
    return object;
}

QJsonObject endpointObject(const ProjectEndpointRef& endpoint) {
    QJsonObject object;
    object.insert(QStringLiteral("instance"), endpoint.instanceId);
    object.insert(QStringLiteral("interface"), endpoint.interfaceId);
    insertString(object, QStringLiteral("port"), endpoint.portId);
    insertString(object, QStringLiteral("role"), endpoint.role);
    insertObject(object, QStringLiteral("properties"), endpoint.properties);
    return object;
}

QJsonObject connectionObject(const ProjectConnectionRecord& connection) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), connection.id);
    insertString(object, QStringLiteral("type"), connection.type);

    QJsonArray endpoints;
    for (const ProjectEndpointRef& endpoint : connection.endpoints) {
        endpoints.append(endpointObject(endpoint));
    }
    object.insert(QStringLiteral("endpoints"), endpoints);

    insertString(object, QStringLiteral("source"), connection.sourceKind);
    insertObject(object, QStringLiteral("properties"), connection.properties);
    insertObject(object, QStringLiteral("native"), connection.native);
    return object;
}

QJsonObject externalPortObject(const ProjectExternalPortRecord& port) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), port.id);
    insertString(object, QStringLiteral("name"), port.name);
    if (port.hasInterface) {
        object.insert(QStringLiteral("interface"), endpointObject(port.interfaceRef));
    }
    insertObject(object, QStringLiteral("properties"), port.properties);
    insertObject(object, QStringLiteral("native"), port.native);
    return object;
}

QJsonObject instanceObject(const ProjectIpInstanceRecord& instance) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), instance.id);
    insertString(object, QStringLiteral("display_name"), instance.displayName);

    object.insert(QStringLiteral("package"), packageRefObject(instance.package));

    insertObject(object, QStringLiteral("config"), instance.config);
    if (instance.hasGraphConfig) {
        if (instance.graphConfigIsNull) {
            object.insert(QStringLiteral("graph_config"), QJsonValue::Null);
        } else {
            object.insert(QStringLiteral("graph_config"), instance.graphConfig);
        }
    }
    insertObject(object, QStringLiteral("native"), instance.native);
    insertObject(object, QStringLiteral("last_runs"), instance.lastRuns);
    insertObject(object, QStringLiteral("artifacts"), instance.artifacts);
    insertObject(object, QStringLiteral("diagnostics"), instance.diagnostics);
    insertObject(object, QStringLiteral("view"), instance.view);
    return object;
}

QJsonObject migrationObject(const ProjectMigration& migration) {
    QJsonObject object;
    insertString(object, QStringLiteral("from_schema"), migration.fromSchema);
    insertString(object, QStringLiteral("from_version"), migration.fromVersion);
    insertObject(object, QStringLiteral("preserved"), migration.preserved);
    insertObject(object, QStringLiteral("metadata"), migration.metadata);
    insertObject(object, QStringLiteral("native"), migration.native);
    return object;
}

QJsonObject toJson(const ProjectDocument& document) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), ipcraft::schemaids::projectV1);

    QJsonObject project;
    project.insert(QStringLiteral("id"), document.projectId);
    project.insert(QStringLiteral("name"), document.projectName);
    insertString(project, QStringLiteral("description"), document.projectDescription);
    insertObject(project, QStringLiteral("display"), document.projectDisplay);
    insertObject(project, QStringLiteral("metadata"), document.projectMetadata);
    insertObject(project, QStringLiteral("native"), document.projectNative);
    root.insert(QStringLiteral("project"), project);

    QJsonArray instances;
    for (const ProjectIpInstanceRecord& instance : document.instances) {
        instances.append(instanceObject(instance));
    }
    root.insert(QStringLiteral("instances"), instances);

    QJsonObject composition;
    QJsonArray connections;
    for (const ProjectConnectionRecord& connection : document.composition.connections) {
        connections.append(connectionObject(connection));
    }
    composition.insert(QStringLiteral("connections"), connections);

    QJsonArray externalPorts;
    for (const ProjectExternalPortRecord& port : document.composition.externalPorts) {
        externalPorts.append(externalPortObject(port));
    }
    composition.insert(QStringLiteral("external_ports"), externalPorts);
    if (!document.composition.groups.isEmpty()) {
        composition.insert(QStringLiteral("groups"), document.composition.groups);
    }
    insertObject(composition, QStringLiteral("properties"), document.composition.properties);
    insertObject(composition, QStringLiteral("native"), document.composition.native);
    root.insert(QStringLiteral("composition"), composition);

    root.insert(QStringLiteral("layout"), document.layout);
    root.insert(QStringLiteral("diagnostics"), document.diagnostics.toJson());
    root.insert(QStringLiteral("artifacts"), document.artifacts);
    root.insert(QStringLiteral("migration"), migrationObject(document.migration));
    root.insert(QStringLiteral("native"), document.native);
    return root;
}

bool isNonEmpty(const QString& value) {
    return !value.trimmed().isEmpty();
}

ProjectWriteResult writeFailure(const QString& message) {
    return {false, message};
}

ProjectWriteResult validateEndpoint(const ProjectEndpointRef& endpoint,
                                    const QString& context,
                                    const QSet<QString>& instanceIds) {
    if (!isNonEmpty(endpoint.instanceId)) {
        return writeFailure(QStringLiteral("%1 endpoint instance is required").arg(context));
    }
    if (!instanceIds.contains(endpoint.instanceId)) {
        return writeFailure(QStringLiteral("%1 endpoint instance is not declared").arg(context));
    }
    if (!isNonEmpty(endpoint.interfaceId)) {
        return writeFailure(QStringLiteral("%1 endpoint interface is required").arg(context));
    }
    return {true, {}};
}

ProjectWriteResult validateDocument(const ProjectDocument& document) {
    if (!isNonEmpty(document.projectId)) {
        return writeFailure(QStringLiteral("Project project.id is required"));
    }
    if (!isNonEmpty(document.projectName)) {
        return writeFailure(QStringLiteral("Project project.name is required"));
    }
    QSet<QString> instanceIds;
    for (const ProjectIpInstanceRecord& instance : document.instances) {
        if (!isNonEmpty(instance.id)) {
            return writeFailure(QStringLiteral("Project instance id is required"));
        }
        if (instanceIds.contains(instance.id)) {
            return writeFailure(QStringLiteral("Duplicate project instance id: %1").arg(instance.id));
        }
        instanceIds.insert(instance.id);
        if (!isNonEmpty(instance.package.id)) {
            return writeFailure(QStringLiteral("Project instance package.id is required"));
        }
        if (!isNonEmpty(instance.package.version)) {
            return writeFailure(QStringLiteral("Project instance package.version is required"));
        }
        if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
            const ipcraft::GraphConfigReadResult graphConfigResult =
                ipcraft::GraphConfig::fromJson(instance.graphConfig);
            if (!graphConfigResult.diagnostics.records.isEmpty()) {
                return writeFailure(QStringLiteral("Project instance graph_config is invalid"));
            }
            const ipcraft::DiagnosticStore graphConfigDiagnostics =
                ipcraft::validateGraphConfig(graphConfigResult.config);
            if (!graphConfigDiagnostics.records.isEmpty()) {
                return writeFailure(QStringLiteral("Project instance graph_config is invalid"));
            }
        }
    }
    QSet<QString> connectionIds;
    for (const ProjectConnectionRecord& connection : document.composition.connections) {
        if (!isNonEmpty(connection.id)) {
            return writeFailure(QStringLiteral("Project composition connection id is required"));
        }
        if (connectionIds.contains(connection.id)) {
            return writeFailure(QStringLiteral("Duplicate project connection id: %1").arg(connection.id));
        }
        connectionIds.insert(connection.id);
        if (!connection.sourceKind.isEmpty() &&
            connection.sourceKind != QStringLiteral("user") &&
            connection.sourceKind != QStringLiteral("generated") &&
            connection.sourceKind != QStringLiteral("imported")) {
            return writeFailure(QStringLiteral("Project composition connection source is invalid"));
        }
        if (connection.endpoints.size() < 2) {
            return writeFailure(QStringLiteral("Project composition connection endpoints require at least two entries"));
        }
        for (const ProjectEndpointRef& endpoint : connection.endpoints) {
            ProjectWriteResult endpointResult = validateEndpoint(
                endpoint,
                QStringLiteral("Project composition connection"),
                instanceIds);
            if (!endpointResult.success) {
                return endpointResult;
            }
        }
    }
    QSet<QString> externalPortIds;
    for (const ProjectExternalPortRecord& port : document.composition.externalPorts) {
        if (!isNonEmpty(port.id)) {
            return writeFailure(QStringLiteral("Project external port id is required"));
        }
        if (externalPortIds.contains(port.id)) {
            return writeFailure(QStringLiteral("Duplicate project external port id: %1").arg(port.id));
        }
        externalPortIds.insert(port.id);
        if (port.hasInterface) {
            ProjectWriteResult endpointResult = validateEndpoint(
                port.interfaceRef,
                QStringLiteral("Project external port"),
                instanceIds);
            if (!endpointResult.success) {
                return endpointResult;
            }
        }
    }
    for (const QJsonValue& group : document.composition.groups) {
        if (!group.isObject()) {
            return writeFailure(QStringLiteral("Project composition groups entries must be objects"));
        }
    }
    return {true, {}};
}

} // namespace

ProjectWriteResult ProjectWriter::writeFile(const QString& path, const ProjectDocument& document) {
    ProjectWriteResult validationResult = validateDocument(document);
    if (!validationResult.success) {
        return validationResult;
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
        return {false, QStringLiteral("Could not create project directory: %1").arg(fileInfo.absolutePath())};
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("Could not open project file for writing: %1").arg(path)};
    }

    const QByteArray content = ipcraft::toDeterministicJson(toJson(document));
    if (file.write(content) != content.size()) {
        return {false, QStringLiteral("Could not write project file: %1").arg(path)};
    }
    if (!file.commit()) {
        return {false, QStringLiteral("Could not commit project file: %1").arg(path)};
    }

    return {true, {}};
}

QJsonObject ProjectWriter::toJsonObject(const ProjectDocument& document) {
    return toJson(document);
}
