#include "noc/model.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>
#include <functional>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics, const QString& code) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

NocDesign domainDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain_model_test");
    design.name = QStringLiteral("Domain model test");
    design.package = PackageReference{
        QStringLiteral("test.domain-model"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 2, 2};
    design.endpoints.append(EndpointInstance{
        QStringLiteral("ep0"),
        QStringLiteral("client"),
        EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
        QJsonObject{}
    });

    design.domains = {
        DomainDefinition{
            QStringLiteral("clk_a"),
            QStringLiteral("clock"),
            QStringLiteral("Clock A"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 800}}
        },
        DomainDefinition{
            QStringLiteral("clk_b"),
            QStringLiteral("clock"),
            QStringLiteral("Clock B"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 400}}
        },
        DomainDefinition{
            QStringLiteral("pd_main"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            QJsonObject{{QStringLiteral("voltageMv"), 900}}
        }
    };

    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_a")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_b")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_a")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        }
    };

    design.domainRelations.append(DomainRelation{
        QStringLiteral("synchronous-peer"),
        QStringLiteral("clk_a"),
        QStringLiteral("clk_b"),
        QJsonObject{{QStringLiteral("latency"), 1}}
    });
    design.crossingPolicies.append(DomainCrossingPolicy{
        QStringLiteral("clock_async"),
        QStringLiteral("clock"),
        QStringLiteral("clk_a"),
        QStringLiteral("clk_b"),
        QJsonObject{{QStringLiteral("synchronizerStages"), 2}}
    });
    design.edgeOverrides.append(DomainEdgeOverride{
        ElementRef{
            ElementKind::RouterLink,
            linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
        },
        QStringLiteral("clock"),
        QStringLiteral("clock_async"),
        QJsonObject{{QStringLiteral("implementation"), QStringLiteral("async-fifo")}}
    });
    return design;
}

NocDesign projectionDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain_projection_test");
    design.name = QStringLiteral("Domain projection test");
    design.package = PackageReference{
        QStringLiteral("test.domain-projection"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep0"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            QJsonObject{}
        },
        EndpointInstance{
            QStringLiteral("ep1"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("0")},
            QJsonObject{}
        }
    };
    design.domains = {
        DomainDefinition{
            QStringLiteral("clk_a"),
            QStringLiteral("clock"),
            QStringLiteral("Clock A"),
            QJsonObject{}
        },
        DomainDefinition{
            QStringLiteral("clk_b"),
            QStringLiteral("clock"),
            QStringLiteral("Clock B"),
            QJsonObject{}
        },
        DomainDefinition{
            QStringLiteral("clk_c"),
            QStringLiteral("clock"),
            QStringLiteral("Clock C"),
            QJsonObject{}
        },
        DomainDefinition{
            QStringLiteral("pd_main"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            QJsonObject{}
        },
        DomainDefinition{
            QStringLiteral("pd_aux"),
            QStringLiteral("power"),
            QStringLiteral("Auxiliary power"),
            QJsonObject{}
        }
    };
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{
                    QStringLiteral("clk_b"), QStringLiteral("clk_a")
                }},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{
                    QStringLiteral("clk_a"), QStringLiteral("clk_b")
                }},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_aux")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_c")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep1")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{
                    QStringLiteral("clk_a"), QStringLiteral("clk_b")
                }},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        }
    };
    design.crossingPolicies = {
        DomainCrossingPolicy{
            QStringLiteral("power_transition"),
            QStringLiteral("power"),
            QStringLiteral("pd_main"),
            QStringLiteral("pd_aux"),
            QJsonObject{}
        },
        DomainCrossingPolicy{
            QStringLiteral("endpoint_clock"),
            QStringLiteral("clock"),
            QStringLiteral("clk_a"),
            QStringLiteral("clk_c"),
            QJsonObject{}
        }
    };
    design.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("power"),
            QStringLiteral("power_transition"),
            QJsonObject{{QStringLiteral("isolationCells"), 2}}
        },
        DomainEdgeOverride{
            ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")},
            QStringLiteral("clock"),
            QStringLiteral("endpoint_clock"),
            QJsonObject{{QStringLiteral("synchronizerStages"), 3}}
        }
    };
    return design;
}

