#include "application/domain_service.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace finepaper::domain_service {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path,
                      const QString& source = QStringLiteral("finepaper")) {
    diagnostics.append(Diagnostic{severity, code, message, path, source});
}

QJsonObject defaultsFor(const QVector<DomainPropertyDefinition>& definitions) {
    QJsonObject values;
    for (const DomainPropertyDefinition& definition : definitions) {
        if (definition.hasDefault) {
            values.insert(definition.id, definition.defaultValue);
        }
    }
    return values;
}

void mergeValues(QJsonObject& target, const QJsonObject& source) {
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
}

bool domainTypeAppliesTo(const DomainTypeDefinition& type, ElementKind kind) {
    return type.appliesTo.contains(kind);
}

QString elementReferenceKey(const ElementRef& element) {
    return elementKindId(element.kind) + QChar(0x1f) + element.id;
}

DomainMembership* findMembership(NocDesign& design, const ElementRef& element) {
    const auto it = std::find_if(
        design.domainMemberships.begin(),
        design.domainMemberships.end(),
        [&](const DomainMembership& membership) {
            return membership.element == element;
        });
    return it == design.domainMemberships.end() ? nullptr : &(*it);
}

const DomainMembership* findMembership(const NocDesign& design,
                                       const ElementRef& element) {
    const auto it = std::find_if(
        design.domainMemberships.cbegin(),
        design.domainMemberships.cend(),
        [&](const DomainMembership& membership) {
            return membership.element == element;
        });
    return it == design.domainMemberships.cend() ? nullptr : &(*it);
}

void setMembershipAssignments(NocDesign& design,
                              const ElementRef& element,
                              const QHash<QString, QStringList>& assignments) {
    if (assignments.isEmpty()) {
        return;
    }
    DomainMembership* membership = findMembership(design, element);
    if (!membership) {
        design.domainMemberships.append(DomainMembership{element, assignments});
        return;
    }
    for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it) {
        membership->assignments.insert(it.key(), it.value());
    }
}

QStringList domainIdsForType(const NocDesign& design, const QString& type) {
    QStringList ids;
    for (const DomainDefinition& domain : design.domains) {
        if (domain.type == type) {
            ids.append(domain.id);
        }
    }
    return ids;
}

void completeRequiredAssignments(
    const NocDesign& design,
    const PackageDefinition& package,
    ElementKind elementKind,
    const QString& elementId,
    QHash<QString, QStringList>& assignments,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    for (const DomainTypeDefinition& type : package.domainTypes) {
        if (!type.required || !domainTypeAppliesTo(type, elementKind)
            || !assignments.value(type.id).isEmpty()) {
            continue;
        }
        const QStringList available = domainIdsForType(design, type.id);
        if (available.size() == 1) {
            assignments.insert(type.id, available);
            continue;
        }
        assignments.remove(type.id);
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("domain_assignment.required_choice"),
            QStringLiteral("Element %1 requires an explicit %2 Domain choice; %3 instances are available")
                .arg(elementId, type.id)
                .arg(available.size()),
            path + QStringLiteral("/assignments/") + type.id,
            QStringLiteral("package"));
    }
}

QString uniqueDomainId(const NocDesign& design, const QString& baseId) {
    QSet<QString> ids;
    for (const DomainDefinition& domain : design.domains) {
        ids.insert(domain.id);
    }
    if (!ids.contains(baseId)) {
        return baseId;
    }
    for (int suffix = 2; ; ++suffix) {
        const QString candidate = QStringLiteral("%1-%2").arg(baseId).arg(suffix);
        if (!ids.contains(candidate)) {
            return candidate;
        }
    }
}

const DomainRelationDefinition* domainRelationDefinition(
    const DomainTypeDefinition& type,
    const QString& relationId) {
    const auto it = std::find_if(
        type.relations.cbegin(),
        type.relations.cend(),
        [&](const DomainRelationDefinition& relation) {
            return relation.id == relationId;
        });
    return it == type.relations.cend() ? nullptr : &(*it);
}

bool valueMatchesDomainPropertyType(const QJsonValue& value,
                                    ParameterType type) {
    if (type == ParameterType::Integer) {
        return value.isDouble()
            && std::isfinite(value.toDouble())
            && std::floor(value.toDouble()) == value.toDouble();
    }
    if (type == ParameterType::Number) {
        return value.isDouble() && std::isfinite(value.toDouble());
    }
    if (type == ParameterType::Boolean) {
        return value.isBool();
    }
    if (type == ParameterType::String
        || type == ParameterType::Enumeration) {
        return value.isString();
    }
    return false;
}

