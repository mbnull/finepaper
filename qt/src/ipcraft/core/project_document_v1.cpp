#include "ipcraft/core/project_document_v1.h"

#include "ipcraft/contract/projectkeys.h"
#include "ipcraft/diagnosticids.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <initializer_list>

namespace ipcraft::core {
namespace {

namespace projectkeys = ipcraft::contract::projectkeys;
namespace diagnosticids = ipcraft::diagnosticids;

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
        projectkeys::schema(),
        projectkeys::id(),
        projectkeys::name(),
        projectkeys::packages(),
        projectkeys::components(),
        projectkeys::interfaces(),
        projectkeys::connections(),
        projectkeys::topologies(),
        projectkeys::constraints(),
        projectkeys::views(),
        projectkeys::diagnostics(),
        projectkeys::artifacts(),
        projectkeys::extensions(),
        projectkeys::metadata()
    };

    return allowedKeys.contains(key);
}

QString childPath(const QString& path, const QString& key) {
    return path + QStringLiteral("/") + key;
}

bool isAllowedKey(const QString& key, std::initializer_list<QString> allowedKeys) {
    for (const QString& allowedKey : allowedKeys) {
        if (key == allowedKey) {
            return true;
        }
    }

    return false;
}

void appendUnknownFieldIssues(QVector<ValidationIssue>& issues,
                              const QJsonObject& object,
                              const QString& path,
                              std::initializer_list<QString> allowedKeys) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!isAllowedKey(it.key(), allowedKeys)) {
            issues.append(issue(QStringLiteral("project.unknown_field"),
                                QStringLiteral("Project field is not supported."),
                                childPath(path, it.key())));
        }
    }
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

void appendCollectionUnknownFieldIssues(QVector<ValidationIssue>& issues,
                                        const QJsonObject& project,
                                        const QString& collection,
                                        std::initializer_list<QString> allowedKeys) {
    const QJsonArray items = project.value(collection).toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            continue;
        }

        appendUnknownFieldIssues(issues,
                                 items.at(index).toObject(),
                                 QStringLiteral("/%1/%2").arg(collection).arg(index),
                                 allowedKeys);
    }
}

