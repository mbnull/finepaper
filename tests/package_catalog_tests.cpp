#include "application/application.h"
#include "package/package.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

bool writeManifestBytes(const QString& packageRoot, const QByteArray& bytes) {
    if (!QDir().mkpath(packageRoot)) {
        return false;
    }
    QFile manifest(QDir(packageRoot).filePath(QStringLiteral("package.json")));
    return manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && manifest.write(bytes) == bytes.size();
}

void checkManifestRejected(const QString& packageRoot,
                           const QString& code,
                           const QString& path,
                           const QString& label) {
    const PackageLoadResult result = loadPackage(packageRoot);
    check(!result.success && hasDiagnosticCode(result.diagnostics, code), label);
    if (!path.isEmpty()) {
        check(std::any_of(result.diagnostics.cbegin(), result.diagnostics.cend(),
                          [&](const Diagnostic& diagnostic) {
                              return diagnostic.code == code
                                  && diagnostic.path == path;
                          }),
              label + QStringLiteral(" (diagnostic path)"));
    }
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

    QTemporaryDir resourceFixture(
        QStringLiteral("/tmp/finepaper-manifest-resources-XXXXXX"));
    const QString resourceRoot = resourceFixture.path();
    check(resourceFixture.isValid(), QStringLiteral("manifest resource fixture is available"));
    if (!resourceFixture.isValid()) {
        return 1;
    }
    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("too-large")),
              QByteArray(kMaximumPackageManifestBytes + 1, 'x')),
          QStringLiteral("oversized manifest fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("too-large")),
        QStringLiteral("package.manifest_too_large"),
        QDir(resourceRoot).filePath(QStringLiteral("too-large/package.json")),
        QStringLiteral("manifest byte budget is enforced before parsing"));

    QJsonArray oversizedArray;
    for (int index = 0; index <= 65'536; ++index) {
        oversizedArray.append(index);
    }
    QJsonObject oversizedArrayObject;
    oversizedArrayObject.insert(QStringLiteral("value"), oversizedArray);
    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("array")),
              QJsonDocument(oversizedArrayObject).toJson()),
          QStringLiteral("oversized array fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("array")),
        QStringLiteral("package.manifest_array_too_large"),
        QStringLiteral("/value"),
        QStringLiteral("manifest array budget is enforced"));

    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("string")),
              QJsonDocument(QJsonObject{{QStringLiteral("value"),
                                         QString(65'537, QLatin1Char('s'))}})
                  .toJson()),
          QStringLiteral("oversized string fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("string")),
        QStringLiteral("package.manifest_string_too_long"),
        QStringLiteral("/value"),
        QStringLiteral("manifest string budget is enforced"));

    QByteArray deepManifest("{\"value\":");
    for (int index = 0; index < 65; ++index) {
        deepManifest.append('[');
    }
    deepManifest.append("null");
    for (int index = 0; index < 65; ++index) {
        deepManifest.append(']');
    }
    deepManifest.append("}");
    check(writeManifestBytes(QDir(resourceRoot).filePath(QStringLiteral("depth")),
                             deepManifest),
          QStringLiteral("deep manifest fixture is writable"));
    const QString depthPointer = QStringLiteral("/value")
        + QStringLiteral("/0").repeated(64);
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("depth")),
        QStringLiteral("package.manifest_depth_exceeded"),
        depthPointer,
        QStringLiteral("manifest nesting budget is enforced"));

    QJsonObject excessiveMembers;
    for (int index = 0; index <= 65'536; ++index) {
        excessiveMembers.insert(QStringLiteral("member%1").arg(index),
                                QJsonValue::Null);
    }
    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("object-members")),
              QJsonDocument(QJsonObject{
                  {QStringLiteral("value"), excessiveMembers}})
                  .toJson(QJsonDocument::Compact)),
          QStringLiteral("oversized manifest object fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("object-members")),
        QStringLiteral("package.manifest_object_too_large"),
        QStringLiteral("/value"),
        QStringLiteral("manifest object member budget is enforced"));

    const QString longKey(4'097, QLatin1Char('k'));
    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("object-key")),
              QJsonDocument(QJsonObject{
                  {QStringLiteral("value"),
                   QJsonObject{{longKey, QJsonValue::Null}}}})
                  .toJson(QJsonDocument::Compact)),
          QStringLiteral("oversized manifest object key fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("object-key")),
        QStringLiteral("package.manifest_object_key_too_long"),
        QStringLiteral("/value/") + longKey,
        QStringLiteral("manifest object key budget is enforced"));

    QJsonObject valueBuckets;
    for (int bucket = 0; bucket < 16; ++bucket) {
        QJsonArray values;
        for (int index = 0; index < 65'536; ++index) {
            values.append(QJsonValue::Null);
        }
        valueBuckets.insert(
            QStringLiteral("bucket%1")
                .arg(bucket, 2, 10, QLatin1Char('0')),
            values);
    }
    check(writeManifestBytes(
              QDir(resourceRoot).filePath(QStringLiteral("value-budget")),
              QJsonDocument(QJsonObject{
                  {QStringLiteral("value"), valueBuckets}})
                  .toJson(QJsonDocument::Compact)),
          QStringLiteral("manifest value budget fixture is writable"));
    checkManifestRejected(
        QDir(resourceRoot).filePath(QStringLiteral("value-budget")),
        QStringLiteral("package.manifest_value_budget_exceeded"),
        QStringLiteral("/value/bucket15/16942"),
        QStringLiteral("manifest total value budget is enforced"));

    if (failures == 0) {
        QTextStream(stdout) << "All Package catalog tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
