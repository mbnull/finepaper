// PackageService tests.
#include "app/capabilityregistry.h"
#include "modules/moduleregistry.h"
#include "package/packageservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePath));
            if (info.exists()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePath)).absoluteFilePath();
}

bool hasManifest(const QVector<IpcraftPackageManifest>& manifests, const QString& packageId) {
    return std::any_of(manifests.cbegin(), manifests.cend(), [&](const IpcraftPackageManifest& manifest) {
        return manifest.id == packageId;
    });
}

bool hasDiagnosticRule(const QVector<IpcraftDiagnostic>& diagnostics, const QString& ruleId) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const IpcraftDiagnostic& diagnostic) {
        return diagnostic.ruleId == ruleId;
    });
}

void writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "package fixture should open for writing");
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size(), "package fixture should write");
}

QJsonObject packageWithExtensions(const QString& packageId, const QJsonArray& extensions) {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("ipcraft.package.v1")},
        {QStringLiteral("id"), packageId},
        {QStringLiteral("name"), QStringLiteral("Capability Fixture")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), extensions}
    };
}

PackageFeatureCoverageItem requireCoverageItem(const PackageCoverageReport& report,
                                               const QString& id,
                                               const char* message) {
    const PackageFeatureCoverageItem item = report.item(id);
    require(item.id == id, message);
    return item;
}

void testEmptyCatalogDefaultConstructs() {
    IpCatalogService catalog;

    require(catalog.entries().isEmpty(), "default catalog should be empty");
    require(catalog.selectableEntries().isEmpty(), "default catalog should have no selectable entries");
    require(!catalog.entry(QStringLiteral("missing")).has_value(),
            "default catalog lookup should not find entries");
}

void testLoadsAnchorPackagesAndCatalogEntries() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    const QString ipcoresRoot = repositoryPath(QStringLiteral("ipcores"));

    const PackageServiceLoadResult result =
        service.reloadPackageRoots(QStringList{ipcoresRoot});

    require(result.success, "loading repository ipcores should succeed");
    require(result.packageRootCount == 1, "result should record root count");
    require(result.packageCount >= 3, "result should record loaded package count");
    require(service.packageRoots() == QStringList{ipcoresRoot},
            "service should retain requested package roots");
    require(service.diagnostics().isEmpty(), "anchor package load should have no diagnostics");
    require(hasManifest(service.manifests(), QStringLiteral("finepaper.noc")),
            "finepaper-noc package should load as finepaper.noc");
    require(hasManifest(service.manifests(), QStringLiteral("finepaper.ravenoc")),
            "ravenoc package should load");
    require(hasManifest(service.manifests(), QStringLiteral("finepaper.opennoc")),
            "opennoc package should load");

    const IpCatalogService& catalog = service.catalog();
    const std::optional<IpCatalogEntry> noc = catalog.entry(QStringLiteral("finepaper.noc"));
    const std::optional<IpCatalogEntry> ravenoc = catalog.entry(QStringLiteral("finepaper.ravenoc"));
    const std::optional<IpCatalogEntry> opennoc = catalog.entry(QStringLiteral("finepaper.opennoc"));
    require(noc.has_value(), "catalog should expose finepaper-noc");
    require(ravenoc.has_value(), "catalog should expose ravenoc");
    require(opennoc.has_value(), "catalog should expose opennoc");
    require(noc->isSelectable(), "finepaper-noc catalog entry should be selectable");
    require(ravenoc->isSelectable(), "ravenoc catalog entry should be selectable");
    require(opennoc->isSelectable(), "opennoc catalog entry should be selectable");

    require(!registry.availableTypesForIpcore(QStringLiteral("finepaper.noc")).isEmpty(),
            "finepaper-noc module types should be registered");
    require(!registry.availableTypesForIpcore(QStringLiteral("finepaper.ravenoc")).isEmpty(),
            "ravenoc module types should be registered");
    require(!registry.availableTypesForIpcore(QStringLiteral("finepaper.opennoc")).isEmpty(),
            "opennoc module types should be registered");
}

void testDiagnosticsAreStoredForInvalidRoots() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("broken")), "broken package directory should be created");

    QFile manifest(root.filePath(QStringLiteral("broken/ipcraft.json")));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "broken manifest should open");
    }
    manifest.write("{\"schema\":\"ipcraft.package.v1\",\"id\":\"broken.pkg\"}");
    manifest.close();

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    const PackageServiceLoadResult result =
        service.reloadPackageRoots(QStringList{root.absolutePath()});

    require(!result.success, "invalid package load should report failure");
    require(result.packageCount == 0, "invalid package should not be loaded");
    require(!result.diagnostics.isEmpty(), "invalid package load should report diagnostics");
    require(!service.diagnostics().isEmpty(), "service should store diagnostics");
    require(service.catalog().entries().isEmpty(), "invalid package should not populate catalog");
    require(registry.availableTypes().isEmpty(), "invalid package should not register modules");
}

