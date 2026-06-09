// IP catalog service implementation.
#include "ipcore/ipcatalogservice.h"

#include "modules/moduleprovider.h"
#include "modules/moduleregistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace {

QString catalogSortLabel(const IpCatalogEntry& entry) {
    return entry.name.trimmed().isEmpty() ? entry.id : entry.name;
}

bool isNocKind(const QString& kind) {
    return kind.trimmed().compare(QStringLiteral("noc"), Qt::CaseInsensitive) == 0;
}

QVector<IpCatalogInstanceLimit> builtInInstanceLimitsForKind(const QString& kind) {
    if (!isNocKind(kind)) {
        return {};
    }

    return {
        IpCatalogInstanceLimit{
            QStringLiteral("kind:noc"),
            QStringLiteral("NoC IP instance"),
            1
        }
    };
}

Parameter::Value parameterDefaultValue(const QString& type, const QJsonValue& value) {
    if (type == QStringLiteral("int")) {
        return value.toInt();
    }
    if (type == QStringLiteral("double")) {
        return value.toDouble();
    }
    if (type == QStringLiteral("bool")) {
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isDouble()) {
            return value.toInt() != 0;
        }
        const QString text = value.toString().trimmed().toLower();
        return text == QStringLiteral("true") ||
               text == QStringLiteral("1") ||
               text == QStringLiteral("yes");
    }
    return value.toString();
}

std::optional<double> optionalDoubleValue(const QJsonValue& value) {
    return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
}

QVector<IpCoreInstanceParameterChoice> parameterChoices(const QJsonObject& object) {
    QVector<IpCoreInstanceParameterChoice> choices;
    const QJsonArray enumValues = object.value(QStringLiteral("enum")).toArray();
    const QJsonObject labels = object.value(QStringLiteral("labels")).toObject();
    for (const QJsonValue& value : enumValues) {
        if (!value.isString()) {
            continue;
        }
        const QString choiceValue = value.toString().trimmed();
        choices.push_back(IpCoreInstanceParameterChoice{
            choiceValue,
            labels.value(choiceValue).toString(choiceValue)
        });
    }
    return choices;
}

QHash<QString, IpCoreInstanceParameterDescriptor>
instanceParametersFromManifest(const QJsonObject& parametersObject) {
    QHash<QString, IpCoreInstanceParameterDescriptor> parameters;
    for (auto it = parametersObject.constBegin(); it != parametersObject.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }

        const QJsonObject object = it.value().toObject();
        IpCoreInstanceParameterDescriptor parameter;
        parameter.name = it.key();
        parameter.type = object.value(QStringLiteral("type")).toString().trimmed();
        parameter.defaultValue =
            parameterDefaultValue(parameter.type, object.value(QStringLiteral("default")));
        parameter.label = object.value(QStringLiteral("label")).toString().trimmed();
        parameter.description = object.value(QStringLiteral("description")).toString().trimmed();
        parameter.minimumValue = optionalDoubleValue(object.value(QStringLiteral("minimum")));
        if (!parameter.minimumValue.has_value()) {
            parameter.minimumValue = optionalDoubleValue(object.value(QStringLiteral("min")));
        }
        parameter.maximumValue = optionalDoubleValue(object.value(QStringLiteral("maximum")));
        if (!parameter.maximumValue.has_value()) {
            parameter.maximumValue = optionalDoubleValue(object.value(QStringLiteral("max")));
        }
        if (object.contains(QStringLiteral("configurable"))) {
            const QJsonValue configurable = object.value(QStringLiteral("configurable"));
            parameter.configurable = configurable.isBool() ? configurable.toBool()
                : configurable.isDouble() ? configurable.toInt() != 0
                : configurable.toString().trimmed().toLower() != QStringLiteral("false");
        }
        parameter.choices = parameterChoices(object);
        parameters.insert(parameter.name, parameter);
    }
    return parameters;
}

