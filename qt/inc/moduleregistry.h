// ModuleRegistry is a singleton that stores available module type definitions
#pragma once

#include "port.h"
#include "parameter.h"
#include <QString>
#include <QStringList>
#include <QHash>
#include <vector>
#include <memory>

class ModuleProvider;

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    QHash<QString, Parameter> defaultParameters;
};

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void addProvider(std::unique_ptr<ModuleProvider> provider);
    void registerType(const ModuleType& type);
    const ModuleType* getType(const QString& name) const;
    QStringList availableTypes() const;

private:
    ModuleRegistry();
    QHash<QString, ModuleType> m_types;
};
