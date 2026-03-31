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
    QString paletteLabel;
    QString nodeColor;
    QString editorLayout;
    QString graphGroup;
    QString externalIdPrefix;
    QString displayPrefix;
    int identityWidth = 2;
    bool supportsCollapse = false;
    bool supportsMeshCoordinates = false;
};

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void addProvider(std::unique_ptr<ModuleProvider> provider);
    void registerType(const ModuleType& type);
    const ModuleType* getType(const QString& name) const;
    const ModuleType* getTypeForGraphGroup(const QString& graphGroup) const;
    QStringList availableTypes() const;

private:
    ModuleRegistry();
    QHash<QString, ModuleType> m_types;
};
