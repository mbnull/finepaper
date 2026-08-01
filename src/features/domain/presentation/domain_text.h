#pragma once

#include "package/package.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace finepaper::domain_text {

[[nodiscard]] QString elementKindDisplayText(ElementKind elementKind);

// Plain-text labels deliberately retain stable identifiers. Callers that
// render rich text must escape the returned value before inserting it.
[[nodiscard]] QString domainTypeDisplayText(
    const QString& domainTypeId,
    const QString& domainTypeLabel = {});
[[nodiscard]] QString domainTypeDisplayText(
    const DomainTypeDefinition& domainType);
[[nodiscard]] QString domainInstanceDisplayText(
    const QString& domainId,
    const QString& domainName = {});
[[nodiscard]] QString domainInstanceDisplayText(
    const DomainDefinition& domain);

// Describes the canonical Package rule for one element kind. The wording is
// intentionally explicit about both bounds so an unbounded maximum cannot be
// mistaken for a missing or inferred constraint.
[[nodiscard]] QString domainAssignmentConstraintText(
    const DomainAssignmentRule& assignmentRule);
[[nodiscard]] QString domainAssignmentConstraintText(
    const DomainTypeDefinition& domainType,
    ElementKind elementKind);

// Resolves every stored id to its Domain name when possible while retaining
// the id as the durable reference. Empty assignments are stated in text.
[[nodiscard]] QString domainAssignmentListText(
    const QVector<DomainDefinition>& domains,
    const QString& domainTypeId,
    const QStringList& domainIds);

// Combines applicability with the complete stored assignment. Stale data on a
// non-applicable element remains visible instead of being silently hidden.
[[nodiscard]] QString domainAssignmentText(
    const DomainTypeDefinition& domainType,
    ElementKind elementKind,
    const QVector<DomainDefinition>& domains,
    const QStringList& domainIds);

} // namespace finepaper::domain_text
