// PackageService implementation.
#include "package/packageservice.h"

#include "ipcraft/ipcraftregistry.h"
#include "modules/moduleregistry.h"

#include <stdexcept>

namespace {

IpcraftDiagnostic missingCapabilityHandlerDiagnostic(const ipcraft::PackageSpec& spec,
                                                    const PackageFeatureCoverageItem& item) {
    IpcraftDiagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("package.coverage");
    diagnostic.ruleId = QStringLiteral("package.capability_missing_handler");
    diagnostic.category = QStringLiteral("package");
    diagnostic.packageRootPath = spec.packageRootPath;
    diagnostic.path = item.id;
    diagnostic.message = item.message;
    return diagnostic;
}

bool isBlockingCapability(const PackageFeatureCoverageItem& item) {
    return item.status == PackageFeatureCoverageStatus::Blocking &&
           item.id.startsWith(QStringLiteral("capability:"));
}

} // namespace

PackageService::PackageService(ModuleRegistry* moduleRegistry)
    : m_moduleRegistry(moduleRegistry) {
    if (!m_moduleRegistry) {
        throw std::invalid_argument("PackageService requires a ModuleRegistry.");
    }
}

void PackageService::setCapabilityRegistry(const CapabilityRegistry* registry) {
    m_capabilityRegistry = registry;
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
    m_coverageReports.clear();
    m_coverageReports.reserve(loadResult.packageSpecs.size());
    CapabilityRegistry emptyCapabilities;
    const CapabilityRegistry& capabilities =
        m_capabilityRegistry ? *m_capabilityRegistry : emptyCapabilities;
    for (const ipcraft::PackageSpec& spec : loadResult.packageSpecs) {
        PackageCoverageReport report = buildPackageCoverageReport(spec, capabilities);
        if (m_capabilityRegistry) {
            for (const PackageFeatureCoverageItem& item : report.items) {
                if (isBlockingCapability(item)) {
                    m_diagnostics.append(missingCapabilityHandlerDiagnostic(spec, item));
                }
            }
        }
        m_coverageReports.append(report);
    }

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

const QVector<PackageCoverageReport>& PackageService::coverageReports() const {
    return m_coverageReports;
}

const PackageCoverageReport* PackageService::coverageReport(const QString& packageId) const {
    for (const PackageCoverageReport& report : m_coverageReports) {
        if (report.packageId == packageId) {
            return &report;
        }
    }
    return nullptr;
}

const IpCatalogService& PackageService::catalog() const {
    return m_catalog;
}

const ModuleRegistry* PackageService::moduleRegistry() const {
    return m_moduleRegistry;
}
