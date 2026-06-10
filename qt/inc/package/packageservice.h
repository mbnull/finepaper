// PackageService owns extension package loading and catalog projection.
#pragma once

#include "ipcore/ipcatalogservice.h"

#include <QStringList>
#include <QVector>

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

    PackageServiceLoadResult reloadPackageRoots(const QStringList& rootPaths);

    const QStringList& packageRoots() const;
    const QVector<IpcraftPackageManifest>& manifests() const;
    const QVector<IpcraftDiagnostic>& diagnostics() const;
    const IpCatalogService& catalog() const;
    ModuleRegistry* moduleRegistry() const;

private:
    ModuleRegistry* m_moduleRegistry = nullptr;
    QStringList m_packageRoots;
    QVector<IpcraftPackageManifest> m_manifests;
    QVector<IpcraftDiagnostic> m_diagnostics;
    IpCatalogService m_catalog;
};