enum class DomainPropertyValidationMode {
    Complete,
    Partial
};

QVector<Diagnostic> validateDomainPropertyObject(
    const QJsonObject& values,
    const QVector<DomainPropertyDefinition>& definitions,
    const QHash<QString, const DomainDefinition*>& domainsById,
    const QString& basePath,
    DomainPropertyValidationMode mode) {
    QVector<Diagnostic> diagnostics;
    QSet<QString> knownIds;
    for (const DomainPropertyDefinition& definition : definitions) {
        knownIds.insert(definition.id);
        const QString propertyPath = basePath + QLatin1Char('/') + definition.id;
        if (!values.contains(definition.id)) {
            if (mode == DomainPropertyValidationMode::Complete
                && definition.required) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_property.missing"),
                                 QStringLiteral("Domain property %1 is required")
                                     .arg(definition.id),
                                 propertyPath,
                                 QStringLiteral("package"));
            }
            continue;
        }

        const QJsonValue value = values.value(definition.id);
        QJsonArray items;
        if (definition.multiple) {
            if (!value.isArray()) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_property.invalid_type"),
                                 QStringLiteral("Domain property %1 must be an array")
                                     .arg(definition.id),
                                 propertyPath,
                                 QStringLiteral("package"));
                continue;
            }
            items = value.toArray();
            if (definition.required && items.isEmpty()) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_property.empty"),
                                 QStringLiteral("required Domain property %1 must not be empty")
                                     .arg(definition.id),
                                 propertyPath,
                                 QStringLiteral("package"));
            }
        } else {
            items.append(value);
        }

        for (qsizetype index = 0; index < items.size(); ++index) {
            const QJsonValue item = items.at(index);
            const QString itemPath = definition.multiple
                ? QStringLiteral("%1/%2").arg(propertyPath).arg(index)
                : propertyPath;
            if (!valueMatchesDomainPropertyType(item, definition.type)) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_property.invalid_type"),
                                 QStringLiteral("Domain property %1 has the wrong type")
                                     .arg(definition.id),
                                 itemPath,
                                 QStringLiteral("package"));
                continue;
            }
            if (item.isDouble()) {
                const double number = item.toDouble();
                if (definition.minimum && number < *definition.minimum) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("domain_property.below_minimum"),
                                     QStringLiteral("Domain property %1 is below its minimum")
                                         .arg(definition.id),
                                     itemPath,
                                     QStringLiteral("package"));
                }
                if (definition.maximum && number > *definition.maximum) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("domain_property.above_maximum"),
                                     QStringLiteral("Domain property %1 is above its maximum")
                                         .arg(definition.id),
                                     itemPath,
                                     QStringLiteral("package"));
                }
            }
            if (definition.type == ParameterType::Enumeration
                && !definition.values.contains(item.toString())) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_property.invalid_enum"),
                                 QStringLiteral("Domain property %1 has an unsupported value")
                                     .arg(definition.id),
                                 itemPath,
                                 QStringLiteral("package"));
            }
            if (definition.referenceDomainType) {
                const auto referenced = domainsById.constFind(item.toString());
                if (referenced == domainsById.constEnd()) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("domain_property.unknown_reference"),
                                     QStringLiteral("Domain property %1 references an unknown Domain")
                                         .arg(definition.id),
                                     itemPath,
                                     QStringLiteral("package"));
                } else if (referenced.value()->type
                           != *definition.referenceDomainType) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("domain_property.reference_type_mismatch"),
                                     QStringLiteral("Domain property %1 references the wrong Domain type")
                                         .arg(definition.id),
                                     itemPath,
                                     QStringLiteral("package"));
                }
            }
        }
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!knownIds.contains(it.key())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_property.unknown"),
                             QStringLiteral("Domain property %1 is not declared by the Package")
                                 .arg(it.key()),
                             basePath + QLatin1Char('/') + it.key(),
                             QStringLiteral("package"));
        }
    }
    return diagnostics;
}

void appendDomainPropertyReferenceDiagnostics(
    const QJsonObject& values,
    const QVector<DomainPropertyDefinition>& definitions,
    const QString& targetDomainId,
    const QString& basePath,
    QVector<Diagnostic>& diagnostics) {
    for (const DomainPropertyDefinition& definition : definitions) {
        if (!definition.referenceDomainType || !values.contains(definition.id)) {
            continue;
        }
        const QJsonValue value = values.value(definition.id);
        QJsonArray items;
        if (definition.multiple && value.isArray()) {
            items = value.toArray();
        } else {
            items.append(value);
        }
        for (qsizetype index = 0; index < items.size(); ++index) {
            if (!items.at(index).isString()
                || items.at(index).toString() != targetDomainId) {
                continue;
            }
            const QString path = definition.multiple
                ? QStringLiteral("%1/%2/%3")
                      .arg(basePath, definition.id)
                      .arg(index)
                : basePath + QLatin1Char('/') + definition.id;
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain.remove_referenced"),
                             QStringLiteral("Domain is referenced by property %1")
                                 .arg(definition.id),
                             path,
                             QStringLiteral("package"));
        }
    }
}

} // namespace

