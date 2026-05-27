#pragma once

#include "ipcraft/diagnostics.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace ipcraft {

struct PackageEndpointMatch {
    QString kind;
    QString protocol;
    QString role;
    QString direction;
};

struct PackageCompatibilityRule {
    QString connectionType;
    PackageEndpointMatch from;
    PackageEndpointMatch to;
    QString arity;
    QJsonObject metadata;
};

struct PackageConnectionRules {
    QHash<QString, QString> protocolAliases;
    QHash<QString, QString> kindAliases;
    QVector<PackageCompatibilityRule> compatibility;
    QJsonObject metadata;
    QJsonObject native;
};

struct PackageInterfaceSpec {
    QString id;
    QString name;
    QString label;
    QString kind;
    QString protocol;
    QString role;
    QString direction;
    bool required = false;
    QString fanout;
    QJsonObject properties;
    QJsonObject metadata;
    QJsonObject native;
};

struct PackageSpec {
    QString schema;
    QString id;
    QString version;
    QString name;
    QString packageRootPath;
    QJsonObject display;
    QStringList extensions;
    QJsonObject extensionPayloads;
    QJsonObject configSchema;
    QVector<PackageInterfaceSpec> interfaces;
    PackageConnectionRules connectionRules;
    QJsonArray emitters;
    QJsonArray flows;
    QJsonArray artifacts;
    QJsonObject diagnostics;
    QJsonArray views;
    QJsonObject graphConfig;
    bool hasPlugin = false;
    QJsonObject plugin;
    QJsonObject nativeSchema;
    QJsonObject metadata;
    QJsonObject native;

    bool hasExtension(const QString& extensionId) const;
};

struct PackageSpecReadResult {
    bool ok = false;
    PackageSpec spec;
    DiagnosticStore diagnostics;
};

struct PackageSpecCollectionResult {
    QVector<PackageSpec> packages;
    DiagnosticStore diagnostics;
};

struct PackageSpecResolveResult {
    bool ok = false;
    PackageSpec spec;
    DiagnosticStore diagnostics;
};

class PackageSpecReader {
public:
    PackageSpecReadResult readPackageRoot(const QString& packageRootPath) const;
    PackageSpecReadResult readSpecFile(const QString& specPath) const;
    PackageSpecCollectionResult discoverPackageRoots(const QStringList& rootPaths) const;
};

PackageSpecResolveResult resolvePackageSpec(const QVector<PackageSpec>& packages,
                                            const QString& packageId,
                                            const QString& version);

} // namespace ipcraft
