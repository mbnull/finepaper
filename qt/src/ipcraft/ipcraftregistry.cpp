#include "ipcraft/ipcraftregistry.h"
#include "ipcraft/ipcraftmanifestreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

bool isAttachmentZoneElement(const QString& elementName) {
    return elementName == QStringLiteral("zone")
           || elementName == QStringLiteral("attachment-zone");
}

bool isInterfaceReferenceAttribute(const QString& elementName, const QString& attributeName) {
    if (attributeName == QStringLiteral("ref")) {
        return elementName == QStringLiteral("anchor")
               || elementName == QStringLiteral("interface");
    }
    static const QSet<QString> kInterfaceReferenceAttributes{
        QStringLiteral("interface"),
        QStringLiteral("interface_id"),
        QStringLiteral("interface_ref")
    };
    return kInterfaceReferenceAttributes.contains(attributeName);
}

bool isAttachmentZoneReferenceAttribute(const QString& elementName, const QString& attributeName) {
    if (attributeName == QStringLiteral("ref")) {
        return isAttachmentZoneElement(elementName);
    }
    static const QSet<QString> kAttachmentZoneReferenceAttributes{
        QStringLiteral("zone"),
        QStringLiteral("attach_zone"),
        QStringLiteral("attachment_zone")
    };
    return kAttachmentZoneReferenceAttributes.contains(attributeName);
}

QString attachZone(const IpcraftModuleDescriptor& module) {
    return module.attach.value(QStringLiteral("zone")).toString().trimmed();
}

QStringList attachHosts(const IpcraftModuleDescriptor& module) {
    QStringList hosts;
    const QJsonValue hostsValue = module.attach.value(QStringLiteral("hosts"));
    if (hostsValue.isArray()) {
        const QJsonArray hostArray = hostsValue.toArray();
        for (const QJsonValue& hostValue : hostArray) {
            const QString host = hostValue.toString().trimmed();
            if (!host.isEmpty() && !hosts.contains(host)) {
                hosts.append(host);
            }
        }
    }

    const QString host = module.attach.value(QStringLiteral("host")).toString().trimmed();
    if (!host.isEmpty() && !hosts.contains(host)) {
        hosts.append(host);
    }
    return hosts;
}

QSet<QString> attachmentZonesForModule(const IpcraftPackageManifest& manifest,
                                       const QString& moduleId) {
    QSet<QString> zones;
    const IpcraftModuleDescriptor* module = manifest.module(moduleId);
    if (module != nullptr) {
        const QString selfZone = attachZone(*module);
        if (!selfZone.isEmpty()) {
            zones.insert(selfZone);
        }
    }

    for (const IpcraftModuleDescriptor& candidate : manifest.modules) {
        const QString zone = attachZone(candidate);
        if (zone.isEmpty()) {
            continue;
        }
        if (attachHosts(candidate).contains(moduleId)) {
            zones.insert(zone);
        }
    }
    return zones;
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
    const QSet<QString> attachmentZones = attachmentZonesForModule(manifest, view.moduleId);

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

        const QString elementName = xml.name().toString();
        const QXmlStreamAttributes attributes = xml.attributes();
        for (const QXmlStreamAttribute& attribute : attributes) {
            const QString attributeName = attribute.name().toString();
            const QString attributeValue = attribute.value().toString().trimmed();

            if (isInterfaceReferenceAttribute(elementName, attributeName)
                && !interfaceIds.contains(attributeValue)) {
                const bool isAnchorRef =
                    elementName == QStringLiteral("anchor")
                    && attributeName == QStringLiteral("ref");
                const QString message = isAnchorRef
                    ? QStringLiteral("View XML anchor references missing interface '%1' on module '%2'")
                          .arg(attributeValue, view.moduleId)
                    : QStringLiteral("View XML interface reference attribute '%1' references missing interface '%2' on module '%3'")
                          .arg(attributeName, attributeValue, view.moduleId);
                addDiagnostic(diagnostics,
                              manifest,
                              view.filePath,
                              message);
                return false;
            }

            if (isAttachmentZoneReferenceAttribute(elementName, attributeName)) {
                if (attributeValue.isEmpty()) {
                    addDiagnostic(diagnostics,
                                  manifest,
                                  view.filePath,
                                  QStringLiteral("View XML attachment zone attribute '%1' must not be empty on module '%2'")
                                      .arg(attributeName, view.moduleId));
                    return false;
                }

                if (!attachmentZones.contains(attributeValue)) {
                    addDiagnostic(diagnostics,
                                  manifest,
                                  view.filePath,
                                  QStringLiteral("View XML attachment zone attribute '%1' references missing attachment zone '%2' on module '%3'")
                                      .arg(attributeName, attributeValue, view.moduleId));
                    return false;
                }
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
