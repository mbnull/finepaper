// IpCoreRuntimeDiagnostics formats loaded IP core runtime and IP metadata.
#include "ipcore/ipcoreruntimediagnostics.h"

#include <QFileInfo>

namespace {

QString emptyFallback(const QString& value, const QString& fallback = QStringLiteral("(none)")) {
    return value.trimmed().isEmpty() ? fallback : value;
}

QString runtimeVersionText(const IpCoreRuntimeDescriptor& runtime) {
    return runtime.version.trimmed().isEmpty() ? QString() : QStringLiteral(" v%1").arg(runtime.version);
}

QString catalogVersionText(const IpCatalogEntry& entry) {
    return entry.version.trimmed().isEmpty() ? QString() : QStringLiteral(" v%1").arg(entry.version);
}

QString moduleBundleFormat(const QString& modulesPath) {
    const QString suffix = QFileInfo(modulesPath).suffix().toLower();
    if (suffix == QStringLiteral("json")) {
        return QStringLiteral("json(unsupported)");
    }
    if (suffix == QStringLiteral("xml")) {
        return QStringLiteral("xml");
    }
    return modulesPath.trimmed().isEmpty() ? QStringLiteral("(none)") : suffix;
}

} // namespace

namespace IpCoreRuntimeDiagnostics {

QStringList runtimeLogLines(const QList<IpCoreRuntimeDescriptor>& runtimes) {
    if (runtimes.isEmpty()) {
        return {QStringLiteral("[Startup] No IP core runtimes loaded.")};
    }

    QStringList lines;
    for (const IpCoreRuntimeDescriptor& runtime : runtimes) {
        const QString generator = runtime.generator.hasCommand()
            ? runtime.generator.command
            : QStringLiteral("(none)");
        lines.append(QStringLiteral("[Startup] IP core runtime %1: %2%3 bundle=%4 modules=%5 graphics=%6 generator=%7")
                         .arg(runtime.id,
                              emptyFallback(runtime.name, runtime.id),
                              runtimeVersionText(runtime),
                              moduleBundleFormat(runtime.modulesPath),
                              emptyFallback(runtime.modulesPath),
                              emptyFallback(runtime.graphicsPath),
                              generator));
    }
    return lines;
}

QStringList catalogLogLines(const QList<IpCatalogEntry>& entries) {
    if (entries.isEmpty()) {
        return {QStringLiteral("[Startup] No IP core packages loaded.")};
    }

    QStringList lines;
    for (const IpCatalogEntry& entry : entries) {
        const QString generator = entry.generator.hasCommand()
            ? entry.generator.command
            : QStringLiteral("(none)");
        lines.append(QStringLiteral("[Startup] IP core package %1: %2%3 root=%4 modules=%5 generator=%6")
                         .arg(entry.id,
                              emptyFallback(entry.name, entry.id),
                              catalogVersionText(entry),
                              emptyFallback(entry.runtimeRootPath),
                              QString::number(entry.moduleTypes.size()),
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

        lines.append(QStringLiteral("[Startup] IP %1: ipcore=%2 group=%3 ports=%4 parameters=%5 interfaces=%6")
                         .arg(type->name,
                              emptyFallback(type->ipcoreId),
                              emptyFallback(type->graphGroup),
                              QString::number(static_cast<int>(type->defaultPorts.size())),
                              QString::number(type->defaultParameters.size()),
                              QString::number(type->interfaceMetadata.size())));
    }
    return lines;
}

QStringList logLines(const QList<IpCoreRuntimeDescriptor>& runtimes, const ModuleRegistry& registry) {
    QStringList lines = runtimeLogLines(runtimes);
    lines.append(ipLogLines(registry));
    return lines;
}

QStringList logLines(const QList<IpCatalogEntry>& entries, const ModuleRegistry& registry) {
    QStringList lines = catalogLogLines(entries);
    lines.append(ipLogLines(registry));
    return lines;
}

} // namespace IpCoreRuntimeDiagnostics
