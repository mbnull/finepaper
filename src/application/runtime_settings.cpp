#include "application/runtime_settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace finepaper {
namespace {

QString normalizedPath(const QDir& workingDirectory, const QString& path) {
    return QDir::cleanPath(workingDirectory.absoluteFilePath(path));
}

QStringList normalizedPaths(const QStringList& paths, const QDir& workingDirectory) {
    QStringList result;
    QSet<QString> seen;
    for (const QString& path : paths) {
        if (path.trimmed().isEmpty()) {
            continue;
        }
        const QString normalized = normalizedPath(workingDirectory, path);
        if (!seen.contains(normalized)) {
            seen.insert(normalized);
            result.append(normalized);
        }
    }
    return result;
}

} // namespace

RuntimeLocations resolveRuntimeLocations(const QStringList& explicitPackageRoots,
                                         const QString& workingDirectory) {
    const QString basePath = workingDirectory.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(workingDirectory).absoluteFilePath();
    const QDir baseDirectory(basePath);
    RuntimeLocations locations;
    locations.packageRoots = normalizedPaths(explicitPackageRoots, baseDirectory);
    if (locations.packageRoots.isEmpty()) {
        const QString environmentRoots = qEnvironmentVariable("FINEPAPER_PACKAGE_PATH");
        locations.packageRoots = normalizedPaths(
            environmentRoots.split(QDir::listSeparator(), Qt::SkipEmptyParts), baseDirectory);
    }
    if (locations.packageRoots.isEmpty()) {
        locations.packageRoots.append(baseDirectory.filePath(QStringLiteral("packages")));
    }
    locations.defaultOutputRoot = baseDirectory.filePath(QStringLiteral("output"));
    return locations;
}

void appendPackageRoots(RuntimeLocations& locations,
                        const QStringList& additionalPackageRoots,
                        const QString& workingDirectory) {
    const QString basePath = workingDirectory.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(workingDirectory).absoluteFilePath();
    const QDir baseDirectory(basePath);
    locations.packageRoots = normalizedPaths(
        locations.packageRoots + additionalPackageRoots, baseDirectory);
}

} // namespace finepaper