bool canProjectTopology(const TopologySpec& topology) {
    return topology.rows > 0
        && topology.columns > 0
        && topology.rows <= kMaximumMeshDimension
        && topology.columns <= kMaximumMeshDimension
        && static_cast<qint64>(topology.rows)
                * static_cast<qint64>(topology.columns)
            <= kMaximumProjectedRouterCount;
}

MutationResult materializeRequiredDomains(
    const NocDesign& design,
    const PackageDefinition& package) {
    MutationResult result;
    result.design = design;
    if (package.formatVersion != 2) {
        return result;
    }

    QHash<QString, QString> defaultDomainIds;
    for (const DomainTypeDefinition& type : package.domainTypes) {
        if (!type.required) {
            continue;
        }
        DomainDefinition domain;
        domain.id = uniqueDomainId(
            result.design, type.id + QStringLiteral("-default"));
        domain.type = type.id;
        domain.name = type.label.trimmed().isEmpty()
            ? type.id
            : type.label.trimmed();
        domain.properties = defaultsFor(type.properties);
        defaultDomainIds.insert(type.id, domain.id);
        result.design.domains.append(std::move(domain));
    }

    const auto materializeMembership = [&](const ElementRef& element) {
        QHash<QString, QStringList> assignments;
        for (const DomainTypeDefinition& type : package.domainTypes) {
            if (type.required && domainTypeAppliesTo(type, element.kind)) {
                assignments.insert(
                    type.id, QStringList{defaultDomainIds.value(type.id)});
            }
        }
        setMembershipAssignments(result.design, element, assignments);
    };

    if (canProjectTopology(result.design.topology)) {
        const TopologyProjection projection = projectTopology(result.design);
        for (const RouterView& router : projection.routers) {
            materializeMembership(ElementRef{ElementKind::Router, router.id});
        }
    }
    for (const EndpointInstance& endpoint : result.design.endpoints) {
        materializeMembership(ElementRef{ElementKind::Endpoint, endpoint.id});
    }
    return result;
}

MutationResult resizeMesh(
    const NocDesign& design,
    const PackageDefinition& package,
    int rows,
    int columns,
    const QVector<DomainMembership>& newRouterMemberships) {
    MutationResult result;
    result.design = design;
    result.design.topology.rows = rows;
    result.design.topology.columns = columns;
    const TopologyProjection resizedProjection = projectTopology(result.design);

    for (qsizetype index = 0; index < design.domainMemberships.size(); ++index) {
        const DomainMembership& membership = design.domainMemberships.at(index);
        if (membership.element.kind == ElementKind::Router
            && !designReferenceExists(result.design, membership.element)) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("mesh.resize_would_remove_domain_membership"),
                QStringLiteral("resize would remove Router %1 with Domain assignments")
                    .arg(membership.element.id),
                QStringLiteral("/domainMemberships/%1/element").arg(index));
        }
    }
    for (qsizetype index = 0; index < design.edgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride = design.edgeOverrides.at(index);
        if (edgeOverride.edge.kind == ElementKind::RouterLink
            && !designReferenceExists(result.design, edgeOverride.edge)) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("mesh.resize_would_remove_edge_override"),
                QStringLiteral("resize would remove Router link %1 with a Domain override")
                    .arg(edgeOverride.edge.id),
                QStringLiteral("/edgeOverrides/%1/edge").arg(index));
        }
    }
    if (hasErrors(result.diagnostics)) {
        result.design = design;
        return result;
    }

    QSet<QString> newRouterIds;
    for (const RouterView& router : resizedProjection.routers) {
        if (router.position.x >= design.topology.columns
            || router.position.y >= design.topology.rows) {
            newRouterIds.insert(router.id);
        }
    }

    QHash<QString, DomainMembership> providedMemberships;
    for (qsizetype index = 0; index < newRouterMemberships.size(); ++index) {
        const DomainMembership& membership = newRouterMemberships.at(index);
        const QString path = QStringLiteral("/newRouterMemberships/%1").arg(index);
        if (membership.element.kind != ElementKind::Router) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_assignment.invalid_element_kind"),
                             QStringLiteral("new Router membership must reference a Router"),
                             path + QStringLiteral("/element/kind"));
            continue;
        }
        if (!newRouterIds.contains(membership.element.id)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_assignment.not_new_router"),
                             QStringLiteral("membership must reference a Router created by this resize"),
                             path + QStringLiteral("/element/id"));
            continue;
        }
        if (providedMemberships.contains(membership.element.id)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_assignment.duplicate_element"),
                             QStringLiteral("new Router membership is duplicated"),
                             path + QStringLiteral("/element"));
            continue;
        }
        if (findMembership(design, membership.element)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_assignment.duplicate_element"),
                             QStringLiteral("Router already has a Domain membership record"),
                             path + QStringLiteral("/element"));
            continue;
        }
        providedMemberships.insert(membership.element.id, membership);
    }
    if (hasErrors(result.diagnostics)) {
        result.design = design;
        return result;
    }

    for (const RouterView& router : resizedProjection.routers) {
        if (!newRouterIds.contains(router.id)) {
            continue;
        }
        DomainMembership membership = providedMemberships.value(
            router.id,
            DomainMembership{ElementRef{ElementKind::Router, router.id}, {}});
        completeRequiredAssignments(result.design,
                                    package,
                                    ElementKind::Router,
                                    router.id,
                                    membership.assignments,
                                    QStringLiteral("/newRouterMemberships/") + router.id,
                                    result.diagnostics);
        if (!membership.assignments.isEmpty()) {
            result.design.domainMemberships.append(std::move(membership));
        }
    }
    if (hasErrors(result.diagnostics)) {
        result.design = design;
    }
    return result;
}

