#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>
#include <utility>

namespace finepaper {

// Operational safety limits for projections loaded from untrusted or damaged
// design files. Runtime Packages may impose much smaller product limits.
inline constexpr int kMaximumMeshDimension = 4096;
inline constexpr qint64 kMaximumProjectedRouterCount = 1'000'000;
inline constexpr int kMinimumDesignFormatVersion = 1;
inline constexpr int kMaximumDesignFormatVersion = 3;

inline constexpr bool formatVersionSupportsDomains(int formatVersion) noexcept {
    return formatVersion >= 2;
}

inline constexpr bool formatVersionSupportsElementConfigurations(
    int formatVersion) noexcept {
    return formatVersion >= 3;
}

struct Diagnostic {
    QString severity;
    QString code;
    QString message;
    QString path;
    QString source = QStringLiteral("finepaper");
};

using ValidationCancellationCheck = std::function<bool()>;

bool hasErrors(const QVector<Diagnostic>& diagnostics);

struct PackageReference {
    QString id;
    QString version;
};

struct RouterPosition {
    int x = 0;
    int y = 0;

    bool operator==(const RouterPosition&) const = default;
};

struct TopologySpec {
    QString type = QStringLiteral("mesh");
    int rows = 1;
    int columns = 1;
};

struct EndpointAttachment {
    RouterPosition router;
    std::optional<QString> slot = std::nullopt;
};

struct EndpointInstance {
    QString id;
    QString type;
    EndpointAttachment attachment;
    QJsonObject parameters;
};

struct DomainDefinition {
    QString id;
    QString type;
    QString name;
    QJsonObject properties;

    bool operator==(const DomainDefinition&) const = default;
};

enum class ElementKind {
    Invalid,
    Router,
    Endpoint,
    RouterLink,
    EndpointAttachment
};

QString elementKindId(ElementKind kind);
ElementKind elementKindFromId(const QString& id);
bool isDomainMembershipElementKind(ElementKind kind);
bool isDomainCrossingEdgeKind(ElementKind kind);
bool isElementConfigurationTargetKind(ElementKind kind);

struct ElementRef {
    ElementKind kind = ElementKind::Invalid;
    QString id;

    bool operator==(const ElementRef&) const = default;
};

size_t qHash(const ElementRef& reference, size_t seed = 0) noexcept;

struct DomainMembership {
    ElementRef element;
    QHash<QString, QStringList> assignments;

    bool operator==(const DomainMembership&) const = default;
};

struct DomainRelation {
    QString type;
    QString from;
    QString to;
    QJsonObject properties;

    bool operator==(const DomainRelation&) const = default;
};

struct DomainCrossingPolicy {
    QString id;
    QString domainType;
    // from/to orient one complete bidirectional physical boundary according
    // to edgeEndpoints(); they are not individual traffic-channel directions.
    QString from;
    QString to;
    QJsonObject properties;

    bool operator==(const DomainCrossingPolicy&) const = default;
};

struct DomainEdgeOverride {
    ElementRef edge;
    QString domainType;
    QString policy;
    QJsonObject properties;

    bool operator==(const DomainEdgeOverride&) const = default;
};

struct ElementConfiguration {
    ElementRef element;
    QString propertySet;
    QJsonObject properties;

    bool operator==(const ElementConfiguration&) const = default;
};

struct NocDesign {
    QString format = QStringLiteral("finepaper.noc-design");
    int formatVersion = 1;
    QString id;
    QString name;
    PackageReference package;
    TopologySpec topology;
    QJsonObject parameters;
    QVector<EndpointInstance> endpoints;
    QVector<DomainDefinition> domains;
    QVector<DomainMembership> domainMemberships;
    QVector<DomainRelation> domainRelations;
    QVector<DomainCrossingPolicy> crossingPolicies;
    QVector<DomainEdgeOverride> edgeOverrides;
    QVector<ElementConfiguration> elementConfigurations;
    QJsonObject packageData;
};

struct RouterView {
    QString id;
    RouterPosition position;
};

struct LinkView {
    QString id;
    QString fromRouter;
    QString toRouter;
};

struct EndpointView {
    QString id;
    QString type;
    RouterPosition router;
    QString routerId;
    QString slot;
};

struct TopologyProjection {
    QVector<RouterView> routers;
    QVector<LinkView> links;
    QVector<EndpointView> endpoints;
};

struct DomainCrossingView {
    ElementRef edge;
    // Stable canonical orientation of the bidirectional physical edge.
    ElementRef fromElement;
    ElementRef toElement;
    QString domainType;
    QStringList fromDomains;
    QStringList toDomains;
    std::optional<QString> defaultPolicy = std::nullopt;
    QJsonObject defaultProperties;
    std::optional<QString> overridePolicy = std::nullopt;
    QJsonObject overrideProperties;

    bool operator==(const DomainCrossingView&) const = default;
};

struct ResolvedDesign {
    NocDesign design;
    TopologyProjection topology;
    QVector<DomainCrossingView> domainCrossings;
};

QString routerId(RouterPosition position);
QString linkId(const QString& fromRouter, const QString& toRouter);
std::optional<RouterPosition> routerPositionFromId(const QString& id);
bool designReferenceExists(const NocDesign& design, const ElementRef& reference);
std::optional<std::pair<ElementRef, ElementRef>> edgeEndpoints(
    const NocDesign& design,
    const ElementRef& edge);
NocDesign withResolvedAutomaticSlots(
    const NocDesign& design,
    const ValidationCancellationCheck& cancellationRequested = {});
TopologyProjection projectTopology(
    const NocDesign& design,
    const ValidationCancellationCheck& cancellationRequested = {});
QVector<DomainCrossingView> projectDomainCrossings(
    const NocDesign& design,
    const ValidationCancellationCheck& cancellationRequested = {});
ResolvedDesign resolveDesign(const NocDesign& design);
QVector<Diagnostic> validateDesignStructure(
    const NocDesign& design,
    const ValidationCancellationCheck& cancellationRequested = {});

} // namespace finepaper
