// ModuleRegistry is a singleton that stores available module type definitions
#pragma once

#include "graph/port.h"
#include "graph/parameter.h"
#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QSize>
#include <QVector>
#include <optional>
#include <vector>
#include <memory>

class ModuleProvider;
struct PluginDescriptor;

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

struct ModuleInterfaceMetadata {
    QString id;
    QString bus;
    QString role;
    QStringList compatibleRoles;
    QStringList matchFields;
    QHash<QString, QStringList> acceptedValues;
    QHash<QString, QString> parameterBindings;
};

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    QHash<QString, Parameter> defaultParameters;
    QHash<QString, ModuleParameterMetadata> parameterMetadata;
    QString pluginId;
    QHash<QString, ModuleInterfaceMetadata> interfaceMetadata;
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
    enum class LoadMode {
        Auto,
        Empty
    };

    static ModuleRegistry& instance();

    explicit ModuleRegistry(LoadMode loadMode = LoadMode::Auto);

    // Adds a provider and imports all types it exposes.
    void addProvider(std::unique_ptr<ModuleProvider> provider);
    // Inserts one module type definition; duplicate type names are skipped.
    bool registerType(const ModuleType& type);
    // Imports module types from plugin manifests.
    bool loadPlugins(const QList<PluginDescriptor>& plugins);
    // Looks up type metadata by canonical type name.
    const ModuleType* getType(const QString& name) const;
    // Looks up the first type that belongs to a graph group (e.g., "xps", "endpoints").
    const ModuleType* getTypeForGraphGroup(const QString& graphGroup) const;
    // Returns type names sorted for stable UI listing.
    QStringList availableTypes() const;

private:
    QHash<QString, ModuleType> m_types;
};
