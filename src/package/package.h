#pragma once

#include "noc/model.h"
#include "package/domain/assignment_rule.h"
#include "schema/json_schema.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>

namespace finepaper {

inline constexpr int kMaximumPackageTimeoutSeconds = 86'400;
inline constexpr int kMaximumEndpointAttachmentsPerRouter = 4'096;
inline constexpr int kMaximumDesignExtensionsPerPackage = 64;
inline constexpr int kMaximumDesignExtensionDomainReferences = 64;
inline constexpr int
    kMaximumDesignExtensionDomainReferencePointerCharacters = 4'096;
inline constexpr int kMaximumDesignExtensionDomainReferencePointerTokens = 64;
inline constexpr int kMaximumDesignExtensionSchemaBytes = 1 * 1024 * 1024;
inline constexpr int kMaximumDesignExtensionSchemaTotalBytes = 8 * 1024 * 1024;
inline constexpr int kMinimumPackageFormatVersion = 1;
inline constexpr int kMaximumPackageFormatVersion = 3;

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
    // Optional Package-owned presentation metadata. These fields do not alter
    // parameter semantics; generic editors may use them to explain and group
    // parameters without recognizing Package-specific ids.
    QString description;
    QString unit;
    QString category;
    bool advanced = false;
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

// Package V3 properties for sparse per-element overrides. A property always
// declares a default; when multiple is true both defaults and overrides use an
// array whose items have the declared scalar type.
struct ElementPropertyDefinition : ParameterDefinition {
    bool multiple = false;
};

struct ElementPropertySetDefinition {
    QString id;
    QString label;
    QVector<ElementKind> appliesTo;
    // Optional filter for EndpointAttachment targets. An empty list means all
    // Endpoint types and is also the representation used when no filter was
    // declared.
    QStringList endpointTypes;
    QVector<ElementPropertyDefinition> properties;

    const ElementPropertyDefinition* property(const QString& id) const;
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
    // V2/V3 source fields remain available during the compatibility window.
    // Package parsing also materializes assignmentRules, which is the
    // canonical semantic view consumed by Application and UI code.
    QVector<ElementKind> appliesTo;
    DomainCardinality cardinality = DomainCardinality::Single;
    bool required = false;
    QVector<DomainAssignmentRule> assignmentRules;
    std::optional<DomainDefaultInstanceDefinition> defaultInstance;
    QVector<DomainPropertyDefinition> properties;
    QVector<DomainRelationDefinition> relations;
    QVector<DomainPropertyDefinition> crossingProperties;

    [[nodiscard]] std::optional<DomainAssignmentRule> assignmentRule(
        ElementKind elementKind) const;
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

// Older explicit-slot Packages omitted `slots` and used the numeric range
// 0..maxPerRouter-1. Keep that compatibility rule at the Package boundary so
// validation, GUI projection, and runtime consumers share one catalog.
[[nodiscard]] QVector<AttachmentSlotDefinition>
effectiveExplicitAttachmentSlots(const AttachmentDefinition& definition);

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

// Package V2+ runtimes declare Domain consumption per normalized Design data
// plane. A true value is an explicit promise that the Package execution
// pipeline validates and materializes that data in a declared output or
// implementation stage; false is an explicit lack of support. This promise
// does not by itself claim that a primary RTL artifact already implements the
// resulting constraints. Keeping the fields separate prevents partial
// implementations from being hidden behind a single "supports domains"
// switch.
struct DomainConfigurationRuntimeCapabilities {
    bool domains = false;
    bool memberships = false;
    bool relations = false;
    bool crossingPolicies = false;
    bool edgeOverrides = false;
};

struct RuntimeCapabilitiesDefinition {
    std::optional<DomainConfigurationRuntimeCapabilities> domainConfiguration;
};

// A Package-owned editor capability. Core preserves future capability ids so a
// client that does not implement them can fail closed instead of guessing an
// editor from the extension namespace.
struct DesignExtensionEditorDefinition {
    QString kind;
};

// A Package-declared reference from one or more locations in an extension
// value to Design Domains of a specific type. A token equal to `*` is a
// single-array-level wildcard. The source pointer is reconstructed from the
// sole canonical token representation so traversal and diagnostics cannot
// disagree.
struct DesignExtensionDomainReferenceDefinition {
    QStringList pointerTokens;
    QString domainType;

    [[nodiscard]] QString pointer() const {
        QString value;
        for (QString token : pointerTokens) {
            token.replace(QLatin1Char('~'), QStringLiteral("~0"));
            token.replace(QLatin1Char('/'), QStringLiteral("~1"));
            value += QLatin1Char('/');
            value += token;
        }
        return value;
    }

    bool operator==(const DesignExtensionDomainReferenceDefinition&) const = default;
};

// A Package-owned namespace in NocDesign::packageData. The schema document is
// loaded at the Package boundary after path containment, size, JSON-root, and
// local-reference checks. Generic consumers may validate the namespace against
// it without recognizing Package-specific ids or fields.
struct DesignExtensionDefinition {
    QString id;
    QString schema;
    QJsonObject schemaDocument;
    json_schema::CompileStatus schemaStatus =
        json_schema::CompileStatus::Invalid;
    std::shared_ptr<const json_schema::CompiledSchema> compiledSchema;
    QVector<json_schema::Issue> schemaIssues;
    int version = 0;
    std::optional<DesignExtensionEditorDefinition> editor = std::nullopt;
    QVector<DesignExtensionDomainReferenceDefinition> domainReferences;
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
    QVector<ElementPropertySetDefinition> elementPropertySets;
    AttachmentDefinition attachment;
    GeneratorDefinition generator;
    std::optional<EngineDefinition> engine;
    RuntimeCapabilitiesDefinition runtimeCapabilities;
    QVector<DesignExtensionDefinition> designExtensions;
    // Presence is semantically relevant for V1/V2 Engine compatibility: an
    // explicit empty declaration opts into strict namespace validation.
    bool designExtensionsDeclared = false;

    QString key() const;
    const ParameterDefinition* parameter(const QString& id) const;
    const EndpointTypeDefinition* endpointType(const QString& id) const;
    const DomainTypeDefinition* domainType(const QString& id) const;
    const ElementPropertySetDefinition* elementPropertySet(const QString& id) const;
    const DesignExtensionDefinition* designExtension(const QString& id) const;
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

} // namespace finepaper
