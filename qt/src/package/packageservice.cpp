// PackageService implementation.
#include "package/packageservice.h"

#include "ipcraft/ipcraftregistry.h"
#include "modules/moduleregistry.h"

#include <stdexcept>

PackageService::PackageService(ModuleRegistry* moduleRegistry)
    : m_moduleRegistry(moduleRegistry) {
    if (!m_moduleRegistry) {
        throw std::invalid_argument("PackageService requires a ModuleRegistry.");
    }
}

PackageServiceLoadResult PackageService::reloadPackageRoots(const QStringList& rootPaths) {
    const IpcraftRegistryLoadResult loadResult =
        loadIpcraftPackageManifestsWithDiagnostics(rootPaths);

    *m_moduleRegistry = ModuleRegistry(ModuleRegistry::LoadMode::Empty);
    m_moduleRegistry->loadIpcraftPackages(loadResult.manifests);

    m_packageRoots = rootPaths;
    m_manifests = loadResult.manifests;
    m_diagnostics = loadResult.diagnostics;
    m_catalog = IpCatalogService(m_manifests, m_moduleRegistry);

    PackageServiceLoadResult result;
    result.success = m_diagnostics.isEmpty();
    result.packageRootCount = rootPaths.size();
    result.packageCount = m_manifests.size();
    result.diagnostics = m_diagnostics;
    return result;
}

const QStringList& PackageService::packageRoots() const {
    return m_packageRoots;
}

const QVector<IpcraftPackageManifest>& PackageService::manifests() const {
    return m_manifests;
}

const QVector<IpcraftDiagnostic>& PackageService::diagnostics() const {
    return m_diagnostics;
}

const IpCatalogService& PackageService::catalog() const {
    return m_catalog;
}

ModuleRegistry* PackageService::moduleRegistry() const {
    return m_moduleRegistry;
}
