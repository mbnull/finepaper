// ProjectDesignSerializer converts runtime ProjectDesign records to/from ProjectDocument V1.
#include "project/projectdesignserializer.h"

#include "ipcraft/compositionmodel.h"
#include "ipcraft/contract/projectkeys.h"
#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QSet>

namespace {

namespace projectkeys = ipcraft::contract::projectkeys;

QString packageRefKey(const QString& id, const QString& version) {
    return version.isEmpty() ? id : id + QLatin1Char('@') + version;
}

QString packageRefKey(const ipcraft::core::PackageRef& package) {
    return packageRefKey(package.id, package.version);
}

ProjectPackageRef packageRefFromComponentRef(const QString& componentPackageRef,
                                             const QVector<ipcraft::core::PackageRef>& packages) {
    for (const ipcraft::core::PackageRef& package : packages) {
        if (componentPackageRef == packageRefKey(package) ||
            componentPackageRef == package.id) {
            return ProjectPackageRef{package.id, package.version};
        }
    }

    const qsizetype separator = componentPackageRef.lastIndexOf(QLatin1Char('@'));
    if (separator > 0 && separator + 1 < componentPackageRef.size()) {
        return ProjectPackageRef{componentPackageRef.left(separator),
                                 componentPackageRef.mid(separator + 1)};
    }

    return ProjectPackageRef{componentPackageRef, {}};
}

void appendPackageRefIfNew(QVector<ipcraft::core::PackageRef>& packages,
                           QSet<QString>& packageKeys,
                           const QString& id,
                           const QString& version) {
    if (id.trimmed().isEmpty()) {
        return;
    }

    const QString key = packageRefKey(id, version);
    if (packageKeys.contains(key)) {
        return;
    }

    packageKeys.insert(key);
    packages.append(ipcraft::core::PackageRef{id, version});
}

QString componentTypeFromInstance(const ProjectIpInstanceRecord& instance) {
    const QString nativeComponentType =
        instance.native.value(QStringLiteral("componentType")).toString().trimmed();
    if (!nativeComponentType.isEmpty()) {
        return nativeComponentType;
    }

    const QString nativeType =
        instance.native.value(QStringLiteral("type")).toString().trimmed();
    if (!nativeType.isEmpty()) {
        return nativeType;
    }

    return instance.state.value(QStringLiteral("componentType")).toString().trimmed();
}

bool isConfigBundleKey(const QString& key) {
    return key == QStringLiteral("parameters") ||
           key == QStringLiteral("tables") ||
           key == QStringLiteral("documents") ||
           key == QStringLiteral("files") ||
           key == QStringLiteral("preserved");
}

bool isConfigBundleSection(const QString& key, const QJsonValue& value) {
    return isConfigBundleKey(key) && value.isObject();
}

QJsonObject runtimeConfigFromDocumentConfig(const QJsonObject& config) {
    bool hasBundleSection = false;
    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        hasBundleSection = hasBundleSection || isConfigBundleSection(it.key(), it.value());
    }
    if (!hasBundleSection) {
        return config;
    }

    QJsonObject runtimeConfig;
    const QJsonValue parameters = config.value(QStringLiteral("parameters"));
    if (parameters.isObject()) {
        const QJsonObject parameterObject = parameters.toObject();
        for (auto it = parameterObject.constBegin(); it != parameterObject.constEnd(); ++it) {
            runtimeConfig.insert(it.key(), it.value());
        }
    }

    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        if (it.key() == QStringLiteral("parameters")) {
            continue;
        }
        if (isConfigBundleSection(it.key(), it.value())) {
            runtimeConfig.insert(it.key(), it.value().toObject());
            continue;
        }
        if (!isConfigBundleKey(it.key())) {
            runtimeConfig.insert(it.key(), it.value());
        }
    }
    return runtimeConfig;
}

