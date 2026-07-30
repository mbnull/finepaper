#pragma once

#include "noc/model.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

inline constexpr int kMaximumPackageTimeoutSeconds = 86'400;

enum class ParameterType {
    Invalid,
    Integer,
    Number,
    Boolean,
    String,
    Enumeration
};

enum class AttachmentSlotMode {
    Invalid,
    Automatic,
    Explicit
};

enum class DomainCardinality {
    Invalid,
    Single,
    Multiple
};

struct ParameterDefinition {
    QString id;
    ParameterType type = ParameterType::Invalid;
    QString label;
    bool hasDefault = false;
    QJsonValue defaultValue;
    std::optional<double> minimum;
    std::optional<double> maximum;
    QStringList values;
};

struct EndpointTypeDefinition {
    QString id;
    QString label;
    QString icon;
    QVector<ParameterDefinition> parameters;
};

struct DomainPropertyDefinition : ParameterDefinition {
    bool required = false;
    bool multiple = false;
    std::optional<QString> referenceDomainType;
};

struct DomainRelationDefinition {
    QString id;
    QString label;
    QStringList targetTypes;
    DomainCardinality cardinality = DomainCardinality::Single;
    bool required = false;
    QVector<DomainPropertyDefinition> properties;
};

// Canonical Package V2 scaffold used only when createDesign receives no explicit
// Domain configuration. The parser resolves the legacy convention into this
// structure for every required Domain Type, so Application code never derives
// instance identity or values itself. Optional Domain Types have no scaffold.
struct DomainDefaultInstanceDefinition {
    QString id;
    QString name;
    QJsonObject properties;
};

struct DomainTypeDefinition {
    QString id;
    QString label;
    QVector<ElementKind> appliesTo;
    DomainCardinality cardinality = DomainCardinality::Single;
    bool required = false;
    std::optional<DomainDefaultInstanceDefinition> defaultInstance;
    QVector<DomainPropertyDefinition> properties;
    QVector<DomainRelationDefinition> relations;
    QVector<DomainPropertyDefinition> crossingProperties;
};

struct AttachmentSlotDefinition {
    QString id;
    QString label;
};

struct MeshDefinition {
    int minimumRows = 1;
    int maximumRows = 1;
    int defaultRows = 1;
    int minimumColumns = 1;
    int maximumColumns = 1;
    int defaultColumns = 1;
};

struct AttachmentDefinition {
    int maxPerRouter = 1;
    AttachmentSlotMode slotMode = AttachmentSlotMode::Automatic;
    QVector<AttachmentSlotDefinition> positions;
};

struct GeneratorDefinition {
    QString name;
    QString version;
    QString executable;
    bool supportsValidate = false;
    int timeoutSeconds = 300;
};

struct EngineDefinition {
    QString executable;
    bool providesValidation = false;
    int timeoutSeconds = 1800;
};

struct PackageDefinition {
    QString format;
    int formatVersion = 0;
    QString id;
    QString name;
    QString version;
    QString rootPath;
    MeshDefinition mesh;
    QVector<ParameterDefinition> parameters;
    QVector<EndpointTypeDefinition> endpointTypes;
    QVector<DomainTypeDefinition> domainTypes;
    AttachmentDefinition attachment;
    GeneratorDefinition generator;
    std::optional<EngineDefinition> engine;

    QString key() const;
    const ParameterDefinition* parameter(const QString& id) const;
    const EndpointTypeDefinition* endpointType(const QString& id) const;
    const DomainTypeDefinition* domainType(const QString& id) const;
};

struct PackageLoadResult {
    bool success = false;
    std::optional<PackageDefinition> package;
    QVector<Diagnostic> diagnostics;
};

PackageLoadResult loadPackage(const QString& packageRoot);
QVector<Diagnostic> validateParameterObject(
    const QJsonObject& values,
    const QVector<ParameterDefinition>& definitions,
    const QString& basePath,
    const QString& source);

class PackageCatalog {
public:
    QVector<Diagnostic> reload(const QStringList& roots);
    const QVector<PackageDefinition>& packages() const;
    std::optional<PackageDefinition> resolve(const PackageReference& reference) const;

private:
    QVector<PackageDefinition> m_packages;
};

} // namespace finepaper
