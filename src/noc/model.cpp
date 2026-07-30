#include "noc/model.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace finepaper {
namespace {

QString routerKey(RouterPosition position) {
    return QStringLiteral("%1,%2").arg(position.x).arg(position.y);
}

void appendError(QVector<Diagnostic>& diagnostics,
                 const QString& code,
                 const QString& message,
                 const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("finepaper")
    });
}

QString referenceKey(const ElementRef& reference) {
    return elementKindId(reference.kind) + QChar(0x1f) + reference.id;
}

QString relationKey(const DomainRelation& relation) {
    return relation.type + QChar(0x1f)
        + relation.from + QChar(0x1f) + relation.to;
}

QString edgeOverrideKey(const DomainEdgeOverride& edgeOverride) {
    return referenceKey(edgeOverride.edge) + QChar(0x1f) + edgeOverride.domainType;
}

std::optional<RouterPosition> routerPositionFromStableId(const QString& id) {
    const QStringList parts = id.split(QLatin1Char('-'));
    if (parts.size() != 3 || parts.at(0) != QStringLiteral("r")) {
        return std::nullopt;
    }
    bool xValid = false;
    bool yValid = false;
    const int x = parts.at(1).toInt(&xValid);
    const int y = parts.at(2).toInt(&yValid);
    const RouterPosition position{x, y};
    if (!xValid || !yValid || x < 0 || y < 0 || routerId(position) != id) {
        return std::nullopt;
    }
    return position;
}

bool routerReferenceExists(const NocDesign& design, const QString& id) {
    const std::optional<RouterPosition> position = routerPositionFromStableId(id);
    return position
        && position->x < design.topology.columns
        && position->y < design.topology.rows;
}

std::optional<std::pair<QString, QString>> linkRouterIds(
    const NocDesign& design,
    const QString& id) {
    constexpr qsizetype prefixLength = 5;
    if (!id.startsWith(QStringLiteral("link-"))) {
        return std::nullopt;
    }
    const QString body = id.mid(prefixLength);
    const qsizetype separator = body.indexOf(QStringLiteral("--"));
    if (separator <= 0 || separator + 2 >= body.size()) {
        return std::nullopt;
    }
    const QString fromId = body.left(separator);
    const QString toId = body.mid(separator + 2);
    const std::optional<RouterPosition> from = routerPositionFromStableId(fromId);
    const std::optional<RouterPosition> to = routerPositionFromStableId(toId);
    if (!from || !to
        || !routerReferenceExists(design, fromId)
        || !routerReferenceExists(design, toId)
        || linkId(fromId, toId) != id) {
        return std::nullopt;
    }
    if ((to->x == from->x + 1 && to->y == from->y)
        || (to->x == from->x && to->y == from->y + 1)) {
        return std::pair<QString, QString>{fromId, toId};
    }
    return std::nullopt;
}

bool linkReferenceExists(const NocDesign& design, const QString& id) {
    return linkRouterIds(design, id).has_value();
}

bool membershipElementReferenceExists(const NocDesign& design,
                                      const ElementRef& reference,
                                      const QSet<QString>& endpointIds) {
    if (reference.kind == ElementKind::Router) {
        return routerReferenceExists(design, reference.id);
    }
    if (reference.kind == ElementKind::Endpoint) {
        return endpointIds.contains(reference.id);
    }
    return false;
}

bool edgeReferenceExists(const NocDesign& design,
                         const ElementRef& reference,
                         const QSet<QString>& endpointIds) {
    if (reference.kind == ElementKind::RouterLink) {
        return linkReferenceExists(design, reference.id);
    }
    if (reference.kind == ElementKind::EndpointAttachment) {
        return endpointIds.contains(reference.id);
    }
    return false;
}

bool hasDomainData(const NocDesign& design) {
    return !design.domains.isEmpty()
        || !design.domainMemberships.isEmpty()
        || !design.domainRelations.isEmpty()
        || !design.crossingPolicies.isEmpty()
        || !design.edgeOverrides.isEmpty();
}

QStringList normalizedDomainIds(QStringList ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

bool hasErrors(const QVector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == QStringLiteral("error");
    });
}

QString routerId(RouterPosition position) {
    return QStringLiteral("r-%1-%2").arg(position.x).arg(position.y);
}

