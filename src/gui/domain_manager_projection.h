#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

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

// A QWidget-independent projection of one Domain type over the current
// semantic selection. It is suitable for a combo box, a tri-state list, or a
// different presentation without changing the assignment semantics.
struct DomainAssignmentAggregate {
    QString domainType;
    QString domainTypeLabel;
    DomainCardinality cardinality = DomainCardinality::Invalid;
    bool required = false;
    DomainAssignmentAggregateState state =
        DomainAssignmentAggregateState::Unavailable;

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

    bool operator==(const DomainAssignmentAggregate&) const = default;
};

[[nodiscard]] DomainAssignmentAggregate buildDomainAssignmentAggregate(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& selectionRefs,
    const QString& domainType);

} // namespace finepaper
