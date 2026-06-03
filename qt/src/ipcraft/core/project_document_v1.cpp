#include "ipcraft/core/project_document_v1.h"

#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <initializer_list>

namespace ipcraft::core {
namespace {

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        return {};
    }

    return value.toObject();
}

QVector<QJsonObject> objectVectorFromJson(const QJsonValue& value) {
    QVector<QJsonObject> objects;
    const QJsonArray array = value.toArray();
    objects.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            objects.append(item.toObject());
        }
    }

    return objects;
}

QJsonArray objectVectorToJson(const QVector<QJsonObject>& objects) {
    QJsonArray array;
    for (const QJsonObject& object : objects) {
        array.append(object);
    }

    return array;
}

void insertStringIfNonEmpty(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

void insertObjectIfNonEmpty(QJsonObject& object, const QString& key, const QJsonObject& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

ValidationIssue issue(const QString& code, const QString& message, const QString& path) {
    return ValidationIssue{code, message, path};
}

bool isAllowedTopLevelKey(const QString& key) {
    static const QSet<QString> allowedKeys{
        QStringLiteral("schema"),
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("packages"),
        QStringLiteral("components"),
        QStringLiteral("interfaces"),
        QStringLiteral("connections"),
        QStringLiteral("topologies"),
        QStringLiteral("constraints"),
        QStringLiteral("views"),
        QStringLiteral("diagnostics"),
        QStringLiteral("artifacts"),
        QStringLiteral("extensions"),
        QStringLiteral("metadata")
    };

    return allowedKeys.contains(key);
}

QString childPath(const QString& path, const QString& key) {
    return path + QStringLiteral("/") + key;
}

void appendUnknownTopLevelFieldIssues(QVector<ValidationIssue>& issues,
                                      const QJsonObject& object) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!isAllowedTopLevelKey(it.key())) {
            issues.append(issue(QStringLiteral("project.unknown_field"),
                                QStringLiteral("Top-level project field is not supported."),
                                QStringLiteral("/%1").arg(it.key())));
        }
    }
}

void appendObjectFieldShapeIssue(QVector<ValidationIssue>& issues,
                                 const QJsonObject& object,
                                 const QString& key,
                                 const QString& code,
                                 const QString& path) {
    if (object.contains(key) && !object.value(key).isObject()) {
        issues.append(issue(code,
                            QStringLiteral("Field must be an object."),
                            childPath(path, key)));
    }
}

void appendArrayFieldShapeIssue(QVector<ValidationIssue>& issues,
                                const QJsonObject& object,
                                const QString& key,
                                const QString& code,
                                const QString& path) {
    if (object.contains(key) && !object.value(key).isArray()) {
        issues.append(issue(code,
                            QStringLiteral("Field must be an array."),
                            childPath(path, key)));
    }
}

struct ObjectFieldShapeRule {
    QString key;
    QString code;
};

struct ObjectArrayFieldShapeRule {
    QString key;
    QString arrayCode;
    QString entryCode;
};

void appendObjectFieldShapeIssues(QVector<ValidationIssue>& issues,
                                  const QJsonObject& object,
                                  const QString& path,
                                  std::initializer_list<ObjectFieldShapeRule> rules) {
    for (const ObjectFieldShapeRule& rule : rules) {
        appendObjectFieldShapeIssue(issues, object, rule.key, rule.code, path);
    }
}

void appendCollectionObjectFieldShapeIssues(QVector<ValidationIssue>& issues,
                                            const QJsonObject& project,
                                            const QString& collection,
                                            std::initializer_list<ObjectFieldShapeRule> rules) {
    const QJsonArray items = project.value(collection).toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            continue;
        }

        appendObjectFieldShapeIssues(issues,
                                     items.at(index).toObject(),
                                     QStringLiteral("/%1/%2").arg(collection).arg(index),
                                     rules);
    }
}