MutationResult addEndpoint(
    const NocDesign& design,
    const PackageDefinition& package,
    EndpointInstance endpoint,
    const QHash<QString, QStringList>& domainAssignments) {
    MutationResult result;
    result.design = design;
    QHash<QString, QStringList> assignments = domainAssignments;
    completeRequiredAssignments(result.design,
                                package,
                                ElementKind::Endpoint,
                                endpoint.id,
                                assignments,
                                QStringLiteral("/domainMemberships/new-endpoint"),
                                result.diagnostics);
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    result.design.endpoints.append(std::move(endpoint));
    setMembershipAssignments(
        result.design,
        ElementRef{ElementKind::Endpoint, result.design.endpoints.constLast().id},
        assignments);
    return result;
}

NocDesign removeEndpointReferences(const NocDesign& design,
                                   const QString& endpointId) {
    NocDesign edited = design;
    edited.domainMemberships.erase(
        std::remove_if(
            edited.domainMemberships.begin(),
            edited.domainMemberships.end(),
            [&](const DomainMembership& membership) {
                return membership.element.kind == ElementKind::Endpoint
                    && membership.element.id == endpointId;
            }),
        edited.domainMemberships.end());
    edited.edgeOverrides.erase(
        std::remove_if(
            edited.edgeOverrides.begin(),
            edited.edgeOverrides.end(),
            [&](const DomainEdgeOverride& edgeOverride) {
                return edgeOverride.edge.kind == ElementKind::EndpointAttachment
                    && edgeOverride.edge.id == endpointId;
            }),
        edited.edgeOverrides.end());
    return edited;
}

MutationResult addDomain(const NocDesign& design,
                         const PackageDefinition& package,
                         DomainDefinition domain) {
    MutationResult result;
    result.design = design;
    domain.id = domain.id.trimmed();
    domain.type = domain.type.trimmed();
    domain.name = domain.name.trimmed();
    if (std::any_of(design.domains.cbegin(),
                    design.domains.cend(),
                    [&](const DomainDefinition& existing) {
                        return existing.id == domain.id;
                    })) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain.duplicate_id"),
                         QStringLiteral("Domain id is already in use"),
                         QStringLiteral("/domains"));
        return result;
    }
    if (const DomainTypeDefinition* type = package.domainType(domain.type)) {
        const QJsonObject provided = domain.properties;
        domain.properties = defaultsFor(type->properties);
        mergeValues(domain.properties, provided);
    }
    result.design.domains.append(std::move(domain));
    return result;
}