void appendRequiredTopLevelFieldIssue(QVector<ValidationIssue>& issues,
                                      const QJsonObject& object,
                                      const QString& key,
                                      const QString& code) {
    if (!object.contains(key)) {
        issues.append(issue(code,
                            QStringLiteral("Required project field is missing."),
                            childPath(QStringLiteral(""), key)));
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
    const QJsonArray topologies = project.value(projectkeys::topologies()).toArray();
    for (qsizetype index = 0; index < topologies.size(); ++index) {
        if (!topologies.at(index).isObject()) {
            continue;
        }

        const QJsonObject topology = topologies.at(index).toObject();
        const QString path = QStringLiteral("/topologies/%1").arg(index);
        appendUnknownFieldIssues(issues,
                                 topology,
                                 path,
                                 {projectkeys::id(),
                                  projectkeys::schema(),
                                  projectkeys::ownerComponentId(),
                                  projectkeys::kind(),
                                  projectkeys::family(),
                                  projectkeys::providerRef(),
                                  projectkeys::parameters(),
                                  projectkeys::constraints(),
                                  projectkeys::nodes(),
                                  projectkeys::links(),
                                  projectkeys::attachments(),
                                  projectkeys::routing(),
                                  projectkeys::metadata()});
        if (topology.value(projectkeys::schema()).toString() ==
            schemaids::topologyParametricV1) {
            if (!topology.contains(projectkeys::parameters())) {
                issues.append(issue(QStringLiteral("topology.missing_parameters"),
                                    QStringLiteral("Parametric topology parameters are required."),
                                    childPath(path, projectkeys::parameters())));
            }

            if (topology.contains(projectkeys::nodes())) {
                issues.append(issue(QStringLiteral("project.parametric_topology_nodes_forbidden"),
                                    QStringLiteral("Parametric topology must not contain graph nodes."),
                                    childPath(path, projectkeys::nodes())));
            }

            if (topology.contains(projectkeys::links())) {
                issues.append(issue(QStringLiteral("project.parametric_topology_links_forbidden"),
                                    QStringLiteral("Parametric topology must not contain graph links."),
                                    childPath(path, projectkeys::links())));
            }
        }

        if (topology.value(projectkeys::schema()).toString() ==
            schemaids::topologyGraphV1) {
            if (!topology.contains(projectkeys::nodes())) {
                issues.append(issue(QStringLiteral("topology.missing_nodes"),
                                    QStringLiteral("Graph topology nodes are required."),
                                    childPath(path, projectkeys::nodes())));
            }

            if (!topology.contains(projectkeys::links())) {
                issues.append(issue(QStringLiteral("topology.missing_links"),
                                    QStringLiteral("Graph topology links are required."),
                                    childPath(path, projectkeys::links())));
            }
        }

        appendObjectFieldShapeIssues(issues,
                                     topology,
                                     path,
                                     {{projectkeys::parameters(),
                                       QStringLiteral("project.invalid_topology_parameters_shape")},
                                      {projectkeys::constraints(),
                                       QStringLiteral("project.invalid_topology_constraints_shape")},
                                      {projectkeys::routing(),
                                       QStringLiteral("project.invalid_topology_routing_shape")},
                                      {projectkeys::metadata(),
                                       QStringLiteral("project.invalid_topology_metadata_shape")}});
        appendObjectArrayFieldShapeIssues(issues,
                                          topology,
                                          path,
                                          {{projectkeys::nodes(),
                                            QStringLiteral("project.invalid_topology_nodes_shape"),
                                            QStringLiteral("project.invalid_topology_node_shape")},
                                           {projectkeys::links(),
                                            QStringLiteral("project.invalid_topology_links_shape"),
                                            QStringLiteral("project.invalid_topology_link_shape")},
                                           {projectkeys::attachments(),
                                            QStringLiteral("project.invalid_topology_attachments_shape"),
                                            QStringLiteral("project.invalid_topology_attachment_shape")}});

        const QJsonValue attachmentsValue = topology.value(projectkeys::attachments());
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
            appendUnknownFieldIssues(issues,
                                     attachment,
                                     attachmentPath,
                                     {projectkeys::id(),
                                      projectkeys::topologyId(),
                                      projectkeys::attachmentPoint(),
                                      projectkeys::componentRef(),
                                      projectkeys::interfaceRef(),
                                      projectkeys::adapterRef(),
                                      projectkeys::config()});
            appendObjectFieldShapeIssues(
                issues,
                attachment,
                attachmentPath,
                {{projectkeys::attachmentPoint(),
                  QStringLiteral("project.invalid_topology_attachment_point_shape")},
                 {projectkeys::config(),
                  QStringLiteral("project.invalid_topology_attachment_config_shape")}});
        }
    }
}