void appendObjectArrayFieldShapeIssue(QVector<ValidationIssue>& issues,
                                      const QJsonObject& object,
                                      const QString& key,
                                      const QString& arrayCode,
                                      const QString& entryCode,
                                      const QString& path) {
    if (!object.contains(key)) {
        return;
    }

    const QString fieldPath = childPath(path, key);
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        issues.append(issue(arrayCode,
                            QStringLiteral("Field must be an array."),
                            fieldPath));
        return;
    }

    const QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            issues.append(issue(entryCode,
                                QStringLiteral("Array entries must be objects."),
                                QStringLiteral("%1/%2").arg(fieldPath).arg(index)));
        }
    }
}

void appendObjectArrayFieldShapeIssues(QVector<ValidationIssue>& issues,
                                       const QJsonObject& object,
                                       const QString& path,
                                       std::initializer_list<ObjectArrayFieldShapeRule> rules) {
    for (const ObjectArrayFieldShapeRule& rule : rules) {
        appendObjectArrayFieldShapeIssue(issues,
                                         object,
                                         rule.key,
                                         rule.arrayCode,
                                         rule.entryCode,
                                         path);
    }
}

void appendNonObjectArrayEntryIssues(QVector<ValidationIssue>& issues,
                                     const QJsonValue& value,
                                     const QString& code,
                                     const QString& pathPrefix) {
    if (!value.isArray()) {
        return;
    }

    const QJsonArray array = value.toArray();
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            issues.append(issue(code,
                                QStringLiteral("Array entries must be objects."),
                                QStringLiteral("%1/%2").arg(pathPrefix).arg(index)));
        }
    }
}

void appendTopologyShapeIssues(QVector<ValidationIssue>& issues,
                               const QJsonObject& project) {
    const QJsonArray topologies = project.value(QStringLiteral("topologies")).toArray();
    for (qsizetype index = 0; index < topologies.size(); ++index) {
        if (!topologies.at(index).isObject()) {
            continue;
        }

        const QJsonObject topology = topologies.at(index).toObject();
        const QString path = QStringLiteral("/topologies/%1").arg(index);
        appendObjectFieldShapeIssues(issues,
                                     topology,
                                     path,
                                     {{QStringLiteral("parameters"),
                                       QStringLiteral("project.invalid_topology_parameters_shape")},
                                      {QStringLiteral("constraints"),
                                       QStringLiteral("project.invalid_topology_constraints_shape")},
                                      {QStringLiteral("routing"),
                                       QStringLiteral("project.invalid_topology_routing_shape")},
                                      {QStringLiteral("metadata"),
                                       QStringLiteral("project.invalid_topology_metadata_shape")}});
        appendObjectArrayFieldShapeIssues(issues,
                                          topology,
                                          path,
                                          {{QStringLiteral("nodes"),
                                            QStringLiteral("project.invalid_topology_nodes_shape"),
                                            QStringLiteral("project.invalid_topology_node_shape")},
                                           {QStringLiteral("links"),
                                            QStringLiteral("project.invalid_topology_links_shape"),
                                            QStringLiteral("project.invalid_topology_link_shape")},
                                           {QStringLiteral("attachments"),
                                            QStringLiteral("project.invalid_topology_attachments_shape"),
                                            QStringLiteral("project.invalid_topology_attachment_shape")}});

        const QJsonValue attachmentsValue = topology.value(QStringLiteral("attachments"));
        if (!attachmentsValue.isArray()) {
            continue;
        }

        const QJsonArray attachments = attachmentsValue.toArray();
        for (qsizetype attachmentIndex = 0; attachmentIndex < attachments.size();
             ++attachmentIndex) {
            if (!attachments.at(attachmentIndex).isObject()) {
                continue;
            }

            const QJsonObject attachment = attachments.at(attachmentIndex).toObject();
            const QString attachmentPath =
                QStringLiteral("%1/attachments/%2").arg(path).arg(attachmentIndex);
            appendObjectFieldShapeIssues(
                issues,
                attachment,
                attachmentPath,
                {{QStringLiteral("attachmentPoint"),
                  QStringLiteral("project.invalid_topology_attachment_point_shape")},
                 {QStringLiteral("config"),
                  QStringLiteral("project.invalid_topology_attachment_config_shape")}});
        }
    }
}

