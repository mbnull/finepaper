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

    if (failures == 0) {
        QTextStream(stdout) << "Domain model tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Domain model test(s) failed" << Qt::endl;
    return 1;
}