MutationResult updateDomain(const NocDesign& design,
                            const QString& domainId,
                            DomainDefinition domain) {
    MutationResult result;
    result.design = design;
    const QString requestedId = domainId.trimmed();
    const auto it = std::find_if(
        result.design.domains.begin(),
        result.design.domains.end(),
        [&](const DomainDefinition& existing) {
            return existing.id == requestedId;
        });
    if (it == result.design.domains.end()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain.not_found"),
                         QStringLiteral("Domain does not exist"),
                         QStringLiteral("/domains"));
        return result;
    }
    domain.id = domain.id.trimmed();
    domain.type = domain.type.trimmed();
    if (domain.id != it->id || domain.type != it->type) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain.update_identity_forbidden"),
                         QStringLiteral("updateDomain cannot change Domain id or type"),
                         QStringLiteral("/domains"));
        return result;
    }
    it->name = domain.name.trimmed();
    it->properties = std::move(domain.properties);
    return result;
}

MutationResult removeDomain(const NocDesign& design,
                            const PackageDefinition& package,
                            const QString& domainId) {
    MutationResult result;
    result.design = design;
    const QString requestedId = domainId.trimmed();
    const auto target = std::find_if(
        design.domains.cbegin(),
        design.domains.cend(),
        [&](const DomainDefinition& domain) {
            return domain.id == requestedId;
        });
    if (target == design.domains.cend()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain.not_found"),
                         QStringLiteral("Domain does not exist"),
                         QStringLiteral("/domains"));
        return result;
    }

    const auto appendReference = [&](const QString& message,
                                     const QString& path) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain.remove_referenced"),
                         message,
                         path,
                         QStringLiteral("package"));
    };
    QHash<QString, const DomainDefinition*> domainsById;
    for (const DomainDefinition& domain : design.domains) {
        if (!domainsById.contains(domain.id)) {
            domainsById.insert(domain.id, &domain);
        }
    }
    for (qsizetype index = 0; index < design.domainMemberships.size(); ++index) {
        const DomainMembership& membership = design.domainMemberships.at(index);
        for (auto assignment = membership.assignments.constBegin();
             assignment != membership.assignments.constEnd(); ++assignment) {
            for (qsizetype assignmentIndex = 0;
                 assignmentIndex < assignment.value().size(); ++assignmentIndex) {
                if (assignment.value().at(assignmentIndex) == requestedId) {
                    appendReference(
                        QStringLiteral("Domain is assigned to an element"),
                        QStringLiteral("/domainMemberships/%1/assignments/%2/%3")
                            .arg(index)
                            .arg(assignment.key())
                            .arg(assignmentIndex));
                }
            }
        }
    }
    for (qsizetype index = 0; index < design.domainRelations.size(); ++index) {
        const DomainRelation& relation = design.domainRelations.at(index);
        const QString base = QStringLiteral("/domainRelations/%1").arg(index);
        if (relation.from == requestedId) {
            appendReference(QStringLiteral("Domain is a relation source"),
                            base + QStringLiteral("/from"));
        }
        if (relation.to == requestedId) {
            appendReference(QStringLiteral("Domain is a relation target"),
                            base + QStringLiteral("/to"));
        }
        const auto source = domainsById.constFind(relation.from);
        if (source != domainsById.constEnd()) {
            if (const DomainTypeDefinition* sourceType =
                    package.domainType(source.value()->type)) {
                if (const DomainRelationDefinition* relationType =
                        domainRelationDefinition(*sourceType, relation.type)) {
                    appendDomainPropertyReferenceDiagnostics(
                        relation.properties,
                        relationType->properties,
                        requestedId,
                        base + QStringLiteral("/properties"),
                        result.diagnostics);
                }
            }
        }
    }
    for (qsizetype index = 0; index < design.crossingPolicies.size(); ++index) {
        const DomainCrossingPolicy& policy = design.crossingPolicies.at(index);
        const QString base = QStringLiteral("/crossingPolicies/%1").arg(index);
        if (policy.from == requestedId) {
            appendReference(QStringLiteral("Domain is a crossing policy source"),
                            base + QStringLiteral("/from"));
        }
        if (policy.to == requestedId) {
            appendReference(QStringLiteral("Domain is a crossing policy target"),
                            base + QStringLiteral("/to"));
        }
        if (const DomainTypeDefinition* type =
                package.domainType(policy.domainType)) {
            appendDomainPropertyReferenceDiagnostics(
                policy.properties,
                type->crossingProperties,
                requestedId,
                base + QStringLiteral("/properties"),
                result.diagnostics);
        }
    }
    for (qsizetype index = 0; index < design.edgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride = design.edgeOverrides.at(index);
        if (const DomainTypeDefinition* type =
                package.domainType(edgeOverride.domainType)) {
            appendDomainPropertyReferenceDiagnostics(
                edgeOverride.properties,
                type->crossingProperties,
                requestedId,
                QStringLiteral("/edgeOverrides/%1/properties").arg(index),
                result.diagnostics);
        }
    }
    for (qsizetype index = 0; index < design.domains.size(); ++index) {
        const DomainDefinition& domain = design.domains.at(index);
        if (const DomainTypeDefinition* type = package.domainType(domain.type)) {
            appendDomainPropertyReferenceDiagnostics(
                domain.properties,
                type->properties,
                requestedId,
                QStringLiteral("/domains/%1/properties").arg(index),
                result.diagnostics);
        }
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    result.design.domains.erase(
        std::remove_if(
            result.design.domains.begin(),
            result.design.domains.end(),
            [&](const DomainDefinition& domain) {
                return domain.id == requestedId;
            }),
        result.design.domains.end());
    return result;
}

