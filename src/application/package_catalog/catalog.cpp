#include "application/package_catalog/catalog.h"

#include "package/package_root_identity.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

struct DiscoveredPackage {
    PackageDefinition package;
    QString canonicalPath;
};

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path = {}) {
    diagnostics.append(Diagnostic{severity, code, message, path});
}

QStringList packagePathsInRoot(const QString& rootPath) {
    if (QFileInfo(QDir(rootPath).filePath(QStringLiteral("package.json"))).isFile()) {
        return QStringList{rootPath};
    }

    QStringList packagePaths;
    const QFileInfoList children = QDir(rootPath).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QFileInfo& child : children) {
        if (QFileInfo(QDir(child.absoluteFilePath())
                          .filePath(QStringLiteral("package.json"))).isFile()) {
            packagePaths.append(child.absoluteFilePath());
        }
    }
    return packagePaths;
}

} // namespace

PackageCatalogDiscoveryResult discoverPackageCatalog(const QStringList& roots) {
    PackageCatalogDiscoveryResult result;
    QVector<DiscoveredPackage> discovered;
    QSet<QString> discoveredPackagePaths;
    bool foundUsableRoot = false;

    for (const QString& rootValue : roots) {
        const QString rootPath = QDir::cleanPath(
            QFileInfo(rootValue).absoluteFilePath());
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir() || !rootInfo.isReadable()) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("warning"),
                QStringLiteral("package.root_missing"),
                QStringLiteral("package root is unavailable and was skipped"),
                rootPath);
            continue;
        }
        foundUsableRoot = true;

        for (const QString& packagePath : packagePathsInRoot(rootPath)) {
            const QString canonicalPath =
                normalizedPackageRootPath(packagePath);
            if (discoveredPackagePaths.contains(canonicalPath)) {
                continue;
            }
            discoveredPackagePaths.insert(canonicalPath);
            ++result.candidateCount;

            PackageLoadResult loadResult = loadPackage(canonicalPath);
            result.diagnostics += loadResult.diagnostics;
            if (!loadResult.success || !loadResult.package) {
                continue;
            }
            discovered.append(
                DiscoveredPackage{std::move(*loadResult.package), canonicalPath});
        }
    }

    // An empty roots list remains the explicit way to clear a catalog. A
    // non-empty list with no usable directory cannot establish a replacement.
    if (!roots.isEmpty() && !foundUsableRoot) {
        result.catalogFatal = true;
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.no_usable_roots"),
            QStringLiteral("none of the configured Package roots are available"));
        return result;
    }

    QHash<QString, QVector<qsizetype>> candidatesByKey;
    for (qsizetype index = 0; index < discovered.size(); ++index) {
        candidatesByKey[discovered.at(index).package.key()].append(index);
    }

    QStringList keys = candidatesByKey.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        const auto candidatesIt = candidatesByKey.constFind(key);
        const QVector<qsizetype>& candidateIndexes = candidatesIt.value();
        if (candidateIndexes.size() == 1) {
            const qsizetype index = candidateIndexes.front();
            result.packages.append(std::move(discovered[index].package));
            continue;
        }

        QStringList conflictingPaths;
        conflictingPaths.reserve(candidateIndexes.size());
        for (const qsizetype index : candidateIndexes) {
            conflictingPaths.append(discovered.at(index).canonicalPath);
        }
        std::sort(conflictingPaths.begin(), conflictingPaths.end());
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.duplicate_conflict"),
            QStringLiteral("Package %1 was isolated because multiple candidates provide "
                           "the same id and version: %2")
                .arg(key, conflictingPaths.join(QStringLiteral("; "))),
            conflictingPaths.front());
    }

    result.rejectedCount = result.candidateCount - result.packages.size();
    return result;
}

PackageCatalogReloadResult PackageCatalog::reload(const QStringList& roots) {
    PackageCatalogDiscoveryResult discovery = discoverPackageCatalog(roots);
    PackageCatalogReloadResult result;
    result.acceptedCount = discovery.packages.size();
    result.rejectedCount = discovery.rejectedCount;
    result.diagnostics = std::move(discovery.diagnostics);
    if (discovery.catalogFatal) {
        result.disposition = PackageCatalogReloadDisposition::CatalogFatal;
        return result;
    }
    if (discovery.candidateCount > 0 && discovery.packages.isEmpty()) {
        result.disposition = PackageCatalogReloadDisposition::Rejected;
        return result;
    }

    m_packages = std::move(discovery.packages);
    result.disposition = PackageCatalogReloadDisposition::Committed;
    return result;
}

const QVector<PackageDefinition>& PackageCatalog::packages() const {
    return m_packages;
}

std::optional<PackageDefinition> PackageCatalog::resolve(
    const PackageReference& reference) const {
    const auto it = std::find_if(
        m_packages.cbegin(), m_packages.cend(),
        [&](const PackageDefinition& package) {
            return package.id == reference.id
                && package.version == reference.version;
        });
    if (it == m_packages.cend()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace finepaper