QString linkId(const QString& fromRouter, const QString& toRouter) {
    return QStringLiteral("link-%1--%2").arg(fromRouter, toRouter);
}

QString elementKindId(ElementKind kind) {
    switch (kind) {
    case ElementKind::Router:
        return QStringLiteral("router");
    case ElementKind::Endpoint:
        return QStringLiteral("endpoint");
    case ElementKind::RouterLink:
        return QStringLiteral("router-link");
    case ElementKind::EndpointAttachment:
        return QStringLiteral("endpoint-attachment");
    case ElementKind::Invalid:
        break;
    }
    return {};
}

ElementKind elementKindFromId(const QString& id) {
    if (id == QStringLiteral("router")) {
        return ElementKind::Router;
    }
    if (id == QStringLiteral("endpoint")) {
        return ElementKind::Endpoint;
    }
    if (id == QStringLiteral("router-link") || id == QStringLiteral("link")) {
        return ElementKind::RouterLink;
    }
    if (id == QStringLiteral("endpoint-attachment")
        || id == QStringLiteral("attachment")) {
        return ElementKind::EndpointAttachment;
    }
    return ElementKind::Invalid;
}

size_t qHash(const ElementRef& reference, size_t seed) noexcept {
    const size_t kindHash = ::qHash(static_cast<int>(reference.kind), seed);
    return ::qHash(reference.id, kindHash);
}

std::optional<RouterPosition> routerPositionFromId(const QString& id) {
    return routerPositionFromStableId(id);
}

bool designReferenceExists(const NocDesign& design, const ElementRef& reference) {
    switch (reference.kind) {
    case ElementKind::Router:
        return routerReferenceExists(design, reference.id);
    case ElementKind::Endpoint:
    case ElementKind::EndpointAttachment:
        return std::any_of(
            design.endpoints.cbegin(), design.endpoints.cend(),
            [&](const EndpointInstance& endpoint) { return endpoint.id == reference.id; });
    case ElementKind::RouterLink:
        return linkReferenceExists(design, reference.id);
    case ElementKind::Invalid:
        return false;
    }
    return false;
}

std::optional<std::pair<ElementRef, ElementRef>> edgeEndpoints(
    const NocDesign& design,
    const ElementRef& edge) {
    if (edge.kind == ElementKind::RouterLink) {
        const auto routers = linkRouterIds(design, edge.id);
        if (!routers) {
            return std::nullopt;
        }
        return std::pair<ElementRef, ElementRef>{
            ElementRef{ElementKind::Router, routers->first},
            ElementRef{ElementKind::Router, routers->second}
        };
    }
    if (edge.kind == ElementKind::EndpointAttachment) {
        const auto endpoint = std::find_if(
            design.endpoints.cbegin(), design.endpoints.cend(),
            [&](const EndpointInstance& value) { return value.id == edge.id; });
        if (endpoint == design.endpoints.cend()) {
            return std::nullopt;
        }
        return std::pair<ElementRef, ElementRef>{
            ElementRef{ElementKind::Router, routerId(endpoint->attachment.router)},
            ElementRef{ElementKind::Endpoint, endpoint->id}
        };
    }
    return std::nullopt;
}

NocDesign withResolvedAutomaticSlots(const NocDesign& design) {
    NocDesign resolved = design;
    QHash<QString, QVector<qsizetype>> byRouter;
    for (qsizetype index = 0; index < resolved.endpoints.size(); ++index) {
        byRouter[routerKey(resolved.endpoints.at(index).attachment.router)].append(index);
    }

    for (auto it = byRouter.begin(); it != byRouter.end(); ++it) {
        QVector<qsizetype>& indices = it.value();
        std::sort(indices.begin(), indices.end(), [&](qsizetype lhs, qsizetype rhs) {
            return resolved.endpoints.at(lhs).id < resolved.endpoints.at(rhs).id;
        });

        QSet<QString> usedSlots;
        for (qsizetype index : std::as_const(indices)) {
            const auto& slot = resolved.endpoints.at(index).attachment.slot;
            if (slot && !slot->isEmpty()) {
                usedSlots.insert(*slot);
            }
        }

        int nextSlot = 0;
        for (qsizetype index : std::as_const(indices)) {
            EndpointInstance& endpoint = resolved.endpoints[index];
            if (endpoint.attachment.slot && !endpoint.attachment.slot->isEmpty()) {
                continue;
            }
            while (usedSlots.contains(QString::number(nextSlot))) {
                ++nextSlot;
            }
            endpoint.attachment.slot = QString::number(nextSlot);
            usedSlots.insert(QString::number(nextSlot));
            ++nextSlot;
        }
    }
    return resolved;
}