TopologyPresetParameterDescriptor topologyParameterFromJson(const QJsonObject& object) {
    TopologyPresetParameterDescriptor parameter;
    parameter.label = object.value(QStringLiteral("label")).toString().trimmed();
    parameter.defaultValue = object.value(QStringLiteral("default")).toInt();
    parameter.minimumValue = object.value(QStringLiteral("min")).toInt(parameter.defaultValue);
    parameter.maximumValue = object.value(QStringLiteral("max")).toInt(parameter.defaultValue);
    return parameter;
}

QVector<TopologyPresetDescriptor> topologyPresetsFromManifest(
    const QVector<QJsonObject>& topologies) {
    QVector<TopologyPresetDescriptor> presets;
    for (const QJsonObject& object : topologies) {
        TopologyPresetDescriptor preset;
        preset.id = object.value(QStringLiteral("id")).toString().trimmed();
        preset.label = object.value(QStringLiteral("label")).toString().trimmed();
        preset.kind = object.value(QStringLiteral("kind")).toString().trimmed();
        preset.routerModule = object.value(QStringLiteral("module")).toString().trimmed();
        preset.idPattern = object.value(QStringLiteral("id_pattern")).toString().trimmed();

        const QJsonObject ports = object.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            if (it.value().isString()) {
                preset.ports.insert(it.key(), it.value().toString().trimmed());
            }
        }

        const QJsonObject parameters = object.value(QStringLiteral("parameters")).toObject();
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            if (it.value().isObject()) {
                preset.parameters.insert(it.key(), topologyParameterFromJson(it.value().toObject()));
            }
        }

        if (!preset.id.isEmpty() && !preset.kind.isEmpty() && !preset.routerModule.isEmpty()) {
            if (preset.label.isEmpty()) {
                preset.label = preset.id;
            }
            presets.push_back(preset);
        }
    }
    return presets;
}

IpCoreCommandDescriptor compatibilityCommand(const IpcraftCommandDescriptor& command) {
    IpCoreCommandDescriptor descriptor;
    descriptor.command = command.executablePath.trimmed().isEmpty()
        ? command.frameworkTool
        : command.executablePath;
    descriptor.inputFormat = command.inputSchema;
    descriptor.args = command.args;
    return descriptor;
}

QStringList moduleTypesForManifest(const IpcraftPackageManifest& manifest,
                                   const ModuleRegistry* moduleRegistry) {
    QStringList moduleTypes = moduleRegistry
        ? moduleRegistry->availableTypesForIpcore(manifest.id)
        : QStringList{};
    if (!moduleTypes.isEmpty()) {
        return moduleTypes;
    }

    for (const IpcraftModuleDescriptor& module : manifest.modules) {
        if (!module.id.isEmpty()) {
            moduleTypes.append(module.id);
        }
    }
    moduleTypes.sort();
    return moduleTypes;
}

IpCatalogEntry catalogEntryFromDescriptor(const IpCoreRuntimeDescriptor& descriptor,
                                          const ModuleRegistry* moduleRegistry) {
    IpCatalogEntry entry;
    entry.id = descriptor.id;
    entry.packageId = descriptor.id;
    entry.name = descriptor.name;
    entry.version = descriptor.version;
    entry.kind = descriptor.kind;
    entry.instanceLimits = builtInInstanceLimitsForKind(entry.kind);
    entry.runtimeRootPath = descriptor.runtimeRootPath;
    entry.sourceRootPath = descriptor.sourceRootPath;
    entry.modulesPath = descriptor.modulesPath;
    entry.graphicsPath = descriptor.graphicsPath;
    entry.instanceParameters = descriptor.instanceParameters;
    entry.generator = descriptor.generator;
    entry.drc = descriptor.drc;
    entry.topologyPresets = descriptor.topologyPresets;
    if (moduleRegistry) {
        entry.moduleTypes = moduleRegistry->availableTypesForIpcore(descriptor.id);
    }
    return entry;
}

