// ModuleRegistry is a singleton that stores available module type definitions
#pragma once

#include "graph/port.h"
#include "graph/parameter.h"
#include "ipcraft/ipcraftmanifest.h"
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
struct IpCoreRuntimeDescriptor;

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
    QString label;
    QString bus;
    QString role;
    QStringList compatibleRoles;
    QStringList matchFields;
    QHash<QString, QStringList> acceptedValues;
    QHash<QString, QString> parameterBindings;
    QString cardinality = QStringLiteral("one");
    QString autocompleteGroup;
    QString topologyRule;
    QVector<IpcraftInterfaceAcceptRule> acceptRules;
};

struct ModuleInterfaceAnchor {
    QString interfaceId;
    double x = 0.0;
    double y = 0.0;
    std::optional<double> normalX;
    std::optional<double> normalY;
    QString label;
    std::optional<double> labelX;
    std::optional<double> labelY;
};

struct ModuleAttachmentZone {
    QString id;
    double x = 0.0;
    double y = 0.0;
    std::optional<double> normalX;
    std::optional<double> normalY;
    QString label;
    std::optional<bool> mirrorAttachedNode;
};

struct ModuleType {
    QString name;
    std::vector<Port> defaultPorts;
    QHash<QString, Parameter> defaultParameters;
    QHash<QString, ModuleParameterMetadata> parameterMetadata;
    QString packageId;
    QString moduleId;
    QString graphRole;
    QStringList attachHostModuleIds;
    QString attachZoneId;
    QString viewFilePath;
    QString ipcoreId;
    QHash<QString, ModuleInterfaceMetadata> interfaceMetadata;
    QString paletteLabel;
    QString description;
    QString displayLabelParameter;
    QString shortLabelParameter;
    QString nodeColor;
    QString editorLayout;
    QString graphGroup;
    QHash<QString, ModuleInterfaceAnchor> interfaceAnchors;
    QHash<QString, ModuleAttachmentZone> attachmentZones;
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
    static QString scopedTypeName(const QString& packageId, const QString& moduleId);

    explicit ModuleRegistry(LoadMode loadMode = LoadMode::Auto);

    // Adds a provider and imports all types it exposes.
    void addProvider(std::unique_ptr<ModuleProvider> provider);
    // Inserts one module type definition; duplicate type names are skipped.
    bool registerType(const ModuleType& type);
    // Imports module types from IP core runtime manifests.
    bool loadIpCoreRuntimes(const QList<IpCoreRuntimeDescriptor>& runtimes);
    // Imports module types from Ipcraft package manifests.
    bool loadIpcraftPackages(const QVector<IpcraftPackageManifest>& packages);
    // Looks up type metadata by canonical type name.
    const ModuleType* getType(const QString& name) const;
    // Looks up type metadata by package and manifest module ID.
    const ModuleType* getType(const QString& packageId, const QString& moduleId) const;
    // Looks up the first type that belongs to a graph group (e.g., "xps", "endpoints").
    const ModuleType* getTypeForGraphGroup(const QString& graphGroup) const;
    // Looks up the first type in a graph group owned by one IP core.
    const ModuleType* getTypeForGraphGroup(const QString& ipcoreId, const QString& graphGroup) const;
    // Looks up a loaded Ipcraft package manifest by package ID.
    const IpcraftPackageManifest* packageManifest(const QString& packageId) const;
    // Returns the loaded Ipcraft package manifests.
    QVector<IpcraftPackageManifest> packageManifests() const;
    // Returns type names sorted for stable UI listing.
    QStringList availableTypes() const;
    // Returns type names owned by one IP core, sorted for stable UI listing.
    QStringList availableTypesForIpcore(const QString& ipcoreId) const;

private:
    QHash<QString, ModuleType> m_types;
    QHash<QString, IpcraftPackageManifest> m_packageManifests;
};