void appendReadShapeIssues(QVector<ValidationIssue>& issues,
                           const QJsonObject& object) {
    appendUnknownTopLevelFieldIssues(issues, object);
    appendObjectFieldShapeIssue(issues,
                                object,
                                QStringLiteral("constraints"),
                                QStringLiteral("project.invalid_constraints_shape"),
                                QStringLiteral(""));
    appendObjectFieldShapeIssue(issues,
                                object,
                                QStringLiteral("metadata"),
                                QStringLiteral("project.invalid_metadata_shape"),
                                QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("packages"),
                               QStringLiteral("project.invalid_packages_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("components"),
                               QStringLiteral("project.invalid_components_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("interfaces"),
                               QStringLiteral("project.invalid_interfaces_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("connections"),
                               QStringLiteral("project.invalid_connections_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("topologies"),
                               QStringLiteral("project.invalid_topologies_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("views"),
                               QStringLiteral("project.invalid_views_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("diagnostics"),
                               QStringLiteral("project.invalid_diagnostics_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("artifacts"),
                               QStringLiteral("project.invalid_artifacts_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               QStringLiteral("extensions"),
                               QStringLiteral("project.invalid_extensions_shape"),
                               QStringLiteral(""));
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        QStringLiteral("components"),
        {{QStringLiteral("identity"), QStringLiteral("project.invalid_component_identity_shape")},
         {QStringLiteral("config"), QStringLiteral("project.invalid_component_config_shape")},
         {QStringLiteral("metadata"), QStringLiteral("project.invalid_component_metadata_shape")},
         {QStringLiteral("extensionData"),
          QStringLiteral("project.invalid_component_extension_data_shape")}});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        QStringLiteral("interfaces"),
        {{QStringLiteral("config"), QStringLiteral("project.invalid_interface_config_shape")},
         {QStringLiteral("metadata"), QStringLiteral("project.invalid_interface_metadata_shape")}});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        QStringLiteral("connections"),
        {{QStringLiteral("from"), QStringLiteral("project.invalid_connection_endpoint_shape")},
         {QStringLiteral("to"), QStringLiteral("project.invalid_connection_endpoint_shape")},
         {QStringLiteral("config"), QStringLiteral("project.invalid_connection_config_shape")},
         {QStringLiteral("constraints"),
          QStringLiteral("project.invalid_connection_constraints_shape")},
         {QStringLiteral("metadata"), QStringLiteral("project.invalid_connection_metadata_shape")}});
    appendTopologyShapeIssues(issues, object);
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        QStringLiteral("views"),
        {{QStringLiteral("templates"), QStringLiteral("project.invalid_view_templates_shape")},
         {QStringLiteral("portGrouping"),
          QStringLiteral("project.invalid_view_port_grouping_shape")},
         {QStringLiteral("labels"), QStringLiteral("project.invalid_view_labels_shape")},
         {QStringLiteral("badges"), QStringLiteral("project.invalid_view_badges_shape")},
         {QStringLiteral("propertyGroups"),
          QStringLiteral("project.invalid_view_property_groups_shape")},
         {QStringLiteral("layoutPreference"),
          QStringLiteral("project.invalid_view_layout_preference_shape")},
         {QStringLiteral("interactionAffordances"),
          QStringLiteral("project.invalid_view_interaction_affordances_shape")},
         {QStringLiteral("diagnosticsOverlay"),
          QStringLiteral("project.invalid_view_diagnostics_overlay_shape")},
         {QStringLiteral("icons"), QStringLiteral("project.invalid_view_icons_shape")},
         {QStringLiteral("layout"), QStringLiteral("project.invalid_view_layout_shape")},
         {QStringLiteral("presentationState"),
          QStringLiteral("project.invalid_view_presentation_state_shape")},
         {QStringLiteral("metadata"), QStringLiteral("project.invalid_view_metadata_shape")}});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        QStringLiteral("extensions"),
        {{QStringLiteral("data"), QStringLiteral("project.invalid_extension_data_shape")},
         {QStringLiteral("validationState"),
          QStringLiteral("project.invalid_extension_validation_state_shape")}});
}

EndpointRef endpointFromJson(const QJsonObject& object) {
    EndpointRef endpoint;
    endpoint.component = object.value(QStringLiteral("component")).toString();
    endpoint.interface = object.value(QStringLiteral("interface")).toString();
    return endpoint;
}

QJsonObject endpointToJson(const EndpointRef& endpoint) {
    QJsonObject object;
    object.insert(QStringLiteral("component"), endpoint.component);
    object.insert(QStringLiteral("interface"), endpoint.interface);
    return object;
}

PackageRef packageRefFromJson(const QJsonObject& object) {
    PackageRef package;
    package.id = object.value(QStringLiteral("id")).toString();
    package.version = object.value(QStringLiteral("version")).toString();
    return package;
}

QJsonObject packageRefToJson(const PackageRef& package) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), package.id);
    object.insert(QStringLiteral("version"), package.version);
    return object;
}

