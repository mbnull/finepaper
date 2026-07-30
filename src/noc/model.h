#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

// Operational safety limits for projections loaded from untrusted or damaged
// design files. Runtime Packages may impose much smaller product limits.
inline constexpr int kMaximumMeshDimension = 4096;
inline constexpr qint64 kMaximumProjectedRouterCount = 1'000'000;
inline constexpr int kMinimumDesignFormatVersion = 1;
inline constexpr int kMaximumDesignFormatVersion = 2;

struct Diagnostic {
    QString severity;
    QString code;
    QString message;
    QString path;
    QString source = QStringLiteral("finepaper");
};

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
    std::optional<QString> slot;
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

struct ElementRef {
    ElementKind kind = ElementKind::Invalid;
    QString id;

    bool operator==(const ElementRef&) const = default;
};

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

QString routerId(RouterPosition position);
QString linkId(const QString& fromRouter, const QString& toRouter);
TopologyProjection projectTopology(const NocDesign& design);
NocDesign withResolvedAutomaticSlots(const NocDesign& design);
QVector<Diagnostic> validateDesignStructure(const NocDesign& design);

} // namespace finepaper
