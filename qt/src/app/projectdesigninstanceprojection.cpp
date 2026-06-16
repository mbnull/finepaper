#include "app/projectdesigninstanceprojection.h"

#include "ipcraft/schemaids.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <optional>

namespace {

const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId) {
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const IpCatalogEntry& entry) {
        return entry.id == ipcoreId;
    });
    return it == entries.cend() ? nullptr : &(*it);
}

QString packageRefKey(const QString& id, const QString& version) {
    return version.trimmed().isEmpty() ? id : id + QLatin1Char('@') + version;
}

QString packageRefKey(const ipcraft::core::PackageRef& package) {
    return packageRefKey(package.id, package.version);
}

ProjectPackageRef packageRefFromComponentRef(
    const QString& componentPackageRef,
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

QJsonObject configBundleJsonFromComponentConfig(const QJsonObject& config) {
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

std::optional<QJsonObject> graphConfigFromObject(const QJsonObject& object) {
    const QStringList keys{
        QStringLiteral("graph_config"),
        QStringLiteral("graphConfig"),
        QStringLiteral("ipcraft.graph_config")
    };
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return std::nullopt;
}

std::optional<QJsonObject> graphConfigForComponent(
    const ipcraft::core::ComponentInstance& component) {
    if (const std::optional<QJsonObject> fromExtension =
            graphConfigFromObject(component.extensionData)) {
        return fromExtension;
    }
    return graphConfigFromObject(component.metadata);
}

QJsonObject normalizedGraphConfig(QJsonObject graphConfig = {}) {
    graphConfig.insert(QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1);
    if (!graphConfig.value(QStringLiteral("objects")).isArray()) {
        graphConfig.insert(QStringLiteral("objects"), QJsonArray{});
    }
    if (!graphConfig.value(QStringLiteral("relationships")).isArray()) {
        graphConfig.insert(QStringLiteral("relationships"), QJsonArray{});
    }
    if (!graphConfig.value(QStringLiteral("properties")).isObject()) {
        graphConfig.insert(QStringLiteral("properties"), QJsonObject{});
    }
    if (!graphConfig.value(QStringLiteral("native")).isObject()) {
        graphConfig.insert(QStringLiteral("native"), QJsonObject{});
    }
    return graphConfig;
}

QJsonArray objectVectorToArray(const QVector<QJsonObject>& objects) {
    QJsonArray array;
    for (const QJsonObject& object : objects) {
        array.append(object);
    }
    return array;
}

void appendObjects(QJsonArray& target, const QVector<QJsonObject>& objects) {
    for (const QJsonObject& object : objects) {
        target.append(object);
    }
}

void mergeTopologyIntoGraphConfig(QJsonObject& graphConfig,
                                  const ipcraft::core::TopologyGraph& topology) {
    graphConfig = normalizedGraphConfig(graphConfig);
    QJsonArray objects = graphConfig.value(QStringLiteral("objects")).toArray();
    QJsonArray relationships = graphConfig.value(QStringLiteral("relationships")).toArray();
    appendObjects(objects, topology.nodes);
    appendObjects(relationships, topology.links);
    graphConfig.insert(QStringLiteral("objects"), objects);
    graphConfig.insert(QStringLiteral("relationships"), relationships);

    QJsonObject properties = graphConfig.value(QStringLiteral("properties")).toObject();
    if (!topology.parameters.isEmpty()) {
        properties.insert(QStringLiteral("parameters"), topology.parameters);
    }
    if (!topology.constraints.isEmpty()) {
        properties.insert(QStringLiteral("constraints"), topology.constraints);
    }
    graphConfig.insert(QStringLiteral("properties"), properties);

    QJsonObject native = graphConfig.value(QStringLiteral("native")).toObject();
    QJsonArray topologies = native.value(QStringLiteral("topologies")).toArray();
    QJsonObject topologyNative;
    topologyNative.insert(QStringLiteral("id"), topology.id);
    topologyNative.insert(QStringLiteral("kind"), topology.kind);
    if (!topology.family.trimmed().isEmpty()) {
        topologyNative.insert(QStringLiteral("family"), topology.family);
    }
    if (!topology.providerRef.trimmed().isEmpty()) {
        topologyNative.insert(QStringLiteral("providerRef"), topology.providerRef);
    }
    if (!topology.routing.isEmpty()) {
        topologyNative.insert(QStringLiteral("routing"), topology.routing);
    }
    if (!topology.metadata.isEmpty()) {
        topologyNative.insert(QStringLiteral("metadata"), topology.metadata);
    }
    topologyNative.insert(QStringLiteral("objects"), objectVectorToArray(topology.nodes));
    topologyNative.insert(QStringLiteral("relationships"), objectVectorToArray(topology.links));
    topologies.append(topologyNative);
    native.insert(QStringLiteral("topologies"), topologies);
    graphConfig.insert(QStringLiteral("native"), native);
}

QHash<QString, QJsonObject> graphConfigsByComponentId(
    const ipcraft::core::ProjectDesign& design) {
    QHash<QString, QJsonObject> graphConfigs;
    for (const ipcraft::core::ComponentInstance& component : design.components) {
        if (const std::optional<QJsonObject> graphConfig =
                graphConfigForComponent(component)) {
            graphConfigs.insert(component.id, normalizedGraphConfig(*graphConfig));
        }
    }

    for (const ipcraft::core::TopologyGraph& topology : design.topologies) {
        const QString ownerComponentId = topology.ownerComponentId.trimmed();
        if (ownerComponentId.isEmpty()) {
            continue;
        }
        QJsonObject graphConfig = graphConfigs.value(ownerComponentId);
        mergeTopologyIntoGraphConfig(graphConfig, topology);
        graphConfigs.insert(ownerComponentId, graphConfig);
    }
    return graphConfigs;
}

void setLegacyStateFromConfig(ProjectIpInstanceRecord& record) {
    const QJsonValue parameters = record.config.value(QStringLiteral("parameters"));
    if (parameters.isObject()) {
        record.state.insert(QStringLiteral("global_parameters"), parameters.toObject());
    }
}

} // namespace

namespace ProjectDesignInstanceProjection {

QVector<ProjectIpInstanceRecord> instancesFromProjectDesign(
    const ipcraft::core::ProjectDesign& design,
    const QList<IpCatalogEntry>& catalogEntries) {
    QVector<ProjectIpInstanceRecord> instances;
    instances.reserve(design.components.size());
    const QHash<QString, QJsonObject> graphConfigs = graphConfigsByComponentId(design);

    for (const ipcraft::core::ComponentInstance& component : design.components) {
        ProjectIpInstanceRecord record;
        record.id = component.id;
        record.instanceId = component.id;
        record.package = packageRefFromComponentRef(component.packageRef, design.packages);
        record.ipcoreId = record.package.id;
        const IpCatalogEntry* entry = findCatalogEntry(catalogEntries, record.ipcoreId);
        if (entry &&
            !component.type.trimmed().isEmpty() &&
            !entry->moduleTypes.isEmpty() &&
            !entry->moduleTypes.contains(component.type)) {
            continue;
        }
        record.config = configBundleJsonFromComponentConfig(component.config);
        record.displayName = component.metadata.value(QStringLiteral("label")).toString();
        if (record.displayName.trimmed().isEmpty()) {
            record.displayName = component.metadata.value(QStringLiteral("name")).toString();
        }
        if (record.displayName.trimmed().isEmpty()) {
            record.displayName = record.id;
        }
        if (record.package.version.trimmed().isEmpty()) {
            record.package.version = QStringLiteral("1.0");
        }
        if (!component.type.trimmed().isEmpty()) {
            record.native.insert(QStringLiteral("componentType"), component.type);
        }
        if (!component.identity.isEmpty()) {
            record.native.insert(QStringLiteral("identity"), component.identity);
        }
        if (!component.metadata.isEmpty()) {
            record.native.insert(QStringLiteral("metadata"), component.metadata);
        }
        if (!component.extensionData.isEmpty()) {
            record.native.insert(QStringLiteral("extensionData"), component.extensionData);
        }
        if (graphConfigs.contains(component.id)) {
            record.hasGraphConfig = true;
            record.graphConfigIsNull = false;
            record.graphConfig = graphConfigs.value(component.id);
        }
        setLegacyStateFromConfig(record);
        instances.append(record);
    }
    return instances;
}

} // namespace ProjectDesignInstanceProjection