QVector<PackageRef> packagesFromJson(const QJsonValue& value) {
    QVector<PackageRef> packages;
    const QJsonArray array = value.toArray();
    packages.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            packages.append(packageRefFromJson(item.toObject()));
        }
    }

    return packages;
}

QJsonArray packagesToJson(const QVector<PackageRef>& packages) {
    QJsonArray array;
    for (const PackageRef& package : packages) {
        array.append(packageRefToJson(package));
    }

    return array;
}

ComponentInstance componentFromJson(const QJsonObject& object) {
    ComponentInstance component;
    component.id = object.value(QStringLiteral("id")).toString();
    component.type = object.value(QStringLiteral("type")).toString();
    component.packageRef = object.value(QStringLiteral("packageRef")).toString();
    component.config = objectValue(object, QStringLiteral("config"));
    component.identity = objectValue(object, QStringLiteral("identity"));
    component.metadata = objectValue(object, QStringLiteral("metadata"));
    component.extensionData = objectValue(object, QStringLiteral("extensionData"));
    return component;
}

QJsonObject componentToJson(const ComponentInstance& component) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), component.id);
    insertStringIfNonEmpty(object, QStringLiteral("type"), component.type);
    insertStringIfNonEmpty(object, QStringLiteral("packageRef"), component.packageRef);
    insertObjectIfNonEmpty(object, QStringLiteral("identity"), component.identity);
    insertObjectIfNonEmpty(object, QStringLiteral("config"), component.config);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), component.metadata);
    insertObjectIfNonEmpty(object, QStringLiteral("extensionData"), component.extensionData);
    return object;
}

QVector<ComponentInstance> componentsFromJson(const QJsonValue& value) {
    QVector<ComponentInstance> components;
    const QJsonArray array = value.toArray();
    components.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            components.append(componentFromJson(item.toObject()));
        }
    }

    return components;
}

QJsonArray componentsToJson(const QVector<ComponentInstance>& components) {
    QJsonArray array;
    for (const ComponentInstance& component : components) {
        array.append(componentToJson(component));
    }

    return array;
}

InterfaceInstance interfaceFromJson(const QJsonObject& object) {
    InterfaceInstance interface;
    interface.id = object.value(QStringLiteral("id")).toString();
    interface.ownerComponentId = object.value(QStringLiteral("ownerComponentId")).toString();
    interface.type = object.value(QStringLiteral("type")).toString();
    interface.role = object.value(QStringLiteral("role")).toString();
    interface.direction = object.value(QStringLiteral("direction")).toString();
    interface.protocol = object.value(QStringLiteral("protocol")).toString();
    interface.clockRef = object.value(QStringLiteral("clockRef")).toString();
    interface.resetRef = object.value(QStringLiteral("resetRef")).toString();
    interface.config = objectValue(object, QStringLiteral("config"));
    interface.metadata = objectValue(object, QStringLiteral("metadata"));
    return interface;
}

