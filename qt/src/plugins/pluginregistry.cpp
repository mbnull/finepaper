// PluginRegistry parses plugin manifests and exposes startup-loaded plugins.
#include "plugins/pluginregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QDebug>
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

bool boolValue(const QJsonValue& value, bool fallbackValue = false) {
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes")) {
            return true;
        }
        if (text == QStringLiteral("false") || text == QStringLiteral("0") || text == QStringLiteral("no")) {
            return false;
        }
    }
    return fallbackValue;
}

std::optional<PluginDescriptor> loadManifest(const QString& pluginDirectory) {
    const QFileInfo manifestInfo(QDir(pluginDirectory).filePath(QStringLiteral("plugin.json")));
    if (!manifestInfo.isFile()) {
        return std::nullopt;
    }

    QFile file(manifestInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open plugin manifest" << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid plugin manifest" << manifestInfo.absoluteFilePath() << parseError.errorString();
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    PluginDescriptor descriptor;
    descriptor.id = object.value(QStringLiteral("id")).toString().trimmed();
    descriptor.name = object.value(QStringLiteral("name")).toString().trimmed();
    descriptor.version = object.value(QStringLiteral("version")).toString().trimmed();
    descriptor.rootPath = QFileInfo(pluginDirectory).absoluteFilePath();
    descriptor.modulesPath = resolvePath(descriptor.rootPath, object.value(QStringLiteral("modules")).toString());
    descriptor.graphicsPath = resolvePath(descriptor.rootPath, object.value(QStringLiteral("graphics")).toString());
    descriptor.topologyPresets = topologyPresetsFromJson(object.value(QStringLiteral("topology_presets")));

    const QJsonObject generator = object.value(QStringLiteral("generator")).toObject();
    descriptor.generator.command = generator.value(QStringLiteral("command")).toString().trimmed();
    descriptor.generator.inputFormat =
        generator.value(QStringLiteral("input_format")).toString(QStringLiteral("legacy_noc_json")).trimmed();
    if (descriptor.generator.inputFormat.isEmpty()) {
        descriptor.generator.inputFormat = QStringLiteral("legacy_noc_json");
    }
    descriptor.generator.args = stringArray(generator.value(QStringLiteral("args")));

    const QJsonObject native = object.value(QStringLiteral("native")).toObject();
    descriptor.native.enabled = boolValue(native.value(QStringLiteral("enabled")), false);
    descriptor.native.library = native.value(QStringLiteral("library")).toString().trimmed();

    if (descriptor.id.isEmpty()) {
        qWarning() << "Skipping plugin manifest without id" << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }

    if (descriptor.name.isEmpty()) {
        descriptor.name = descriptor.id;
    }

    return descriptor;
}

void appendPluginFromDirectory(QList<PluginDescriptor>& plugins,
                               QSet<QString>& seenIds,
                               const QString& pluginDirectory) {
    const auto descriptor = loadManifest(pluginDirectory);
    if (!descriptor.has_value()) {
        return;
    }

    if (seenIds.contains(descriptor->id)) {
        qWarning() << "Skipping duplicate plugin id" << descriptor->id << "from" << pluginDirectory;
        return;
    }

    seenIds.insert(descriptor->id);
    plugins.append(*descriptor);
}

void appendLocalPluginRootsFrom(QStringList& roots, const QString& startPath) {
    if (startPath.isEmpty()) {
        return;
    }

    QDir dir(startPath);
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral("plugins"));
        if (QFileInfo(candidate).isDir()) {
            appendUniquePath(roots, candidate);
        }

        if (!dir.cdUp()) {
            break;
        }
    }
}

} // namespace

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry registry;
    return registry;
}

PluginRegistry::PluginRegistry()
    : m_plugins(discover(defaultPluginRoots())) {}

QList<PluginDescriptor> PluginRegistry::discover(const QStringList& roots) {
    QList<PluginDescriptor> plugins;
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

        appendPluginFromDirectory(plugins, seenIds, absoluteRootPath);

        const QDir rootDir(absoluteRootPath);
        const QFileInfoList pluginDirectories =
            rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& pluginDirectory : pluginDirectories) {
            appendPluginFromDirectory(plugins, seenIds, pluginDirectory.absoluteFilePath());
        }
    }

    return plugins;
}

QStringList PluginRegistry::defaultPluginRoots() {
    QStringList roots;

    const QString envPath = qEnvironmentVariable("FINEPAPER_PLUGIN_PATH");
    for (const QString& path : envPath.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        appendUniquePath(roots, path);
    }

    appendLocalPluginRootsFrom(roots, QDir::currentPath());
    appendLocalPluginRootsFrom(roots, QCoreApplication::applicationDirPath());
    return roots;
}

const QList<PluginDescriptor>& PluginRegistry::plugins() const {
    return m_plugins;
}

const PluginDescriptor* PluginRegistry::plugin(const QString& pluginId) const {
    for (const PluginDescriptor& descriptor : m_plugins) {
        if (descriptor.id == pluginId) {
            return &descriptor;
        }
    }
    return nullptr;
}
