// ModuleRegistry is a singleton that stores available module type definitions
#pragma once

#include "port.h"
#include "parameter.h"
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSize>
#include <QVector>
#include <vector>
#include <memory>

class ModuleProvider;

struct ModuleConfigField {
    QString parameterName;
    QString label;
    QString description;
};

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    QHash<QString, Parameter> defaultParameters;
    QString paletteLabel;
    QString description;
    QString nodeColor;
    QString editorLayout;
    QString graphGroup;
    int expandedNodeMinWidth = 104;
    int expandedNodeHeight = 54;
    int collapsedNodeMinWidth = 104;
    int collapsedNodeHeight = 54;
    qreal expandedCaptionLeftInset = 8.0;
    qreal expandedCaptionTopInset = 6.0;
    qreal collapsedCaptionLeftInset = 8.0;
    qreal collapsedCaptionTopInset = 6.0;
    qreal expandedPortInset = 16.0;
    qreal collapsedEndpointPortInset = 18.0;
    int linkedEndpointOffsetX = 156;
    int meshSpacingX = 220;
    int meshSpacingY = 168;
    int looseEndpointSpacingX = 168;
    int looseEndpointSpacingY = 84;
    int looseEndpointMarginY = 116;
    QVector<ModuleConfigField> configFields;
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