const DomainCrossingView* findCrossing(
    const QVector<DomainCrossingView>& crossings,
    ElementKind edgeKind,
    const QString& edgeId,
    const QString& domainType) {
    const auto crossing = std::find_if(
        crossings.cbegin(), crossings.cend(), [&](const DomainCrossingView& value) {
            return value.edge.kind == edgeKind
                && value.edge.id == edgeId
                && value.domainType == domainType;
        });
    return crossing == crossings.cend() ? nullptr : &(*crossing);
}

void expectDiagnostic(const QString& code,
                      const QString& description,
                      const std::function<void(NocDesign&)>& mutate) {
    NocDesign design = domainDesign();
    mutate(design);
    const QVector<Diagnostic> diagnostics = validateDesignStructure(design);
    const bool found = hasDiagnosticCode(diagnostics, code);
    if (!found) {
        QTextStream stream(stderr);
        stream << "Diagnostics for " << description << ':';
        for (const Diagnostic& diagnostic : diagnostics) {
            stream << ' ' << diagnostic.code;
        }
        stream << Qt::endl;
    }
    check(found, description + QStringLiteral(" reports ") + code);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    QHash<ElementRef, QString> semanticReferences;
    semanticReferences.insert(
        ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
        QStringLiteral("router"));
    semanticReferences.insert(
        ElementRef{ElementKind::RouterLink, QStringLiteral("link-r-0-0--r-1-0")},
        QStringLiteral("link"));
    check(semanticReferences.value(
              ElementRef{ElementKind::Router, QStringLiteral("r-0-0")})
              == QStringLiteral("router")
              && semanticReferences.value(
                  ElementRef{
                      ElementKind::RouterLink,
                      QStringLiteral("link-r-0-0--r-1-0")})
                  == QStringLiteral("link"),
          QStringLiteral("ElementRef is a strong QHash key for semantic GUI mappings"));

    const NocDesign valid = domainDesign();
    check(!hasErrors(validateDesignStructure(valid)),
          QStringLiteral("a complete clock/power Domain model is valid"));

    expectDiagnostic(
        QStringLiteral("design.domains_require_v2"),
        QStringLiteral("V1 Domain data"),
        [](NocDesign& design) { design.formatVersion = 1; });

    expectDiagnostic(
        QStringLiteral("domain.missing_id"),
        QStringLiteral("a Domain without an id"),
        [](NocDesign& design) { design.domains[0].id.clear(); });
    expectDiagnostic(
        QStringLiteral("domain.missing_type"),
        QStringLiteral("a Domain without a type"),
        [](NocDesign& design) { design.domains[0].type.clear(); });
    expectDiagnostic(
        QStringLiteral("domain.missing_name"),
        QStringLiteral("a Domain without a display name"),
        [](NocDesign& design) { design.domains[0].name.clear(); });
    expectDiagnostic(
        QStringLiteral("domain.duplicate_id"),
        QStringLiteral("globally duplicated Domain ids"),
        [](NocDesign& design) { design.domains[2].id = design.domains[0].id; });

    expectDiagnostic(
        QStringLiteral("domain_membership.unsupported_element_kind"),
        QStringLiteral("a membership on an unsupported element kind"),
        [](NocDesign& design) {
            design.domainMemberships[0].element.kind = ElementKind::RouterLink;
        });
    expectDiagnostic(
        QStringLiteral("domain_membership.unknown_element"),
        QStringLiteral("a membership referencing an unknown Router"),
        [](NocDesign& design) {
            design.domainMemberships[0].element.id = QStringLiteral("r-9-9");
        });
    expectDiagnostic(
        QStringLiteral("domain_membership.duplicate_element"),
        QStringLiteral("two memberships for the same element"),
        [](NocDesign& design) { design.domainMemberships.append(design.domainMemberships[0]); });
    expectDiagnostic(
        QStringLiteral("domain_membership.empty_membership"),
        QStringLiteral("a membership without any Domain assignments"),
        [](NocDesign& design) { design.domainMemberships[0].assignments.clear(); });
    expectDiagnostic(
        QStringLiteral("domain_membership.empty_assignment"),
        QStringLiteral("an empty Domain assignment list"),
        [](NocDesign& design) {
            design.domainMemberships[0].assignments[QStringLiteral("clock")].clear();
        });
    expectDiagnostic(
        QStringLiteral("domain_membership.duplicate_assignment"),
        QStringLiteral("a duplicated Domain assignment"),
        [](NocDesign& design) {
            design.domainMemberships[0].assignments[QStringLiteral("clock")]
                .append(QStringLiteral("clk_a"));
        });
    expectDiagnostic(
        QStringLiteral("domain_membership.unknown_domain"),
        QStringLiteral("an assignment referencing an unknown Domain"),
        [](NocDesign& design) {
            design.domainMemberships[0].assignments[QStringLiteral("clock")][0]
                = QStringLiteral("clk_missing");
        });
    expectDiagnostic(
        QStringLiteral("domain_membership.type_mismatch"),
        QStringLiteral("an assignment whose key disagrees with the Domain type"),
        [](NocDesign& design) {
            design.domainMemberships[0].assignments[QStringLiteral("clock")][0]
                = QStringLiteral("pd_main");
        });

    expectDiagnostic(
        QStringLiteral("domain_relation.missing_type"),
        QStringLiteral("a relation without a type"),
        [](NocDesign& design) { design.domainRelations[0].type.clear(); });
    expectDiagnostic(
        QStringLiteral("domain_relation.unknown_from"),
        QStringLiteral("a relation with an unknown source Domain"),
        [](NocDesign& design) { design.domainRelations[0].from = QStringLiteral("missing"); });
    expectDiagnostic(
        QStringLiteral("domain_relation.unknown_to"),
        QStringLiteral("a relation with an unknown target Domain"),
        [](NocDesign& design) { design.domainRelations[0].to = QStringLiteral("missing"); });
    expectDiagnostic(
        QStringLiteral("domain_relation.duplicate"),
        QStringLiteral("a duplicated Domain relation"),
        [](NocDesign& design) { design.domainRelations.append(design.domainRelations[0]); });

    expectDiagnostic(
        QStringLiteral("domain_policy.missing_id"),
        QStringLiteral("a crossing policy without an id"),
        [](NocDesign& design) { design.crossingPolicies[0].id.clear(); });
    expectDiagnostic(
        QStringLiteral("domain_policy.missing_domain_type"),
        QStringLiteral("a crossing policy without a Domain type"),
        [](NocDesign& design) { design.crossingPolicies[0].domainType.clear(); });
    expectDiagnostic(
        QStringLiteral("domain_policy.unknown_from"),
        QStringLiteral("a crossing policy with an unknown source Domain"),
        [](NocDesign& design) { design.crossingPolicies[0].from = QStringLiteral("missing"); });
    expectDiagnostic(
        QStringLiteral("domain_policy.unknown_to"),
        QStringLiteral("a crossing policy with an unknown target Domain"),
        [](NocDesign& design) { design.crossingPolicies[0].to = QStringLiteral("missing"); });
    expectDiagnostic(
        QStringLiteral("domain_policy.from_type_mismatch"),
        QStringLiteral("a crossing policy whose source has the wrong Domain type"),
        [](NocDesign& design) { design.crossingPolicies[0].from = QStringLiteral("pd_main"); });
    expectDiagnostic(
        QStringLiteral("domain_policy.duplicate_id"),
        QStringLiteral("duplicated crossing policy ids"),
        [](NocDesign& design) { design.crossingPolicies.append(design.crossingPolicies[0]); });

    expectDiagnostic(
        QStringLiteral("domain_edge_override.unsupported_edge_kind"),
        QStringLiteral("an override on an unsupported edge kind"),
        [](NocDesign& design) { design.edgeOverrides[0].edge.kind = ElementKind::Router; });
    expectDiagnostic(
        QStringLiteral("domain_edge_override.unknown_edge"),
        QStringLiteral("an override referencing an unknown Link"),
        [](NocDesign& design) {
            design.edgeOverrides[0].edge.id = linkId(
                QStringLiteral("r-1-0"), QStringLiteral("r-0-0"));
        });
    expectDiagnostic(
        QStringLiteral("domain_edge_override.missing_domain_type"),
        QStringLiteral("an override without a Domain type"),
        [](NocDesign& design) { design.edgeOverrides[0].domainType.clear(); });
    expectDiagnostic(
        QStringLiteral("domain_edge_override.unknown_policy"),
        QStringLiteral("an override referencing an unknown crossing policy"),
        [](NocDesign& design) { design.edgeOverrides[0].policy = QStringLiteral("missing"); });
    expectDiagnostic(
        QStringLiteral("domain_edge_override.policy_type_mismatch"),
        QStringLiteral("an override whose policy has the wrong Domain type"),
        [](NocDesign& design) { design.edgeOverrides[0].domainType = QStringLiteral("power"); });
    expectDiagnostic(
        QStringLiteral("domain_edge_override.duplicate"),
        QStringLiteral("two overrides for the same edge and Domain type"),
        [](NocDesign& design) { design.edgeOverrides.append(design.edgeOverrides[0]); });

    NocDesign validAttachmentOverride = domainDesign();
    validAttachmentOverride.edgeOverrides.append(DomainEdgeOverride{
        ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")},
        QStringLiteral("clock"),
        QStringLiteral("clock_async"),
        QJsonObject{}
    });
    check(!hasErrors(validateDesignStructure(validAttachmentOverride)),
          QStringLiteral("endpoint attachment edge references are accepted"));

    const NocDesign projectedDesign = projectionDesign();
    check(!hasErrors(validateDesignStructure(projectedDesign)),
          QStringLiteral("the Domain projection fixture is structurally valid"));
    const QVector<DomainCrossingView> crossings = projectDomainCrossings(projectedDesign);
    check(crossings.size() == 2,
          QStringLiteral("only edges whose Domain assignment sets differ become crossings"));

    const QString projectedLinkId = linkId(
        QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
    const DomainCrossingView* linkCrossing = findCrossing(
        crossings,
        ElementKind::RouterLink,
        projectedLinkId,
        QStringLiteral("power"));
    check(linkCrossing
              && linkCrossing->fromElement
                  == ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}
              && linkCrossing->toElement
                  == ElementRef{ElementKind::Router, QStringLiteral("r-1-0")}
              && linkCrossing->fromDomains
                  == QStringList{QStringLiteral("pd_main")}
              && linkCrossing->toDomains
                  == QStringList{QStringLiteral("pd_aux")},
          QStringLiteral("Router Link Domain crossings expose their endpoint assignments"));
    check(linkCrossing
              && linkCrossing->overridePolicy == QStringLiteral("power_transition")
              && linkCrossing->overrideProperties
                  == QJsonObject{{QStringLiteral("isolationCells"), 2}},
          QStringLiteral("Router Link overrides map policy and properties onto crossings"));

    const DomainCrossingView* attachmentCrossing = findCrossing(
        crossings,
        ElementKind::EndpointAttachment,
        QStringLiteral("ep0"),
        QStringLiteral("clock"));
    check(attachmentCrossing
              && attachmentCrossing->fromElement
                  == ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}
              && attachmentCrossing->toElement
                  == ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")}
              && attachmentCrossing->fromDomains
                  == QStringList{QStringLiteral("clk_a"), QStringLiteral("clk_b")}
              && attachmentCrossing->toDomains
                  == QStringList{QStringLiteral("clk_c")},
          QStringLiteral("Endpoint Attachment crossings are projected with normalized Domain sets"));
    check(attachmentCrossing
              && attachmentCrossing->overridePolicy == QStringLiteral("endpoint_clock")
              && attachmentCrossing->overrideProperties
                  == QJsonObject{{QStringLiteral("synchronizerStages"), 3}},
          QStringLiteral("Endpoint Attachment overrides map onto crossings"));
    check(findCrossing(crossings,
                       ElementKind::RouterLink,
                       projectedLinkId,
                       QStringLiteral("clock")) == nullptr,
          QStringLiteral("multiple Domain assignments compare as unordered sets"));
    check(std::none_of(
              crossings.cbegin(), crossings.cend(), [](const DomainCrossingView& crossing) {
                  return crossing.edge.kind == ElementKind::EndpointAttachment
                      && crossing.edge.id == QStringLiteral("ep1");
              }),
          QStringLiteral("equal Router and Endpoint Domain sets do not create a crossing"));

    const std::optional<RouterPosition> routerPosition = routerPositionFromId(
        QStringLiteral("r-1-0"));
    check(routerPosition && *routerPosition == RouterPosition{1, 0}
              && !routerPositionFromId(QStringLiteral("r-01-0"))
              && !routerPositionFromId(QStringLiteral("router-1-0")),
          QStringLiteral("Router ids are parsed only in canonical stable form"));

    check(designReferenceExists(
              projectedDesign,
              ElementRef{ElementKind::Router, QStringLiteral("r-1-0")})
              && designReferenceExists(
                  projectedDesign,
                  ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")})
              && designReferenceExists(
                  projectedDesign,
                  ElementRef{ElementKind::RouterLink, projectedLinkId})
              && designReferenceExists(
                  projectedDesign,
                  ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")})
              && !designReferenceExists(
                  projectedDesign,
                  ElementRef{ElementKind::Router, QStringLiteral("r-2-0")}),
          QStringLiteral("design references resolve all supported element and edge kinds"));

    const auto linkEndpoints = edgeEndpoints(
        projectedDesign,
        ElementRef{ElementKind::RouterLink, projectedLinkId});
    check(linkEndpoints
              && linkEndpoints->first
                  == ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}
              && linkEndpoints->second
                  == ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
          QStringLiteral("Router Link endpoint references are resolved"));
    const auto attachmentEndpoints = edgeEndpoints(
        projectedDesign,
        ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")});
    check(attachmentEndpoints
              && attachmentEndpoints->first
                  == ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}
              && attachmentEndpoints->second
                  == ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")}
              && !edgeEndpoints(
                  projectedDesign,
                  ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}),
          QStringLiteral("Endpoint Attachment endpoints resolve and non-edge references do not"));

    const ResolvedDesign resolved = resolveDesign(projectedDesign);
    const auto resolvedEndpoint = std::find_if(
        resolved.design.endpoints.cbegin(),
        resolved.design.endpoints.cend(),
        [](const EndpointInstance& endpoint) {
            return endpoint.id == QStringLiteral("ep0");
        });
    const auto projectedEndpoint = std::find_if(
        resolved.topology.endpoints.cbegin(),
        resolved.topology.endpoints.cend(),
        [](const EndpointView& endpoint) {
            return endpoint.id == QStringLiteral("ep0");
        });
    check(!projectedDesign.endpoints[0].attachment.slot
              && resolvedEndpoint != resolved.design.endpoints.cend()
              && resolvedEndpoint->attachment.slot == QStringLiteral("1")
              && projectedEndpoint != resolved.topology.endpoints.cend()
              && projectedEndpoint->slot == QStringLiteral("1"),
          QStringLiteral("resolveDesign materializes automatic slots in both design and topology"));
    check(resolved.domainCrossings == crossings,
          QStringLiteral("resolveDesign includes the Domain crossing projection"));

    if (failures == 0) {
        QTextStream(stdout) << "Domain model tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Domain model test(s) failed" << Qt::endl;
    return 1;
}