MutationResult assignDomainsToElements(
    const NocDesign& design,
    const QVector<ElementRef>& elements,
    const QString& domainType,
    const QStringList& domainIds) {
    MutationResult result;
    result.design = design;
    const QString normalizedType = domainType.trimmed();
    if (elements.isEmpty()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain_assignment.no_elements"),
                         QStringLiteral("at least one element is required"),
                         QStringLiteral("/domainMemberships"));
        return result;
    }
    if (domainIds.isEmpty()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain_assignment.empty"),
                         QStringLiteral("assignDomainsToElements requires at least one Domain"),
                         QStringLiteral("/domainMemberships"));
        return result;
    }

    QStringList normalizedDomainIds;
    normalizedDomainIds.reserve(domainIds.size());
    for (const QString& id : domainIds) {
        normalizedDomainIds.append(id.trimmed());
    }
    QSet<QString> seenElements;
    for (const ElementRef& element : elements) {
        const QString key = elementReferenceKey(element);
        if (seenElements.contains(key)) {
            continue;
        }
        seenElements.insert(key);
        setMembershipAssignments(
            result.design,
            element,
            QHash<QString, QStringList>{{normalizedType, normalizedDomainIds}});
    }
    return result;
}

MutationResult clearDomainAssignment(
    const NocDesign& design,
    const PackageDefinition& package,
    const QVector<ElementRef>& elements,
    const QString& domainType) {
    MutationResult result;
    result.design = design;
    if (elements.isEmpty()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain_assignment.no_elements"),
                         QStringLiteral("at least one element is required"),
                         QStringLiteral("/domainMemberships"));
        return result;
    }
    const QString normalizedType = domainType.trimmed();
    if (!package.domainType(normalizedType)) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain_assignment.unknown_type"),
                         QStringLiteral("Domain assignment type is not declared by the Package"),
                         QStringLiteral("/domainMemberships"),
                         QStringLiteral("package"));
        return result;
    }
    QSet<QString> elementKeys;
    for (const ElementRef& element : elements) {
        elementKeys.insert(elementReferenceKey(element));
    }
    for (DomainMembership& membership : result.design.domainMemberships) {
        if (elementKeys.contains(elementReferenceKey(membership.element))) {
            membership.assignments.remove(normalizedType);
        }
    }
    result.design.domainMemberships.erase(
        std::remove_if(
            result.design.domainMemberships.begin(),
            result.design.domainMemberships.end(),
            [](const DomainMembership& membership) {
                return membership.assignments.isEmpty();
            }),
        result.design.domainMemberships.end());
    return result;
}

