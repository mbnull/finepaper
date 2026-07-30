#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QStringList>
#include <QVector>

namespace finepaper {

// Read-only, Package-driven information used to preview a Mesh resize before
// any NocDesign state is changed.  It deliberately does not infer assignments
// from neighbouring Routers: only a required type with exactly one available
// Domain instance receives an automatic assignment.
struct MeshResizeDomainOption {
    QString id;
    QString type;
    QString name;

    bool operator==(const MeshResizeDomainOption&) const = default;
};

struct MeshResizeDomainAssignmentPlan {
    QString domainType;
    QString label;
    DomainCardinality cardinality = DomainCardinality::Invalid;
    bool required = false;
    QStringList availableDomainIds;
    QStringList automaticAssignment;

    bool requiresExplicitChoice() const;

    bool operator==(const MeshResizeDomainAssignmentPlan&) const = default;
};

struct MeshResizeRouterPlan {
    ElementRef element;
    RouterPosition position;

    bool operator==(const MeshResizeRouterPlan&) const = default;
};

// A resize that removes Routers or Router links also removes state whose
// identity is tied to those projected elements.  The caller must echo the
// exact records from MeshResizePlan; a broad allow-data-loss flag would be
// unable to detect a stale preview or an unrelated extra deletion request.
struct MeshResizeImpactConfirmation {
    QVector<DomainMembership> removedMemberships;
    QVector<DomainEdgeOverride> removedEdgeOverrides;

    bool operator==(const MeshResizeImpactConfirmation&) const = default;
};

struct MeshResizePlan {
    TopologySpec requestedTopology;
    QVector<MeshResizeRouterPlan> newRouters;
    QVector<ElementRef> newRouterLinks;
    QVector<ElementRef> removedRouters;
    QVector<ElementRef> removedRouterLinks;

    // Explicit blocker summaries complement diagnostics so a GUI can present
    // the affected intent without decoding diagnostic strings.
    QVector<ElementRef> detachedEndpoints;
    QVector<DomainMembership> removedMemberships;
    QVector<DomainEdgeOverride> removedEdgeOverrides;

    QVector<MeshResizeDomainOption> domains;
    QStringList declaredDomainTypes;
    QVector<MeshResizeDomainAssignmentPlan> routerAssignmentPlans;
    QVector<Diagnostic> diagnostics;

    bool hasBlockingDiagnostics() const;
    bool requiresExplicitAssignments() const;
    bool requiresImpactConfirmation() const;
    bool canApplyWithoutAssignments() const;
};

struct MeshResizeAssignmentResolution {
    QVector<DomainMembership> newRouterMemberships;
    QVector<Diagnostic> diagnostics;

    bool success() const;
};

MeshResizePlan buildMeshResizePlan(const NocDesign& design,
                                   const PackageDefinition& package,
                                   int rows,
                                   int columns);

// Validates and normalizes the caller's per-new-Router assignments, fills only
// unambiguous required assignments, and returns no memberships on any error.
MeshResizeAssignmentResolution resolveMeshResizeAssignments(
    const MeshResizePlan& plan,
    const QVector<DomainMembership>& providedMemberships = {},
    const MeshResizeImpactConfirmation& confirmation = {});

} // namespace finepaper