QJsonObject documentConfigFromRuntimeConfig(const QJsonObject& config) {
    if (config.isEmpty()) {
        return {};
    }

    bool allKeysAreBundleSections = true;
    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        allKeysAreBundleSections =
            allKeysAreBundleSections && isConfigBundleSection(it.key(), it.value());
    }
    if (allKeysAreBundleSections) {
        return config;
    }

    QJsonObject bundleConfig;
    QJsonObject parameters;
    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        if (isConfigBundleSection(it.key(), it.value())) {
            if (it.key() == QStringLiteral("parameters")) {
                const QJsonObject existingParameters = it.value().toObject();
                for (auto param = existingParameters.constBegin();
                     param != existingParameters.constEnd();
                     ++param) {
                    parameters.insert(param.key(), param.value());
                }
            } else {
                bundleConfig.insert(it.key(), it.value().toObject());
            }
            continue;
        }

        parameters.insert(it.key(), it.value());
    }

    if (!parameters.isEmpty()) {
        bundleConfig.insert(QStringLiteral("parameters"), parameters);
    }
    return bundleConfig;
}

QString designSupplementKey() {
    return QStringLiteral("ipcraft.projectDesignSupplement.v1");
}

QString legacyCompositionMetadataKey() {
    return QStringLiteral("ipcraft.legacyComposition.v1");
}

bool isSupplementField(const QString& key) {
    return key == projectkeys::interfaces() ||
           key == projectkeys::connections() ||
           key == projectkeys::topologies() ||
           key == projectkeys::constraints() ||
           key == projectkeys::views() ||
           key == projectkeys::diagnostics() ||
           key == projectkeys::artifacts() ||
           key == projectkeys::extensions();
}

bool shouldStoreSupplementValue(const QJsonValue& value) {
    if (value.isArray()) {
        return !value.toArray().isEmpty();
    }
    if (value.isObject()) {
        return !value.toObject().isEmpty();
    }
    return !value.isUndefined() && !value.isNull();
}

QJsonObject designSupplementFromSerializedProject(const QJsonObject& serializedProject) {
    QJsonObject supplement;
    for (auto it = serializedProject.constBegin(); it != serializedProject.constEnd(); ++it) {
        if (isSupplementField(it.key()) && shouldStoreSupplementValue(it.value())) {
            supplement.insert(it.key(), it.value());
        }
    }
    return supplement;
}

QJsonObject filteredDesignSupplement(const QJsonObject& object) {
    QJsonObject supplement;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isSupplementField(it.key())) {
            supplement.insert(it.key(), it.value());
        }
    }
    return supplement;
}

void applySupplementField(QJsonObject& serializedProject,
                          const QJsonObject& supplement,
                          const QString& key) {
    if (supplement.contains(key)) {
        serializedProject.insert(key, supplement.value(key));
    }
}

bool hasLegacyComposition(const ProjectComposition& composition) {
    return !composition.connections.isEmpty() ||
           !composition.externalPorts.isEmpty() ||
           !composition.groups.isEmpty() ||
           !composition.properties.isEmpty() ||
           !composition.native.isEmpty();
}

ipcraft::CompositionEndpointRef compositionEndpointFromProjectEndpoint(
    const ProjectEndpointRef& endpoint) {
    ipcraft::CompositionEndpointRef ref;
    ref.instanceId = endpoint.instanceId;
    ref.interfaceId = endpoint.interfaceId;
    ref.portId = endpoint.portId;
    ref.role = endpoint.role;
    ref.properties = endpoint.properties;
    return ref;
}

ipcraft::SystemConnection systemConnectionFromProjectConnection(
    const ProjectConnectionRecord& connection) {
    ipcraft::SystemConnection systemConnection;
    systemConnection.id = connection.id;
    systemConnection.type = connection.type;
    systemConnection.source = connection.sourceKind;
    systemConnection.properties = connection.properties;
    systemConnection.native = connection.native;
    for (const ProjectEndpointRef& endpoint : connection.endpoints) {
        systemConnection.endpoints.append(compositionEndpointFromProjectEndpoint(endpoint));
    }
    return systemConnection;
}

