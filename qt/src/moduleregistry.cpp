#include "moduleregistry.h"
#include "moduleprovider.h"

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

ModuleRegistry::ModuleRegistry() {
    addProvider(std::make_unique<JsonBundleProvider>("bundles/modules.json"));
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
    return it != m_types.end() ? &it->second : nullptr;
}

QStringList ModuleRegistry::availableTypes() const {
    QStringList types;
    for (const auto& pair : m_types) {
        types.append(pair.first);
    }
    types.sort();
    return types;
}
