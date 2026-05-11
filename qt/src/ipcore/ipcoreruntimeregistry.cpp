// IpCoreRuntimeRegistry parses IP core runtime manifests discovered at startup.
#include "ipcore/ipcoreruntimeregistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <optional>

namespace {

void appendUniquePath(QStringList& paths, const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    const QString absolutePath = info.absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

QString resolvePath(const QString& rootPath, const QString& path) {
    if (path.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }

    return QFileInfo(QDir(rootPath).filePath(path)).absoluteFilePath();
}

QStringList stringArray(const QJsonValue& value) {
    QStringList strings;
    if (!value.isArray()) {
        return strings;
    }

    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            strings.append(item.toString());
        }
    }
    return strings;
}

TopologyPresetParameterDescriptor topologyParameter(const QJsonObject& object) {
    TopologyPresetParameterDescriptor parameter;
    parameter.label = object.value(QStringLiteral("label")).toString();
    parameter.defaultValue = object.value(QStringLiteral("default")).toInt();
    parameter.minimumValue = object.value(QStringLiteral("min")).toInt(parameter.defaultValue);
    parameter.maximumValue = object.value(QStringLiteral("max")).toInt(parameter.defaultValue);
    return parameter;
}

std::optional<double> optionalDoubleValue(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    return value.toDouble();
}

Parameter::Value parameterDefaultValue(const QString& type, const QJsonValue& value) {
    if (type == QStringLiteral("int")) {
        return value.toInt();
    }
    if (type == QStringLiteral("bool")) {
        if (value.isBool()) {
            return value.toBool();
        }
        if (value.isDouble()) {
            return value.toInt() != 0;
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed().toLower();
            return text == QStringLiteral("true") ||
                   text == QStringLiteral("1") ||
                   text == QStringLiteral("yes");
        }
        return false;
    }
    return value.toString();
}

QVector<IpCoreInstanceParameterChoice> instanceParameterChoices(const QJsonObject& object) {
    QVector<IpCoreInstanceParameterChoice> choices;
    const QJsonArray enumValues = object.value(QStringLiteral("enum")).toArray();
    const QJsonObject labels = object.value(QStringLiteral("labels")).toObject();
    for (const QJsonValue& value : enumValues) {
        if (!value.isString()) {
            continue;
        }
        const QString choiceValue = value.toString();
        choices.push_back(IpCoreInstanceParameterChoice{
            choiceValue,
            labels.value(choiceValue).toString(choiceValue)
        });
    }
    return choices;
}

QHash<QString, IpCoreInstanceParameterDescriptor> instanceParametersFromJson(const QJsonValue& value) {
    QHash<QString, IpCoreInstanceParameterDescriptor> parameters;
    if (!value.isObject()) {
        return parameters;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonObject parameterObject = it.value().toObject();
        IpCoreInstanceParameterDescriptor parameter;
        parameter.name = it.key();
        parameter.type = parameterObject.value(QStringLiteral("type")).toString().trimmed();
        parameter.defaultValue = parameterDefaultValue(
            parameter.type,
            parameterObject.value(QStringLiteral("default")));
        parameter.label = parameterObject.value(QStringLiteral("label")).toString();
        parameter.description = parameterObject.value(QStringLiteral("description")).toString();
        parameter.minimumValue = optionalDoubleValue(parameterObject.value(QStringLiteral("min")));
        parameter.maximumValue = optionalDoubleValue(parameterObject.value(QStringLiteral("max")));
        parameter.configurable = true;
        if (parameterObject.contains(QStringLiteral("configurable"))) {
            const QJsonValue configurable = parameterObject.value(QStringLiteral("configurable"));
            if (configurable.isBool()) {
                parameter.configurable = configurable.toBool();
            } else if (configurable.isDouble()) {
                parameter.configurable = configurable.toInt() != 0;
            } else if (configurable.isString()) {
                const QString text = configurable.toString().trimmed().toLower();
                parameter.configurable = text == QStringLiteral("true") ||
                                         text == QStringLiteral("1") ||
                                         text == QStringLiteral("yes");
            }
        }
        parameter.choices = instanceParameterChoices(parameterObject);
        parameters.insert(parameter.name, parameter);
    }
    return parameters;
}

