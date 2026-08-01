#pragma once

#include "application/domain_assignment.h"
#include "noc/model.h"
#include "package/package.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

enum class DomainAssignmentAggregateState {
    Unavailable,
    NoEligible,
    Unassigned,
    Common,
    Mixed
};

enum class DomainAssignmentPresence {
    None,
    Some,
    All
};

enum class DomainAssignmentSelectionScope {
    AllEligible,
    Unassigned,
    AssignedToDomain
};

struct DomainAssignmentPatchEvaluation {
    bool accepted = false;
    std::optional<ElementRef> violatingElement = std::nullopt;
    std::optional<DomainAssignmentRule> violatedRule = std::nullopt;
    qsizetype resultingAssignmentCount = 0;

    bool operator==(const DomainAssignmentPatchEvaluation&) const = default;
};

// A QWidget-independent projection of one Domain type over the current
// semantic selection. It is suitable for a combo box, a tri-state list, or a
// different presentation without changing the assignment semantics.
struct DomainAssignmentAggregate {
    QString domainType;
    QString domainTypeLabel;
    DomainAssignmentAggregateState state =
        DomainAssignmentAggregateState::Unavailable;

    // Rules remain per element kind. applicableRules describes the Package
    // type; eligibleRules is the subset represented by the current semantic
    // selection. Keeping both prevents mixed Router/Endpoint selections from
    // accidentally inheriting one legacy global cardinality.
    QVector<DomainAssignmentRule> applicableRules;
    QVector<DomainAssignmentRule> eligibleRules;
    // Only non-empty current assignments are retained. Missing entries are
    // semantically unassigned, which avoids a second per-element allocation
    // for large selections.
    QHash<ElementRef, QStringList> assignmentsByEligibleElement;

    // Counts use normalized, unique semantic references. totalElements also
    // includes edges and stale references so a GUI can report N of M eligible.
    qsizetype totalElements = 0;
    qsizetype eligibleElements = 0;
    QVector<ElementRef> eligibleElementRefs;

    // Domain ids are sorted for deterministic presentation. commonAssignments
    // is the normalized set intersection across all eligible elements.
    QStringList domainIds;
    QHash<QString, DomainAssignmentPresence> presenceByDomain;
    QStringList commonAssignments;

    [[nodiscard]] DomainAssignmentPresence presence(
        const QString& domainId) const;
    [[nodiscard]] const QVector<DomainAssignmentRule>& editingRules() const;
    [[nodiscard]] bool usesSingleAssignmentEditor() const;
    [[nodiscard]] bool requiresAssignment() const;
    [[nodiscard]] bool acceptsReplacementCount(qsizetype count) const;
    [[nodiscard]] DomainAssignmentPatchEvaluation evaluatePatch(
        const DomainAssignmentPatch& patch) const;
    [[nodiscard]] bool acceptsPatch(
        const DomainAssignmentPatch& patch) const;
    [[nodiscard]] bool permitsClearing() const;
    [[nodiscard]] bool assignmentRulesAreValid() const;

    bool operator==(const DomainAssignmentAggregate&) const = default;
};

[[nodiscard]] DomainAssignmentAggregate buildDomainAssignmentAggregate(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& selectionRefs,
    const QString& domainType);

[[nodiscard]] QVector<ElementRef> buildDomainAssignmentSelection(
    const ResolvedDesign& resolved,
    const DomainTypeDefinition& type,
    DomainAssignmentSelectionScope scope,
    const QString& domainId = {});

} // namespace finepaper