TopologyProjection projectTopology(const NocDesign& design) {
    TopologyProjection projection;
    if (design.topology.type != QStringLiteral("mesh") ||
        design.topology.rows <= 0 ||
        design.topology.columns <= 0 ||
        design.topology.rows > kMaximumMeshDimension ||
        design.topology.columns > kMaximumMeshDimension) {
        return projection;
    }

    const qint64 routerCount = static_cast<qint64>(design.topology.rows)
        * static_cast<qint64>(design.topology.columns);
    if (routerCount > kMaximumProjectedRouterCount) {
        return projection;
    }

    projection.routers.reserve(static_cast<qsizetype>(routerCount));
    for (int y = 0; y < design.topology.rows; ++y) {
        for (int x = 0; x < design.topology.columns; ++x) {
            const RouterPosition current{x, y};
            const QString currentId = routerId(current);
            projection.routers.append(RouterView{currentId, current});

            if (x + 1 < design.topology.columns) {
                const QString eastId = routerId(RouterPosition{x + 1, y});
                projection.links.append(LinkView{
                    linkId(currentId, eastId),
                    currentId,
                    eastId
                });
            }
            if (y + 1 < design.topology.rows) {
                const QString southId = routerId(RouterPosition{x, y + 1});
                projection.links.append(LinkView{
                    linkId(currentId, southId),
                    currentId,
                    southId
                });
            }
        }
    }

    const NocDesign resolved = withResolvedAutomaticSlots(design);
    projection.endpoints.reserve(resolved.endpoints.size());
    for (const EndpointInstance& endpoint : resolved.endpoints) {
        projection.endpoints.append(EndpointView{
            endpoint.id,
            endpoint.type,
            endpoint.attachment.router,
            routerId(endpoint.attachment.router),
            endpoint.attachment.slot.value_or(QString())
        });
    }
    return projection;
}

QVector<DomainCrossingView> projectDomainCrossings(const NocDesign& design) {
    QHash<QString, QHash<QString, QStringList>> assignmentsByElement;
    for (const DomainMembership& membership : design.domainMemberships) {
        QHash<QString, QStringList> normalizedAssignments;
        for (auto iterator = membership.assignments.constBegin();
             iterator != membership.assignments.constEnd(); ++iterator) {
            normalizedAssignments.insert(
                iterator.key(), normalizedDomainIds(iterator.value()));
        }
        assignmentsByElement.insert(referenceKey(membership.element),
                                    std::move(normalizedAssignments));
    }

    QHash<QString, const DomainEdgeOverride*> overrides;
    for (const DomainEdgeOverride& edgeOverride : design.edgeOverrides) {
        overrides.insert(edgeOverrideKey(edgeOverride), &edgeOverride);
    }

    QVector<DomainCrossingView> crossings;
    const auto appendCrossingsForEdge = [&](const ElementRef& edge,
                                            const ElementRef& from,
                                            const ElementRef& to) {
        static const QHash<QString, QStringList> emptyAssignments;
        const auto fromIterator = assignmentsByElement.constFind(referenceKey(from));
        const auto toIterator = assignmentsByElement.constFind(referenceKey(to));
        const QHash<QString, QStringList>& fromAssignments =
            fromIterator == assignmentsByElement.constEnd()
                ? emptyAssignments : fromIterator.value();
        const QHash<QString, QStringList>& toAssignments =
            toIterator == assignmentsByElement.constEnd()
                ? emptyAssignments : toIterator.value();
        QSet<QString> types;
        for (auto iterator = fromAssignments.constBegin();
             iterator != fromAssignments.constEnd(); ++iterator) {
            types.insert(iterator.key());
        }
        for (auto iterator = toAssignments.constBegin();
             iterator != toAssignments.constEnd(); ++iterator) {
            types.insert(iterator.key());
        }
        QStringList sortedTypes = types.values();
        std::sort(sortedTypes.begin(), sortedTypes.end());
        for (const QString& type : std::as_const(sortedTypes)) {
            const QStringList fromDomains = normalizedDomainIds(
                fromAssignments.value(type));
            const QStringList toDomains = normalizedDomainIds(
                toAssignments.value(type));
            if (fromDomains == toDomains) {
                continue;
            }
            DomainCrossingView crossing{
                edge,
                from,
                to,
                type,
                fromDomains,
                toDomains,
                std::nullopt,
                {}
            };
            const DomainEdgeOverride lookup{edge, type, {}, {}};
            const auto override = overrides.constFind(edgeOverrideKey(lookup));
            if (override != overrides.constEnd()) {
                crossing.overridePolicy = (*override)->policy;
                crossing.overrideProperties = (*override)->properties;
            }
            crossings.append(std::move(crossing));
        }
    };

    const TopologyProjection topology = projectTopology(design);
    for (const LinkView& link : topology.links) {
        appendCrossingsForEdge(
            ElementRef{ElementKind::RouterLink, link.id},
            ElementRef{ElementKind::Router, link.fromRouter},
            ElementRef{ElementKind::Router, link.toRouter});
    }
    for (const EndpointView& endpoint : topology.endpoints) {
        appendCrossingsForEdge(
            ElementRef{ElementKind::EndpointAttachment, endpoint.id},
            ElementRef{ElementKind::Router, endpoint.routerId},
            ElementRef{ElementKind::Endpoint, endpoint.id});
    }
    return crossings;
}

