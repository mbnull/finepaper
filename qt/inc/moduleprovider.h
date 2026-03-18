#pragma once

#include "moduleregistry.h"
#include <vector>

class ModuleProvider {
public:
    virtual ~ModuleProvider() = default;
    virtual std::vector<ModuleType> loadModules() = 0;
};

class HardcodedProvider : public ModuleProvider {
public:
    std::vector<ModuleType> loadModules() override;
};
