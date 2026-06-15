// ProjectDesignSerializer converts runtime ProjectDesign records to/from ProjectDocument V1.
#include "project/projectdesignserializer.h"

#include "ipcraft/schemaids.h"

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
        design.components.append(component);
    }

    design.metadata = document.projectMetadata;
    return design;
}