QVector<TopologyPresetDescriptor> topologyPresetsFromJson(const QJsonValue& value) {
    QVector<TopologyPresetDescriptor> presets;
    if (!value.isArray()) {
        return presets;
    }

    for (const QJsonValue& item : value.toArray()) {
        const QJsonObject object = item.toObject();
        TopologyPresetDescriptor preset;
        preset.id = object.value(QStringLiteral("id")).toString().trimmed();
        preset.label = object.value(QStringLiteral("label")).toString().trimmed();
        preset.kind = object.value(QStringLiteral("kind")).toString().trimmed();
        preset.routerModule = object.value(QStringLiteral("router_module")).toString().trimmed();
        preset.idPattern = object.value(QStringLiteral("id_pattern")).toString().trimmed();

        const QJsonObject ports = object.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            if (it.value().isString()) {
                preset.ports.insert(it.key(), it.value().toString());
            }
        }

        const QJsonObject parameters = object.value(QStringLiteral("parameters")).toObject();
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            preset.parameters.insert(it.key(), topologyParameter(it.value().toObject()));
        }

        if (!preset.id.isEmpty() && !preset.kind.isEmpty() && !preset.routerModule.isEmpty()) {
            if (preset.label.isEmpty()) {
                preset.label = preset.id;
            }
            presets.append(preset);
        }
    }
    return presets;
}

IpCoreCommandDescriptor commandFromJson(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    IpCoreCommandDescriptor command;
    command.command = object.value(QStringLiteral("command")).toString().trimmed();
    command.inputFormat =
        object.value(QStringLiteral("input_format")).toString(QStringLiteral("ipcore_graph_v1")).trimmed();
    if (command.inputFormat.isEmpty()) {
        command.inputFormat = QStringLiteral("ipcore_graph_v1");
    }
    command.args = stringArray(object.value(QStringLiteral("args")));
    return command;
}

std::optional<IpCoreRuntimeDescriptor> loadManifest(const QString& runtimeDirectory) {
    const QFileInfo manifestInfo(QDir(runtimeDirectory).filePath(QStringLiteral("ipcore-runtime.json")));
    if (!manifestInfo.isFile()) {
        return std::nullopt;
    }

    QFile file(manifestInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open IP core runtime manifest" << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid IP core runtime manifest"
                   << manifestInfo.absoluteFilePath()
                   << parseError.errorString();
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = object.value(QStringLiteral("id")).toString().trimmed();
    descriptor.name = object.value(QStringLiteral("name")).toString().trimmed();
    descriptor.version = object.value(QStringLiteral("version")).toString().trimmed();
    descriptor.kind = object.value(QStringLiteral("kind")).toString().trimmed();
    descriptor.runtimeRootPath = QFileInfo(runtimeDirectory).absoluteFilePath();
    const QString sourceRoot = object.value(QStringLiteral("source_root")).toString().trimmed();
    if (sourceRoot.isEmpty()) {
        qWarning() << "Skipping IP core runtime manifest without source_root"
                   << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }
    descriptor.sourceRootPath = resolvePath(descriptor.runtimeRootPath, sourceRoot);
    if (!QFileInfo(descriptor.sourceRootPath).isDir()) {
        qWarning() << "Skipping IP core runtime manifest with missing source_root directory"
                   << manifestInfo.absoluteFilePath()
                   << descriptor.sourceRootPath;
        return std::nullopt;
    }

    const QString modules = object.value(QStringLiteral("modules")).toString().trimmed();
    if (modules.isEmpty()) {
        qWarning() << "Skipping IP core runtime manifest without modules"
                   << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }
    descriptor.modulesPath = resolvePath(descriptor.runtimeRootPath, modules);
    if (!QFileInfo(descriptor.modulesPath).isFile()) {
        qWarning() << "Skipping IP core runtime manifest with missing modules file"
                   << manifestInfo.absoluteFilePath()
                   << descriptor.modulesPath;
        return std::nullopt;
    }

    const QString graphics = object.value(QStringLiteral("graphics")).toString().trimmed();
    descriptor.graphicsPath = resolvePath(descriptor.runtimeRootPath, graphics);
    if (!graphics.isEmpty() && !QFileInfo(descriptor.graphicsPath).isDir()) {
        qWarning() << "Ignoring missing graphics directory for IP core runtime"
                   << descriptor.id
                   << descriptor.graphicsPath;
        descriptor.graphicsPath.clear();
    }

    descriptor.instanceParameters = instanceParametersFromJson(object.value(QStringLiteral("instance_parameters")));
    descriptor.topologyPresets = topologyPresetsFromJson(object.value(QStringLiteral("topology_presets")));
    descriptor.generator = commandFromJson(object.value(QStringLiteral("generator")));
    descriptor.drc = commandFromJson(object.value(QStringLiteral("drc")));

    if (descriptor.id.isEmpty()) {
        qWarning() << "Skipping IP core runtime manifest without id" << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }

    if (descriptor.name.isEmpty()) {
        descriptor.name = descriptor.id;
    }

    return descriptor;
}

void appendRuntimeFromDirectory(QList<IpCoreRuntimeDescriptor>& runtimes,
                                QSet<QString>& seenIds,
                                const QString& runtimeDirectory) {
    const auto descriptor = loadManifest(runtimeDirectory);
    if (!descriptor.has_value()) {
        return;
    }

    if (seenIds.contains(descriptor->id)) {
        qWarning() << "Skipping duplicate IP core runtime id" << descriptor->id << "from" << runtimeDirectory;
        return;
    }

    seenIds.insert(descriptor->id);
    runtimes.append(*descriptor);
}

void appendLocalRuntimeRootsFrom(QStringList& roots, const QString& startPath) {
    if (startPath.isEmpty()) {
        return;
    }

    QDir dir(startPath);
    while (true) {
        const QString generatedIpcores = dir.filePath(QStringLiteral("generated/ipcores"));
        if (QFileInfo(generatedIpcores).isDir()) {
            appendUniquePath(roots, generatedIpcores);
        }

        if (!dir.cdUp()) {
            break;
        }
    }
}

} // namespace