void appendReadShapeIssues(QVector<ValidationIssue>& issues,
                           const QJsonObject& object) {
    appendUnknownTopLevelFieldIssues(issues, object);
    appendRequiredTopLevelFieldIssue(issues,
                                     object,
                                     projectkeys::packages(),
                                     QStringLiteral("project.missing_packages"));
    appendRequiredTopLevelFieldIssue(issues,
                                     object,
                                     projectkeys::components(),
                                     QStringLiteral("project.missing_components"));
    appendObjectFieldShapeIssue(issues,
                                object,
                                projectkeys::constraints(),
                                QStringLiteral("project.invalid_constraints_shape"),
                                QStringLiteral(""));
    appendObjectFieldShapeIssue(issues,
                                object,
                                projectkeys::metadata(),
                                QStringLiteral("project.invalid_metadata_shape"),
                                QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::packages(),
                               QStringLiteral("project.invalid_packages_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::components(),
                               QStringLiteral("project.invalid_components_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::interfaces(),
                               QStringLiteral("project.invalid_interfaces_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::connections(),
                               QStringLiteral("project.invalid_connections_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::topologies(),
                               QStringLiteral("project.invalid_topologies_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::views(),
                               QStringLiteral("project.invalid_views_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::diagnostics(),
                               QStringLiteral("project.invalid_diagnostics_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::artifacts(),
                               QStringLiteral("project.invalid_artifacts_shape"),
                               QStringLiteral(""));
    appendArrayFieldShapeIssue(issues,
                               object,
                               projectkeys::extensions(),
                               QStringLiteral("project.invalid_extensions_shape"),
                               QStringLiteral(""));
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::packages(),
        {projectkeys::id(), projectkeys::version()});
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::components(),
        {projectkeys::id(),
         projectkeys::type(),
         projectkeys::packageRef(),
         projectkeys::identity(),
         projectkeys::config(),
         projectkeys::metadata(),
         projectkeys::extensionData()});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        projectkeys::components(),
        {{projectkeys::identity(), QStringLiteral("project.invalid_component_identity_shape")},
         {projectkeys::config(), QStringLiteral("project.invalid_component_config_shape")},
         {projectkeys::metadata(), QStringLiteral("project.invalid_component_metadata_shape")},
         {projectkeys::extensionData(),
          QStringLiteral("project.invalid_component_extension_data_shape")}});
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::interfaces(),
        {projectkeys::id(),
         projectkeys::ownerComponentId(),
         projectkeys::type(),
         projectkeys::role(),
         projectkeys::direction(),
         projectkeys::protocol(),
         projectkeys::clockRef(),
         projectkeys::resetRef(),
         projectkeys::config(),
         projectkeys::metadata()});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        projectkeys::interfaces(),
        {{projectkeys::config(), QStringLiteral("project.invalid_interface_config_shape")},
         {projectkeys::metadata(), QStringLiteral("project.invalid_interface_metadata_shape")}});
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::connections(),
        {projectkeys::id(),
         projectkeys::from(),
         projectkeys::to(),
         projectkeys::kind(),
         projectkeys::config(),
         projectkeys::constraints(),
         projectkeys::metadata()});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        projectkeys::connections(),
        {{projectkeys::from(), QStringLiteral("project.invalid_connection_endpoint_shape")},
         {projectkeys::to(), QStringLiteral("project.invalid_connection_endpoint_shape")},
         {projectkeys::config(), QStringLiteral("project.invalid_connection_config_shape")},
         {projectkeys::constraints(),
          QStringLiteral("project.invalid_connection_constraints_shape")},
         {projectkeys::metadata(), QStringLiteral("project.invalid_connection_metadata_shape")}});
    const QJsonArray connections = object.value(projectkeys::connections()).toArray();
    for (qsizetype index = 0; index < connections.size(); ++index) {
        if (!connections.at(index).isObject()) {
            continue;
        }

        const QJsonObject connection = connections.at(index).toObject();
        const QString path = QStringLiteral("/connections/%1").arg(index);
        for (const QString& endpointKey : {projectkeys::from(), projectkeys::to()}) {
            if (connection.value(endpointKey).isObject()) {
                appendUnknownFieldIssues(issues,
                                         connection.value(endpointKey).toObject(),
                                         childPath(path, endpointKey),
                                         {projectkeys::component(),
                                          projectkeys::interfaceId()});
            }
        }
    }
    appendTopologyShapeIssues(issues, object);
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::views(),
        {projectkeys::id(),
         projectkeys::schema(),
         projectkeys::kind(),
         projectkeys::targetRef(),
         projectkeys::providerRef(),
         projectkeys::sourceRef(),
         projectkeys::templates(),
         projectkeys::portGrouping(),
         projectkeys::labels(),
         projectkeys::badges(),
         projectkeys::propertyGroups(),
         projectkeys::layoutPreference(),
         projectkeys::interactionAffordances(),
         projectkeys::diagnosticsOverlay(),
         projectkeys::icons(),
         projectkeys::layout(),
         projectkeys::presentationState(),
         projectkeys::metadata()});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        projectkeys::views(),
        {{projectkeys::templates(), QStringLiteral("project.invalid_view_templates_shape")},
         {projectkeys::portGrouping(),
          QStringLiteral("project.invalid_view_port_grouping_shape")},
         {projectkeys::labels(), QStringLiteral("project.invalid_view_labels_shape")},
         {projectkeys::badges(), QStringLiteral("project.invalid_view_badges_shape")},
         {projectkeys::propertyGroups(),
          QStringLiteral("project.invalid_view_property_groups_shape")},
         {projectkeys::layoutPreference(),
          QStringLiteral("project.invalid_view_layout_preference_shape")},
         {projectkeys::interactionAffordances(),
          QStringLiteral("project.invalid_view_interaction_affordances_shape")},
         {projectkeys::diagnosticsOverlay(),
          QStringLiteral("project.invalid_view_diagnostics_overlay_shape")},
         {projectkeys::icons(), QStringLiteral("project.invalid_view_icons_shape")},
         {projectkeys::layout(), QStringLiteral("project.invalid_view_layout_shape")},
         {projectkeys::presentationState(),
          QStringLiteral("project.invalid_view_presentation_state_shape")},
         {projectkeys::metadata(), QStringLiteral("project.invalid_view_metadata_shape")}});
    appendCollectionUnknownFieldIssues(
        issues,
        object,
        projectkeys::extensions(),
        {projectkeys::ownerPackageId(),
         projectkeys::schemaId(),
         projectkeys::version(),
         projectkeys::data(),
         projectkeys::validationState()});
    appendCollectionObjectFieldShapeIssues(
        issues,
        object,
        projectkeys::extensions(),
        {{projectkeys::data(), QStringLiteral("project.invalid_extension_data_shape")},
         {projectkeys::validationState(),
          QStringLiteral("project.invalid_extension_validation_state_shape")}});
}

