#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace finepaper {

// Endpoint parameters are materialized effective values, unlike sparse
// elementConfigurations.  A type change must therefore state how the complete
// target parameter object is produced before an optional sparse patch is
// applied.
enum class EndpointParameterMigration {
    ResetToDefaults,
    PreserveCompatible
};

// Attachment property sets may be filtered by Endpoint type.  Echoing the
// exact records from the preview prevents a stale dialog or a broad
// "allow data loss" flag from deleting unrelated state.
struct EndpointTypeChangeImpactConfirmation {
    QVector<ElementConfiguration> removedAttachmentConfigurations;

    bool operator==(const EndpointTypeChangeImpactConfirmation&) const = default;
};

struct EndpointTypeChangePlan {
    QString endpointId;
    QString currentType;
    QString targetType;
    EndpointParameterMigration parameterMigration =
        EndpointParameterMigration::ResetToDefaults;
    QJsonObject parameters;
    QVector<ElementConfiguration> removedAttachmentConfigurations;
    // Domain applicability is currently based on ElementKind, not Endpoint
    // type.  Exposing the retained records makes that lifecycle rule explicit
    // to callers and leaves the final full-design validator as the authority.
    QVector<DomainMembership> retainedDomainMemberships;
    QVector<Diagnostic> diagnostics;

    [[nodiscard]] bool canApply() const;
    [[nodiscard]] bool requiresImpactConfirmation() const;
};

namespace endpoint_configuration {

struct MutationResult {
    NocDesign design;
    QVector<Diagnostic> diagnostics;
};

EndpointTypeChangePlan buildTypeChangePlan(
    const NocDesign& design,
    const PackageDefinition& package,
    const QString& endpointId,
    const QString& targetType,
    EndpointParameterMigration migration,
    const QJsonObject& parameterPatch = {});

MutationResult applyTypeChange(
    const NocDesign& design,
    const EndpointTypeChangePlan& plan,
    const EndpointTypeChangeImpactConfirmation& confirmation = {});

} // namespace endpoint_configuration
} // namespace finepaper
