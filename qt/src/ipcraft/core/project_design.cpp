#include "ipcraft/core/project_design.h"

#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace ipcraft::core {
namespace {

bool isBlank(const QString& value) {
    return value.trimmed().isEmpty();
}

bool isForbiddenLayoutKey(const QString& key) {
    return key == QStringLiteral("x") ||
           key == QStringLiteral("y") ||
           key == QStringLiteral("node_width") ||
           key == QStringLiteral("node_height") ||
           key == QStringLiteral("collapsed") ||
           key == QStringLiteral("waypoints") ||
           key == QStringLiteral("zoom") ||
           key == QStringLiteral("pan");
}

bool containsForbiddenLayoutKey(const QJsonValue& value);

bool containsForbiddenLayoutKey(const QJsonArray& array) {
    for (const QJsonValue& child : array) {
        if (containsForbiddenLayoutKey(child)) {
            return true;
        }
    }

    return false;
}

bool containsForbiddenLayoutKey(const QJsonObject& object) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isForbiddenLayoutKey(it.key()) || containsForbiddenLayoutKey(it.value())) {
            return true;
        }
    }

    return false;
}

bool containsForbiddenLayoutKey(const QJsonValue& value) {
    if (value.isObject()) {
        return containsForbiddenLayoutKey(value.toObject());
    }

    if (value.isArray()) {
        return containsForbiddenLayoutKey(value.toArray());
    }

    return false;
}

void appendIssue(QVector<ValidationIssue>& issues,
                 const QString& code,
                 const QString& message,
                 const QString& path) {
    issues.append(ValidationIssue{code, message, path});
}

bool isSupportedTopologySchema(const QString& schema) {
    return schema == schemaids::topologyGraphV1 ||
           schema == schemaids::topologyParametricV1;
}

} // namespace

