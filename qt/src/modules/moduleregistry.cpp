// ModuleRegistry bootstraps, indexes, and serves module type metadata to the editor.
#include "common/frameworkpaths.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include <QDebug>

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

// Search for module bundle files in environment or parent directories.
ModuleRegistry::ModuleRegistry() {
    const QString bundlePath = FrameworkPaths::resolveModuleBundlePath();
    if (!bundlePath.isEmpty()) {
        std::unique_ptr<LayeredModuleProvider> provider;
        if (bundlePath.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
            provider = std::make_unique<LayeredModuleProvider>(
                std::make_unique<XmlModuleTypeSource>(bundlePath));
            const QString graphicsDirectory = FrameworkPaths::resolveModuleGraphicsDirectory();
            if (!graphicsDirectory.isEmpty()) {
                provider->addOverlay(std::make_unique<XmlModuleGraphicsOverlay>(graphicsDirectory));
            }
        } else {
            provider = std::make_unique<LayeredModuleProvider>(
                std::make_unique<JsonModuleTypeSource>(bundlePath));
            provider->addOverlay(std::make_unique<XmlModulePresentationOverlay>(
                FrameworkPaths::resolveModulePresentationPath()));
        }
        addProvider(std::move(provider));
        return;
    }

    qWarning() << "Bundle file not found. Searched current repo and framework bundle locations.";
}

void ModuleRegistry::addProvider(std::unique_ptr<ModuleProvider> provider) {
    auto types = provider->loadModules();
    for (const auto& type : types) {
        registerType(type);
    }
}

void ModuleRegistry::registerType(const ModuleType& type) {
    m_types[type.name] = type;
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

QStringList ModuleRegistry::availableTypes() const {
    QStringList types;
    for (auto it = m_types.begin(); it != m_types.end(); ++it) {
        types.append(it.key());
    }
    types.sort();
    return types;
}
