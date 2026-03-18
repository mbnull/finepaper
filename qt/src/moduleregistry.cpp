#include "moduleregistry.h"
#include "moduleprovider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

ModuleRegistry::ModuleRegistry() {
    QString bundlePath = QDir(QCoreApplication::applicationDirPath()).filePath("../bundles/modules.json");
    bundlePath = QDir::cleanPath(bundlePath);

    if (!QFile::exists(bundlePath)) {
        qWarning() << "Bundle file not found:" << bundlePath;
        return;
    }

    addProvider(std::make_unique<JsonBundleProvider>(bundlePath));
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
