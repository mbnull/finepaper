// ProjectWriter serializes Ipcraft V1 project documents as stable JSON.
#include "project/projectwriter.h"

#include "ipcraft/compositionmodel.h"
#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/core/project_design.h"
#include "ipcraft/jsonhelpers.h"
#include "ipcraft/schemaids.h"
#include "project/projectdesignserializer.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace {

bool isNonEmpty(const QString& value) {
    return !value.trimmed().isEmpty();
}

ProjectWriteResult writeFailure(const QString& message) {
    return {false, message};
}

QString migrationMetadataKey() {
    return QStringLiteral("ipcraft.migration.v1");
}

void insertObjectIfNonEmpty(QJsonObject& object, const QString& key, const QJsonObject& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

void insertStringIfNonEmpty(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.trimmed().isEmpty()) {
        object.insert(key, value);
    }
}

QJsonObject migrationMetadataObject(const ProjectMigration& migration) {
    QJsonObject object;
    insertStringIfNonEmpty(object, QStringLiteral("from_schema"), migration.fromSchema);
    insertStringIfNonEmpty(object, QStringLiteral("from_version"), migration.fromVersion);
    insertObjectIfNonEmpty(object, QStringLiteral("preserved"), migration.preserved);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), migration.metadata);
    insertObjectIfNonEmpty(object, QStringLiteral("native"), migration.native);
    return object;
}

void appendMigrationMetadata(ipcraft::core::ProjectDesign& design,
                             const ProjectMigration& migration) {
    const QJsonObject migrationObject = migrationMetadataObject(migration);
    if (!migrationObject.isEmpty()) {
        design.metadata.insert(migrationMetadataKey(), migrationObject);
    }
}

void appendLegacyLayoutViews(ipcraft::core::ProjectDesign& design,
                             const QJsonObject& layout) {
    QSet<QString> existingViewIds;
    for (const ipcraft::core::ViewDocument& view : design.views) {
        existingViewIds.insert(view.id);
    }

    const QJsonArray views = layout.value(QStringLiteral("views")).toArray();
    for (qsizetype index = 0; index < views.size(); ++index) {
        if (!views.at(index).isObject()) {
            continue;
        }
        const QJsonObject legacyView = views.at(index).toObject();
        const QString id = legacyView.value(QStringLiteral("id"))
            .toString(QStringLiteral("layout_%1").arg(index));
        if (id.trimmed().isEmpty() || existingViewIds.contains(id)) {
            continue;
        }

        ipcraft::core::ViewDocument view;
        view.id = id;
        view.schema = ipcraft::schemaids::viewV1;
        view.kind = legacyView.value(QStringLiteral("kind")).toString(QStringLiteral("canvas"));
        view.targetRef = QStringLiteral("project");
        view.providerRef = QStringLiteral("finepaper.editor");

        const QJsonValue canvas = legacyView.value(QStringLiteral("canvas"));
        if (canvas.isObject()) {
            view.layout.insert(QStringLiteral("canvas"), canvas.toObject());
        }
        const QJsonValue native = legacyView.value(QStringLiteral("native"));
        if (native.isObject()) {
            view.metadata.insert(QStringLiteral("native"), native.toObject());
        }

        existingViewIds.insert(view.id);
        design.views.append(view);
    }
}

ipcraft::core::ProjectDesign designForWrite(const ProjectDocument& document) {
    ipcraft::core::ProjectDesign design = ProjectDesignSerializer::fromDocument(document);
    design.schema = ipcraft::schemaids::projectV1;
    for (ipcraft::core::ComponentInstance& component : design.components) {
        if (component.type.trimmed().isEmpty()) {
            component.type = component.packageRef.section(QLatin1Char('@'), 0, 0).trimmed();
        }
        if (component.type.isEmpty()) {
            component.type = component.packageRef.trimmed();
        }
    }
    appendLegacyLayoutViews(design, document.layout);
    appendMigrationMetadata(design, document.migration);
    return design;
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
    const ProjectJsonResult jsonResult = ProjectWriter::toJsonObjectResult(document);
    if (!jsonResult.success) {
        return writeFailure(jsonResult.error);
    }

    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
        return {false, QStringLiteral("Could not create project directory: %1").arg(fileInfo.absolutePath())};
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("Could not open project file for writing: %1").arg(path)};
    }

    const QByteArray content = ipcraft::toDeterministicJson(jsonResult.object);
    if (file.write(content) != content.size()) {
        return {false, QStringLiteral("Could not write project file: %1").arg(path)};
    }
    if (!file.commit()) {
        return {false, QStringLiteral("Could not commit project file: %1").arg(path)};
    }

    return {true, {}};
}

QJsonObject ProjectWriter::toJsonObject(const ProjectDocument& document) {
    return ProjectWriter::toJsonObjectResult(document).object;
}

ProjectJsonResult ProjectWriter::toJsonObjectResult(const ProjectDocument& document) {
    const ProjectWriteResult validationResult = validateDocument(document);
    if (!validationResult.success) {
        return {false, {}, validationResult.error};
    }

    const ipcraft::core::ProjectDesign design = designForWrite(document);
    const QVector<ipcraft::core::ValidationIssue> designIssues =
        ipcraft::core::validateProjectDesign(design);
    if (!designIssues.isEmpty()) {
        const ipcraft::core::ValidationIssue& issue = designIssues.first();
        return {false,
                {},
                QStringLiteral("Project design is invalid: %1 at %2").arg(issue.code, issue.path)};
    }

    return {true, ipcraft::core::ProjectDocumentV1::writeObject(design), {}};
}