ipcraft::ExternalPort externalPortFromProjectExternalPort(
    const ProjectExternalPortRecord& port) {
    ipcraft::ExternalPort externalPort;
    externalPort.id = port.id;
    externalPort.name = port.name;
    externalPort.hasInterface = port.hasInterface;
    externalPort.interfaceRef = compositionEndpointFromProjectEndpoint(port.interfaceRef);
    externalPort.properties = port.properties;
    externalPort.native = port.native;
    return externalPort;
}

QJsonObject legacyCompositionMetadataObject(const ProjectComposition& composition) {
    ipcraft::CompositionModel model;
    for (const ProjectConnectionRecord& connection : composition.connections) {
        model.connections.append(systemConnectionFromProjectConnection(connection));
    }
    for (const ProjectExternalPortRecord& port : composition.externalPorts) {
        model.externalPorts.append(externalPortFromProjectExternalPort(port));
    }
    model.groups = composition.groups;
    model.properties = composition.properties;
    model.native = composition.native;
    return model.toJson();
}

QJsonObject legacyConnectionMetadataObject(const ProjectConnectionRecord& connection) {
    return QJsonObject{
        {QStringLiteral("legacy"), systemConnectionFromProjectConnection(connection).toJson()}
    };
}

void appendLegacyCompositionProjection(ipcraft::core::ProjectDesign& design,
                                       const ProjectComposition& composition) {
    if (!hasLegacyComposition(composition)) {
        return;
    }

    design.metadata.insert(legacyCompositionMetadataKey(),
                           legacyCompositionMetadataObject(composition));
    for (const ProjectConnectionRecord& legacyConnection : composition.connections) {
        if (legacyConnection.endpoints.size() < 2) {
            continue;
        }

        ipcraft::core::Connection connection;
        connection.id = legacyConnection.id;
        connection.from = ipcraft::core::EndpointRef{
            legacyConnection.endpoints.at(0).instanceId,
            legacyConnection.endpoints.at(0).interfaceId
        };
        connection.to = ipcraft::core::EndpointRef{
            legacyConnection.endpoints.at(1).instanceId,
            legacyConnection.endpoints.at(1).interfaceId
        };
        if (!legacyConnection.type.trimmed().isEmpty()) {
            connection.kind = legacyConnection.type;
        }
        connection.config = legacyConnection.properties;
        connection.metadata = legacyConnectionMetadataObject(legacyConnection);
        design.connections.append(connection);
    }
}

void mergeDesignSupplement(ipcraft::core::ProjectDesign& design,
                           const QJsonObject& documentNative) {
    const QJsonValue supplementValue = documentNative.value(designSupplementKey());
    if (!supplementValue.isObject()) {
        return;
    }

    const QJsonObject supplement = filteredDesignSupplement(supplementValue.toObject());
    if (supplement.isEmpty()) {
        return;
    }

    QJsonObject serializedProject = ipcraft::core::ProjectDocumentV1::writeObject(design);
    applySupplementField(serializedProject, supplement, projectkeys::interfaces());
    applySupplementField(serializedProject, supplement, projectkeys::connections());
    applySupplementField(serializedProject, supplement, projectkeys::topologies());
    applySupplementField(serializedProject, supplement, projectkeys::constraints());
    applySupplementField(serializedProject, supplement, projectkeys::views());
    applySupplementField(serializedProject, supplement, projectkeys::diagnostics());
    applySupplementField(serializedProject, supplement, projectkeys::artifacts());
    applySupplementField(serializedProject, supplement, projectkeys::extensions());

    const ipcraft::core::ProjectDocumentReadResult readResult =
        ipcraft::core::ProjectDocumentV1::readObject(serializedProject);
    if (!readResult.success) {
        return;
    }
    const ipcraft::core::ProjectDesign& supplemented = readResult.project;
    if (supplement.contains(projectkeys::interfaces())) {
        design.interfaces = supplemented.interfaces;
    }
    if (supplement.contains(projectkeys::connections())) {
        design.connections = supplemented.connections;
    }
    if (supplement.contains(projectkeys::topologies())) {
        design.topologies = supplemented.topologies;
    }
    if (supplement.contains(projectkeys::constraints())) {
        design.constraints = supplemented.constraints;
    }
    if (supplement.contains(projectkeys::views())) {
        design.views = supplemented.views;
    }
    if (supplement.contains(projectkeys::diagnostics())) {
        design.diagnostics = supplemented.diagnostics;
    }
    if (supplement.contains(projectkeys::artifacts())) {
        design.artifacts = supplemented.artifacts;
    }
    if (supplement.contains(projectkeys::extensions())) {
        design.extensions = supplemented.extensions;
    }
}

} // namespace

