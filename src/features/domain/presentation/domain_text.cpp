#include "features/domain/presentation/domain_text.h"

#include <QHash>

#include <algorithm>

namespace finepaper::domain_text {
namespace {

QString elementKindLabel(ElementKind kind) {
    switch (kind) {
    case ElementKind::Router:
        return QStringLiteral("Router");
    case ElementKind::Endpoint:
        return QStringLiteral("Endpoint");
    case ElementKind::RouterLink:
        return QStringLiteral("Router Link");
    case ElementKind::EndpointAttachment:
        return QStringLiteral("Endpoint Attachment");
    case ElementKind::Invalid:
        return QStringLiteral("Element");
    }
    return QStringLiteral("Element");
}

QString labeledStableId(const QString& stableId, const QString& label) {
    const QString id = stableId.trimmed();
    const QString name = label.trimmed();
    if (id.isEmpty()) {
        return name.isEmpty() ? QStringLiteral("Unnamed Domain type") : name;
    }
    if (name.isEmpty() || name == id) {
        return id;
    }
    return name + QStringLiteral(" (") + id + QLatin1Char(')');
}

QString labeledDomainId(const QString& stableId, const QString& name) {
    const QString id = stableId.trimmed();
    const QString label = name.trimmed();
    if (id.isEmpty()) {
        return QStringLiteral("Empty Domain id");
    }
    if (label.isEmpty() || label == id) {
        return id;
    }
    return label + QStringLiteral(" (") + id + QLatin1Char(')');
}

QStringList normalizedDomainIds(QStringList ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

QString elementKindDisplayText(ElementKind elementKind) {
    return elementKindLabel(elementKind);
}

QString domainTypeDisplayText(const QString& domainTypeId,
                              const QString& domainTypeLabel) {
    return labeledStableId(domainTypeId, domainTypeLabel);
}

QString domainTypeDisplayText(const DomainTypeDefinition& domainType) {
    return domainTypeDisplayText(domainType.id, domainType.label);
}

QString domainInstanceDisplayText(const QString& domainId,
                                  const QString& domainName) {
    return labeledDomainId(domainId, domainName);
}

QString domainInstanceDisplayText(const DomainDefinition& domain) {
    return domainInstanceDisplayText(domain.id, domain.name);
}

QString domainAssignmentConstraintText(
    const DomainAssignmentRule& assignmentRule) {
    const QString kind = elementKindDisplayText(assignmentRule.elementKind);
    const QString maximum = assignmentRule.maximumAssignments
        ? QString::number(*assignmentRule.maximumAssignments)
        : QStringLiteral("unbounded");
    QString text = kind + QStringLiteral(" assignments: minimum ")
        + QString::number(assignmentRule.minimumAssignments)
        + QStringLiteral(", maximum ") + maximum + QLatin1Char('.');
    if (!assignmentRule.isValid()) {
        text += QStringLiteral(" Invalid Package rule.");
    }
    return text;
}

QString domainAssignmentConstraintText(
    const DomainTypeDefinition& domainType,
    ElementKind elementKind) {
    const QString kind = elementKindDisplayText(elementKind);
    const std::optional<DomainAssignmentRule> rule =
        domainType.assignmentRule(elementKind);
    if (!rule) {
        return kind + QStringLiteral(" assignments: not applicable.");
    }

    return domainAssignmentConstraintText(*rule);
}

QString domainAssignmentListText(
    const QVector<DomainDefinition>& domains,
    const QString& domainTypeId,
    const QStringList& domainIds) {
    const QStringList normalized = normalizedDomainIds(domainIds);
    if (normalized.isEmpty()) {
        return QStringLiteral("Unassigned");
    }

    QHash<QString, QString> namesById;
    for (const DomainDefinition& domain : domains) {
        if (domain.type == domainTypeId && !namesById.contains(domain.id)) {
            namesById.insert(domain.id, domain.name);
        }
    }

    QStringList assignments;
    assignments.reserve(normalized.size());
    for (const QString& domainId : normalized) {
        assignments.append(domainInstanceDisplayText(
            domainId, namesById.value(domainId)));
    }
    return assignments.join(QStringLiteral(", "));
}

QString domainAssignmentText(
    const DomainTypeDefinition& domainType,
    ElementKind elementKind,
    const QVector<DomainDefinition>& domains,
    const QStringList& domainIds) {
    const QString assignments = domainAssignmentListText(
        domains, domainType.id, domainIds);
    if (domainType.assignmentRule(elementKind)) {
        return assignments;
    }
    if (domainIds.isEmpty()) {
        return QStringLiteral("Not applicable");
    }
    return QStringLiteral("Not applicable; stored assignment: ")
        + assignments;
}

} // namespace finepaper::domain_text