QVector<ValidationIssue> validateProjectDesign(const ProjectDesign& project) {
    QVector<ValidationIssue> issues;

    if (!project.schema.isEmpty() && project.schema != schemaids::projectV1) {
        appendIssue(issues,
                    QStringLiteral("project.unsupported_schema"),
                    QStringLiteral("Project schema is not supported."),
                    QStringLiteral("/schema"));
    }

    if (isBlank(project.id)) {
        appendIssue(issues,
                    QStringLiteral("project.missing_id"),
                    QStringLiteral("Project id is required."),
                    QStringLiteral("/id"));
    }

    if (isBlank(project.name)) {
        appendIssue(issues,
                    QStringLiteral("project.missing_name"),
                    QStringLiteral("Project name is required."),
                    QStringLiteral("/name"));
    }

    for (qsizetype index = 0; index < project.packages.size(); ++index) {
        const PackageRef& package = project.packages.at(index);

        if (isBlank(package.id)) {
            appendIssue(issues,
                        QStringLiteral("package.missing_id"),
                        QStringLiteral("Package id is required."),
                        QStringLiteral("/packages/%1/id").arg(index));
        }

        if (isBlank(package.version)) {
            appendIssue(issues,
                        QStringLiteral("package.missing_version"),
                        QStringLiteral("Package version is required."),
                        QStringLiteral("/packages/%1/version").arg(index));
        }
    }

    QSet<QString> componentIds;
    for (qsizetype index = 0; index < project.components.size(); ++index) {
        const ComponentInstance& component = project.components.at(index);
        const QString idPath = QStringLiteral("/components/%1/id").arg(index);

        if (isBlank(component.id)) {
            appendIssue(issues,
                        QStringLiteral("project.missing_component_id"),
                        QStringLiteral("Component id is required."),
                        idPath);
        } else if (componentIds.contains(component.id)) {
            appendIssue(issues,
                        QStringLiteral("project.duplicate_component_id"),
                        QStringLiteral("Component id is duplicated."),
                        idPath);
        } else {
            componentIds.insert(component.id);
        }

        if (isBlank(component.type)) {
            appendIssue(issues,
                        QStringLiteral("component.missing_type"),
                        QStringLiteral("Component type is required."),
                        QStringLiteral("/components/%1/type").arg(index));
        }

        if (isBlank(component.packageRef)) {
            appendIssue(issues,
                        QStringLiteral("component.missing_package_ref"),
                        QStringLiteral("Component packageRef is required."),
                        QStringLiteral("/components/%1/packageRef").arg(index));
        }

        if (containsForbiddenLayoutKey(component.config)) {
            appendIssue(issues,
                        QStringLiteral("project.layout_in_component_config"),
                        QStringLiteral("Component config cannot contain layout fields."),
                        QStringLiteral("/components/%1/config").arg(index));
        }
    }

    QSet<QString> connectionIds;
    for (qsizetype index = 0; index < project.connections.size(); ++index) {
        const Connection& connection = project.connections.at(index);
        const QString idPath = QStringLiteral("/connections/%1/id").arg(index);

        if (isBlank(connection.id)) {
            appendIssue(issues,
                        QStringLiteral("project.missing_connection_id"),
                        QStringLiteral("Connection id is required."),
                        idPath);
        } else if (connectionIds.contains(connection.id)) {
            appendIssue(issues,
                        QStringLiteral("project.duplicate_connection_id"),
                        QStringLiteral("Connection id is duplicated."),
                        idPath);
        } else {
            connectionIds.insert(connection.id);
        }

        if (connection.kind == QStringLiteral("attachment")) {
            appendIssue(issues,
                        QStringLiteral("project.attachment_connection_forbidden"),
                        QStringLiteral("Attachment connections must use topology attachments."),
                        QStringLiteral("/connections/%1/kind").arg(index));
        }

        if (isBlank(connection.from.component) ||
            isBlank(connection.from.interface) ||
            isBlank(connection.to.component) ||
            isBlank(connection.to.interface)) {
            appendIssue(issues,
                        QStringLiteral("connection.missing_endpoint"),
                        QStringLiteral("Connection endpoints must include component and interface."),
                        QStringLiteral("/connections/%1").arg(index));
        }
    }

    for (qsizetype index = 0; index < project.topologies.size(); ++index) {
        const TopologyGraph& topology = project.topologies.at(index);

        if (!isSupportedTopologySchema(topology.schema)) {
            appendIssue(issues,
                        QStringLiteral("topology.unsupported_schema"),
                        QStringLiteral("Topology schema is not supported."),
                        QStringLiteral("/topologies/%1/schema").arg(index));
        }

        QSet<QString> attachmentIds;
        for (qsizetype attachmentIndex = 0; attachmentIndex < topology.attachments.size();
             ++attachmentIndex) {
            const TopologyAttachment& attachment = topology.attachments.at(attachmentIndex);
            if (isBlank(attachment.id)) {
                continue;
            }

            if (attachmentIds.contains(attachment.id)) {
                appendIssue(issues,
                            QStringLiteral("topology.duplicate_attachment_id"),
                            QStringLiteral("Topology attachment id is duplicated."),
                            QStringLiteral("/topologies/%1/attachments/%2/id")
                                .arg(index)
                                .arg(attachmentIndex));
            } else {
                attachmentIds.insert(attachment.id);
            }
        }
    }

    for (qsizetype index = 0; index < project.extensions.size(); ++index) {
        const ExtensionBlock& extension = project.extensions.at(index);

        if (isBlank(extension.ownerPackageId)) {
            appendIssue(issues,
                        QStringLiteral("extension.missing_owner"),
                        QStringLiteral("Extension ownerPackageId is required."),
                        QStringLiteral("/extensions/%1/ownerPackageId").arg(index));
        }

        if (isBlank(extension.schemaId)) {
            appendIssue(issues,
                        QStringLiteral("extension.missing_schema"),
                        QStringLiteral("Extension schemaId is required."),
                        QStringLiteral("/extensions/%1/schemaId").arg(index));
        }

        if (extension.version <= 0) {
            appendIssue(issues,
                        QStringLiteral("extension.missing_version"),
                        QStringLiteral("Extension version is required."),
                        QStringLiteral("/extensions/%1/version").arg(index));
        }

        if (extension.data.isEmpty()) {
            appendIssue(issues,
                        QStringLiteral("extension.missing_data"),
                        QStringLiteral("Extension data is required."),
                        QStringLiteral("/extensions/%1/data").arg(index));
        }
    }

    return issues;
}

QJsonObject extensionBlockToJson(const ExtensionBlock& extension) {
    QJsonObject object;

    if (!extension.ownerPackageId.isEmpty()) {
        object.insert(QStringLiteral("ownerPackageId"), extension.ownerPackageId);
    }
    if (!extension.schemaId.isEmpty()) {
        object.insert(QStringLiteral("schemaId"), extension.schemaId);
    }
    if (extension.version != 0) {
        object.insert(QStringLiteral("version"), extension.version);
    }
    if (!extension.data.isEmpty()) {
        object.insert(QStringLiteral("data"), extension.data);
    }
    if (!extension.validationState.isEmpty()) {
        object.insert(QStringLiteral("validationState"), extension.validationState);
    }

    return object;
}

ExtensionBlock extensionBlockFromJson(const QJsonObject& object) {
    ExtensionBlock extension;
    extension.ownerPackageId = object.value(QStringLiteral("ownerPackageId")).toString();
    extension.schemaId = object.value(QStringLiteral("schemaId")).toString();
    extension.version = object.value(QStringLiteral("version")).toInt();
    extension.data = object.value(QStringLiteral("data")).toObject();
    extension.validationState = object.value(QStringLiteral("validationState")).toObject();
    return extension;
}

} // namespace ipcraft::core
