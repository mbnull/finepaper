#include "application/mesh_resize_plan.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path,
                      const QString& source = QStringLiteral("finepaper")) {
    diagnostics.append(Diagnostic{severity, code, message, path, source});
}

bool projectableTopology(const TopologySpec& topology) {
    return topology.type == QStringLiteral("mesh")
        && topology.rows > 0
        && topology.columns > 0
        && topology.rows <= kMaximumMeshDimension
        && topology.columns <= kMaximumMeshDimension
        && static_cast<qint64>(topology.rows)
                * static_cast<qint64>(topology.columns)
            <= kMaximumProjectedRouterCount;
}

QStringList normalizedDomainIds(QStringList ids,
                                const QString& path,
                                QVector<Diagnostic>& diagnostics) {
    for (qsizetype index = 0; index < ids.size(); ++index) {
        ids[index] = ids.at(index).trimmed();
        if (ids.at(index).isEmpty()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("domain_assignment.empty_domain_id"),
                QStringLiteral("Domain assignment contains an empty Domain id"),
                QStringLiteral("%1/%2").arg(path).arg(index),
                QStringLiteral("package"));
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

template <typename Record, typename SameIdentity>
void validateExactImpactConfirmation(
    const QVector<Record>& expected,
    const QVector<Record>& confirmed,
    SameIdentity sameIdentity,
    const QString& path,
    const QString& recordDescription,
    const QString& missingCode,
    const QString& extraCode,
    const QString& staleCode,
    QVector<Diagnostic>& diagnostics) {
    QVector<bool> expectedAccounted(expected.size(), false);
    QVector<bool> confirmedMatched(confirmed.size(), false);

    // Match exact records first so a duplicate extra confirmation cannot hide
    // a valid confirmation for the same stable identity.
    for (qsizetype confirmedIndex = 0;
         confirmedIndex < confirmed.size(); ++confirmedIndex) {
        for (qsizetype expectedIndex = 0;
             expectedIndex < expected.size(); ++expectedIndex) {
            if (!expectedAccounted.at(expectedIndex)
                && confirmed.at(confirmedIndex) == expected.at(expectedIndex)) {
                expectedAccounted[expectedIndex] = true;
                confirmedMatched[confirmedIndex] = true;
                break;
            }
        }
    }

    for (qsizetype confirmedIndex = 0;
         confirmedIndex < confirmed.size(); ++confirmedIndex) {
        if (confirmedMatched.at(confirmedIndex)) {
            continue;
        }
        qsizetype staleExpected = -1;
        for (qsizetype expectedIndex = 0;
             expectedIndex < expected.size(); ++expectedIndex) {
            if (!expectedAccounted.at(expectedIndex)
                && sameIdentity(confirmed.at(confirmedIndex),
                                expected.at(expectedIndex))) {
                staleExpected = expectedIndex;
                break;
            }
        }
        if (staleExpected >= 0) {
            expectedAccounted[staleExpected] = true;
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                staleCode,
                QStringLiteral("confirmed %1 no longer exactly matches the resize preview")
                    .arg(recordDescription),
                QStringLiteral("%1/%2").arg(path).arg(confirmedIndex));
        } else {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                extraCode,
                QStringLiteral("confirmation contains an extra %1 not present in the resize preview")
                    .arg(recordDescription),
                QStringLiteral("%1/%2").arg(path).arg(confirmedIndex));
        }
    }

    for (qsizetype expectedIndex = 0;
         expectedIndex < expected.size(); ++expectedIndex) {
        if (expectedAccounted.at(expectedIndex)) {
            continue;
        }
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            missingCode,
            QStringLiteral("resize requires exact confirmation before removing %1")
                .arg(recordDescription),
            path);
    }
}

} // namespace

bool MeshResizeDomainAssignmentPlan::requiresExplicitChoice() const {
    return assignmentRule.isValid()
        && automaticAssignment.size() < assignmentRule.minimumAssignments;
}

bool MeshResizePlan::hasBlockingDiagnostics() const {
    return hasErrors(diagnostics);
}

bool MeshResizePlan::requiresExplicitAssignments() const {
    if (newRouters.isEmpty()) {
        return false;
    }
    return std::any_of(
        routerAssignmentPlans.cbegin(),
        routerAssignmentPlans.cend(),
        [](const MeshResizeDomainAssignmentPlan& assignment) {
            return assignment.requiresExplicitChoice();
        });
}

