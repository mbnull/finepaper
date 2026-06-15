#include "ipcraft/ipcraftregistry.h"
#include "ipcraft/ipcraftmanifestreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

QString packageVersionKey(const IpcraftPackageManifest& manifest) {
    return manifest.id + QLatin1Char('@') + manifest.version;
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

bool validateViewXmlDescriptor(const IpcraftPackageManifest& manifest,
                               const IpcraftViewDescriptor& view,
                               QVector<IpcraftDiagnostic>& diagnostics) {
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

} // namespace

IpcraftRegistryLoadResult loadIpcraftPackageManifestsWithDiagnostics(const QStringList& rootPaths) {
    struct CandidateManifest {
        IpcraftPackageManifest manifest;
        ipcraft::PackageSpec packageSpec;
        QVector<IpcraftDiagnostic> diagnostics;
    };

    IpcraftRegistryLoadResult loadResult;
    const QVector<QString> packageRoots = discoverPackageRoots(rootPaths);
    QVector<CandidateManifest> candidates;
    QHash<QString, QVector<int>> indexesByPackageVersion;
    QHash<QString, QStringList> rootsByPackageVersion;
    QStringList packageVersionOrder;

    for (const QString& packageRoot : packageRoots) {
        const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(packageRoot);
        if (!result.ok) {
            loadResult.diagnostics += result.diagnostics;
            continue;
        }

        CandidateManifest candidate;
        candidate.manifest = result.manifest;
        candidate.packageSpec = result.spec;
        for (const IpcraftViewDescriptor& view : result.manifest.views) {
            validateViewXmlDescriptor(result.manifest, view, candidate.diagnostics);
        }

        const QString versionKey = packageVersionKey(result.manifest);
        if (!indexesByPackageVersion.contains(versionKey)) {
            packageVersionOrder.append(versionKey);
        }
        indexesByPackageVersion[versionKey].append(candidates.size());
        rootsByPackageVersion[versionKey].append(result.manifest.packageRootPath);
        candidates.append(candidate);
    }

    QSet<int> duplicateCandidateIndexes;
    for (const QString& versionKey : packageVersionOrder) {
        const QVector<int> duplicateIndexes = indexesByPackageVersion.value(versionKey);
        if (duplicateIndexes.size() < 2) {
            continue;
        }

        for (int index : duplicateIndexes) {
            duplicateCandidateIndexes.insert(index);
        }

        const QStringList duplicateRoots = rootsByPackageVersion.value(versionKey);
        IpcraftDiagnostic diagnostic;
        diagnostic.severity = QStringLiteral("error");
        diagnostic.source = QStringLiteral("package.resolver");
        diagnostic.ruleId = QStringLiteral("package.duplicate_version");
        diagnostic.category = QStringLiteral("package");
        diagnostic.packageRootPath = duplicateRoots.isEmpty() ? QString() : duplicateRoots.first();
        diagnostic.path = QStringLiteral("$");
        diagnostic.message = QStringLiteral("Duplicate package version %1 in package roots: %2")
                                 .arg(versionKey, duplicateRoots.join(QStringLiteral(", ")));
        loadResult.diagnostics.append(diagnostic);
    }

    for (qsizetype i = 0; i < candidates.size(); ++i) {
        if (duplicateCandidateIndexes.contains(static_cast<int>(i))) {
            continue;
        }

        if (!candidates.at(i).diagnostics.isEmpty()) {
            loadResult.diagnostics += candidates.at(i).diagnostics;
            continue;
        }

        loadResult.manifests.append(candidates.at(i).manifest);
        loadResult.packageSpecs.append(candidates.at(i).packageSpec);
    }

    return loadResult;
}

bool IpcraftRegistry::loadPackageRoots(const QStringList& rootPaths) {
    m_packages.clear();
    m_diagnostics.clear();

    const IpcraftRegistryLoadResult loadResult =
        loadIpcraftPackageManifestsWithDiagnostics(rootPaths);
    m_diagnostics = loadResult.diagnostics;
    m_packages = loadResult.manifests;
    return m_diagnostics.isEmpty();
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
    return validateViewXmlDescriptor(manifest, view, diagnostics);
}
