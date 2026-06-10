// PackageService tests.
#include "modules/moduleregistry.h"
#include "package/packageservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testEmptyCatalogDefaultConstructs();
        testLoadsAnchorPackagesAndCatalogEntries();
        testDiagnosticsAreStoredForInvalidRoots();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "packageservice_test passed\n";
    return 0;
}