void testOptionalUnknownCapabilitySurvivesReloadAsUnsupportedCoverage() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    writeJsonFile(root.filePath(QStringLiteral("ipcraft.json")),
                  packageWithExtensions(
                      QStringLiteral("vendor.optionalcoverage"),
                      QJsonArray{
                          QJsonObject{
                              {QStringLiteral("id"), QStringLiteral("vendor.extra.v1")},
                              {QStringLiteral("required"), false},
                              {QStringLiteral("version"), QStringLiteral("2.3.4")},
                              {QStringLiteral("metadata"),
                               QJsonObject{{QStringLiteral("lane"), QStringLiteral("sideband")}}},
                              {QStringLiteral("native"),
                               QJsonObject{{QStringLiteral("adapter"), QStringLiteral("vendor-native")}}}
                          }
                      }));

    CapabilityRegistry capabilities;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    service.setCapabilityRegistry(&capabilities);

    const PackageServiceLoadResult result =
        service.reloadPackageRoots(QStringList{root.absolutePath()});

    require(result.success, "optional unknown capability package should load");
    require(result.packageCount == 1, "optional unknown capability package should be counted");
    require(service.diagnostics().isEmpty(),
            "optional unknown capability should not create package diagnostics");

    const PackageCoverageReport* report =
        service.coverageReport(QStringLiteral("vendor.optionalcoverage"));
    require(report != nullptr, "optional package coverage report should exist");
    const PackageFeatureCoverageItem item =
        requireCoverageItem(*report,
                            QStringLiteral("capability:vendor.extra.v1"),
                            "optional unknown capability coverage item should exist");
    require(item.status == PackageFeatureCoverageStatus::Unsupported,
            "optional unknown capability should be unsupported");
    require(item.descriptor.value(QStringLiteral("required")).toBool(true) == false,
            "optional unknown capability should preserve required:false");
    require(item.descriptor.value(QStringLiteral("version")).toString() == QStringLiteral("2.3.4"),
            "optional unknown capability should preserve version");
    require(item.descriptor.value(QStringLiteral("metadata")).toObject()
                .value(QStringLiteral("lane")).toString() == QStringLiteral("sideband"),
            "optional unknown capability should preserve metadata");
    require(item.descriptor.value(QStringLiteral("native")).toObject()
                .value(QStringLiteral("adapter")).toString() == QStringLiteral("vendor-native"),
            "optional unknown capability should preserve native descriptor data");
}

void testRequiredUnknownCapabilityReportsBlockingCoverageAndDiagnostic() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    writeJsonFile(root.filePath(QStringLiteral("ipcraft.json")),
                  packageWithExtensions(
                      QStringLiteral("vendor.requiredcoverage"),
                      QJsonArray{
                          QJsonObject{
                              {QStringLiteral("id"), QStringLiteral("vendor.required.v1")},
                              {QStringLiteral("required"), true}
                          }
                      }));

    CapabilityRegistry capabilities;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    service.setCapabilityRegistry(&capabilities);

    const PackageServiceLoadResult result =
        service.reloadPackageRoots(QStringList{root.absolutePath()});

    require(!result.success,
            "required unknown capability should make package service load result unsuccessful");
    require(result.packageCount == 1,
            "required unknown capability package should still be parsed and counted");

    const PackageCoverageReport* report =
        service.coverageReport(QStringLiteral("vendor.requiredcoverage"));
    require(report != nullptr, "required package coverage report should exist");
    require(requireCoverageItem(*report,
                                QStringLiteral("capability:vendor.required.v1"),
                                "required unknown capability coverage item should exist").status ==
                PackageFeatureCoverageStatus::Blocking,
            "required unknown capability should be blocking");
    require(hasDiagnosticRule(result.diagnostics,
                              QStringLiteral("package.capability_missing_handler")),
            "load result should include stable missing capability handler diagnostic");
    require(hasDiagnosticRule(service.diagnostics(),
                              QStringLiteral("package.capability_missing_handler")),
            "service diagnostics should store stable missing capability handler diagnostic");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testEmptyCatalogDefaultConstructs();
        testLoadsAnchorPackagesAndCatalogEntries();
        testDiagnosticsAreStoredForInvalidRoots();
        testOptionalUnknownCapabilitySurvivesReloadAsUnsupportedCoverage();
        testRequiredUnknownCapabilityReportsBlockingCoverageAndDiagnostic();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "packageservice_test passed\n";
    return 0;
}
