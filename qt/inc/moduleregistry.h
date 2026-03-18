#pragma once

#include "port.h"
#include "parameter.h"
#include <QString>
#include <QStringList>
#include <vector>
#include <unordered_map>

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    std::unordered_map<QString, Parameter> defaultParameters;
};

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void registerType(const ModuleType& type);
    const ModuleType* getType(const QString& name) const;
    QStringList availableTypes() const;

private:
    ModuleRegistry();
    std::unordered_map<QString, ModuleType> m_types;
};