EndpointRef endpointFromJson(const QJsonObject& object) {
    EndpointRef endpoint;
    endpoint.component = object.value(projectkeys::component()).toString();
    endpoint.interface = object.value(projectkeys::interfaceId()).toString();
    return endpoint;
}

QJsonObject endpointToJson(const EndpointRef& endpoint) {
    QJsonObject object;
    object.insert(projectkeys::component(), endpoint.component);
    object.insert(projectkeys::interfaceId(), endpoint.interface);
    return object;
}

PackageRef packageRefFromJson(const QJsonObject& object) {
    PackageRef package;
    package.id = object.value(projectkeys::id()).toString();
    package.version = object.value(projectkeys::version()).toString();
    return package;
}

QJsonObject packageRefToJson(const PackageRef& package) {
    QJsonObject object;
    object.insert(projectkeys::id(), package.id);
    object.insert(projectkeys::version(), package.version);
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
    component.id = object.value(projectkeys::id()).toString();
    component.type = object.value(projectkeys::type()).toString();
    component.packageRef = object.value(projectkeys::packageRef()).toString();
    component.config = objectValue(object, projectkeys::config());
    component.identity = objectValue(object, projectkeys::identity());
    component.metadata = objectValue(object, projectkeys::metadata());
    component.extensionData = objectValue(object, projectkeys::extensionData());
    return component;
}

QJsonObject componentToJson(const ComponentInstance& component) {
    QJsonObject object;
    object.insert(projectkeys::id(), component.id);
    insertStringIfNonEmpty(object, projectkeys::type(), component.type);
    insertStringIfNonEmpty(object, projectkeys::packageRef(), component.packageRef);
    insertObjectIfNonEmpty(object, projectkeys::identity(), component.identity);
    insertObjectIfNonEmpty(object, projectkeys::config(), component.config);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), component.metadata);
    insertObjectIfNonEmpty(object, projectkeys::extensionData(), component.extensionData);
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
    interface.id = object.value(projectkeys::id()).toString();
    interface.ownerComponentId = object.value(projectkeys::ownerComponentId()).toString();
    interface.type = object.value(projectkeys::type()).toString();
    interface.role = object.value(projectkeys::role()).toString();
    interface.direction = object.value(projectkeys::direction()).toString();
    interface.protocol = object.value(projectkeys::protocol()).toString();
    interface.clockRef = object.value(projectkeys::clockRef()).toString();
    interface.resetRef = object.value(projectkeys::resetRef()).toString();
    interface.config = objectValue(object, projectkeys::config());
    interface.metadata = objectValue(object, projectkeys::metadata());
    return interface;
}

