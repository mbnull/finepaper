#include "application/runtime_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace finepaper {
namespace {

QString normalizedPath(const QDir& workingDirectory, const QString& path) {
    const QString absolutePath = QDir::cleanPath(
        workingDirectory.absoluteFilePath(path));
    const QString canonicalPath = QFileInfo(absolutePath).canonicalFilePath();
    return canonicalPath.isEmpty() ? absolutePath : canonicalPath;
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
                                         const QStringList& configuredPackageRoots,
                                         const QString& workingDirectory) {
    const QString basePath = workingDirectory.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(workingDirectory).absoluteFilePath();
    const QDir baseDirectory(basePath);
    RuntimeLocations locations;
    const QString environmentRoots = qEnvironmentVariable("FINEPAPER_PACKAGE_PATH");
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const QDir applicationRoot(applicationDirectory);
    const QStringList defaultRoots{
        applicationRoot.filePath(QStringLiteral("packages")),
        applicationRoot.filePath(QStringLiteral("../share/finepaper/packages")),
        baseDirectory.filePath(QStringLiteral("packages")),
    };
    locations.packageRoots = normalizedPaths(
        explicitPackageRoots + configuredPackageRoots
            + environmentRoots.split(QDir::listSeparator(), Qt::SkipEmptyParts)
            + defaultRoots,
        baseDirectory);
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
