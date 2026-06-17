#include "ipcraft/core/project_design.h"

#include "ipcraft/diagnosticids.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace ipcraft::core {
namespace {

namespace diagnosticids = ipcraft::diagnosticids;

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

bool isSupportedTopologyKind(const QString& schema, const QString& kind) {
    if (schema == schemaids::topologyGraphV1) {
        return kind == QStringLiteral("explicit_graph") ||
               kind == QStringLiteral("expanded_parametric");
    }

    if (schema == schemaids::topologyParametricV1) {
        return kind == QStringLiteral("parametric");
    }

    return true;
}

bool isSupportedViewSchema(const QString& schema) {
    return schema == schemaids::viewV1;
}

QString packageRefKey(const PackageRef& package) {
    return package.id + QLatin1Char('@') + package.version;
}

QString interfaceRefKey(const QString& componentId, const QString& interfaceId) {
    return componentId + QLatin1Char('/') + interfaceId;
}

void appendDuplicateTopologyObjectIdIssues(QVector<ValidationIssue>& issues,
                                           const QVector<QJsonObject>& objects,
                                           const QString& collection,
                                           const QString& code,
                                           const QString& message,
                                           qsizetype topologyIndex) {
    QSet<QString> ids;
    for (qsizetype objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const QString id = objects.at(objectIndex).value(QStringLiteral("id")).toString();
        if (isBlank(id)) {
            continue;
        }

        if (ids.contains(id)) {
            appendIssue(issues,
                        code,
                        message,
                        QStringLiteral("/topologies/%1/%2/%3/id")
                            .arg(topologyIndex)
                            .arg(collection)
                            .arg(objectIndex));
        } else {
            ids.insert(id);
        }
    }
}

} // namespace