ProjectDocument ProjectDesignSerializer::toDocument(const ipcraft::core::ProjectDesign& design) {
    ProjectDocument document;
    document.schema = design.schema.isEmpty() ? ipcraft::schemaids::projectV1 : design.schema;
    document.projectId = design.id;
    document.projectName = design.name;
    document.name = design.name;
    document.projectMetadata = design.metadata;

    for (const ipcraft::core::PackageRef& package : design.packages) {
        document.ipcores.append(ProjectIpcoreRecord{package.id, package.version});
    }

    for (const ipcraft::core::ComponentInstance& component : design.components) {
        ProjectIpInstanceRecord instance;
        instance.id = component.id;
        instance.instanceId = component.id;
        instance.package = packageRefFromComponentRef(component.packageRef, design.packages);
        instance.ipcoreId = instance.package.id;
        instance.config = documentConfigFromRuntimeConfig(component.config);
        if (!component.type.isEmpty()) {
            instance.native.insert(QStringLiteral("componentType"), component.type);
        }
        if (!component.identity.isEmpty()) {
            instance.native.insert(projectkeys::identity(), component.identity);
        }
        if (!component.metadata.isEmpty()) {
            instance.native.insert(projectkeys::metadata(), component.metadata);
        }
        if (!component.extensionData.isEmpty()) {
            instance.native.insert(projectkeys::extensionData(), component.extensionData);
        }
        document.instances.append(instance);
    }

    const QJsonObject supplement = designSupplementFromSerializedProject(
        ipcraft::core::ProjectDocumentV1::writeObject(design));
    if (!supplement.isEmpty()) {
        document.native.insert(designSupplementKey(), supplement);
    }

    return document;
}

ipcraft::core::ProjectDesign ProjectDesignSerializer::fromDocument(const ProjectDocument& document) {
    ipcraft::core::ProjectDesign design;
    design.schema = document.schema.isEmpty() ? ipcraft::schemaids::projectV1 : document.schema;
    design.id = document.projectId;
    design.name = document.projectName;

    QSet<QString> packageKeys;
    for (const ProjectIpcoreRecord& package : document.ipcores) {
        appendPackageRefIfNew(design.packages, packageKeys, package.id, package.version);
    }

    for (const ProjectIpInstanceRecord& instance : document.instances) {
        appendPackageRefIfNew(design.packages,
                              packageKeys,
                              instance.package.id,
                              instance.package.version);

        ipcraft::core::ComponentInstance component;
        component.id = instance.id;
        component.type = componentTypeFromInstance(instance);
        component.packageRef = packageRefKey(instance.package.id, instance.package.version);
        component.config = runtimeConfigFromDocumentConfig(instance.config);
        if (instance.native.value(projectkeys::identity()).isObject()) {
            component.identity = instance.native.value(projectkeys::identity()).toObject();
        }
        if (instance.native.value(projectkeys::metadata()).isObject()) {
            component.metadata = instance.native.value(projectkeys::metadata()).toObject();
        }
        if (instance.native.value(projectkeys::extensionData()).isObject()) {
            component.extensionData = instance.native.value(projectkeys::extensionData()).toObject();
        }
        if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
            component.extensionData.insert(QStringLiteral("graph_config"), instance.graphConfig);
        }
        design.components.append(component);
    }

    design.metadata = document.projectMetadata;
    appendLegacyCompositionProjection(design, document.composition);
    mergeDesignSupplement(design, document.native);
    return design;
}