ResolvedDesign resolveDesign(const NocDesign& design) {
    ResolvedDesign resolved;
    resolved.design = withResolvedAutomaticSlots(design);
    resolved.topology = projectTopology(resolved.design);
    resolved.domainCrossings = projectDomainCrossings(resolved.design);
    return resolved;
}

QVector<Diagnostic> validateDesignStructure(const NocDesign& design) {
    QVector<Diagnostic> diagnostics;
    if (design.format != QStringLiteral("finepaper.noc-design")) {
        appendError(diagnostics,
                    QStringLiteral("design.unsupported_format"),
                    QStringLiteral("format must be finepaper.noc-design"),
                    QStringLiteral("/format"));
    }
    if (design.formatVersion < kMinimumDesignFormatVersion
        || design.formatVersion > kMaximumDesignFormatVersion) {
        appendError(diagnostics,
                    QStringLiteral("design.unsupported_version"),
                    QStringLiteral("formatVersion must be 1 or 2"),
                    QStringLiteral("/formatVersion"));
    }
    if (design.formatVersion == 1 && hasDomainData(design)) {
        appendError(diagnostics,
                    QStringLiteral("design.domains_require_v2"),
                    QStringLiteral("Domain data requires formatVersion 2"),
                    QStringLiteral("/formatVersion"));
    }
    if (design.id.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_id"),
                    QStringLiteral("design id is required"),
                    QStringLiteral("/id"));
    }
    if (design.name.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_name"),
                    QStringLiteral("design name is required"),
                    QStringLiteral("/name"));
    }
    if (design.package.id.trimmed().isEmpty() || design.package.version.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_package"),
                    QStringLiteral("package id and version are required"),
                    QStringLiteral("/package"));
    }
    if (design.topology.type != QStringLiteral("mesh")) {
        appendError(diagnostics,
                    QStringLiteral("topology.unsupported_type"),
                    QStringLiteral("Finepaper currently supports topology.type=mesh"),
                    QStringLiteral("/topology/type"));
    }
    if (design.topology.rows <= 0) {
        appendError(diagnostics,
                    QStringLiteral("topology.invalid_rows"),
                    QStringLiteral("rows must be greater than zero"),
                    QStringLiteral("/topology/rows"));
    }
    if (design.topology.columns <= 0) {
        appendError(diagnostics,
                    QStringLiteral("topology.invalid_columns"),
                    QStringLiteral("columns must be greater than zero"),
                    QStringLiteral("/topology/columns"));
    }
    if (design.topology.rows > kMaximumMeshDimension ||
        design.topology.columns > kMaximumMeshDimension ||
        (design.topology.rows > 0 && design.topology.columns > 0 &&
         static_cast<qint64>(design.topology.rows)
                 * static_cast<qint64>(design.topology.columns)
             > kMaximumProjectedRouterCount)) {
        appendError(diagnostics,
                    QStringLiteral("topology.projection_too_large"),
                    QStringLiteral("topology exceeds Finepaper's safe projection limit"),
                    QStringLiteral("/topology"));
    }

    QSet<QString> endpointIds;
    for (qsizetype index = 0; index < design.endpoints.size(); ++index) {
        const EndpointInstance& endpoint = design.endpoints.at(index);
        const QString base = QStringLiteral("/endpoints/%1").arg(index);
        if (endpoint.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.missing_id"),
                        QStringLiteral("endpoint id is required"),
                        base + QStringLiteral("/id"));
        } else if (endpointIds.contains(endpoint.id)) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.duplicate_id"),
                        QStringLiteral("endpoint id is duplicated"),
                        base + QStringLiteral("/id"));
        } else {
            endpointIds.insert(endpoint.id);
        }
        if (endpoint.type.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.missing_type"),
                        QStringLiteral("endpoint type is required"),
                        base + QStringLiteral("/type"));
        }
        const RouterPosition position = endpoint.attachment.router;
        if (position.x < 0 || position.x >= design.topology.columns ||
            position.y < 0 || position.y >= design.topology.rows) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.router_out_of_range"),
                        QStringLiteral("endpoint router coordinate is outside the Mesh"),
                        base + QStringLiteral("/attachment/router"));
        }
    }

    QHash<QString, QString> domainTypes;
    for (qsizetype index = 0; index < design.domains.size(); ++index) {
        const DomainDefinition& domain = design.domains.at(index);
        const QString base = QStringLiteral("/domains/%1").arg(index);
        if (domain.type.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain.missing_type"),
                        QStringLiteral("Domain type is required"),
                        base + QStringLiteral("/type"));
        }
        if (domain.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain.missing_id"),
                        QStringLiteral("Domain id is required"),
                        base + QStringLiteral("/id"));
        } else if (domainTypes.contains(domain.id)) {
            appendError(diagnostics,
                        QStringLiteral("domain.duplicate_id"),
                        QStringLiteral("Domain id is duplicated"),
                        base + QStringLiteral("/id"));
        } else {
            domainTypes.insert(domain.id, domain.type);
        }
        if (domain.name.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain.missing_name"),
                        QStringLiteral("Domain name is required"),
                        base + QStringLiteral("/name"));
        }
    }

    QSet<QString> membershipElements;
    for (qsizetype index = 0; index < design.domainMemberships.size(); ++index) {
        const DomainMembership& membership = design.domainMemberships.at(index);
        const QString base = QStringLiteral("/domainMemberships/%1").arg(index);
        const QString elementKey = referenceKey(membership.element);
        if (membership.element.kind == ElementKind::Invalid) {
            appendError(diagnostics,
                        QStringLiteral("domain_membership.missing_element_kind"),
                        QStringLiteral("Membership element kind must be router or endpoint"),
                        base + QStringLiteral("/element/kind"));
        } else if (membership.element.kind != ElementKind::Router
                   && membership.element.kind != ElementKind::Endpoint) {
            appendError(diagnostics,
                        QStringLiteral("domain_membership.unsupported_element_kind"),
                        QStringLiteral("Membership element kind must be router or endpoint"),
                        base + QStringLiteral("/element/kind"));
        }
        if (membership.element.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_membership.missing_element_id"),
                        QStringLiteral("Membership element id is required"),
                        base + QStringLiteral("/element/id"));
        } else if ((membership.element.kind == ElementKind::Router
                    || membership.element.kind == ElementKind::Endpoint)
                   && !membershipElementReferenceExists(
                       design, membership.element, endpointIds)) {
            appendError(diagnostics,
                        QStringLiteral("domain_membership.unknown_element"),
                        QStringLiteral("Membership references an unknown element"),
                        base + QStringLiteral("/element"));
        }
        if (membership.element.kind != ElementKind::Invalid
            && !membership.element.id.trimmed().isEmpty()) {
            if (membershipElements.contains(elementKey)) {
                appendError(diagnostics,
                            QStringLiteral("domain_membership.duplicate_element"),
                            QStringLiteral("Element has more than one Domain membership record"),
                            base + QStringLiteral("/element"));
            } else {
                membershipElements.insert(elementKey);
            }
        }

        if (membership.assignments.isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_membership.empty_membership"),
                        QStringLiteral("Domain membership must contain at least one assignment"),
                        base + QStringLiteral("/assignments"));
        }

        QStringList assignmentTypes = membership.assignments.keys();
        std::sort(assignmentTypes.begin(), assignmentTypes.end());
        for (const QString& assignmentType : std::as_const(assignmentTypes)) {
            const QString assignmentPath = base + QStringLiteral("/assignments/")
                + assignmentType;
            if (assignmentType.trimmed().isEmpty()) {
                appendError(diagnostics,
                            QStringLiteral("domain_membership.missing_assignment_type"),
                            QStringLiteral("Assignment type is required"),
                            base + QStringLiteral("/assignments"));
            }
            QSet<QString> assignedDomains;
            const QStringList domainIds = membership.assignments.value(assignmentType);
            if (domainIds.isEmpty()) {
                appendError(diagnostics,
                            QStringLiteral("domain_membership.empty_assignment"),
                            QStringLiteral("Domain assignment list must not be empty"),
                            assignmentPath);
            }
            for (qsizetype assignmentIndex = 0;
                 assignmentIndex < domainIds.size(); ++assignmentIndex) {
                const QString& domainId = domainIds.at(assignmentIndex);
                const QString path = assignmentPath
                    + QStringLiteral("/%1").arg(assignmentIndex);
                if (domainId.trimmed().isEmpty()) {
                    appendError(diagnostics,
                                QStringLiteral("domain_membership.missing_domain_id"),
                                QStringLiteral("Assigned Domain id is required"),
                                path);
                    continue;
                }
                if (assignedDomains.contains(domainId)) {
                    appendError(diagnostics,
                                QStringLiteral("domain_membership.duplicate_assignment"),
                                QStringLiteral("Domain is assigned more than once for this type"),
                                path);
                    continue;
                }
                assignedDomains.insert(domainId);
                const auto domainType = domainTypes.constFind(domainId);
                if (domainType == domainTypes.constEnd()) {
                    appendError(diagnostics,
                                QStringLiteral("domain_membership.unknown_domain"),
                                QStringLiteral("Assignment references an unknown Domain"),
                                path);
                } else if (domainType.value() != assignmentType) {
                    appendError(diagnostics,
                                QStringLiteral("domain_membership.type_mismatch"),
                                QStringLiteral("Assigned Domain type does not match the assignment key"),
                                path);
                }
            }
        }
    }

    QSet<QString> relations;
    for (qsizetype index = 0; index < design.domainRelations.size(); ++index) {
        const DomainRelation& relation = design.domainRelations.at(index);
        const QString base = QStringLiteral("/domainRelations/%1").arg(index);
        if (relation.type.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_relation.missing_type"),
                        QStringLiteral("Domain relation type is required"),
                        base + QStringLiteral("/type"));
        }
        if (!domainTypes.contains(relation.from)) {
            appendError(diagnostics,
                        QStringLiteral("domain_relation.unknown_from"),
                        QStringLiteral("Domain relation source is unknown"),
                        base + QStringLiteral("/from"));
        }
        if (!domainTypes.contains(relation.to)) {
            appendError(diagnostics,
                        QStringLiteral("domain_relation.unknown_to"),
                        QStringLiteral("Domain relation target is unknown"),
                        base + QStringLiteral("/to"));
        }
        const QString key = relationKey(relation);
        if (!relation.type.trimmed().isEmpty()
            && !relation.from.trimmed().isEmpty()
            && !relation.to.trimmed().isEmpty()) {
            if (relations.contains(key)) {
                appendError(diagnostics,
                            QStringLiteral("domain_relation.duplicate"),
                            QStringLiteral("Domain relation is duplicated"),
                            base);
            } else {
                relations.insert(key);
            }
        }
    }

    QHash<QString, QString> policyTypes;
    for (qsizetype index = 0; index < design.crossingPolicies.size(); ++index) {
        const DomainCrossingPolicy& policy = design.crossingPolicies.at(index);
        const QString base = QStringLiteral("/crossingPolicies/%1").arg(index);
        if (policy.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.missing_id"),
                        QStringLiteral("Crossing policy id is required"),
                        base + QStringLiteral("/id"));
        } else if (policyTypes.contains(policy.id)) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.duplicate_id"),
                        QStringLiteral("Crossing policy id is duplicated"),
                        base + QStringLiteral("/id"));
        } else {
            policyTypes.insert(policy.id, policy.domainType);
        }
        if (policy.domainType.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.missing_domain_type"),
                        QStringLiteral("Crossing policy Domain type is required"),
                        base + QStringLiteral("/domainType"));
        }
        const auto fromType = domainTypes.constFind(policy.from);
        if (fromType == domainTypes.constEnd()) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.unknown_from"),
                        QStringLiteral("Crossing policy source Domain is unknown"),
                        base + QStringLiteral("/from"));
        } else if (fromType.value() != policy.domainType) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.from_type_mismatch"),
                        QStringLiteral("Crossing policy source has the wrong Domain type"),
                        base + QStringLiteral("/from"));
        }
        const auto toType = domainTypes.constFind(policy.to);
        if (toType == domainTypes.constEnd()) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.unknown_to"),
                        QStringLiteral("Crossing policy target Domain is unknown"),
                        base + QStringLiteral("/to"));
        } else if (toType.value() != policy.domainType) {
            appendError(diagnostics,
                        QStringLiteral("domain_policy.to_type_mismatch"),
                        QStringLiteral("Crossing policy target has the wrong Domain type"),
                        base + QStringLiteral("/to"));
        }
    }

    QSet<QString> edgeOverrides;
    for (qsizetype index = 0; index < design.edgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride = design.edgeOverrides.at(index);
        const QString base = QStringLiteral("/edgeOverrides/%1").arg(index);
        if (edgeOverride.edge.kind == ElementKind::Invalid) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.missing_edge_kind"),
                        QStringLiteral("Edge kind must be router-link or endpoint-attachment"),
                        base + QStringLiteral("/edge/kind"));
        } else if (edgeOverride.edge.kind != ElementKind::RouterLink
                   && edgeOverride.edge.kind != ElementKind::EndpointAttachment) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.unsupported_edge_kind"),
                        QStringLiteral("Edge kind must be router-link or endpoint-attachment"),
                        base + QStringLiteral("/edge/kind"));
        }
        if (edgeOverride.edge.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.missing_edge_id"),
                        QStringLiteral("Edge id is required"),
                        base + QStringLiteral("/edge/id"));
        } else if ((edgeOverride.edge.kind == ElementKind::RouterLink
                    || edgeOverride.edge.kind == ElementKind::EndpointAttachment)
                   && !edgeReferenceExists(design, edgeOverride.edge, endpointIds)) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.unknown_edge"),
                        QStringLiteral("Domain edge override references an unknown edge"),
                        base + QStringLiteral("/edge"));
        }
        if (edgeOverride.domainType.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.missing_domain_type"),
                        QStringLiteral("Domain edge override type is required"),
                        base + QStringLiteral("/domainType"));
        }
        const auto policyType = policyTypes.constFind(edgeOverride.policy);
        if (policyType == policyTypes.constEnd()) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.unknown_policy"),
                        QStringLiteral("Domain edge override policy is unknown"),
                        base + QStringLiteral("/policy"));
        } else if (policyType.value() != edgeOverride.domainType) {
            appendError(diagnostics,
                        QStringLiteral("domain_edge_override.policy_type_mismatch"),
                        QStringLiteral("Domain edge override policy has the wrong Domain type"),
                        base + QStringLiteral("/policy"));
        }
        const QString key = edgeOverrideKey(edgeOverride);
        if (edgeOverride.edge.kind != ElementKind::Invalid
            && !edgeOverride.edge.id.trimmed().isEmpty()
            && !edgeOverride.domainType.trimmed().isEmpty()) {
            if (edgeOverrides.contains(key)) {
                appendError(diagnostics,
                            QStringLiteral("domain_edge_override.duplicate"),
                            QStringLiteral("Edge has more than one override for this Domain type"),
                            base);
            } else {
                edgeOverrides.insert(key);
            }
        }
    }
    return diagnostics;
}

} // namespace finepaper