QJsonObject interfaceToJson(const InterfaceInstance& interface) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), interface.id);
    insertStringIfNonEmpty(object, QStringLiteral("ownerComponentId"), interface.ownerComponentId);
    insertStringIfNonEmpty(object, QStringLiteral("type"), interface.type);
    insertStringIfNonEmpty(object, QStringLiteral("role"), interface.role);
    insertStringIfNonEmpty(object, QStringLiteral("direction"), interface.direction);
    insertStringIfNonEmpty(object, QStringLiteral("protocol"), interface.protocol);
    insertStringIfNonEmpty(object, QStringLiteral("clockRef"), interface.clockRef);
    insertStringIfNonEmpty(object, QStringLiteral("resetRef"), interface.resetRef);
    insertObjectIfNonEmpty(object, QStringLiteral("config"), interface.config);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), interface.metadata);
    return object;
}

QVector<InterfaceInstance> interfacesFromJson(const QJsonValue& value) {
    QVector<InterfaceInstance> interfaces;
    const QJsonArray array = value.toArray();
    interfaces.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            interfaces.append(interfaceFromJson(item.toObject()));
        }
    }

    return interfaces;
}

QJsonArray interfacesToJson(const QVector<InterfaceInstance>& interfaces) {
    QJsonArray array;
    for (const InterfaceInstance& interface : interfaces) {
        array.append(interfaceToJson(interface));
    }

    return array;
}

Connection connectionFromJson(const QJsonObject& object) {
    Connection connection;
    connection.id = object.value(QStringLiteral("id")).toString();
    connection.from = endpointFromJson(objectValue(object, QStringLiteral("from")));
    connection.to = endpointFromJson(objectValue(object, QStringLiteral("to")));
    if (object.contains(QStringLiteral("kind"))) {
        connection.kind = object.value(QStringLiteral("kind")).toString();
    }
    connection.config = objectValue(object, QStringLiteral("config"));
    connection.constraints = objectValue(object, QStringLiteral("constraints"));
    connection.metadata = objectValue(object, QStringLiteral("metadata"));
    return connection;
}

QJsonObject connectionToJson(const Connection& connection) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), connection.id);
    object.insert(QStringLiteral("from"), endpointToJson(connection.from));
    object.insert(QStringLiteral("to"), endpointToJson(connection.to));
    insertStringIfNonEmpty(object, QStringLiteral("kind"), connection.kind);
    insertObjectIfNonEmpty(object, QStringLiteral("config"), connection.config);
    insertObjectIfNonEmpty(object, QStringLiteral("constraints"), connection.constraints);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), connection.metadata);
    return object;
}

QVector<Connection> connectionsFromJson(const QJsonValue& value) {
    QVector<Connection> connections;
    const QJsonArray array = value.toArray();
    connections.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            connections.append(connectionFromJson(item.toObject()));
        }
    }

    return connections;
}

QJsonArray connectionsToJson(const QVector<Connection>& connections) {
    QJsonArray array;
    for (const Connection& connection : connections) {
        array.append(connectionToJson(connection));
    }

    return array;
}

TopologyAttachment attachmentFromJson(const QJsonObject& object) {
    TopologyAttachment attachment;
    attachment.id = object.value(QStringLiteral("id")).toString();
    attachment.topologyId = object.value(QStringLiteral("topologyId")).toString();
    attachment.attachmentPoint = objectValue(object, QStringLiteral("attachmentPoint"));
    attachment.componentRef = object.value(QStringLiteral("componentRef")).toString();
    attachment.interfaceRef = object.value(QStringLiteral("interfaceRef")).toString();
    attachment.adapterRef = object.value(QStringLiteral("adapterRef")).toString();
    attachment.config = objectValue(object, QStringLiteral("config"));
    return attachment;
}

