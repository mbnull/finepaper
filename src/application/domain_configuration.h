#pragma once

#include "noc/model.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace finepaper {

struct DomainConfiguration {
    QVector<DomainDefinition> domains;
    QVector<DomainMembership> domainMemberships;
    QVector<DomainRelation> domainRelations;
    QVector<DomainCrossingPolicy> crossingPolicies;
    QVector<DomainEdgeOverride> edgeOverrides;

    bool operator==(const DomainConfiguration&) const = default;
};

namespace domain_configuration {

struct ParseResult {
    bool success = false;
    DomainConfiguration configuration;
    QVector<Diagnostic> diagnostics;
};

DomainConfiguration fromDesign(const NocDesign& design);
QJsonObject toJson(const DomainConfiguration& configuration);
NocDesign replace(const NocDesign& design, DomainConfiguration configuration);
ParseResult parse(const QJsonObject& object,
                  const NocDesign& baseDesign,
                  const QString& basePath = QStringLiteral("/domainConfiguration"));
bool ownsDiagnostic(const Diagnostic& diagnostic);

} // namespace domain_configuration
} // namespace finepaper
