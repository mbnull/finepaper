#pragma once

#include "ipcraft/ipcraftmanifest.h"

#include <QStringList>
#include <QVector>

class IpcraftRegistry {
public:
    bool loadPackageRoots(const QStringList& rootPaths);

    const QVector<IpcraftPackageManifest>& packages() const;
    const IpcraftPackageManifest* package(const QString& packageId) const;
    const QVector<IpcraftDiagnostic>& diagnostics() const;

private:
    bool validateViewXml(const IpcraftPackageManifest& manifest,
                         const IpcraftViewDescriptor& view,
                         QVector<IpcraftDiagnostic>& diagnostics) const;

    QVector<IpcraftPackageManifest> m_packages;
    QVector<IpcraftDiagnostic> m_diagnostics;
};