QJsonObject interfaceToJson(const InterfaceInstance& interface) {
    QJsonObject object;
    object.insert(projectkeys::id(), interface.id);
    insertStringIfNonEmpty(object, projectkeys::ownerComponentId(), interface.ownerComponentId);
    insertStringIfNonEmpty(object, projectkeys::type(), interface.type);
    insertStringIfNonEmpty(object, projectkeys::role(), interface.role);
    insertStringIfNonEmpty(object, projectkeys::direction(), interface.direction);
    insertStringIfNonEmpty(object, projectkeys::protocol(), interface.protocol);
    insertStringIfNonEmpty(object, projectkeys::clockRef(), interface.clockRef);
    insertStringIfNonEmpty(object, projectkeys::resetRef(), interface.resetRef);
    insertObjectIfNonEmpty(object, projectkeys::config(), interface.config);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), interface.metadata);
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
    connection.id = object.value(projectkeys::id()).toString();
    connection.from = endpointFromJson(objectValue(object, projectkeys::from()));
    connection.to = endpointFromJson(objectValue(object, projectkeys::to()));
    if (object.contains(projectkeys::kind())) {
        connection.kind = object.value(projectkeys::kind()).toString();
    }
    connection.config = objectValue(object, projectkeys::config());
    connection.constraints = objectValue(object, projectkeys::constraints());
    connection.metadata = objectValue(object, projectkeys::metadata());
    return connection;
}

QJsonObject connectionToJson(const Connection& connection) {
    QJsonObject object;
    object.insert(projectkeys::id(), connection.id);
    object.insert(projectkeys::from(), endpointToJson(connection.from));
    object.insert(projectkeys::to(), endpointToJson(connection.to));
    insertStringIfNonEmpty(object, projectkeys::kind(), connection.kind);
    insertObjectIfNonEmpty(object, projectkeys::config(), connection.config);
    insertObjectIfNonEmpty(object, projectkeys::constraints(), connection.constraints);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), connection.metadata);
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
    attachment.id = object.value(projectkeys::id()).toString();
    attachment.topologyId = object.value(projectkeys::topologyId()).toString();
    attachment.attachmentPoint = objectValue(object, projectkeys::attachmentPoint());
    attachment.componentRef = object.value(projectkeys::componentRef()).toString();
    attachment.interfaceRef = object.value(projectkeys::interfaceRef()).toString();
    attachment.adapterRef = object.value(projectkeys::adapterRef()).toString();
    attachment.config = objectValue(object, projectkeys::config());
    return attachment;
}

QJsonObject attachmentToJson(const TopologyAttachment& attachment) {
    QJsonObject object;
    object.insert(projectkeys::id(), attachment.id);
    insertStringIfNonEmpty(object, projectkeys::topologyId(), attachment.topologyId);
    insertObjectIfNonEmpty(object, projectkeys::attachmentPoint(), attachment.attachmentPoint);
    insertStringIfNonEmpty(object, projectkeys::componentRef(), attachment.componentRef);
    insertStringIfNonEmpty(object, projectkeys::interfaceRef(), attachment.interfaceRef);
    insertStringIfNonEmpty(object, projectkeys::adapterRef(), attachment.adapterRef);
    insertObjectIfNonEmpty(object, projectkeys::config(), attachment.config);
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
    topology.id = object.value(projectkeys::id()).toString();
    topology.schema = object.value(projectkeys::schema()).toString();
    topology.ownerComponentId = object.value(projectkeys::ownerComponentId()).toString();
    topology.kind = object.value(projectkeys::kind()).toString();
    topology.family = object.value(projectkeys::family()).toString();
    topology.providerRef = object.value(projectkeys::providerRef()).toString();
    topology.parameters = objectValue(object, projectkeys::parameters());
    topology.constraints = objectValue(object, projectkeys::constraints());
    topology.nodes = objectVectorFromJson(object.value(projectkeys::nodes()));
    topology.links = objectVectorFromJson(object.value(projectkeys::links()));
    topology.attachments = attachmentsFromJson(object.value(projectkeys::attachments()));
    topology.routing = objectValue(object, projectkeys::routing());
    topology.metadata = objectValue(object, projectkeys::metadata());
    return topology;
}