bool MeshResizePlan::requiresImpactConfirmation() const {
    return !removedMemberships.isEmpty() || !removedEdgeOverrides.isEmpty()
        || !removedElementConfigurations.isEmpty();
}

bool MeshResizePlan::canApplyWithoutAssignments() const {
    return !hasBlockingDiagnostics() && !requiresExplicitAssignments();
}

bool MeshResizeAssignmentResolution::success() const {
    return !hasErrors(diagnostics);
}

MeshResizePlan buildMeshResizePlan(const NocDesign& design,
                                   const PackageDefinition& package,
                                   int rows,
                                   int columns) {
    MeshResizePlan plan;
    plan.requestedTopology = TopologySpec{design.topology.type, rows, columns};

    for (const DomainDefinition& domain : design.domains) {
        plan.domains.append(MeshResizeDomainOption{
            domain.id,
            domain.type,
            domain.name.trimmed().isEmpty() ? domain.id : domain.name.trimmed()
        });
    }
    std::sort(
        plan.domains.begin(),
        plan.domains.end(),
        [](const MeshResizeDomainOption& lhs,
           const MeshResizeDomainOption& rhs) {
            if (lhs.type != rhs.type) {
                return lhs.type < rhs.type;
            }
            return lhs.id < rhs.id;
        });

    for (const DomainTypeDefinition& type : package.domainTypes) {
        plan.declaredDomainTypes.append(type.id);
        const std::optional<DomainAssignmentRule> rule =
            type.assignmentRule(ElementKind::Router);
        if (!rule) {
            continue;
        }
        MeshResizeDomainAssignmentPlan assignment;
        assignment.domainType = type.id;
        assignment.label = type.label.trimmed().isEmpty()
            ? type.id : type.label.trimmed();
        assignment.assignmentRule = *rule;
        for (const MeshResizeDomainOption& domain : std::as_const(plan.domains)) {
            if (domain.type == type.id) {
                assignment.availableDomainIds.append(domain.id);
            }
        }
        if (rule->requiresAssignment()
            && assignment.availableDomainIds.size()
                == rule->minimumAssignments) {
            assignment.automaticAssignment = assignment.availableDomainIds;
        }
        plan.routerAssignmentPlans.append(std::move(assignment));
    }
    std::sort(plan.declaredDomainTypes.begin(), plan.declaredDomainTypes.end());
    plan.declaredDomainTypes.erase(
        std::unique(plan.declaredDomainTypes.begin(),
                    plan.declaredDomainTypes.end()),
        plan.declaredDomainTypes.end());

    if (design.topology.type != QStringLiteral("mesh")) {
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("topology.unsupported_type"),
                         QStringLiteral("Finepaper currently supports topology.type=mesh"),
                         QStringLiteral("/topology/type"));
    }
    if (rows < 1 || columns < 1) {
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.invalid_size"),
                         QStringLiteral("rows and columns must be positive"),
                         QStringLiteral("/topology"));
    } else if (rows > kMaximumMeshDimension
               || columns > kMaximumMeshDimension
               || static_cast<qint64>(rows) * static_cast<qint64>(columns)
                   > kMaximumProjectedRouterCount) {
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("topology.projection_too_large"),
            QStringLiteral("topology exceeds Finepaper's safe projection limit"),
            QStringLiteral("/topology"));
    }
    if (rows < package.mesh.minimumRows || rows > package.mesh.maximumRows) {
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.rows_out_of_range"),
                         QStringLiteral("rows are outside the Package range"),
                         QStringLiteral("/topology/rows"),
                         QStringLiteral("package"));
    }
    if (columns < package.mesh.minimumColumns
        || columns > package.mesh.maximumColumns) {
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.columns_out_of_range"),
                         QStringLiteral("columns are outside the Package range"),
                         QStringLiteral("/topology/columns"),
                         QStringLiteral("package"));
    }
    if (!projectableTopology(plan.requestedTopology)) {
        return plan;
    }

    const TopologyProjection currentProjection = projectTopology(design);
    NocDesign resized = design;
    resized.topology = plan.requestedTopology;
    const TopologyProjection requestedProjection = projectTopology(resized);

    QSet<QString> currentRouterIds;
    QSet<QString> requestedRouterIds;
    for (const RouterView& router : currentProjection.routers) {
        currentRouterIds.insert(router.id);
    }
    for (const RouterView& router : requestedProjection.routers) {
        requestedRouterIds.insert(router.id);
        if (!currentRouterIds.contains(router.id)) {
            plan.newRouters.append(MeshResizeRouterPlan{
                ElementRef{ElementKind::Router, router.id},
                router.position
            });
        }
    }
    for (const RouterView& router : currentProjection.routers) {
        if (!requestedRouterIds.contains(router.id)) {
            plan.removedRouters.append(
                ElementRef{ElementKind::Router, router.id});
        }
    }

    QSet<QString> currentLinkIds;
    QSet<QString> requestedLinkIds;
    for (const LinkView& link : currentProjection.links) {
        currentLinkIds.insert(link.id);
    }
    for (const LinkView& link : requestedProjection.links) {
        requestedLinkIds.insert(link.id);
        if (!currentLinkIds.contains(link.id)) {
            plan.newRouterLinks.append(
                ElementRef{ElementKind::RouterLink, link.id});
        }
    }
    for (const LinkView& link : currentProjection.links) {
        if (!requestedLinkIds.contains(link.id)) {
            plan.removedRouterLinks.append(
                ElementRef{ElementKind::RouterLink, link.id});
        }
    }

    for (qsizetype index = 0; index < design.endpoints.size(); ++index) {
        const EndpointInstance& endpoint = design.endpoints.at(index);
        if (requestedRouterIds.contains(routerId(endpoint.attachment.router))) {
            continue;
        }
        plan.detachedEndpoints.append(
            ElementRef{ElementKind::Endpoint, endpoint.id});
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("mesh.resize_would_detach_endpoint"),
            QStringLiteral("resize would detach Endpoint %1").arg(endpoint.id),
            QStringLiteral("/endpoints/%1/attachment/router").arg(index));
    }

    for (qsizetype index = 0; index < design.domainMemberships.size(); ++index) {
        const DomainMembership& membership = design.domainMemberships.at(index);
        if (membership.element.kind != ElementKind::Router
            || requestedRouterIds.contains(membership.element.id)) {
            continue;
        }
        plan.removedMemberships.append(membership);
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("warning"),
            QStringLiteral("mesh.resize_would_remove_domain_membership"),
            QStringLiteral("resize will remove Router %1 with Domain assignments")
                .arg(membership.element.id),
            QStringLiteral("/domainMemberships/%1/element").arg(index));
    }

    for (qsizetype index = 0; index < design.edgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride = design.edgeOverrides.at(index);
        if (edgeOverride.edge.kind != ElementKind::RouterLink
            || requestedLinkIds.contains(edgeOverride.edge.id)) {
            continue;
        }
        plan.removedEdgeOverrides.append(edgeOverride);
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("warning"),
            QStringLiteral("mesh.resize_would_remove_edge_override"),
            QStringLiteral("resize will remove Router link %1 with a Domain override")
                .arg(edgeOverride.edge.id),
            QStringLiteral("/edgeOverrides/%1/edge").arg(index));
    }

    for (qsizetype index = 0;
         index < design.elementConfigurations.size(); ++index) {
        const ElementConfiguration& configuration =
            design.elementConfigurations.at(index);
        const bool removedRouter =
            configuration.element.kind == ElementKind::Router
            && !requestedRouterIds.contains(configuration.element.id);
        const bool removedRouterLink =
            configuration.element.kind == ElementKind::RouterLink
            && !requestedLinkIds.contains(configuration.element.id);
        if (!removedRouter && !removedRouterLink) {
            continue;
        }
        plan.removedElementConfigurations.append(configuration);
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("warning"),
            QStringLiteral("mesh.resize_would_remove_element_configuration"),
            QStringLiteral(
                "resize will remove %1 %2 configuration %3")
                .arg(elementKindId(configuration.element.kind),
                     configuration.element.id,
                     configuration.propertySet),
            QStringLiteral("/elementConfigurations/%1/element").arg(index));
    }

    return plan;
}

