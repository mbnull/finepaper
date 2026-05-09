// ModuleRegistry bootstraps, indexes, and serves module type metadata to the editor.
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "plugins/pluginregistry.h"
#include <QDebug>
#include <QFileInfo>

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

// Load module definitions from startup-discovered plugins only.
ModuleRegistry::ModuleRegistry(LoadMode loadMode) {
    if (loadMode == LoadMode::Empty) {
        return;
    }

    if (loadPlugins(PluginRegistry::instance().plugins())) {
        return;
    }

    qWarning() << "No runtime-owned module definitions found.";
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
                   << "from runtime bundle" << type.pluginId;
        return false;
    }
    m_types[type.name] = type;
    return true;
}

bool ModuleRegistry::loadPlugins(const QList<PluginDescriptor>& plugins) {
    bool loadedAnyType = false;

    for (const PluginDescriptor& plugin : plugins) {
        if (!plugin.hasModules() || !QFileInfo(plugin.modulesPath).isFile()) {
            continue;
        }

        std::unique_ptr<LayeredModuleProvider> provider;
        if (plugin.modulesPath.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
            provider = std::make_unique<LayeredModuleProvider>(
                std::make_unique<XmlModuleTypeSource>(plugin.modulesPath));
            if (!plugin.graphicsPath.isEmpty() && QFileInfo(plugin.graphicsPath).isDir()) {
                provider->addOverlay(std::make_unique<XmlModuleGraphicsOverlay>(plugin.graphicsPath));
            }
        } else {
            qWarning() << "Skipping plugin with unsupported module bundle" << plugin.id << plugin.modulesPath;
            continue;
        }

        auto types = provider->loadModules();
        for (ModuleType& type : types) {
            type.pluginId = plugin.id;
            loadedAnyType = registerType(type) || loadedAnyType;
        }
    }

    return loadedAnyType;
}

const ModuleType* ModuleRegistry::getType(const QString& name) const {
    auto it = m_types.find(name);
    return it != m_types.end() ? &it.value() : nullptr;
}

const ModuleType* ModuleRegistry::getTypeForGraphGroup(const QString& graphGroup) const {
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().graphGroup == graphGroup) {
            return &it.value();
        }
    }
    return nullptr;
}

const ModuleType* ModuleRegistry::getTypeForGraphGroup(const QString& pluginId,
                                                       const QString& graphGroup) const {
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().pluginId == pluginId && it.value().graphGroup == graphGroup) {
            return &it.value();
        }
    }
    return nullptr;
}

QStringList ModuleRegistry::availableTypes() const {
    QStringList types;
    for (auto it = m_types.begin(); it != m_types.end(); ++it) {
        types.append(it.key());
    }
    types.sort();
    return types;
}

QStringList ModuleRegistry::availableTypesForPlugin(const QString& pluginId) const {
    QStringList types;
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it) {
        if (it.value().pluginId == pluginId) {
            types.append(it.key());
        }
    }
    types.sort();
    return types;
}