QJsonObject topologyToJson(const TopologyGraph& topology) {
    QJsonObject object;
    object.insert(projectkeys::id(), topology.id);
    insertStringIfNonEmpty(object, projectkeys::schema(), topology.schema);
    insertStringIfNonEmpty(object, projectkeys::ownerComponentId(), topology.ownerComponentId);
    insertStringIfNonEmpty(object, projectkeys::kind(), topology.kind);
    insertStringIfNonEmpty(object, projectkeys::family(), topology.family);
    insertStringIfNonEmpty(object, projectkeys::providerRef(), topology.providerRef);
    if (topology.schema == schemaids::topologyParametricV1) {
        object.insert(projectkeys::parameters(), topology.parameters);
    } else {
        insertObjectIfNonEmpty(object, projectkeys::parameters(), topology.parameters);
    }
    insertObjectIfNonEmpty(object, projectkeys::constraints(), topology.constraints);
    if (topology.schema != schemaids::topologyParametricV1) {
        object.insert(projectkeys::nodes(), objectVectorToJson(topology.nodes));
        object.insert(projectkeys::links(), objectVectorToJson(topology.links));
    }
    object.insert(projectkeys::attachments(), attachmentsToJson(topology.attachments));
    insertObjectIfNonEmpty(object, projectkeys::routing(), topology.routing);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), topology.metadata);
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
    view.id = object.value(projectkeys::id()).toString();
    view.schema = object.value(projectkeys::schema()).toString();
    view.kind = object.value(projectkeys::kind()).toString();
    view.targetRef = object.value(projectkeys::targetRef()).toString();
    view.providerRef = object.value(projectkeys::providerRef()).toString();
    view.sourceRef = object.value(projectkeys::sourceRef()).toString();
    view.templates = objectValue(object, projectkeys::templates());
    view.portGrouping = objectValue(object, projectkeys::portGrouping());
    view.labels = objectValue(object, projectkeys::labels());
    view.badges = objectValue(object, projectkeys::badges());
    view.propertyGroups = objectValue(object, projectkeys::propertyGroups());
    view.layoutPreference = objectValue(object, projectkeys::layoutPreference());
    view.interactionAffordances =
        objectValue(object, projectkeys::interactionAffordances());
    view.diagnosticsOverlay = objectValue(object, projectkeys::diagnosticsOverlay());
    view.icons = objectValue(object, projectkeys::icons());
    view.layout = objectValue(object, projectkeys::layout());
    view.presentationState = objectValue(object, projectkeys::presentationState());
    view.metadata = objectValue(object, projectkeys::metadata());
    return view;
}