QVector<ValidationIssue> validateProjectDesign(const ProjectDesign& project) {
    QVector<ValidationIssue> issues;

    if (!project.schema.isEmpty() && project.schema != schemaids::projectV1) {
        appendIssue(issues,
                    diagnosticids::projectUnsupportedSchema(),
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

    QSet<QString> packageRefs;
    for (const PackageRef& package : project.packages) {
        if (!isBlank(package.id) && !isBlank(package.version)) {
            packageRefs.insert(packageRefKey(package));
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
        } else if (!packageRefs.contains(component.packageRef)) {
            appendIssue(issues,
                        QStringLiteral("component.unknown_package_ref"),
                        QStringLiteral("Component packageRef must reference a declared package."),
                        QStringLiteral("/components/%1/packageRef").arg(index));
        }

        if (containsForbiddenLayoutKey(component.config)) {
            appendIssue(issues,
                        QStringLiteral("project.layout_in_component_config"),
                        QStringLiteral("Component config cannot contain layout fields."),
                        QStringLiteral("/components/%1/config").arg(index));
        }
    }

    QSet<QString> declaredInterfaceRefs;
    for (qsizetype index = 0; index < project.interfaces.size(); ++index) {
        const InterfaceInstance& interface = project.interfaces.at(index);

        if (isBlank(interface.id)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_id"),
                        QStringLiteral("Interface id is required."),
                        QStringLiteral("/interfaces/%1/id").arg(index));
        }

        if (isBlank(interface.ownerComponentId)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_owner_component_id"),
                        QStringLiteral("Interface ownerComponentId is required."),
                        QStringLiteral("/interfaces/%1/ownerComponentId").arg(index));
        } else if (!componentIds.contains(interface.ownerComponentId)) {
            appendIssue(issues,
                        QStringLiteral("interface.unknown_owner_component_id"),
                        QStringLiteral("Interface ownerComponentId must reference a component id."),
                        QStringLiteral("/interfaces/%1/ownerComponentId").arg(index));
        }

        if (isBlank(interface.type)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_type"),
                        QStringLiteral("Interface type is required."),
                        QStringLiteral("/interfaces/%1/type").arg(index));
        }

        if (isBlank(interface.role)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_role"),
                        QStringLiteral("Interface role is required."),
                        QStringLiteral("/interfaces/%1/role").arg(index));
        }

        if (isBlank(interface.direction)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_direction"),
                        QStringLiteral("Interface direction is required."),
                        QStringLiteral("/interfaces/%1/direction").arg(index));
        }

        if (isBlank(interface.protocol)) {
            appendIssue(issues,
                        QStringLiteral("interface.missing_protocol"),
                        QStringLiteral("Interface protocol is required."),
                        QStringLiteral("/interfaces/%1/protocol").arg(index));
        }

        if (!isBlank(interface.ownerComponentId) && !isBlank(interface.id)) {
            declaredInterfaceRefs.insert(interfaceRefKey(interface.ownerComponentId,
                                                        interface.id));
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

        if (!isBlank(connection.from.component) &&
            !componentIds.contains(connection.from.component)) {
            appendIssue(issues,
                        QStringLiteral("connection.unknown_component_ref"),
                        QStringLiteral("Connection endpoint component must reference a component id."),
                        QStringLiteral("/connections/%1/from/component").arg(index));
        }

        if (!isBlank(connection.to.component) &&
            !componentIds.contains(connection.to.component)) {
            appendIssue(issues,
                        QStringLiteral("connection.unknown_component_ref"),
                        QStringLiteral("Connection endpoint component must reference a component id."),
                        QStringLiteral("/connections/%1/to/component").arg(index));
        }

        if (!declaredInterfaceRefs.isEmpty() &&
            !isBlank(connection.from.component) &&
            componentIds.contains(connection.from.component) &&
            !isBlank(connection.from.interface) &&
            !declaredInterfaceRefs.contains(interfaceRefKey(connection.from.component,
                                                            connection.from.interface))) {
            appendIssue(issues,
                        QStringLiteral("connection.unknown_interface_ref"),
                        QStringLiteral("Connection endpoint interface must reference a declared interface."),
                        QStringLiteral("/connections/%1/from/interface").arg(index));
        }

        if (!declaredInterfaceRefs.isEmpty() &&
            !isBlank(connection.to.component) &&
            componentIds.contains(connection.to.component) &&
            !isBlank(connection.to.interface) &&
            !declaredInterfaceRefs.contains(interfaceRefKey(connection.to.component,
                                                            connection.to.interface))) {
            appendIssue(issues,
                        QStringLiteral("connection.unknown_interface_ref"),
                        QStringLiteral("Connection endpoint interface must reference a declared interface."),
                        QStringLiteral("/connections/%1/to/interface").arg(index));
        }
    }

    QSet<QString> topologyIds;
    for (qsizetype index = 0; index < project.topologies.size(); ++index) {
        const TopologyGraph& topology = project.topologies.at(index);
        const QString idPath = QStringLiteral("/topologies/%1/id").arg(index);

        if (isBlank(topology.id)) {
            appendIssue(issues,
                        QStringLiteral("topology.missing_id"),
                        QStringLiteral("Topology id is required."),
                        idPath);
        } else if (topologyIds.contains(topology.id)) {
            appendIssue(issues,
                        QStringLiteral("topology.duplicate_id"),
                        QStringLiteral("Topology id is duplicated."),
                        idPath);
        } else {
            topologyIds.insert(topology.id);
        }

        if (!isSupportedTopologySchema(topology.schema)) {
            appendIssue(issues,
                        QStringLiteral("topology.unsupported_schema"),
                        QStringLiteral("Topology schema is not supported."),
                        QStringLiteral("/topologies/%1/schema").arg(index));
        }

        if (isBlank(topology.kind)) {
            appendIssue(issues,
                        QStringLiteral("topology.missing_kind"),
                        QStringLiteral("Topology kind is required."),
                        QStringLiteral("/topologies/%1/kind").arg(index));
        } else if (!isSupportedTopologyKind(topology.schema, topology.kind)) {
            appendIssue(issues,
                        QStringLiteral("topology.invalid_kind"),
                        QStringLiteral("Topology kind must match its schema."),
                        QStringLiteral("/topologies/%1/kind").arg(index));
        }

        if (topology.schema == schemaids::topologyParametricV1 &&
            isBlank(topology.family)) {
            appendIssue(issues,
                        QStringLiteral("topology.missing_family"),
                        QStringLiteral("Parametric topology family is required."),
                        QStringLiteral("/topologies/%1/family").arg(index));
        }

        appendDuplicateTopologyObjectIdIssues(issues,
                                              topology.nodes,
                                              QStringLiteral("nodes"),
                                              QStringLiteral("topology.duplicate_node_id"),
                                              QStringLiteral("Topology node id is duplicated."),
                                              index);
        appendDuplicateTopologyObjectIdIssues(issues,
                                              topology.links,
                                              QStringLiteral("links"),
                                              QStringLiteral("topology.duplicate_link_id"),
                                              QStringLiteral("Topology link id is duplicated."),
                                              index);

        QSet<QString> attachmentIds;
        for (qsizetype attachmentIndex = 0; attachmentIndex < topology.attachments.size();
             ++attachmentIndex) {
            const TopologyAttachment& attachment = topology.attachments.at(attachmentIndex);
            if (isBlank(attachment.id)) {
                appendIssue(issues,
                            QStringLiteral("topology.missing_attachment_id"),
                            QStringLiteral("Topology attachment id is required."),
                            QStringLiteral("/topologies/%1/attachments/%2/id")
                                .arg(index)
                                .arg(attachmentIndex));
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

    QSet<QString> viewIds;
    for (qsizetype index = 0; index < project.views.size(); ++index) {
        const ViewDocument& view = project.views.at(index);
        const QString idPath = QStringLiteral("/views/%1/id").arg(index);

        if (isBlank(view.id)) {
            appendIssue(issues,
                        QStringLiteral("view.missing_id"),
                        QStringLiteral("View id is required."),
                        idPath);
        } else if (viewIds.contains(view.id)) {
            appendIssue(issues,
                        QStringLiteral("view.duplicate_id"),
                        QStringLiteral("View id is duplicated."),
                        idPath);
        } else {
            viewIds.insert(view.id);
        }

        if (isBlank(view.schema)) {
            appendIssue(issues,
                        QStringLiteral("view.missing_schema"),
                        QStringLiteral("View schema is required."),
                        QStringLiteral("/views/%1/schema").arg(index));
        } else if (!isSupportedViewSchema(view.schema)) {
            appendIssue(issues,
                        QStringLiteral("view.unsupported_schema"),
                        QStringLiteral("View schema is not supported."),
                        QStringLiteral("/views/%1/schema").arg(index));
        }

        if (isBlank(view.kind)) {
            appendIssue(issues,
                        QStringLiteral("view.missing_kind"),
                        QStringLiteral("View kind is required."),
                        QStringLiteral("/views/%1/kind").arg(index));
        }

        if (isBlank(view.targetRef)) {
            appendIssue(issues,
                        QStringLiteral("view.missing_target_ref"),
                        QStringLiteral("View targetRef is required."),
                        QStringLiteral("/views/%1/targetRef").arg(index));
        }

        if (isBlank(view.providerRef)) {
            appendIssue(issues,
                        QStringLiteral("view.missing_provider_ref"),
                        QStringLiteral("View providerRef is required."),
                        QStringLiteral("/views/%1/providerRef").arg(index));
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
