#include "application/domain_runtime_validation.h"

namespace finepaper::domain_runtime_validation {
namespace {

void appendUnsupportedPlane(QVector<Diagnostic>& diagnostics,
                            bool populated,
                            bool consumed,
                            const QString& plane,
                            const QString& path) {
    if (!populated || consumed) {
        return;
    }
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        QStringLiteral("runtime.domain_%1_not_consumed").arg(plane),
        QStringLiteral(
            "Package runtime explicitly declares that it does not consume "
            "Domain %1; validation and generation are fail-closed")
            .arg(plane),
        path,
        QStringLiteral("package")
    });
}

} // namespace

QVector<Diagnostic> validateConsumption(const NocDesign& design,
                                        const PackageDefinition& package) {
    QVector<Diagnostic> diagnostics;
    const bool hasDomainData = !design.domains.isEmpty()
        || !design.domainMemberships.isEmpty()
        || !design.domainRelations.isEmpty()
        || !design.crossingPolicies.isEmpty()
        || !design.edgeOverrides.isEmpty();
    if (!hasDomainData) {
        return diagnostics;
    }

    const auto& declared = package.runtimeCapabilities.domainConfiguration;
    if (!declared) {
        diagnostics.append(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("runtime.domain_capabilities_missing"),
            QStringLiteral(
                "Package runtime does not declare how Domain configuration "
                "is consumed; validation and generation are fail-closed"),
            QStringLiteral("/runtimeCapabilities/domainConfiguration"),
            QStringLiteral("package")
        });
        return diagnostics;
    }

    appendUnsupportedPlane(
        diagnostics,
        !design.domains.isEmpty(),
        declared->domains,
        QStringLiteral("domains"),
        QStringLiteral("/domains"));
    appendUnsupportedPlane(
        diagnostics,
        !design.domainMemberships.isEmpty(),
        declared->memberships,
        QStringLiteral("memberships"),
        QStringLiteral("/domainMemberships"));
    appendUnsupportedPlane(
        diagnostics,
        !design.domainRelations.isEmpty(),
        declared->relations,
        QStringLiteral("relations"),
        QStringLiteral("/domainRelations"));
    appendUnsupportedPlane(
        diagnostics,
        !design.crossingPolicies.isEmpty(),
        declared->crossingPolicies,
        QStringLiteral("crossing_policies"),
        QStringLiteral("/crossingPolicies"));
    appendUnsupportedPlane(
        diagnostics,
        !design.edgeOverrides.isEmpty(),
        declared->edgeOverrides,
        QStringLiteral("edge_overrides"),
        QStringLiteral("/edgeOverrides"));
    return diagnostics;
}

} // namespace finepaper::domain_runtime_validation
