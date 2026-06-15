#pragma once

#include "ipcraft/ipcraftmanifest.h"

#include <QString>
#include <QVector>

struct IpcraftManifestReadResult {
    bool ok = false;
    IpcraftPackageManifest manifest;
    ipcraft::PackageSpec spec;
    QVector<IpcraftDiagnostic> diagnostics;
};

class IpcraftManifestReader {
public:
    IpcraftManifestReadResult readPackage(const QString& packageRootPath) const;
    IpcraftManifestReadResult readManifestFile(const QString& manifestPath) const;
};
