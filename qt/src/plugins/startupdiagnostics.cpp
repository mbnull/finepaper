// StartupDiagnostics formats loaded plugin and IP metadata for UI/console logs.
#include "plugins/startupdiagnostics.h"

#include <QFileInfo>

namespace {

QString emptyFallback(const QString& value, const QString& fallback = QStringLiteral("(none)")) {
    return value.trimmed().isEmpty() ? fallback : value;
}

QString pluginVersionText(const PluginDescriptor& plugin) {
    return plugin.version.trimmed().isEmpty() ? QString() : QStringLiteral(" v%1").arg(plugin.version);
}

QString moduleBundleFormat(const QString& modulesPath) {
    const QString suffix = QFileInfo(modulesPath).suffix().toLower();
    if (suffix == QStringLiteral("json")) {
        return QStringLiteral("json(deprecated)");
    }
    if (suffix == QStringLiteral("xml")) {
        return QStringLiteral("xml");
    }
    return modulesPath.trimmed().isEmpty() ? QStringLiteral("(none)") : suffix;
}

} // namespace

namespace StartupDiagnostics {

QStringList pluginLogLines(const QList<PluginDescriptor>& plugins) {
    if (plugins.isEmpty()) {
        return {QStringLiteral("[Startup] No plugins loaded.")};
    }

    QStringList lines;
    for (const PluginDescriptor& plugin : plugins) {
        const QString generator = plugin.generator.hasGenerator()
            ? plugin.generator.command
            : QStringLiteral("(none)");
        lines.append(QStringLiteral("[Startup] Plugin %1: %2%3 bundle=%4 modules=%5 graphics=%6 generator=%7")
                         .arg(plugin.id,
                              emptyFallback(plugin.name, plugin.id),
                              pluginVersionText(plugin),
                              moduleBundleFormat(plugin.modulesPath),
                              emptyFallback(plugin.modulesPath),
                              emptyFallback(plugin.graphicsPath),
                              generator));
    }
    return lines;
}

QStringList ipLogLines(const ModuleRegistry& registry) {
    const QStringList typeNames = registry.availableTypes();
    if (typeNames.isEmpty()) {
        return {QStringLiteral("[Startup] No IP types loaded.")};
    }

    QStringList lines;
    for (const QString& typeName : typeNames) {
        const ModuleType* type = registry.getType(typeName);
        if (!type) {
            continue;
        }

        lines.append(QStringLiteral("[Startup] IP %1: plugin=%2 group=%3 ports=%4 parameters=%5 interfaces=%6")
                         .arg(type->name,
                              emptyFallback(type->pluginId),
                              emptyFallback(type->graphGroup),
                              QString::number(static_cast<int>(type->defaultPorts.size())),
                              QString::number(type->defaultParameters.size()),
                              QString::number(type->interfaceMetadata.size())));
    }
    return lines;
}

QStringList logLines(const QList<PluginDescriptor>& plugins, const ModuleRegistry& registry) {
    QStringList lines = pluginLogLines(plugins);
    lines.append(ipLogLines(registry));
    return lines;
}

} // namespace StartupDiagnostics
