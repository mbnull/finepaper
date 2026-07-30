#include "gui/domain_manager_projection.h"

#include <QSet>

#include <algorithm>
#include <iterator>
#include <utility>

namespace finepaper {
namespace {

QStringList normalizedDomainIds(QStringList ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool appliesTo(const DomainTypeDefinition& type, ElementKind kind) {
    return std::find(type.appliesTo.cbegin(), type.appliesTo.cend(), kind)
        != type.appliesTo.cend();
}

QHash<ElementRef, QStringList> assignmentsByElement(
    const NocDesign& design,
    const QString& domainType) {
    QHash<ElementRef, QStringList> assignments;
    for (const DomainMembership& membership : design.domainMemberships) {
        const auto assignment = membership.assignments.constFind(domainType);
        if (assignment == membership.assignments.constEnd()) {
            continue;
        }
        assignments[membership.element].append(assignment.value());
    }
    for (auto iterator = assignments.begin(); iterator != assignments.end(); ++iterator) {
        iterator.value() = normalizedDomainIds(std::move(iterator.value()));
    }
    return assignments;
}

QStringList setIntersection(const QStringList& lhs, const QStringList& rhs) {
    QStringList intersection;
    std::set_intersection(lhs.cbegin(), lhs.cend(),
                          rhs.cbegin(), rhs.cend(),
                          std::back_inserter(intersection));
    return intersection;
}

} // namespace

DomainAssignmentPresence DomainAssignmentAggregate::presence(
    const QString& domainId) const {
    return presenceByDomain.value(domainId, DomainAssignmentPresence::None);
}

DomainAssignmentAggregate buildDomainAssignmentAggregate(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& selectionRefs,
    const QString& domainType) {
    DomainAssignmentAggregate aggregate;
    aggregate.domainType = domainType.trimmed();

    QSet<ElementRef> seenReferences;
    QVector<ElementRef> normalizedSelection;
    normalizedSelection.reserve(selectionRefs.size());
    for (const ElementRef& reference : selectionRefs) {
        if (seenReferences.contains(reference)) {
            continue;
        }
        seenReferences.insert(reference);
        normalizedSelection.append(reference);
    }
    aggregate.totalElements = normalizedSelection.size();

    const DomainTypeDefinition* type = package.domainType(aggregate.domainType);
    if (!type) {
        return aggregate;
    }
    aggregate.domainTypeLabel = type->label.isEmpty()
        ? aggregate.domainType : type->label;
    aggregate.cardinality = type->cardinality;
    aggregate.required = type->required;

    QSet<QString> seenDomainIds;
    for (const DomainDefinition& domain : design.domains) {
        if (domain.type != aggregate.domainType
            || seenDomainIds.contains(domain.id)) {
            continue;
        }
        seenDomainIds.insert(domain.id);
        aggregate.domainIds.append(domain.id);
    }
    std::sort(aggregate.domainIds.begin(), aggregate.domainIds.end());
    for (const QString& domainId : std::as_const(aggregate.domainIds)) {
        aggregate.presenceByDomain.insert(
            domainId, DomainAssignmentPresence::None);
    }

    for (const ElementRef& reference : std::as_const(normalizedSelection)) {
        if (!isDomainMembershipElementKind(reference.kind)
            || !appliesTo(*type, reference.kind)
            || !designReferenceExists(design, reference)) {
            continue;
        }
        aggregate.eligibleElementRefs.append(reference);
    }
    aggregate.eligibleElements = aggregate.eligibleElementRefs.size();
    if (aggregate.eligibleElements == 0) {
        aggregate.state = DomainAssignmentAggregateState::NoEligible;
        return aggregate;
    }

    const QHash<ElementRef, QStringList> elementAssignments =
        assignmentsByElement(design, aggregate.domainType);
    QHash<QString, qsizetype> assignmentCounts;
    QStringList firstAssignments;
    bool first = true;
    bool allAssignmentsEqual = true;
    bool allAssignmentsEmpty = true;

    for (const ElementRef& reference : std::as_const(aggregate.eligibleElementRefs)) {
        const QStringList assignments = elementAssignments.value(reference);
        if (first) {
            firstAssignments = assignments;
            aggregate.commonAssignments = assignments;
            first = false;
        } else {
            allAssignmentsEqual = allAssignmentsEqual
                && assignments == firstAssignments;
            aggregate.commonAssignments = setIntersection(
                aggregate.commonAssignments, assignments);
        }
        allAssignmentsEmpty = allAssignmentsEmpty && assignments.isEmpty();
        for (const QString& domainId : assignments) {
            ++assignmentCounts[domainId];
        }
    }

    for (const QString& domainId : std::as_const(aggregate.domainIds)) {
        const qsizetype count = assignmentCounts.value(domainId);
        DomainAssignmentPresence presence = DomainAssignmentPresence::Some;
        if (count == 0) {
            presence = DomainAssignmentPresence::None;
        } else if (count == aggregate.eligibleElements) {
            presence = DomainAssignmentPresence::All;
        }
        aggregate.presenceByDomain.insert(domainId, presence);
    }

    if (allAssignmentsEmpty) {
        aggregate.state = DomainAssignmentAggregateState::Unassigned;
    } else if (allAssignmentsEqual) {
        aggregate.state = DomainAssignmentAggregateState::Common;
    } else {
        aggregate.state = DomainAssignmentAggregateState::Mixed;
    }
    return aggregate;
}

QVector<ElementRef> buildDomainAssignmentSelection(
    const ResolvedDesign& resolved,
    const DomainTypeDefinition& type,
    DomainAssignmentSelectionScope scope,
    const QString& domainId) {
    QVector<ElementRef> candidates;
    if (appliesTo(type, ElementKind::Router)) {
        candidates.reserve(resolved.topology.routers.size()
                           + resolved.topology.endpoints.size());
        for (const RouterView& router : resolved.topology.routers) {
            candidates.append(ElementRef{ElementKind::Router, router.id});
        }
    }
    if (appliesTo(type, ElementKind::Endpoint)) {
        for (const EndpointView& endpoint : resolved.topology.endpoints) {
            candidates.append(ElementRef{ElementKind::Endpoint, endpoint.id});
        }
    }
    if (scope == DomainAssignmentSelectionScope::AllEligible) {
        return candidates;
    }

    const QString requestedDomainId = domainId.trimmed();
    if (scope == DomainAssignmentSelectionScope::AssignedToDomain
        && requestedDomainId.isEmpty()) {
        return {};
    }
    const QHash<ElementRef, QStringList> elementAssignments =
        assignmentsByElement(resolved.design, type.id);
    QVector<ElementRef> filtered;
    filtered.reserve(candidates.size());
    for (const ElementRef& reference : std::as_const(candidates)) {
        const QStringList assignments = elementAssignments.value(reference);
        if ((scope == DomainAssignmentSelectionScope::Unassigned
             && assignments.isEmpty())
            || (scope == DomainAssignmentSelectionScope::AssignedToDomain
                && assignments.contains(requestedDomainId))) {
            filtered.append(reference);
        }
    }
    return filtered;
}

} // namespace finepaper