QJsonObject viewToJson(const ViewDocument& view) {
    QJsonObject object;
    object.insert(projectkeys::id(), view.id);
    insertStringIfNonEmpty(object, projectkeys::schema(), view.schema);
    insertStringIfNonEmpty(object, projectkeys::kind(), view.kind);
    insertStringIfNonEmpty(object, projectkeys::targetRef(), view.targetRef);
    insertStringIfNonEmpty(object, projectkeys::providerRef(), view.providerRef);
    insertStringIfNonEmpty(object, projectkeys::sourceRef(), view.sourceRef);
    insertObjectIfNonEmpty(object, projectkeys::templates(), view.templates);
    insertObjectIfNonEmpty(object, projectkeys::portGrouping(), view.portGrouping);
    insertObjectIfNonEmpty(object, projectkeys::labels(), view.labels);
    insertObjectIfNonEmpty(object, projectkeys::badges(), view.badges);
    insertObjectIfNonEmpty(object, projectkeys::propertyGroups(), view.propertyGroups);
    insertObjectIfNonEmpty(object, projectkeys::layoutPreference(), view.layoutPreference);
    insertObjectIfNonEmpty(object,
                           projectkeys::interactionAffordances(),
                           view.interactionAffordances);
    insertObjectIfNonEmpty(object,
                           projectkeys::diagnosticsOverlay(),
                           view.diagnosticsOverlay);
    insertObjectIfNonEmpty(object, projectkeys::icons(), view.icons);
    insertObjectIfNonEmpty(object, projectkeys::layout(), view.layout);
    insertObjectIfNonEmpty(object, projectkeys::presentationState(), view.presentationState);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), view.metadata);
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
    const QString schema = object.value(projectkeys::schema()).toString();
    if (schema != schemaids::projectV1) {
        ProjectDocumentReadResult result;
        result.issues.append(issue(diagnosticids::projectUnsupportedSchema(),
                                   QStringLiteral("Project schema is not supported."),
                                   QStringLiteral("/schema")));
        return result;
    }

    ProjectDesign project;
    project.schema = schema;
    project.id = object.value(projectkeys::id()).toString();
    project.name = object.value(projectkeys::name()).toString();
    project.constraints = objectValue(object, projectkeys::constraints());
    project.metadata = objectValue(object, projectkeys::metadata());
    project.packages = packagesFromJson(object.value(projectkeys::packages()));
    project.components = componentsFromJson(object.value(projectkeys::components()));
    project.interfaces = interfacesFromJson(object.value(projectkeys::interfaces()));
    project.connections = connectionsFromJson(object.value(projectkeys::connections()));
    project.topologies = topologiesFromJson(object.value(projectkeys::topologies()));
    project.views = viewsFromJson(object.value(projectkeys::views()));
    project.diagnostics = objectVectorFromJson(object.value(projectkeys::diagnostics()));
    project.artifacts = objectVectorFromJson(object.value(projectkeys::artifacts()));
    project.extensions = extensionsFromJson(object.value(projectkeys::extensions()));

    QVector<ValidationIssue> readIssues;
    appendReadShapeIssues(readIssues, object);
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::packages()),
                                    QStringLiteral("project.invalid_package_shape"),
                                    QStringLiteral("/packages"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::components()),
                                    QStringLiteral("project.invalid_component_shape"),
                                    QStringLiteral("/components"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::interfaces()),
                                    QStringLiteral("project.invalid_interface_shape"),
                                    QStringLiteral("/interfaces"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::connections()),
                                    QStringLiteral("project.invalid_connection_shape"),
                                    QStringLiteral("/connections"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::topologies()),
                                    QStringLiteral("project.invalid_topology_shape"),
                                    QStringLiteral("/topologies"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::views()),
                                    QStringLiteral("project.invalid_view_shape"),
                                    QStringLiteral("/views"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::diagnostics()),
                                    QStringLiteral("project.invalid_diagnostic_shape"),
                                    QStringLiteral("/diagnostics"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::artifacts()),
                                    QStringLiteral("project.invalid_artifact_shape"),
                                    QStringLiteral("/artifacts"));
    appendNonObjectArrayEntryIssues(readIssues,
                                    object.value(projectkeys::extensions()),
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
    object.insert(projectkeys::schema(), schemaids::projectV1);
    object.insert(projectkeys::id(), project.id);
    object.insert(projectkeys::name(), project.name);
    object.insert(projectkeys::packages(), packagesToJson(project.packages));
    object.insert(projectkeys::components(), componentsToJson(project.components));
    object.insert(projectkeys::interfaces(), interfacesToJson(project.interfaces));
    object.insert(projectkeys::connections(), connectionsToJson(project.connections));
    object.insert(projectkeys::topologies(), topologiesToJson(project.topologies));
    object.insert(projectkeys::views(), viewsToJson(project.views));
    object.insert(projectkeys::diagnostics(), objectVectorToJson(project.diagnostics));
    object.insert(projectkeys::artifacts(), objectVectorToJson(project.artifacts));
    object.insert(projectkeys::extensions(), extensionsToJson(project.extensions));
    insertObjectIfNonEmpty(object, projectkeys::constraints(), project.constraints);
    insertObjectIfNonEmpty(object, projectkeys::metadata(), project.metadata);
    return object;
}

} // namespace ipcraft::core
