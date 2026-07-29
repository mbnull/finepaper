#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <optional>

namespace finepaper {

// Operational safety limits for projections loaded from untrusted or damaged
// design files. Runtime Packages may impose much smaller product limits.
inline constexpr int kMaximumMeshDimension = 4096;
inline constexpr qint64 kMaximumProjectedRouterCount = 1'000'000;

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

struct NocDesign {
    QString format = QStringLiteral("finepaper.noc-design");
    int formatVersion = 1;
    QString id;
    QString name;
    PackageReference package;
    TopologySpec topology;
    QJsonObject parameters;
    QVector<EndpointInstance> endpoints;
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
