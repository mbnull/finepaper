// ModuleProvider interface for loading module type definitions
#pragma once

#include "moduleregistry.h"
#include <vector>

class ModuleProvider {
public:
    virtual ~ModuleProvider() = default;
    virtual std::vector<ModuleType> loadModules() = 0;
};

// BundleProvider loads runtime metadata from JSON and editor metadata from XML.
class BundleProvider : public ModuleProvider {
public:
    BundleProvider(const QString& bundlePath, const QString& presentationPath = {});
    std::vector<ModuleType> loadModules() override;
private:
    QString m_bundlePath;
    QString m_presentationPath;
};