IpCoreRuntimeRegistry& IpCoreRuntimeRegistry::instance() {
    static IpCoreRuntimeRegistry registry;
    return registry;
}

IpCoreRuntimeRegistry::IpCoreRuntimeRegistry()
    : m_runtimes(discover(defaultRuntimeRoots())) {}

QList<IpCoreRuntimeDescriptor> IpCoreRuntimeRegistry::discover(const QStringList& roots) {
    QList<IpCoreRuntimeDescriptor> runtimes;
    QSet<QString> seenIds;
    QSet<QString> seenDirectories;

    for (const QString& rootPath : roots) {
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            continue;
        }

        const QString absoluteRootPath = rootInfo.absoluteFilePath();
        if (seenDirectories.contains(absoluteRootPath)) {
            continue;
        }
        seenDirectories.insert(absoluteRootPath);

        appendRuntimeFromDirectory(runtimes, seenIds, absoluteRootPath);

        const QDir rootDir(absoluteRootPath);
        const QFileInfoList runtimeDirectories =
            rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& runtimeDirectory : runtimeDirectories) {
            appendRuntimeFromDirectory(runtimes, seenIds, runtimeDirectory.absoluteFilePath());
        }
    }

    return runtimes;
}

QStringList IpCoreRuntimeRegistry::defaultRuntimeRoots() {
    QStringList roots;

    const QString envPath = qEnvironmentVariable("FINEPAPER_IPCORE_PATH");
    for (const QString& path : envPath.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        appendUniquePath(roots, path);
    }

    appendLocalRuntimeRootsFrom(roots, QDir::currentPath());
    appendLocalRuntimeRootsFrom(roots, QCoreApplication::applicationDirPath());
    return roots;
}

const QList<IpCoreRuntimeDescriptor>& IpCoreRuntimeRegistry::runtimes() const {
    return m_runtimes;
}

const IpCoreRuntimeDescriptor* IpCoreRuntimeRegistry::runtime(const QString& ipcoreId) const {
    for (const IpCoreRuntimeDescriptor& descriptor : m_runtimes) {
        if (descriptor.id == ipcoreId) {
            return &descriptor;
        }
    }
    return nullptr;
}
