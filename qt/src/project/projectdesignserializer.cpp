// ProjectDesignSerializer converts runtime ProjectDesign records to/from ProjectDocument V1.
#include "project/projectdesignserializer.h"

#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QSet>

namespace {

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

bool isSupplementField(const QString& key) {
    return key == QStringLiteral("interfaces") ||
           key == QStringLiteral("connections") ||
           key == QStringLiteral("topologies") ||
           key == QStringLiteral("constraints") ||
           key == QStringLiteral("views") ||
           key == QStringLiteral("diagnostics") ||
           key == QStringLiteral("artifacts") ||
           key == QStringLiteral("extensions");
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
    applySupplementField(serializedProject, supplement, QStringLiteral("interfaces"));
    applySupplementField(serializedProject, supplement, QStringLiteral("connections"));
    applySupplementField(serializedProject, supplement, QStringLiteral("topologies"));
    applySupplementField(serializedProject, supplement, QStringLiteral("constraints"));
    applySupplementField(serializedProject, supplement, QStringLiteral("views"));
    applySupplementField(serializedProject, supplement, QStringLiteral("diagnostics"));
    applySupplementField(serializedProject, supplement, QStringLiteral("artifacts"));
    applySupplementField(serializedProject, supplement, QStringLiteral("extensions"));

    const ipcraft::core::ProjectDocumentReadResult readResult =
        ipcraft::core::ProjectDocumentV1::readObject(serializedProject);
    const ipcraft::core::ProjectDesign& supplemented = readResult.project;
    if (supplement.contains(QStringLiteral("interfaces"))) {
        design.interfaces = supplemented.interfaces;
    }
    if (supplement.contains(QStringLiteral("connections"))) {
        design.connections = supplemented.connections;
    }
    if (supplement.contains(QStringLiteral("topologies"))) {
        design.topologies = supplemented.topologies;
    }
    if (supplement.contains(QStringLiteral("constraints"))) {
        design.constraints = supplemented.constraints;
    }
    if (supplement.contains(QStringLiteral("views"))) {
        design.views = supplemented.views;
    }
    if (supplement.contains(QStringLiteral("diagnostics"))) {
        design.diagnostics = supplemented.diagnostics;
    }
    if (supplement.contains(QStringLiteral("artifacts"))) {
        design.artifacts = supplemented.artifacts;
    }
    if (supplement.contains(QStringLiteral("extensions"))) {
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
            instance.native.insert(QStringLiteral("identity"), component.identity);
        }
        if (!component.metadata.isEmpty()) {
            instance.native.insert(QStringLiteral("metadata"), component.metadata);
        }
        if (!component.extensionData.isEmpty()) {
            instance.native.insert(QStringLiteral("extensionData"), component.extensionData);
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
        component.type = instance.native.value(QStringLiteral("componentType")).toString();
        if (component.type.isEmpty()) {
            component.type = instance.native.value(QStringLiteral("type")).toString();
        }
        component.packageRef = packageRefKey(instance.package.id, instance.package.version);
        component.config = runtimeConfigFromDocumentConfig(instance.config);
        if (instance.native.value(QStringLiteral("identity")).isObject()) {
            component.identity = instance.native.value(QStringLiteral("identity")).toObject();
        }
        if (instance.native.value(QStringLiteral("metadata")).isObject()) {
            component.metadata = instance.native.value(QStringLiteral("metadata")).toObject();
        }
        if (instance.native.value(QStringLiteral("extensionData")).isObject()) {
            component.extensionData = instance.native.value(QStringLiteral("extensionData")).toObject();
        }
        if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
            component.extensionData.insert(QStringLiteral("graph_config"), instance.graphConfig);
        }
        design.components.append(component);
    }

    design.metadata = document.projectMetadata;
    mergeDesignSupplement(design, document.native);
    return design;
}
