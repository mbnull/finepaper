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
    QString bundlePath;

    // Check environment variable first
    bundlePath = qEnvironmentVariable("BUNDLE_PATH");
    if (!bundlePath.isEmpty()) {
        QFileInfo info(bundlePath);
        if (info.isDir()) {
            bundlePath = QDir(bundlePath).filePath("bundles/modules.json");
        }
        if (QFile::exists(bundlePath)) {
            addProvider(std::make_unique<JsonBundleProvider>(bundlePath));
            return;
        }
    }

    // Search upward from application directory
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        QString candidate = dir.filePath("bundles/modules.json");
        if (QFile::exists(candidate)) {
            addProvider(std::make_unique<JsonBundleProvider>(candidate));
            return;
        }

        if (!dir.cdUp()) {
            qWarning() << "Bundle file not found. Searched from:" << QCoreApplication::applicationDirPath();
            return;
        }
    }
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