QJsonObject attachmentToJson(const TopologyAttachment& attachment) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), attachment.id);
    insertStringIfNonEmpty(object, QStringLiteral("topologyId"), attachment.topologyId);
    insertObjectIfNonEmpty(object, QStringLiteral("attachmentPoint"), attachment.attachmentPoint);
    insertStringIfNonEmpty(object, QStringLiteral("componentRef"), attachment.componentRef);
    insertStringIfNonEmpty(object, QStringLiteral("interfaceRef"), attachment.interfaceRef);
    insertStringIfNonEmpty(object, QStringLiteral("adapterRef"), attachment.adapterRef);
    insertObjectIfNonEmpty(object, QStringLiteral("config"), attachment.config);
    return object;
}

QVector<TopologyAttachment> attachmentsFromJson(const QJsonValue& value) {
    QVector<TopologyAttachment> attachments;
    const QJsonArray array = value.toArray();
    attachments.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            attachments.append(attachmentFromJson(item.toObject()));
        }
    }

    return attachments;
}

QJsonArray attachmentsToJson(const QVector<TopologyAttachment>& attachments) {
    QJsonArray array;
    for (const TopologyAttachment& attachment : attachments) {
        array.append(attachmentToJson(attachment));
    }

    return array;
}

TopologyGraph topologyFromJson(const QJsonObject& object) {
    TopologyGraph topology;
    topology.id = object.value(QStringLiteral("id")).toString();
    topology.schema = object.value(QStringLiteral("schema")).toString();
    topology.ownerComponentId = object.value(QStringLiteral("ownerComponentId")).toString();
    topology.kind = object.value(QStringLiteral("kind")).toString();
    topology.family = object.value(QStringLiteral("family")).toString();
    topology.providerRef = object.value(QStringLiteral("providerRef")).toString();
    topology.parameters = objectValue(object, QStringLiteral("parameters"));
    topology.constraints = objectValue(object, QStringLiteral("constraints"));
    topology.nodes = objectVectorFromJson(object.value(QStringLiteral("nodes")));
    topology.links = objectVectorFromJson(object.value(QStringLiteral("links")));
    topology.attachments = attachmentsFromJson(object.value(QStringLiteral("attachments")));
    topology.routing = objectValue(object, QStringLiteral("routing"));
    topology.metadata = objectValue(object, QStringLiteral("metadata"));
    return topology;
}

QJsonObject topologyToJson(const TopologyGraph& topology) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), topology.id);
    insertStringIfNonEmpty(object, QStringLiteral("schema"), topology.schema);
    insertStringIfNonEmpty(object, QStringLiteral("ownerComponentId"), topology.ownerComponentId);
    insertStringIfNonEmpty(object, QStringLiteral("kind"), topology.kind);
    insertStringIfNonEmpty(object, QStringLiteral("family"), topology.family);
    insertStringIfNonEmpty(object, QStringLiteral("providerRef"), topology.providerRef);
    insertObjectIfNonEmpty(object, QStringLiteral("parameters"), topology.parameters);
    insertObjectIfNonEmpty(object, QStringLiteral("constraints"), topology.constraints);
    object.insert(QStringLiteral("nodes"), objectVectorToJson(topology.nodes));
    object.insert(QStringLiteral("links"), objectVectorToJson(topology.links));
    object.insert(QStringLiteral("attachments"), attachmentsToJson(topology.attachments));
    insertObjectIfNonEmpty(object, QStringLiteral("routing"), topology.routing);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), topology.metadata);
    return object;
}

QVector<TopologyGraph> topologiesFromJson(const QJsonValue& value) {
    QVector<TopologyGraph> topologies;
    const QJsonArray array = value.toArray();
    topologies.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            topologies.append(topologyFromJson(item.toObject()));
        }
    }

    return topologies;
}

QJsonArray topologiesToJson(const QVector<TopologyGraph>& topologies) {
    QJsonArray array;
    for (const TopologyGraph& topology : topologies) {
        array.append(topologyToJson(topology));
    }

    return array;
}

