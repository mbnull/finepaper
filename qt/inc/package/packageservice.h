// PackageService owns extension package loading and catalog projection.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "package/packagecoverage.h"

#include <QStringList>
#include <QVector>

class CapabilityRegistry;
class ModuleRegistry;

struct PackageServiceLoadResult {
    bool success = false;
    int packageRootCount = 0;
    int packageCount = 0;
    QVector<IpcraftDiagnostic> diagnostics;
};

class PackageService {
public:
    explicit PackageService(ModuleRegistry* moduleRegistry);

    void setCapabilityRegistry(const CapabilityRegistry* registry);
    PackageServiceLoadResult reloadPackageRoots(const QStringList& rootPaths);

    const QStringList& packageRoots() const;
    const QVector<IpcraftPackageManifest>& manifests() const;
    const QVector<IpcraftDiagnostic>& diagnostics() const;
    const QVector<PackageCoverageReport>& coverageReports() const;
    const PackageCoverageReport* coverageReport(const QString& packageId) const;
    const IpCatalogService& catalog() const;
    const ModuleRegistry* moduleRegistry() const;

private:
    ModuleRegistry* m_moduleRegistry = nullptr;
    const CapabilityRegistry* m_capabilityRegistry = nullptr;
    QStringList m_packageRoots;
    QVector<IpcraftPackageManifest> m_manifests;
    QVector<IpcraftDiagnostic> m_diagnostics;
    QVector<PackageCoverageReport> m_coverageReports;
    IpCatalogService m_catalog;
};
