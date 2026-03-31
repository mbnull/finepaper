// ModuleProvider interface for loading module type definitions
#pragma once

#include "moduleregistry.h"
#include <vector>

class ModuleProvider {
public:
    virtual ~ModuleProvider() = default;
    virtual std::vector<ModuleType> loadModules() = 0;
};

// JsonBundleProvider loads module types from JSON file
class JsonBundleProvider : public ModuleProvider {
public:
    explicit JsonBundleProvider(const QString& bundlePath);
    std::vector<ModuleType> loadModules() override;
private:
    QString m_bundlePath;
};