ViewDocument viewFromJson(const QJsonObject& object) {
    ViewDocument view;
    view.id = object.value(QStringLiteral("id")).toString();
    view.schema = object.value(QStringLiteral("schema")).toString();
    view.kind = object.value(QStringLiteral("kind")).toString();
    view.targetRef = object.value(QStringLiteral("targetRef")).toString();
    view.providerRef = object.value(QStringLiteral("providerRef")).toString();
    view.sourceRef = object.value(QStringLiteral("sourceRef")).toString();
    view.templates = objectValue(object, QStringLiteral("templates"));
    view.portGrouping = objectValue(object, QStringLiteral("portGrouping"));
    view.labels = objectValue(object, QStringLiteral("labels"));
    view.badges = objectValue(object, QStringLiteral("badges"));
    view.propertyGroups = objectValue(object, QStringLiteral("propertyGroups"));
    view.layoutPreference = objectValue(object, QStringLiteral("layoutPreference"));
    view.interactionAffordances =
        objectValue(object, QStringLiteral("interactionAffordances"));
    view.diagnosticsOverlay = objectValue(object, QStringLiteral("diagnosticsOverlay"));
    view.icons = objectValue(object, QStringLiteral("icons"));
    view.layout = objectValue(object, QStringLiteral("layout"));
    view.presentationState = objectValue(object, QStringLiteral("presentationState"));
    view.metadata = objectValue(object, QStringLiteral("metadata"));
    return view;
}

QJsonObject viewToJson(const ViewDocument& view) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), view.id);
    insertStringIfNonEmpty(object, QStringLiteral("schema"), view.schema);
    insertStringIfNonEmpty(object, QStringLiteral("kind"), view.kind);
    insertStringIfNonEmpty(object, QStringLiteral("targetRef"), view.targetRef);
    insertStringIfNonEmpty(object, QStringLiteral("providerRef"), view.providerRef);
    insertStringIfNonEmpty(object, QStringLiteral("sourceRef"), view.sourceRef);
    insertObjectIfNonEmpty(object, QStringLiteral("templates"), view.templates);
    insertObjectIfNonEmpty(object, QStringLiteral("portGrouping"), view.portGrouping);
    insertObjectIfNonEmpty(object, QStringLiteral("labels"), view.labels);
    insertObjectIfNonEmpty(object, QStringLiteral("badges"), view.badges);
    insertObjectIfNonEmpty(object, QStringLiteral("propertyGroups"), view.propertyGroups);
    insertObjectIfNonEmpty(object, QStringLiteral("layoutPreference"), view.layoutPreference);
    insertObjectIfNonEmpty(object,
                           QStringLiteral("interactionAffordances"),
                           view.interactionAffordances);
    insertObjectIfNonEmpty(object,
                           QStringLiteral("diagnosticsOverlay"),
                           view.diagnosticsOverlay);
    insertObjectIfNonEmpty(object, QStringLiteral("icons"), view.icons);
    insertObjectIfNonEmpty(object, QStringLiteral("layout"), view.layout);
    insertObjectIfNonEmpty(object, QStringLiteral("presentationState"), view.presentationState);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), view.metadata);
    return object;
}

QVector<ViewDocument> viewsFromJson(const QJsonValue& value) {
    QVector<ViewDocument> views;
    const QJsonArray array = value.toArray();
    views.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            views.append(viewFromJson(item.toObject()));
        }
    }

    return views;
}

QJsonArray viewsToJson(const QVector<ViewDocument>& views) {
    QJsonArray array;
    for (const ViewDocument& view : views) {
        array.append(viewToJson(view));
    }

    return array;
}

QVector<ExtensionBlock> extensionsFromJson(const QJsonValue& value) {
    QVector<ExtensionBlock> extensions;
    const QJsonArray array = value.toArray();
    extensions.reserve(array.size());

    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            extensions.append(extensionBlockFromJson(item.toObject()));
        }
    }

    return extensions;
}

QJsonArray extensionsToJson(const QVector<ExtensionBlock>& extensions) {
    QJsonArray array;
    for (const ExtensionBlock& extension : extensions) {
        array.append(extensionBlockToJson(extension));
    }

    return array;
}

} // namespace

