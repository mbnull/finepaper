#pragma once

#include "application/domain_assignment.h"
#include "application/mesh_resize_plan.h"
#include "noc/model.h"
#include "package/package.h"

#include <QHash>
#include <QStringList>
#include <QVector>

namespace finepaper::domain_service {

struct MutationResult {
    NocDesign design;
    QVector<Diagnostic> diagnostics;
};

bool canProjectTopology(const TopologySpec& topology);

MutationResult materializeRequiredDomains(
    const NocDesign& design,
    const PackageDefinition& package);
MutationResult resizeMesh(
    const NocDesign& design,
    const PackageDefinition& package,
    int rows,
    int columns,
    const QVector<DomainMembership>& newRouterMemberships,
    const MeshResizeImpactConfirmation& confirmation = {});
MutationResult addEndpoint(
    const NocDesign& design,
    const PackageDefinition& package,
    EndpointInstance endpoint,
    const QHash<QString, QStringList>& domainAssignments);
NocDesign removeEndpointReferences(const NocDesign& design,
                                   const QString& endpointId);

MutationResult addDomain(const NocDesign& design,
                         const PackageDefinition& package,
                         DomainDefinition domain);
MutationResult updateDomain(const NocDesign& design,
                            const QString& domainId,
                            DomainDefinition domain);
MutationResult removeDomain(const NocDesign& design,
                            const PackageDefinition& package,
                            const QString& domainId);
MutationResult assignDomainsToElements(
    const NocDesign& design,
    const QVector<ElementRef>& elements,
    const QString& domainType,
    const QStringList& domainIds);
MutationResult patchDomainAssignments(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& elements,
    const QString& domainType,
    DomainAssignmentPatch patch);
MutationResult clearDomainAssignment(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& elements,
    const QString& domainType);

QVector<Diagnostic> validateAgainstPackage(
    const NocDesign& design,
    const PackageDefinition& package);

} // namespace finepaper::domain_service