MeshResizeAssignmentResolution resolveMeshResizeAssignments(
    const MeshResizePlan& plan,
    const QVector<DomainMembership>& providedMemberships,
    const MeshResizeImpactConfirmation& confirmation) {
    MeshResizeAssignmentResolution resolution;
    resolution.diagnostics = plan.diagnostics;

    validateExactImpactConfirmation(
        plan.removedMemberships,
        confirmation.removedMemberships,
        [](const DomainMembership& lhs, const DomainMembership& rhs) {
            return lhs.element == rhs.element;
        },
        QStringLiteral("/impactConfirmation/removedMemberships"),
        QStringLiteral("Router Domain membership"),
        QStringLiteral("mesh.resize_missing_membership_confirmation"),
        QStringLiteral("mesh.resize_extra_membership_confirmation"),
        QStringLiteral("mesh.resize_stale_membership_confirmation"),
        resolution.diagnostics);
    validateExactImpactConfirmation(
        plan.removedEdgeOverrides,
        confirmation.removedEdgeOverrides,
        [](const DomainEdgeOverride& lhs, const DomainEdgeOverride& rhs) {
            return lhs.edge == rhs.edge && lhs.domainType == rhs.domainType;
        },
        QStringLiteral("/impactConfirmation/removedEdgeOverrides"),
        QStringLiteral("Router-link Domain override"),
        QStringLiteral("mesh.resize_missing_override_confirmation"),
        QStringLiteral("mesh.resize_extra_override_confirmation"),
        QStringLiteral("mesh.resize_stale_override_confirmation"),
        resolution.diagnostics);
    validateExactImpactConfirmation(
        plan.removedElementConfigurations,
        confirmation.removedElementConfigurations,
        [](const ElementConfiguration& lhs,
           const ElementConfiguration& rhs) {
            return lhs.element == rhs.element
                && lhs.propertySet == rhs.propertySet;
        },
        QStringLiteral("/impactConfirmation/removedElementConfigurations"),
        QStringLiteral("element configuration"),
        QStringLiteral("mesh.resize_missing_element_configuration_confirmation"),
        QStringLiteral("mesh.resize_extra_element_configuration_confirmation"),
        QStringLiteral("mesh.resize_stale_element_configuration_confirmation"),
        resolution.diagnostics);

    QHash<QString, const MeshResizeRouterPlan*> newRouters;
    for (const MeshResizeRouterPlan& router : plan.newRouters) {
        newRouters.insert(router.element.id, &router);
    }
    QHash<QString, const MeshResizeDomainAssignmentPlan*> assignmentPlans;
    for (const MeshResizeDomainAssignmentPlan& assignment
         : plan.routerAssignmentPlans) {
        assignmentPlans.insert(assignment.domainType, &assignment);
    }
    const QSet<QString> declaredTypes(plan.declaredDomainTypes.cbegin(),
                                      plan.declaredDomainTypes.cend());
    QHash<QString, const MeshResizeDomainOption*> domainsById;
    for (const MeshResizeDomainOption& domain : plan.domains) {
        if (!domainsById.contains(domain.id)) {
            domainsById.insert(domain.id, &domain);
        }
    }

    QHash<QString, DomainMembership> providedByRouter;
    for (qsizetype index = 0; index < providedMemberships.size(); ++index) {
        DomainMembership membership = providedMemberships.at(index);
        const QString path = QStringLiteral("/newRouterMemberships/%1").arg(index);
        if (membership.element.kind != ElementKind::Router) {
            appendDiagnostic(
                resolution.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("domain_assignment.invalid_element_kind"),
                QStringLiteral("new Router membership must reference a Router"),
                path + QStringLiteral("/element/kind"));
            continue;
        }
        if (!newRouters.contains(membership.element.id)) {
            appendDiagnostic(
                resolution.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("domain_assignment.not_new_router"),
                QStringLiteral("membership must reference a Router created by this resize"),
                path + QStringLiteral("/element/id"));
            continue;
        }
        if (providedByRouter.contains(membership.element.id)) {
            appendDiagnostic(resolution.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_assignment.duplicate_element"),
                             QStringLiteral("new Router membership is duplicated"),
                             path + QStringLiteral("/element"));
            continue;
        }

        QHash<QString, QStringList> normalizedAssignments;
        for (auto assignment = membership.assignments.constBegin();
             assignment != membership.assignments.constEnd(); ++assignment) {
            const QString assignmentPath = path + QStringLiteral("/assignments/")
                + assignment.key();
            const auto assignmentPlan = assignmentPlans.constFind(assignment.key());
            if (assignmentPlan == assignmentPlans.constEnd()) {
                appendDiagnostic(
                    resolution.diagnostics,
                    QStringLiteral("error"),
                    declaredTypes.contains(assignment.key())
                        ? QStringLiteral("domain_assignment.not_applicable")
                        : QStringLiteral("domain_assignment.unknown_type"),
                    declaredTypes.contains(assignment.key())
                        ? QStringLiteral("Domain type %1 does not apply to Routers")
                              .arg(assignment.key())
                        : QStringLiteral("Domain assignment type is not declared by the Package"),
                    assignmentPath,
                    QStringLiteral("package"));
                continue;
            }

            QStringList ids = normalizedDomainIds(
                assignment.value(), assignmentPath, resolution.diagnostics);
            for (qsizetype assignmentIndex = 0;
                 assignmentIndex < ids.size(); ++assignmentIndex) {
                const QString& id = ids.at(assignmentIndex);
                if (id.isEmpty()) {
                    continue;
                }
                const auto domain = domainsById.constFind(id);
                if (domain == domainsById.constEnd()) {
                    appendDiagnostic(
                        resolution.diagnostics,
                        QStringLiteral("error"),
                        QStringLiteral("domain_assignment.unknown_domain"),
                        QStringLiteral("Domain assignment references an unknown Domain"),
                        QStringLiteral("%1/%2")
                            .arg(assignmentPath)
                            .arg(assignmentIndex),
                        QStringLiteral("package"));
                } else if ((*domain)->type != assignment.key()) {
                    appendDiagnostic(
                        resolution.diagnostics,
                        QStringLiteral("error"),
                        QStringLiteral("domain_assignment.domain_type_mismatch"),
                        QStringLiteral("Domain assignment references a Domain of another type"),
                        QStringLiteral("%1/%2")
                            .arg(assignmentPath)
                            .arg(assignmentIndex),
                        QStringLiteral("package"));
                }
            }
            const DomainAssignmentRule& rule =
                (*assignmentPlan)->assignmentRule;
            if (rule.maximumAssignments
                && ids.size() > *rule.maximumAssignments) {
                const QString message = *rule.maximumAssignments == 1
                    ? QStringLiteral("Domain type %1 allows only one assignment")
                          .arg(assignment.key())
                    : QStringLiteral("Domain type %1 allows at most %2 assignments")
                          .arg(assignment.key())
                          .arg(*rule.maximumAssignments);
                appendDiagnostic(
                    resolution.diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("domain_assignment.cardinality"),
                    message,
                    assignmentPath,
                    QStringLiteral("package"));
            }
            ids.removeAll(QString());
            if (!ids.isEmpty()) {
                normalizedAssignments.insert(assignment.key(), std::move(ids));
            }
        }
        membership.assignments = std::move(normalizedAssignments);
        providedByRouter.insert(membership.element.id, std::move(membership));
    }

    for (const MeshResizeRouterPlan& router : plan.newRouters) {
        DomainMembership membership = providedByRouter.value(
            router.element.id, DomainMembership{router.element, {}});
        for (const MeshResizeDomainAssignmentPlan& assignment
             : plan.routerAssignmentPlans) {
            const qsizetype assignmentCount =
                membership.assignments.value(assignment.domainType).size();
            if (!assignment.assignmentRule.requiresAssignment()
                || assignmentCount
                    >= assignment.assignmentRule.minimumAssignments) {
                continue;
            }
            if (!assignment.automaticAssignment.isEmpty()) {
                QStringList completed =
                    membership.assignments.value(assignment.domainType);
                for (const QString& domainId : assignment.automaticAssignment) {
                    if (!completed.contains(domainId)) {
                        completed.append(domainId);
                    }
                }
                std::sort(completed.begin(), completed.end());
                if (completed.size()
                    >= assignment.assignmentRule.minimumAssignments) {
                    membership.assignments.insert(
                        assignment.domainType, std::move(completed));
                    continue;
                }
            }
            appendDiagnostic(
                resolution.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("domain_assignment.required_choice"),
                QStringLiteral("Element %1 requires an explicit %2 Domain choice; %3 instances are available")
                    .arg(router.element.id, assignment.domainType)
                    .arg(assignment.availableDomainIds.size()),
                QStringLiteral("/newRouterMemberships/%1/assignments/%2")
                    .arg(router.element.id, assignment.domainType),
                QStringLiteral("package"));
        }
        if (!membership.assignments.isEmpty()) {
            resolution.newRouterMemberships.append(std::move(membership));
        }
    }

    if (!resolution.success()) {
        resolution.newRouterMemberships.clear();
    }
    return resolution;
}

} // namespace finepaper