ProjectDocumentReadResult ProjectDocumentV1::readObject(const QJsonObject& object) {
    const QString schema = object.value(QStringLiteral("schema")).toString();
    if (schema != schemaids::projectV1) {
        ProjectDocumentReadResult result;
        result.issues.append(issue(QStringLiteral("project.unsupported_schema"),
                                   QStringLiteral("Project schema is not supported."),
                                   QStringLiteral("/schema")));
        return result;
    }

    ProjectDesign project;
    project.schema = schema;
    project.id = object.value(QStringLiteral("id")).toString();
    project.name = object.value(QStringLiteral("name")).toString();
    project.constraints = objectValue(object, QStringLiteral("constraints"));
    project.metadata = objectValue(object, QStringLiteral("metadata"));
    project.packages = packagesFromJson(object.value(QStringLiteral("packages")));
    project.components = componentsFromJson(object.value(QStringLiteral("components")));
    project.interfaces = interfacesFromJson(object.value(QStringLiteral("interfaces")));
    project.connections = connectionsFromJson(object.value(QStringLiteral("connections")));
    project.topologies = topologiesFromJson(object.value(QStringLiteral("topologies")));
    project.views = viewsFromJson(object.value(QStringLiteral("views")));
    project.diagnostics = objectVectorFromJson(object.value(QStringLiteral("diagnostics")));
    project.artifacts = objectVectorFromJson(object.value(QStringLiteral("artifacts")));
    project.extensions = extensionsFromJson(object.value(QStringLiteral("extensions")));

    QVector<ValidationIssue> readIssues;
    appendReadShapeIssues(readIssues, object);
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("packages")),
                                    QStringLiteral("project.invalid_package_shape"),
                                    QStringLiteral("/packages"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("components")),
                                    QStringLiteral("project.invalid_component_shape"),
                                    QStringLiteral("/components"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("interfaces")),
                                    QStringLiteral("project.invalid_interface_shape"),
                                    QStringLiteral("/interfaces"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("connections")),
                                    QStringLiteral("project.invalid_connection_shape"),
                                    QStringLiteral("/connections"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("topologies")),
                                    QStringLiteral("project.invalid_topology_shape"),
                                    QStringLiteral("/topologies"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("views")),
                                    QStringLiteral("project.invalid_view_shape"),
                                    QStringLiteral("/views"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("diagnostics")),
                                    QStringLiteral("project.invalid_diagnostic_shape"),
                                    QStringLiteral("/diagnostics"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("artifacts")),
                                    QStringLiteral("project.invalid_artifact_shape"),
                                    QStringLiteral("/artifacts"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(QStringLiteral("extensions")),
                                    QStringLiteral("project.invalid_extension_shape"),
                                    QStringLiteral("/extensions"));

    ProjectDocumentReadResult result;
    result.project = project;
    result.issues = readIssues;
    result.issues += validateProjectDesign(project);
    result.success = result.issues.isEmpty();
    return result;
}

QJsonObject ProjectDocumentV1::writeObject(const ProjectDesign& project) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::projectV1);
    object.insert(QStringLiteral("id"), project.id);
    object.insert(QStringLiteral("name"), project.name);
    object.insert(QStringLiteral("packages"), packagesToJson(project.packages));
    object.insert(QStringLiteral("components"), componentsToJson(project.components));
    object.insert(QStringLiteral("interfaces"), interfacesToJson(project.interfaces));
    object.insert(QStringLiteral("connections"), connectionsToJson(project.connections));
    object.insert(QStringLiteral("topologies"), topologiesToJson(project.topologies));
    object.insert(QStringLiteral("views"), viewsToJson(project.views));
    object.insert(QStringLiteral("diagnostics"), objectVectorToJson(project.diagnostics));
    object.insert(QStringLiteral("artifacts"), objectVectorToJson(project.artifacts));
    object.insert(QStringLiteral("extensions"), extensionsToJson(project.extensions));
    insertObjectIfNonEmpty(object, QStringLiteral("constraints"), project.constraints);
    insertObjectIfNonEmpty(object, QStringLiteral("metadata"), project.metadata);
    return object;
}

} // namespace ipcraft::core
