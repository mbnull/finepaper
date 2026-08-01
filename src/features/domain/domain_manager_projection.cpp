#include "features/domain/domain_manager_projection.h"

#include <QSet>

#include <algorithm>
#include <iterator>
#include <utility>

namespace finepaper {
namespace {

QStringList normalizedDomainIds(QStringList ids) {
    QStringList normalized;
    normalized.reserve(ids.size());
    for (const QString& id : std::as_const(ids)) {
        const QString value = id.trimmed();
        if (!value.isEmpty()) {
            normalized.append(value);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(
        std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

QSet<QString> normalizedDomainIdSet(const QStringList& ids) {
    QSet<QString> normalized;
    for (const QString& id : ids) {
        const QString value = id.trimmed();
        if (!value.isEmpty()) {
            normalized.insert(value);
        }
    }
    return normalized;
}

const DomainAssignmentRule* ruleForKind(
    const QVector<DomainAssignmentRule>& rules,
    ElementKind kind) {
    const auto rule = std::find_if(
        rules.cbegin(), rules.cend(), [kind](const auto& rule) {
            return rule.elementKind == kind;
        });
    return rule == rules.cend() ? nullptr : &*rule;
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

const QVector<DomainAssignmentRule>&
DomainAssignmentAggregate::editingRules() const {
    return eligibleRules.isEmpty() ? applicableRules : eligibleRules;
}

bool DomainAssignmentAggregate::usesSingleAssignmentEditor() const {
    const QVector<DomainAssignmentRule>& rules = editingRules();
    return !rules.isEmpty()
        && std::all_of(rules.cbegin(), rules.cend(), [](const auto& rule) {
               return rule.isSingleAssignment();
           });
}

bool DomainAssignmentAggregate::requiresAssignment() const {
    const QVector<DomainAssignmentRule>& rules = editingRules();
    return std::any_of(rules.cbegin(), rules.cend(), [](const auto& rule) {
        return rule.requiresAssignment();
    });
}

bool DomainAssignmentAggregate::acceptsReplacementCount(
    qsizetype count) const {
    return !eligibleRules.isEmpty()
        && std::all_of(
            eligibleRules.cbegin(), eligibleRules.cend(),
            [count](const auto& rule) {
                return rule.acceptsCount(count);
            });
}

DomainAssignmentPatchEvaluation DomainAssignmentAggregate::evaluatePatch(
    const DomainAssignmentPatch& patch) const {
    DomainAssignmentPatchEvaluation evaluation;
    if (eligibleElementRefs.isEmpty()
        || (patch.replacement
            && (!patch.ensurePresent.isEmpty()
                || !patch.ensureAbsent.isEmpty()))) {
        return evaluation;
    }

    const QSet<QString> ensurePresent =
        normalizedDomainIdSet(patch.ensurePresent);
    const QSet<QString> ensureAbsent =
        normalizedDomainIdSet(patch.ensureAbsent);
    for (const QString& domainId : ensurePresent) {
        if (ensureAbsent.contains(domainId)) {
            return evaluation;
        }
    }

    if (patch.replacement) {
        const qsizetype replacementCount =
            normalizedDomainIdSet(*patch.replacement).size();
        for (const DomainAssignmentRule& rule : eligibleRules) {
            if (rule.acceptsCount(replacementCount)) {
                continue;
            }
            const auto reference = std::find_if(
                eligibleElementRefs.cbegin(), eligibleElementRefs.cend(),
                [&](const ElementRef& candidate) {
                    return candidate.kind == rule.elementKind;
                });
            if (reference == eligibleElementRefs.cend()) {
                return evaluation;
            }
            evaluation.violatingElement = *reference;
            evaluation.violatedRule = rule;
            evaluation.resultingAssignmentCount = replacementCount;
            return evaluation;
        }
        evaluation.accepted = !eligibleRules.isEmpty();
        return evaluation;
    }

    for (const ElementRef& reference : eligibleElementRefs) {
        const DomainAssignmentRule* rule = ruleForKind(
            eligibleRules, reference.kind);
        const auto current = assignmentsByEligibleElement.constFind(reference);
        if (!rule) {
            return evaluation;
        }

        const QStringList* currentAssignments =
            current == assignmentsByEligibleElement.constEnd()
            ? nullptr : &current.value();
        qsizetype resultingCount = currentAssignments
            ? currentAssignments->size() : 0;
        for (const QString& domainId : ensureAbsent) {
            if (currentAssignments
                && currentAssignments->contains(domainId)) {
                --resultingCount;
            }
        }
        for (const QString& domainId : ensurePresent) {
            if (!currentAssignments
                || !currentAssignments->contains(domainId)) {
                ++resultingCount;
            }
        }
        if (!rule->acceptsCount(resultingCount)) {
            evaluation.violatingElement = reference;
            evaluation.violatedRule = *rule;
            evaluation.resultingAssignmentCount = resultingCount;
            return evaluation;
        }
    }
    evaluation.accepted = true;
    return evaluation;
}

bool DomainAssignmentAggregate::acceptsPatch(
    const DomainAssignmentPatch& patch) const {
    return evaluatePatch(patch).accepted;
}

bool DomainAssignmentAggregate::permitsClearing() const {
    return acceptsReplacementCount(0);
}

bool DomainAssignmentAggregate::assignmentRulesAreValid() const {
    const QVector<DomainAssignmentRule>& rules = editingRules();
    return !rules.isEmpty()
        && std::all_of(rules.cbegin(), rules.cend(), [](const auto& rule) {
               return rule.isValid();
           });
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
    for (ElementKind kind : {ElementKind::Router, ElementKind::Endpoint}) {
        if (const std::optional<DomainAssignmentRule> rule =
                type->assignmentRule(kind)) {
            aggregate.applicableRules.append(*rule);
        }
    }

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
        const std::optional<DomainAssignmentRule> rule =
            type->assignmentRule(reference.kind);
        if (!isDomainMembershipElementKind(reference.kind) || !rule
            || !designReferenceExists(design, reference)) {
            continue;
        }
        aggregate.eligibleElementRefs.append(reference);
        if (!ruleForKind(aggregate.eligibleRules, reference.kind)) {
            aggregate.eligibleRules.append(*rule);
        }
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
        if (!assignments.isEmpty()) {
            aggregate.assignmentsByEligibleElement.insert(
                reference, assignments);
        }
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
    if (type.assignmentRule(ElementKind::Router)) {
        candidates.reserve(resolved.topology.routers.size()
                           + resolved.topology.endpoints.size());
        for (const RouterView& router : resolved.topology.routers) {
            candidates.append(ElementRef{ElementKind::Router, router.id});
        }
    }
    if (type.assignmentRule(ElementKind::Endpoint)) {
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
