// ModuleRegistry bootstraps, indexes, and serves module type metadata to the editor.
#include "modules/moduleregistry.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "modules/moduleprovider.h"
#include <QDebug>
#include <QFileInfo>

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

QString ModuleRegistry::scopedTypeName(const QString& packageId, const QString& moduleId) {
    const QString trimmedModuleId = moduleId.trimmed();
    const QString trimmedPackageId = packageId.trimmed();
    if (trimmedModuleId.isEmpty() || trimmedPackageId.isEmpty()) {
        return trimmedModuleId;
    }
    if (trimmedModuleId.contains(QStringLiteral("::"))) {
        return trimmedModuleId;
    }
    return trimmedPackageId + QStringLiteral("::") + trimmedModuleId;
}

// Load module definitions from startup-discovered Ipcraft packages.
ModuleRegistry::ModuleRegistry(LoadMode loadMode) {
    if (loadMode == LoadMode::Empty) {
        return;
    }

    if (loadIpcraftPackages(loadIpcraftPackageManifests(defaultIpcraftPackageRoots()))) {
        return;
    }

    qWarning() << "No Ipcraft package module definitions found.";
}

void ModuleRegistry::addProvider(std::unique_ptr<ModuleProvider> provider) {
    auto types = provider->loadModules();
    for (const auto& type : types) {
        registerType(type);
    }
}

bool ModuleRegistry::registerType(const ModuleType& type) {
    if (type.name.isEmpty()) {
        return false;
    }
    if (m_types.contains(type.name)) {
        qWarning() << "Skipping duplicate module type" << type.name
                   << "from package/runtime owner" << type.ipcoreId;
        return false;
    }
    m_types[type.name] = type;
    return true;
}

bool ModuleRegistry::loadIpCoreRuntimes(const QList<IpCoreRuntimeDescriptor>& runtimes) {
    bool loadedAnyType = false;

    for (const IpCoreRuntimeDescriptor& runtime : runtimes) {
        if (!runtime.hasModules() || !QFileInfo(runtime.modulesPath).isFile()) {
            continue;
        }

        std::unique_ptr<LayeredModuleProvider> provider;
        if (runtime.modulesPath.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
            provider = std::make_unique<LayeredModuleProvider>(
                std::make_unique<XmlModuleTypeSource>(runtime.modulesPath));
            if (!runtime.graphicsPath.isEmpty() && QFileInfo(runtime.graphicsPath).isDir()) {
                provider->addOverlay(std::make_unique<XmlModuleGraphicsOverlay>(runtime.graphicsPath));
            }
        } else {
            qWarning() << "Skipping IP core runtime with unsupported module bundle"
                       << runtime.id
                       << runtime.modulesPath;
            continue;
        }

        auto types = provider->loadModules();
        for (ModuleType& type : types) {
            type.ipcoreId = runtime.id;
            loadedAnyType = registerType(type) || loadedAnyType;
        }
    }

    return loadedAnyType;
}

bool ModuleRegistry::loadIpcraftPackages(const QVector<IpcraftPackageManifest>& packages) {
    bool loadedAnyType = false;

    for (const IpcraftPackageManifest& package : packages) {
        if (!package.id.isEmpty()) {
            m_packageManifests.insert(package.id, package);
        }
        if (package.modules.isEmpty()) {
            continue;
        }

        auto provider = std::make_unique<LayeredModuleProvider>(
            std::make_unique<IpcraftModuleTypeSource>(package));
        provider->addOverlay(std::make_unique<IpcraftModuleViewOverlay>(package.views));

        auto types = provider->loadModules();
        for (ModuleType& type : types) {
            loadedAnyType = registerType(type) || loadedAnyType;
        }
    }

    return loadedAnyType;
}

const ModuleType* ModuleRegistry::getType(const QString& name) const {
    auto it = m_types.find(name);
    if (it != m_types.end()) {
        return &it.value();
    }

    const ModuleType* match = nullptr;
    for (auto candidate = m_types.cbegin(); candidate != m_types.cend(); ++candidate) {
        if (candidate.value().moduleId != name) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &candidate.value();
    }
    return match;
}

const ModuleType* ModuleRegistry::getType(const QString& packageId, const QString& moduleId) const {
    if (moduleId.trimmed().isEmpty()) {
        return nullptr;
    }

    const auto exactIt = m_types.find(moduleId);
    if (exactIt != m_types.end()) {
        const QString owner = exactIt.value().packageId.isEmpty()
            ? exactIt.value().ipcoreId
            : exactIt.value().packageId;
        if (packageId.trimmed().isEmpty() || owner == packageId) {
            return &exactIt.value();
        }
    }

    const QString scopedName = scopedTypeName(packageId, moduleId);
    const auto scopedIt = m_types.find(scopedName);
    if (scopedIt != m_types.end()) {
        return &scopedIt.value();
    }

    for (auto candidate = m_types.cbegin(); candidate != m_types.cend(); ++candidate) {
        const ModuleType& type = candidate.value();
        const QString owner = type.packageId.isEmpty() ? type.ipcoreId : type.packageId;
        if (owner == packageId && (type.moduleId == moduleId || type.name == moduleId)) {
            return &candidate.value();
        }
    }

    return nullptr;
}

const ModuleType* ModuleRegistry::getTypeForGraphGroup(const QString& graphGroup) const {
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().graphGroup == graphGroup) {
            return &it.value();
        }
    }
    return nullptr;
}

const ModuleType* ModuleRegistry::getTypeForGraphGroup(const QString& ipcoreId,
                                                       const QString& graphGroup) const {
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().ipcoreId == ipcoreId && it.value().graphGroup == graphGroup) {
            return &it.value();
        }
    }
    return nullptr;
}

const IpcraftPackageManifest* ModuleRegistry::packageManifest(const QString& packageId) const {
    auto it = m_packageManifests.constFind(packageId);
    return it == m_packageManifests.cend() ? nullptr : &it.value();
}

QVector<IpcraftPackageManifest> ModuleRegistry::packageManifests() const {
    QVector<IpcraftPackageManifest> manifests;
    manifests.reserve(m_packageManifests.size());
    for (auto it = m_packageManifests.cbegin(); it != m_packageManifests.cend(); ++it) {
        manifests.push_back(it.value());
    }
    return manifests;
}

QStringList ModuleRegistry::availableTypes() const {
    QStringList types;
    for (auto it = m_types.begin(); it != m_types.end(); ++it) {
        types.append(it.key());
    }
    types.sort();
    return types;
}

QStringList ModuleRegistry::availableTypesForIpcore(const QString& ipcoreId) const {
    QStringList types;
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().ipcoreId == ipcoreId) {
            types.append(it.key());
        }
    }
    types.sort();
    return types;
}
