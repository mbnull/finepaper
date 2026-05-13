#include "ipcraft/ipcraftregistry.h"
#include "ipcraft/ipcraftmanifestreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QXmlStreamReader>

namespace {

void addDiagnostic(QVector<IpcraftDiagnostic>& diagnostics,
                   const IpcraftPackageManifest& manifest,
                   const QString& path,
                   const QString& message) {
    IpcraftDiagnostic diagnostic;
    diagnostic.packageRootPath = manifest.packageRootPath;
    diagnostic.path = path;
    diagnostic.message = message;
    diagnostics.append(diagnostic);
}

void appendCandidate(QVector<QString>& candidates,
                     QSet<QString>& seen,
                     const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir()) {
        return;
    }

    const QString absolutePath = info.absoluteFilePath();
    if (seen.contains(absolutePath)) {
        return;
    }

    if (QFileInfo(QDir(absolutePath).filePath(QStringLiteral("ipcraft.json"))).isFile()) {
        seen.insert(absolutePath);
        candidates.append(absolutePath);
    }
}

QVector<QString> discoverPackageRoots(const QStringList& rootPaths) {
    QVector<QString> candidates;
    QSet<QString> seen;

    for (const QString& rootPath : rootPaths) {
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            continue;
        }

        appendCandidate(candidates, seen, rootInfo.absoluteFilePath());

        const QDir rootDir(rootInfo.absoluteFilePath());
        const QFileInfoList children =
            rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& child : children) {
            appendCandidate(candidates, seen, child.absoluteFilePath());
        }
    }

    return candidates;
}

QSet<QString> interfaceIdsForModule(const IpcraftModuleDescriptor& module) {
    QSet<QString> interfaceIds;
    for (const IpcraftInterfaceDescriptor& descriptor : module.interfaces) {
        interfaceIds.insert(descriptor.id);
    }
    return interfaceIds;
}

} // namespace

bool IpcraftRegistry::loadPackageRoots(const QStringList& rootPaths) {
    m_packages.clear();
    m_diagnostics.clear();

    const QVector<QString> packageRoots = discoverPackageRoots(rootPaths);
    QVector<IpcraftPackageManifest> loadedPackages;
    QSet<QString> loadedPackageIds;
    bool loadedAll = true;

    for (const QString& packageRoot : packageRoots) {
        const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(packageRoot);
        if (!result.ok) {
            m_diagnostics += result.diagnostics;
            loadedAll = false;
            continue;
        }

        if (loadedPackageIds.contains(result.manifest.id)) {
            IpcraftDiagnostic diagnostic;
            diagnostic.packageRootPath = result.manifest.packageRootPath;
            diagnostic.path = QStringLiteral("manifest.id");
            diagnostic.message = QStringLiteral("Duplicate ipcraft package id '%1'")
                                     .arg(result.manifest.id);
            m_diagnostics.append(diagnostic);
            loadedAll = false;
            continue;
        }

        QVector<IpcraftDiagnostic> packageDiagnostics;
        for (const IpcraftViewDescriptor& view : result.manifest.views) {
            validateViewXml(result.manifest, view, packageDiagnostics);
        }

        if (!packageDiagnostics.isEmpty()) {
            m_diagnostics += packageDiagnostics;
            loadedAll = false;
            continue;
        }

        loadedPackageIds.insert(result.manifest.id);
        loadedPackages.append(result.manifest);
    }

    if (loadedAll) {
        m_packages = loadedPackages;
    }

    return loadedAll;
}

const QVector<IpcraftPackageManifest>& IpcraftRegistry::packages() const {
    return m_packages;
}

const IpcraftPackageManifest* IpcraftRegistry::package(const QString& packageId) const {
    for (const IpcraftPackageManifest& manifest : m_packages) {
        if (manifest.id == packageId) {
            return &manifest;
        }
    }
    return nullptr;
}

const QVector<IpcraftDiagnostic>& IpcraftRegistry::diagnostics() const {
    return m_diagnostics;
}

bool IpcraftRegistry::validateViewXml(const IpcraftPackageManifest& manifest,
                                      const IpcraftViewDescriptor& view,
                                      QVector<IpcraftDiagnostic>& diagnostics) const {
    const IpcraftModuleDescriptor* module = manifest.module(view.moduleId);
    if (module == nullptr) {
        addDiagnostic(diagnostics,
                      manifest,
                      view.filePath,
                      QStringLiteral("View XML references missing module '%1'").arg(view.moduleId));
        return false;
    }

    QFile file(view.resolvedFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        addDiagnostic(diagnostics,
                      manifest,
                      view.filePath,
                      QStringLiteral("Could not open view XML '%1'").arg(view.filePath));
        return false;
    }

    QXmlStreamReader xml(&file);
    bool sawRoot = false;
    const QSet<QString> interfaceIds = interfaceIdsForModule(*module);

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        if (!sawRoot) {
            sawRoot = true;
            if (xml.name() != QStringLiteral("module-view")) {
                addDiagnostic(diagnostics,
                              manifest,
                              view.filePath,
                              QStringLiteral("View XML root must be module-view"));
                return false;
            }

            const QString xmlModule =
                xml.attributes().value(QStringLiteral("module")).toString().trimmed();
            if (xmlModule != view.moduleId) {
                addDiagnostic(diagnostics,
                              manifest,
                              view.filePath,
                              QStringLiteral("View XML module '%1' does not match manifest module '%2'")
                                  .arg(xmlModule, view.moduleId));
                return false;
            }
            continue;
        }

        if (xml.name() == QStringLiteral("anchor")) {
            const QString interfaceId =
                xml.attributes().value(QStringLiteral("ref")).toString().trimmed();
            if (!interfaceIds.contains(interfaceId)) {
                addDiagnostic(diagnostics,
                              manifest,
                              view.filePath,
                              QStringLiteral("View XML anchor references missing interface '%1' on module '%2'")
                                  .arg(interfaceId, view.moduleId));
                return false;
            }
        }
    }

    if (xml.hasError()) {
        addDiagnostic(diagnostics,
                      manifest,
                      view.filePath,
                      QStringLiteral("Invalid view XML '%1': %2")
                          .arg(view.filePath, xml.errorString()));
        return false;
    }

    if (!sawRoot) {
        addDiagnostic(diagnostics,
                      manifest,
                      view.filePath,
                      QStringLiteral("View XML is empty"));
        return false;
    }

    return true;
}