QVector<Diagnostic> validateAgainstPackage(
    const NocDesign& design,
    const PackageDefinition& package) {
    QVector<Diagnostic> diagnostics;
    const int requiredDesignVersion = package.formatVersion == 2 ? 2 : 1;
    if (design.formatVersion != requiredDesignVersion) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.package_format_version_mismatch"),
            QStringLiteral("Package formatVersion %1 requires Design formatVersion %2")
                .arg(package.formatVersion)
                .arg(requiredDesignVersion),
            QStringLiteral("/formatVersion"),
            QStringLiteral("package"));
    }

    QHash<QString, const DomainDefinition*> domainsById;
    QHash<QString, int> domainCountsByType;
    for (const DomainDefinition& domain : design.domains) {
        if (!domainsById.contains(domain.id)) {
            domainsById.insert(domain.id, &domain);
        }
        domainCountsByType[domain.type] += 1;
    }
    for (qsizetype index = 0; index < design.domains.size(); ++index) {
        const DomainDefinition& domain = design.domains.at(index);
        const QString base = QStringLiteral("/domains/%1").arg(index);
        const DomainTypeDefinition* type = package.domainType(domain.type);
        if (!type) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain.unknown_type"),
                             QStringLiteral("Domain type is not declared by the Package"),
                             base + QStringLiteral("/type"),
                             QStringLiteral("package"));
            continue;
        }
        diagnostics += validateDomainPropertyObject(
            domain.properties,
            type->properties,
            domainsById,
            base + QStringLiteral("/properties"),
            DomainPropertyValidationMode::Complete);
    }
    for (const DomainTypeDefinition& type : package.domainTypes) {
        if (type.required && domainCountsByType.value(type.id) == 0) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain.required_type_missing"),
                             QStringLiteral("required Domain type %1 has no instances")
                                 .arg(type.id),
                             QStringLiteral("/domains"),
                             QStringLiteral("package"));
        }
    }

    QHash<QString, const DomainMembership*> membershipsByElement;
    for (qsizetype index = 0; index < design.domainMemberships.size(); ++index) {
        const DomainMembership& membership = design.domainMemberships.at(index);
        const QString base = QStringLiteral("/domainMemberships/%1").arg(index);
        const QString key = elementReferenceKey(membership.element);
        if (!membershipsByElement.contains(key)) {
            membershipsByElement.insert(key, &membership);
        }
        for (auto assignment = membership.assignments.constBegin();
             assignment != membership.assignments.constEnd(); ++assignment) {
            const QString assignmentPath = base + QStringLiteral("/assignments/")
                + assignment.key();
            const DomainTypeDefinition* type = package.domainType(assignment.key());
            if (!type) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_assignment.unknown_type"),
                                 QStringLiteral("Domain assignment type is not declared by the Package"),
                                 assignmentPath,
                                 QStringLiteral("package"));
                continue;
            }
            if (!domainTypeAppliesTo(*type, membership.element.kind)) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_assignment.not_applicable"),
                                 QStringLiteral("Domain type %1 does not apply to this element kind")
                                     .arg(type->id),
                                 assignmentPath,
                                 QStringLiteral("package"));
            }
            if (type->cardinality == DomainCardinality::Single
                && assignment.value().size() > 1) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("domain_assignment.cardinality"),
                                 QStringLiteral("Domain type %1 allows only one assignment")
                                     .arg(type->id),
                                 assignmentPath,
                                 QStringLiteral("package"));
            }
        }
    }

    const auto validateRequiredMembership = [&](const ElementRef& element,
                                                const QString& path) {
        const DomainMembership* membership = membershipsByElement.value(
            elementReferenceKey(element), nullptr);
        for (const DomainTypeDefinition& type : package.domainTypes) {
            if (!type.required || !domainTypeAppliesTo(type, element.kind)) {
                continue;
            }
            if (!membership || membership->assignments.value(type.id).isEmpty()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("domain_assignment.required"),
                    QStringLiteral("Element %1 requires a %2 Domain assignment")
                        .arg(element.id, type.id),
                    path + QStringLiteral("/assignments/") + type.id,
                    QStringLiteral("package"));
            }
        }
    };
    if (canProjectTopology(design.topology)) {
        const TopologyProjection projection = projectTopology(design);
        for (const RouterView& router : projection.routers) {
            validateRequiredMembership(
                ElementRef{ElementKind::Router, router.id},
                QStringLiteral("/domainMemberships/") + router.id);
        }
    }
    for (const EndpointInstance& endpoint : design.endpoints) {
        validateRequiredMembership(
            ElementRef{ElementKind::Endpoint, endpoint.id},
            QStringLiteral("/domainMemberships/") + endpoint.id);
    }

    QHash<QString, int> relationCounts;
    const auto relationCountKey = [](const QString& from,
                                     const QString& relationType) {
        return from + QChar(0x1f) + relationType;
    };
    for (qsizetype index = 0; index < design.domainRelations.size(); ++index) {
        const DomainRelation& relation = design.domainRelations.at(index);
        const QString base = QStringLiteral("/domainRelations/%1").arg(index);
        const auto source = domainsById.constFind(relation.from);
        if (source == domainsById.constEnd()) {
            continue;
        }
        const DomainTypeDefinition* sourceType = package.domainType(
            source.value()->type);
        if (!sourceType) {
            continue;
        }
        const DomainRelationDefinition* relationType =
            domainRelationDefinition(*sourceType, relation.type);
        if (!relationType) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_relation.unknown_schema"),
                             QStringLiteral("relation type is not declared for the source Domain type"),
                             base + QStringLiteral("/type"),
                             QStringLiteral("package"));
            continue;
        }
        const QString countKey = relationCountKey(relation.from, relation.type);
        relationCounts[countKey] += 1;
        if (relationType->cardinality == DomainCardinality::Single
            && relationCounts.value(countKey) > 1) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_relation.cardinality"),
                             QStringLiteral("relation type %1 allows only one target")
                                 .arg(relation.type),
                             base,
                             QStringLiteral("package"));
        }
        const auto destination = domainsById.constFind(relation.to);
        if (destination != domainsById.constEnd()
            && !relationType->targetTypes.contains(destination.value()->type)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_relation.invalid_target_type"),
                             QStringLiteral("relation target Domain type is not allowed"),
                             base + QStringLiteral("/to"),
                             QStringLiteral("package"));
        }
        diagnostics += validateDomainPropertyObject(
            relation.properties,
            relationType->properties,
            domainsById,
            base + QStringLiteral("/properties"),
            DomainPropertyValidationMode::Complete);
    }
    for (const DomainDefinition& domain : design.domains) {
        const DomainTypeDefinition* type = package.domainType(domain.type);
        if (!type) {
            continue;
        }
        for (const DomainRelationDefinition& relation : type->relations) {
            if (relation.required
                && relationCounts.value(relationCountKey(domain.id, relation.id)) == 0) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("domain_relation.required"),
                    QStringLiteral("Domain %1 requires relation %2")
                        .arg(domain.id, relation.id),
                    QStringLiteral("/domainRelations"),
                    QStringLiteral("package"));
            }
        }
    }

    QHash<QString, const DomainCrossingPolicy*> policiesById;
    for (qsizetype index = 0; index < design.crossingPolicies.size(); ++index) {
        const DomainCrossingPolicy& policy = design.crossingPolicies.at(index);
        const QString base = QStringLiteral("/crossingPolicies/%1").arg(index);
        if (!policiesById.contains(policy.id)) {
            policiesById.insert(policy.id, &policy);
        }
        const DomainTypeDefinition* type = package.domainType(policy.domainType);
        if (!type) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_policy.unknown_domain_type"),
                             QStringLiteral("crossing policy Domain type is not declared by the Package"),
                             base + QStringLiteral("/domainType"),
                             QStringLiteral("package"));
            continue;
        }
        diagnostics += validateDomainPropertyObject(
            policy.properties,
            type->crossingProperties,
            domainsById,
            base + QStringLiteral("/properties"),
            DomainPropertyValidationMode::Complete);
    }

    const QVector<DomainCrossingView> projectedCrossings =
        canProjectTopology(design.topology)
        ? projectDomainCrossings(design)
        : QVector<DomainCrossingView>{};
    QHash<QString, const DomainCrossingView*> crossingsByEdgeAndType;
    for (const DomainCrossingView& crossing : projectedCrossings) {
        crossingsByEdgeAndType.insert(
            elementReferenceKey(crossing.edge) + QChar(0x1f) + crossing.domainType,
            &crossing);
    }
    for (qsizetype index = 0; index < design.edgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride = design.edgeOverrides.at(index);
        const QString base = QStringLiteral("/edgeOverrides/%1").arg(index);
        const DomainTypeDefinition* type = package.domainType(
            edgeOverride.domainType);
        if (!type) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_edge_override.unknown_domain_type"),
                             QStringLiteral("edge override Domain type is not declared by the Package"),
                             base + QStringLiteral("/domainType"),
                             QStringLiteral("package"));
            continue;
        }
        diagnostics += validateDomainPropertyObject(
            edgeOverride.properties,
            type->crossingProperties,
            domainsById,
            base + QStringLiteral("/properties"),
            DomainPropertyValidationMode::Partial);

        const QString crossingKey = elementReferenceKey(edgeOverride.edge)
            + QChar(0x1f) + edgeOverride.domainType;
        const DomainCrossingView* crossing = crossingsByEdgeAndType.value(
            crossingKey, nullptr);
        if (!crossing) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_edge_override.not_a_crossing"),
                             QStringLiteral("edge is not a crossing for this Domain type"),
                             base + QStringLiteral("/edge"),
                             QStringLiteral("package"));
            continue;
        }
        const DomainCrossingPolicy* policy = policiesById.value(
            edgeOverride.policy, nullptr);
        if (policy
            && (!crossing->fromDomains.contains(policy->from)
                || !crossing->toDomains.contains(policy->to))) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("domain_edge_override.policy_pair_mismatch"),
                             QStringLiteral("override policy does not match the edge's Domain crossing"),
                             base + QStringLiteral("/policy"),
                             QStringLiteral("package"));
        }
    }
    return diagnostics;
}

} // namespace finepaper::domain_service