IpCatalogEntry catalogEntryFromManifest(const IpcraftPackageManifest& manifest,
                                        const ModuleRegistry* moduleRegistry) {
    IpCatalogEntry entry;
    entry.id = manifest.id;
    entry.packageId = manifest.id;
    entry.name = manifest.name.isEmpty() ? manifest.id : manifest.name;
    entry.version = manifest.version;
    entry.kind = manifest.extensions.contains(QStringLiteral("noc.v1"))
        ? QStringLiteral("noc")
        : QStringLiteral("ipcraft");
    entry.maxInstances = manifest.instances.max;
    entry.instanceLimits = builtInInstanceLimitsForKind(entry.kind);
    entry.packageManifest = manifest;
    entry.runtimeRootPath = manifest.packageRootPath;
    entry.sourceRootPath = manifest.packageRootPath;
    entry.moduleTypes = moduleTypesForManifest(manifest, moduleRegistry);
    entry.instanceParameters = instanceParametersFromManifest(manifest.parameters);
    entry.topologyPresets = topologyPresetsFromManifest(manifest.topologies);

    if (manifest.commands.contains(QStringLiteral("generate"))) {
        entry.generator = compatibilityCommand(manifest.commands.value(QStringLiteral("generate")));
    }
    if (manifest.commands.contains(QStringLiteral("validate"))) {
        entry.drc = compatibilityCommand(manifest.commands.value(QStringLiteral("validate")));
    }

    return entry;
}

} // namespace

bool IpCatalogEntry::hasModules() const {
    return !modulesPath.isEmpty() || !packageManifest.modules.isEmpty() || !moduleTypes.isEmpty();
}

bool IpCatalogEntry::isSelectable() const {
    return !moduleTypes.isEmpty();
}

IpCatalogService::IpCatalogService(QList<IpCoreRuntimeDescriptor> descriptors,
                                   const ModuleRegistry* moduleRegistry) {
    m_entries.reserve(descriptors.size());
    for (const IpCoreRuntimeDescriptor& descriptor : descriptors) {
        m_entries.append(catalogEntryFromDescriptor(descriptor, moduleRegistry));
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const IpCatalogEntry& left, const IpCatalogEntry& right) {
                  const int labelCompare =
                      QString::compare(catalogSortLabel(left),
                                       catalogSortLabel(right),
                                       Qt::CaseInsensitive);
                  if (labelCompare != 0) {
                      return labelCompare < 0;
                  }
                  return QString::compare(left.id, right.id, Qt::CaseInsensitive) < 0;
              });
}

IpCatalogService::IpCatalogService(QVector<IpcraftPackageManifest> manifests,
                                   const ModuleRegistry* moduleRegistry) {
    m_entries.reserve(manifests.size());
    for (const IpcraftPackageManifest& manifest : manifests) {
        m_entries.append(catalogEntryFromManifest(manifest, moduleRegistry));
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const IpCatalogEntry& left, const IpCatalogEntry& right) {
                  const int labelCompare =
                      QString::compare(catalogSortLabel(left),
                                       catalogSortLabel(right),
                                       Qt::CaseInsensitive);
                  if (labelCompare != 0) {
                      return labelCompare < 0;
                  }
                  return QString::compare(left.id, right.id, Qt::CaseInsensitive) < 0;
              });
}

IpCatalogService IpCatalogService::fromRuntimeRegistries() {
    return IpCatalogService(loadIpcraftPackageManifests(defaultIpcraftPackageRoots()),
                            &ModuleRegistry::instance());
}

const QList<IpCatalogEntry>& IpCatalogService::entries() const {
    return m_entries;
}

QList<IpCatalogEntry> IpCatalogService::selectableEntries() const {
    QList<IpCatalogEntry> selectable;
    for (const IpCatalogEntry& catalogEntry : m_entries) {
        if (catalogEntry.isSelectable()) {
            selectable.append(catalogEntry);
        }
    }
    return selectable;
}

std::optional<IpCatalogEntry> IpCatalogService::entry(const QString& id) const {
    for (const IpCatalogEntry& catalogEntry : m_entries) {
        if (catalogEntry.id == id) {
            return catalogEntry;
        }
    }
    return std::nullopt;
}
