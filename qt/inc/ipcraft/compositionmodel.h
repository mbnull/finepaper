#pragma once

#include "ipcraft/diagnostics.h"
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ipcraft {

struct CompositionEndpointRef {
    QString instanceId;
    QString interfaceId;
    QString portId;
    QString role;
    QJsonObject properties;

    QJsonObject toJson() const;
    static CompositionEndpointRef fromJson(const QJsonObject& object);
};

struct SystemConnection {
    QString id;
    QString type;
    QVector<CompositionEndpointRef> endpoints;
    QString source = QStringLiteral("user");
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static SystemConnection fromJson(const QJsonObject& object);
};

struct ExternalPort {
    QString id;
    QString name;
    bool hasInterface = false;
    CompositionEndpointRef interfaceRef;
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static ExternalPort fromJson(const QJsonObject& object);
};

struct CompositionModel {
    QVector<SystemConnection> connections;
    QVector<ExternalPort> externalPorts;
    QJsonArray groups;
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static CompositionModel fromJson(const QJsonObject& object);
};

struct CompositionInstance {
    QString instanceId;
    PackageSpec package;
};

struct CompositionValidationResult {
    bool ok = false;
    DiagnosticStore diagnostics;
};

CompositionValidationResult validateCompositionModel(
    const CompositionModel& model,
    const QVector<CompositionInstance>& instances);

struct GraphConfigObject {
    QString id;
    QString type;
    QJsonObject properties;

    QJsonObject toJson() const;
    static GraphConfigObject fromJson(const QJsonObject& object);
};

struct GraphConfigEndpoint {
    QString objectId;
    QString role;
    QJsonObject properties;

    QJsonObject toJson() const;
    static GraphConfigEndpoint fromJson(const QJsonObject& object);
};

struct GraphConfigRelationship {
    QString id;
    QString type;
    QVector<GraphConfigEndpoint> endpoints;
    QJsonObject properties;

    QJsonObject toJson() const;
    static GraphConfigRelationship fromJson(const QJsonObject& object);
};

struct GraphConfigReadResult;

struct GraphConfig {
    QString schema = schemaids::graphConfigV1;
    QVector<GraphConfigObject> objects;
    QVector<GraphConfigRelationship> relationships;
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static GraphConfigReadResult fromJson(const QJsonObject& object);
};

struct GraphConfigReadResult {
    bool ok = false;
    GraphConfig config;
    DiagnosticStore diagnostics;
};

DiagnosticStore validateGraphConfig(const GraphConfig& graphConfig);

} // namespace ipcraft
