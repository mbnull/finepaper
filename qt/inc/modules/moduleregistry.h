// ModuleRegistry is a singleton that stores available module type definitions
#pragma once

#include "graph/port.h"
#include "graph/parameter.h"
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSize>
#include <QVector>
#include <optional>
#include <vector>
#include <memory>

class ModuleProvider;

struct ModuleParameterChoice {
    QString value;
    QString label;
};

struct ModuleParameterMetadata {
    QString name;
    QString label;
    QString description;
    QString unit;
    QVector<ModuleParameterChoice> choices;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
    bool configurable = true;
    bool readOnly = false;
};

struct ModuleConfigField {
    QString parameterName;
    QString label;
    QString description;
};

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    QHash<QString, Parameter> defaultParameters;
    QHash<QString, ModuleParameterMetadata> parameterMetadata;
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

    // Adds a provider and imports all types it exposes.
    void addProvider(std::unique_ptr<ModuleProvider> provider);
    // Inserts or replaces one module type definition in the registry.
    void registerType(const ModuleType& type);
    // Looks up type metadata by canonical type name.
    const ModuleType* getType(const QString& name) const;
    // Looks up the first type that belongs to a graph group (e.g., "xps", "endpoints").
    const ModuleType* getTypeForGraphGroup(const QString& graphGroup) const;
    // Returns type names sorted for stable UI listing.
    QStringList availableTypes() const;

private:
    ModuleRegistry();
    QHash<QString, ModuleType> m_types;
};
