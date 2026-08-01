#include "application/application.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics,
                       const QString& code) {
    return std::any_of(
        diagnostics.cbegin(), diagnostics.cend(),
        [&](const Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

bool writeInvalidManifest(const QString& packageRoot) {
    if (!QDir().mkpath(packageRoot)) {
        return false;
    }
    QFile manifest(QDir(packageRoot).filePath(QStringLiteral("package.json")));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return manifest.write(QJsonDocument(QJsonObject{}).toJson()) >= 0;
}

bool copyDirectory(const QString& sourcePath, const QString& destinationPath) {
    if (!QDir().mkpath(destinationPath)) {
        return false;
    }
    const QDir source(sourcePath);
    QDirIterator iterator(
        sourcePath,
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceEntry = iterator.next();
        const QFileInfo sourceInfo(sourceEntry);
        const QString destinationEntry = QDir(destinationPath).filePath(
            source.relativeFilePath(sourceEntry));
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(destinationEntry)) {
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(destinationEntry).absolutePath())
            || !QFile::copy(sourceEntry, destinationEntry)
            || !QFile::setPermissions(destinationEntry, sourceInfo.permissions())) {
            return false;
        }
    }
    return true;
}

QStringList packageKeys(const FinepaperApplication& application) {
    QStringList keys;
    for (const PackageDefinition& package : application.packages()) {
        keys.append(package.key());
    }
    return keys;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    const QString bundledRoot = QDir(projectRoot).filePath(
        QStringLiteral("packages"));
    const QString bundledV1 = QDir(bundledRoot).filePath(
        QStringLiteral("finepaper-noc"));

    QTemporaryDir invalidFixture(
        QStringLiteral("/tmp/finepaper-invalid-catalog-XXXXXX"));
    const QString invalidPackage = QDir(invalidFixture.path()).filePath(
        QStringLiteral("invalid-package"));
    check(invalidFixture.isValid() && writeInvalidManifest(invalidPackage),
          QStringLiteral("invalid Package candidate fixture is available"));

    FinepaperApplication catalog;
    const PackageCatalogReloadResult partial = catalog.reloadPackages(
        QStringList{bundledRoot, invalidFixture.path()});
    check(partial.committed()
              && !partial.catalogFatal()
              && partial.acceptedCount == 2
              && partial.rejectedCount == 1
              && hasErrors(partial.diagnostics)
              && catalog.packages().size() == 2,
          QStringLiteral("a bad Package is isolated while valid Packages commit"));

    const QStringList retainedKeys = packageKeys(catalog);
    const PackageCatalogReloadResult allRejected = catalog.reloadPackages(
        QStringList{invalidFixture.path()});
    check(!allRejected.committed()
              && !allRejected.catalogFatal()
              && allRejected.rejectedCount == 1
              && packageKeys(catalog) == retainedKeys,
          QStringLiteral("an all-rejected discovery preserves the previous snapshot"));

    QTemporaryDir duplicateFixture(
        QStringLiteral("/tmp/finepaper-duplicate-catalog-XXXXXX"));
    const QString duplicateV1 = QDir(duplicateFixture.path()).filePath(
        QStringLiteral("finepaper-noc-copy"));
    check(duplicateFixture.isValid()
              && copyDirectory(bundledV1, duplicateV1),
          QStringLiteral("duplicate Package candidate fixture is available"));
    FinepaperApplication duplicateCatalog;
    const PackageCatalogReloadResult duplicateReload =
        duplicateCatalog.reloadPackages(
            QStringList{bundledRoot, duplicateV1});
    check(duplicateReload.committed()
              && duplicateReload.acceptedCount == 1
              && duplicateReload.rejectedCount == 2
              && duplicateCatalog.packages().size() == 1
              && duplicateCatalog.packages().constFirst().key()
                  == QStringLiteral("finepaper.noc@3.1.0")
              && hasDiagnosticCode(
                  duplicateReload.diagnostics,
                  QStringLiteral("package.duplicate_conflict")),
          QStringLiteral("all candidates in an id/version conflict are isolated"));

    FinepaperApplication reverseDuplicateCatalog;
    const PackageCatalogReloadResult reverseDuplicateReload =
        reverseDuplicateCatalog.reloadPackages(
            QStringList{duplicateV1, bundledRoot});
    check(reverseDuplicateReload.committed()
              && reverseDuplicateReload.acceptedCount
                  == duplicateReload.acceptedCount
              && reverseDuplicateReload.rejectedCount
                  == duplicateReload.rejectedCount
              && packageKeys(reverseDuplicateCatalog)
                  == packageKeys(duplicateCatalog),
          QStringLiteral("duplicate conflict isolation is independent of Package root order"));

    FinepaperApplication overlappingRootCatalog;
    const PackageCatalogReloadResult overlappingRootReload =
        overlappingRootCatalog.reloadPackages(
            QStringList{bundledRoot, bundledV1});
    check(overlappingRootReload.committed()
              && overlappingRootReload.acceptedCount == 2
              && overlappingRootReload.rejectedCount == 0
              && overlappingRootCatalog.packages().size() == 2
              && !hasDiagnosticCode(
                  overlappingRootReload.diagnostics,
                  QStringLiteral("package.duplicate_conflict")),
          QStringLiteral("overlapping roots deduplicate one canonical Package without conflict"));

    const QString missingRoot = QDir(invalidFixture.path()).filePath(
        QStringLiteral("missing-root"));
    const PackageCatalogReloadResult fatal = catalog.reloadPackages(
        QStringList{missingRoot});
    check(!fatal.committed()
              && fatal.catalogFatal()
              && hasDiagnosticCode(
                  fatal.diagnostics,
                  QStringLiteral("package.no_usable_roots"))
              && packageKeys(catalog) == retainedKeys,
          QStringLiteral("no usable configured root is catalog-fatal and preserves the old snapshot"));

    QTemporaryDir emptyRoot(
        QStringLiteral("/tmp/finepaper-empty-catalog-XXXXXX"));
    const PackageCatalogReloadResult emptyDirectory = catalog.reloadPackages(
        QStringList{emptyRoot.path()});
    check(emptyRoot.isValid()
              && emptyDirectory.committed()
              && emptyDirectory.acceptedCount == 0
              && emptyDirectory.rejectedCount == 0
              && catalog.packages().isEmpty(),
          QStringLiteral("a genuinely empty usable root commits an empty snapshot"));

    const PackageCatalogReloadResult explicitClear = catalog.reloadPackages({});
    check(explicitClear.committed()
              && !explicitClear.catalogFatal()
              && catalog.packages().isEmpty(),
          QStringLiteral("an empty roots list retains explicit catalog-clear semantics"));

    if (failures == 0) {
        QTextStream(stdout) << "All Package catalog tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
