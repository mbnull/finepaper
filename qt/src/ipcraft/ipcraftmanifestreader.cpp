#include "ipcraft/ipcraftmanifestreader.h"

#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"

#include <QDir>
#include <QFileInfo>

namespace {

constexpr auto kPackageFileName = "ipcraft.json";

QString locationPath(const ipcraft::Diagnostic& diagnostic) {
    if (diagnostic.locations.isEmpty()) {
        return {};
    }

    const ipcraft::DiagnosticLocation& location = diagnostic.locations.first();
    if (!location.path.isEmpty()) {
        return location.path;
    }
    if (!location.file.isEmpty()) {
        return location.file;
    }
    if (!location.instanceId.isEmpty()) {
        return location.instanceId;
    }
    return location.kind;
}

IpcraftDiagnostic toLegacyDiagnostic(const ipcraft::Diagnostic& diagnostic,
                                     const QString& packageRootPath) {
    IpcraftDiagnostic converted;
    converted.severity = diagnostic.severity;
    converted.source = diagnostic.source;
    converted.ruleId = diagnostic.ruleId;
    converted.category = diagnostic.category;
    converted.packageRootPath = packageRootPath;
    converted.path = locationPath(diagnostic);
    converted.message = diagnostic.message;
    return converted;
}

QVector<IpcraftDiagnostic> toLegacyDiagnostics(const ipcraft::DiagnosticStore& diagnostics,
                                               const QString& packageRootPath) {
    QVector<IpcraftDiagnostic> converted;
    converted.reserve(diagnostics.records.size());
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        converted.append(toLegacyDiagnostic(diagnostic, packageRootPath));
    }
    return converted;
}

QString packageLocalAbsolutePath(const QString& packageRootPath, const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QFileInfo(QDir(packageRootPath).filePath(trimmed)).absoluteFilePath();
}

std::optional<IpcraftDynamicPluginMetadata>
pluginFromPackageSpec(const ipcraft::PackageSpec& spec) {
    if (!spec.hasPlugin) {
        return std::nullopt;
    }

    IpcraftDynamicPluginMetadata plugin;
    plugin.id = spec.plugin.value(QStringLiteral("id")).toString();
    plugin.libraryPath = spec.plugin.value(QStringLiteral("library")).toString();
    if (plugin.libraryPath.isEmpty()) {
        plugin.libraryPath = spec.plugin.value(QStringLiteral("library_path")).toString();
    }
    plugin.resolvedLibraryPath = packageLocalAbsolutePath(spec.packageRootPath,
                                                          plugin.libraryPath);
    plugin.entrypoint = spec.plugin.value(QStringLiteral("entrypoint")).toString();
    if (plugin.entrypoint.isEmpty()) {
        plugin.entrypoint = spec.plugin.value(QStringLiteral("entry")).toString();
    }
    return plugin;
}

IpcraftPackageManifest manifestFromPackageSpec(const ipcraft::PackageSpec& spec) {
    IpcraftPackageManifest manifest;
    manifest.schema = ipcraft::schemaids::packageV1;
    manifest.id = spec.id;
    manifest.name = spec.name;
    manifest.version = spec.version;
    manifest.packageRootPath = spec.packageRootPath;
    manifest.plugin = pluginFromPackageSpec(spec);

    for (const QString& extensionId : spec.extensions) {
        IpcraftExtensionDescriptor extension;
        extension.id = extensionId;
        extension.enabled = true;
        manifest.extensions.insert(extensionId, extension);
    }

    return manifest;
}

} // namespace

IpcraftManifestReadResult
IpcraftManifestReader::readPackage(const QString& packageRootPath) const {
    return readManifestFile(QDir(packageRootPath).filePath(QString::fromLatin1(kPackageFileName)));
}

IpcraftManifestReadResult
IpcraftManifestReader::readManifestFile(const QString& manifestPath) const {
    IpcraftManifestReadResult result;
    const ipcraft::PackageSpecReadResult packageResult =
        ipcraft::PackageSpecReader().readSpecFile(manifestPath);

    result.manifest = manifestFromPackageSpec(packageResult.spec);
    result.diagnostics = toLegacyDiagnostics(packageResult.diagnostics,
                                             packageResult.spec.packageRootPath);
    result.ok = packageResult.ok;
    return result;
}
