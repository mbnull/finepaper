#include "application/mesh_resize_plan.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics,
                       const QString& code) {
    return std::any_of(
        diagnostics.cbegin(),
        diagnostics.cend(),
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

int diagnosticCount(const QVector<Diagnostic>& diagnostics,
                    const QString& code) {
    return static_cast<int>(std::count_if(
        diagnostics.cbegin(),
        diagnostics.cend(),
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; }));
}

const MeshResizeDomainAssignmentPlan* assignmentPlan(
    const MeshResizePlan& plan,
    const QString& domainType) {
    const auto found = std::find_if(
        plan.routerAssignmentPlans.cbegin(),
        plan.routerAssignmentPlans.cend(),
        [&](const MeshResizeDomainAssignmentPlan& value) {
            return value.domainType == domainType;
        });
    return found == plan.routerAssignmentPlans.cend() ? nullptr : &*found;
}

const DomainMembership* membershipFor(
    const MeshResizeAssignmentResolution& resolution,
    const QString& routerId) {
    const auto found = std::find_if(
        resolution.newRouterMemberships.cbegin(),
        resolution.newRouterMemberships.cend(),
        [&](const DomainMembership& membership) {
            return membership.element
                == ElementRef{ElementKind::Router, routerId};
        });
    return found == resolution.newRouterMemberships.cend() ? nullptr : &*found;
}

PackageDefinition packageDefinition() {
    DomainTypeDefinition clock;
    clock.id = QStringLiteral("clock");
    clock.label = QStringLiteral("Clock domain");
    clock.appliesTo = {ElementKind::Router, ElementKind::Endpoint};
    clock.cardinality = DomainCardinality::Single;
    clock.required = true;
    clock.assignmentRules = {
        DomainAssignmentRule{ElementKind::Router, 1, qsizetype{1}},
        DomainAssignmentRule{ElementKind::Endpoint, 1, qsizetype{1}}
    };

    DomainTypeDefinition power;
    power.id = QStringLiteral("power");
    power.label = QStringLiteral("Power domain");
    power.appliesTo = {ElementKind::Router};
    power.cardinality = DomainCardinality::Single;
    power.required = true;
    power.assignmentRules = {
        DomainAssignmentRule{ElementKind::Router, 1, qsizetype{1}}
    };

    DomainTypeDefinition tags;
    tags.id = QStringLiteral("tag");
    tags.label = QStringLiteral("Router tags");
    tags.appliesTo = {ElementKind::Router};
    tags.cardinality = DomainCardinality::Multiple;
    tags.assignmentRules = {
        DomainAssignmentRule{ElementKind::Router, 0, std::nullopt}
    };

    DomainTypeDefinition endpointSecurity;
    endpointSecurity.id = QStringLiteral("endpoint-security");
    endpointSecurity.appliesTo = {
        ElementKind::Router, ElementKind::Endpoint
    };
    endpointSecurity.cardinality = DomainCardinality::Single;
    endpointSecurity.required = true;
    endpointSecurity.assignmentRules = {
        DomainAssignmentRule{ElementKind::Endpoint, 1, qsizetype{1}}
    };

    PackageDefinition package;
    package.formatVersion = 2;
    package.id = QStringLiteral("test.mesh-resize-plan");
    package.version = QStringLiteral("1.0.0");
    package.mesh.minimumRows = 1;
    package.mesh.maximumRows = 8;
    package.mesh.defaultRows = 1;
    package.mesh.minimumColumns = 1;
    package.mesh.maximumColumns = 8;
    package.mesh.defaultColumns = 2;
    package.domainTypes = {clock, power, tags, endpointSecurity};
    return package;
}

NocDesign designFixture() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("mesh-resize-plan");
    design.name = QStringLiteral("Mesh resize plan");
    design.package = PackageReference{
        QStringLiteral("test.mesh-resize-plan"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.domains = {
        DomainDefinition{
            QStringLiteral("clock-a"),
            QStringLiteral("clock"),
            QStringLiteral("Clock A"),
            {}
        },
        DomainDefinition{
            QStringLiteral("clock-b"),
            QStringLiteral("clock"),
            QStringLiteral("Clock B"),
            {}
        },
        DomainDefinition{
            QStringLiteral("power-main"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            {}
        },
        DomainDefinition{
            QStringLiteral("tag-b"),
            QStringLiteral("tag"),
            QStringLiteral("Tag B"),
            {}
        },
        DomainDefinition{
            QStringLiteral("tag-a"),
            QStringLiteral("tag"),
            QStringLiteral("Tag A"),
            {}
        },
        DomainDefinition{
            QStringLiteral("endpoint-secure"),
            QStringLiteral("endpoint-security"),
            QStringLiteral("Endpoint secure"),
            {}
        }
    };
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"),
                 QStringList{QStringLiteral("clock-a")}},
                {QStringLiteral("power"),
                 QStringList{QStringLiteral("power-main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"),
                 QStringList{QStringLiteral("clock-b")}},
                {QStringLiteral("power"),
                 QStringList{QStringLiteral("power-main")}}
            }
        }
    };
    return design;
}

QVector<DomainMembership> explicitClockChoices(
    const MeshResizePlan& plan) {
    QVector<DomainMembership> memberships;
    for (qsizetype index = 0; index < plan.newRouters.size(); ++index) {
        const MeshResizeRouterPlan& router = plan.newRouters.at(index);
        QHash<QString, QStringList> assignments{
            {QStringLiteral("clock"),
             QStringList{index % 2 == 0
                             ? QStringLiteral("clock-b")
                             : QStringLiteral("clock-a")}}
        };
        if (index == 0) {
            assignments.insert(
                QStringLiteral("tag"),
                QStringList{
                    QStringLiteral(" tag-b "),
                    QStringLiteral("tag-a"),
                    QStringLiteral("tag-a")
                });
        }
        memberships.append(DomainMembership{router.element, assignments});
    }
    return memberships;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const PackageDefinition package = packageDefinition();
    const NocDesign design = designFixture();
    const MeshResizePlan grow = buildMeshResizePlan(design, package, 2, 3);

    check(!grow.hasBlockingDiagnostics()
              && grow.newRouters.size() == 4
              && grow.newRouters.at(0).element.id == QStringLiteral("r-2-0")
              && grow.newRouters.at(1).element.id == QStringLiteral("r-0-1")
              && grow.newRouters.at(3).element.id == QStringLiteral("r-2-1"),
          QStringLiteral("growth planning derives every new Router deterministically"));
    check(!grow.newRouterLinks.isEmpty()
              && grow.removedRouters.isEmpty()
              && grow.removedRouterLinks.isEmpty(),
          QStringLiteral("growth planning exposes derived Link changes without removing intent"));

    const MeshResizeDomainAssignmentPlan* clock = assignmentPlan(
        grow, QStringLiteral("clock"));
    const MeshResizeDomainAssignmentPlan* power = assignmentPlan(
        grow, QStringLiteral("power"));
    const MeshResizeDomainAssignmentPlan* tag = assignmentPlan(
        grow, QStringLiteral("tag"));
    check(clock
              && clock->assignmentRule.elementKind == ElementKind::Router
              && clock->assignmentRule.minimumAssignments == 1
              && clock->assignmentRule.maximumAssignments == 1
              && clock->availableDomainIds
                  == QStringList{QStringLiteral("clock-a"),
                                 QStringLiteral("clock-b")}
              && clock->automaticAssignment.isEmpty()
              && clock->requiresExplicitChoice(),
          QStringLiteral("multiple required instances remain an explicit choice"));
    check(power && power->assignmentRule.requiresAssignment()
              && power->automaticAssignment
                  == QStringList{QStringLiteral("power-main")}
              && !power->requiresExplicitChoice(),
          QStringLiteral("a unique required instance is the only automatic assignment"));
    check(tag && tag->assignmentRule.minimumAssignments == 0
              && !tag->assignmentRule.maximumAssignments
              && tag->automaticAssignment.isEmpty(),
          QStringLiteral("optional Router Domains remain optional even when choices exist"));
    check(!assignmentPlan(grow, QStringLiteral("endpoint-security"))
              && grow.requiresExplicitAssignments()
              && !grow.canApplyWithoutAssignments(),
          QStringLiteral("Router planning excludes Endpoint-only types and reports missing choices"));

    PackageDefinition minimumRulePackage = package;
    auto minimumTag = std::find_if(
        minimumRulePackage.domainTypes.begin(),
        minimumRulePackage.domainTypes.end(),
        [](const DomainTypeDefinition& type) {
            return type.id == QStringLiteral("tag");
        });
    minimumTag->assignmentRules = {
        DomainAssignmentRule{ElementKind::Router, 2, std::nullopt}
    };
    const MeshResizePlan minimumRulePlan = buildMeshResizePlan(
        design, minimumRulePackage, 2, 3);
    const MeshResizeDomainAssignmentPlan* minimumTagPlan = assignmentPlan(
        minimumRulePlan, QStringLiteral("tag"));
    const MeshResizeAssignmentResolution minimumRuleResolution =
        resolveMeshResizeAssignments(
            minimumRulePlan, explicitClockChoices(minimumRulePlan));
    const DomainMembership* minimumRuleRouter = membershipFor(
        minimumRuleResolution, QStringLiteral("r-0-1"));
    check(minimumTagPlan
              && minimumTagPlan->assignmentRule.minimumAssignments == 2
              && minimumTagPlan->automaticAssignment
                  == QStringList{QStringLiteral("tag-a"),
                                 QStringLiteral("tag-b")}
              && !minimumTagPlan->requiresExplicitChoice()
              && minimumRuleResolution.success()
              && minimumRuleRouter
              && minimumRuleRouter->assignments.value(QStringLiteral("tag"))
                  == minimumTagPlan->automaticAssignment,
          QStringLiteral("canonical minimum assignments drive Mesh auto-completion independently of legacy required"));

    PackageDefinition maximumRulePackage = package;
    auto maximumTag = std::find_if(
        maximumRulePackage.domainTypes.begin(),
        maximumRulePackage.domainTypes.end(),
        [](const DomainTypeDefinition& type) {
            return type.id == QStringLiteral("tag");
        });
    maximumTag->assignmentRules = {
        DomainAssignmentRule{ElementKind::Router, 0, qsizetype{1}}
    };
    const MeshResizePlan maximumRulePlan = buildMeshResizePlan(
        design, maximumRulePackage, 2, 3);
    const MeshResizeAssignmentResolution maximumRuleResolution =
        resolveMeshResizeAssignments(
            maximumRulePlan, explicitClockChoices(maximumRulePlan));
    check(!maximumRuleResolution.success()
              && hasDiagnosticCode(
                  maximumRuleResolution.diagnostics,
                  QStringLiteral("domain_assignment.cardinality")),
          QStringLiteral("canonical maximum assignments retain the V2 cardinality diagnostic code"));

    const MeshResizeAssignmentResolution missing =
        resolveMeshResizeAssignments(grow);
    check(!missing.success()
              && missing.newRouterMemberships.isEmpty()
              && diagnosticCount(
                     missing.diagnostics,
                     QStringLiteral("domain_assignment.required_choice"))
                  == grow.newRouters.size(),
          QStringLiteral("each new Router reports its own missing required choice atomically"));

    const MeshResizeAssignmentResolution explicitResolution =
        resolveMeshResizeAssignments(grow, explicitClockChoices(grow));
    const DomainMembership* first = membershipFor(
        explicitResolution, QStringLiteral("r-2-0"));
    check(explicitResolution.success()
              && explicitResolution.newRouterMemberships.size()
                  == grow.newRouters.size()
              && first
              && first->assignments.value(QStringLiteral("clock"))
                  == QStringList{QStringLiteral("clock-b")}
              && first->assignments.value(QStringLiteral("power"))
                  == QStringList{QStringLiteral("power-main")}
              && first->assignments.value(QStringLiteral("tag"))
                  == QStringList{QStringLiteral("tag-a"),
                                 QStringLiteral("tag-b")},
          QStringLiteral("explicit choices combine with unique defaults and normalize as one transaction"));

    NocDesign uniqueClock = design;
    uniqueClock.domains.erase(
        std::remove_if(
            uniqueClock.domains.begin(),
            uniqueClock.domains.end(),
            [](const DomainDefinition& domain) {
                return domain.id == QStringLiteral("clock-b");
            }),
        uniqueClock.domains.end());
    const MeshResizePlan automaticPlan = buildMeshResizePlan(
        uniqueClock, package, 2, 2);
    const MeshResizeAssignmentResolution automaticResolution =
        resolveMeshResizeAssignments(automaticPlan);
    const DomainMembership* automaticRouter = membershipFor(
        automaticResolution, QStringLiteral("r-0-1"));
    check(automaticPlan.canApplyWithoutAssignments()
              && automaticResolution.success()
              && automaticRouter
              && automaticRouter->assignments.value(QStringLiteral("clock"))
                  == QStringList{QStringLiteral("clock-a")}
              && automaticRouter->assignments.value(QStringLiteral("power"))
                  == QStringList{QStringLiteral("power-main")},
          QStringLiteral("all unique required instances resolve without user input"));

    NocDesign noClock = uniqueClock;
    noClock.domains.erase(
        std::remove_if(
            noClock.domains.begin(),
            noClock.domains.end(),
            [](const DomainDefinition& domain) {
                return domain.type == QStringLiteral("clock");
            }),
        noClock.domains.end());
    const MeshResizePlan unavailablePlan = buildMeshResizePlan(
        noClock, package, 2, 2);
    const MeshResizeDomainAssignmentPlan* unavailableClock = assignmentPlan(
        unavailablePlan, QStringLiteral("clock"));
    check(unavailableClock && unavailableClock->availableDomainIds.isEmpty()
              && unavailableClock->requiresExplicitChoice()
              && hasDiagnosticCode(
                  resolveMeshResizeAssignments(unavailablePlan).diagnostics,
                  QStringLiteral("domain_assignment.required_choice")),
          QStringLiteral("zero available required instances remain a visible unresolved choice"));

    NocDesign shrink = design;
    shrink.topology = TopologySpec{QStringLiteral("mesh"), 2, 2};
    shrink.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-removed"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{1, 1}, std::nullopt},
            {}
        }
    };
    shrink.domainMemberships.append(DomainMembership{
        ElementRef{ElementKind::Router, QStringLiteral("r-1-1")},
        QHash<QString, QStringList>{
            {QStringLiteral("clock"), QStringList{QStringLiteral("clock-a")}}
        }
    });
    shrink.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("clock"),
            QStringLiteral("clock-crossing"),
            {}
        }
    };
    const MeshResizePlan blockedShrink = buildMeshResizePlan(
        shrink, package, 1, 1);
    check(blockedShrink.hasBlockingDiagnostics()
              && blockedShrink.detachedEndpoints
                  == QVector<ElementRef>{ElementRef{
                      ElementKind::Endpoint, QStringLiteral("ep-removed")}}
              && blockedShrink.removedMemberships.contains(
                  shrink.domainMemberships.constLast())
              && blockedShrink.removedEdgeOverrides == shrink.edgeOverrides,
          QStringLiteral("shrink planning retains its hard Endpoint blocker and complete impact records"));
    check(hasDiagnosticCode(
              blockedShrink.diagnostics,
              QStringLiteral("mesh.resize_would_detach_endpoint"))
              && hasDiagnosticCode(
                  blockedShrink.diagnostics,
                  QStringLiteral("mesh.resize_would_remove_domain_membership"))
              && hasDiagnosticCode(
                  blockedShrink.diagnostics,
                  QStringLiteral("mesh.resize_would_remove_edge_override")),
          QStringLiteral("hard blockers and confirmable shrink impacts are reported together"));
    const MeshResizeImpactConfirmation blockedConfirmation{
        blockedShrink.removedMemberships,
        blockedShrink.removedEdgeOverrides};
    check(!resolveMeshResizeAssignments(
               blockedShrink, {}, blockedConfirmation).success(),
          QStringLiteral("exact impact confirmation cannot bypass an Endpoint blocker"));

    NocDesign confirmableShrink = design;
    confirmableShrink.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("clock"),
            QStringLiteral("clock-crossing"),
            QJsonObject{{QStringLiteral("mode"), QStringLiteral("safe")}}
        }
    };
    const MeshResizePlan confirmationPlan = buildMeshResizePlan(
        confirmableShrink, package, 1, 1);
    check(!confirmationPlan.hasBlockingDiagnostics()
              && confirmationPlan.requiresImpactConfirmation()
              && confirmationPlan.removedMemberships.size() == 1
              && confirmationPlan.removedMemberships.constFirst().element
                  == ElementRef{ElementKind::Router,
                                QStringLiteral("r-1-0")}
              && confirmationPlan.removedEdgeOverrides
                  == confirmableShrink.edgeOverrides,
          QStringLiteral("Router memberships and Link overrides are complete confirmable impacts, not hard blockers"));

    const MeshResizeAssignmentResolution unconfirmedShrink =
        resolveMeshResizeAssignments(confirmationPlan);
    check(!unconfirmedShrink.success()
              && unconfirmedShrink.newRouterMemberships.isEmpty()
              && hasDiagnosticCode(
                  unconfirmedShrink.diagnostics,
                  QStringLiteral("mesh.resize_missing_membership_confirmation"))
              && hasDiagnosticCode(
                  unconfirmedShrink.diagnostics,
                  QStringLiteral("mesh.resize_missing_override_confirmation")),
          QStringLiteral("default-empty confirmation rejects every unacknowledged impact"));

    const MeshResizeImpactConfirmation exactConfirmation{
        confirmationPlan.removedMemberships,
        confirmationPlan.removedEdgeOverrides};
    check(resolveMeshResizeAssignments(
              confirmationPlan, {}, exactConfirmation).success(),
          QStringLiteral("echoing every exact preview record resolves a confirmable shrink"));

    MeshResizeImpactConfirmation staleMembership = exactConfirmation;
    staleMembership.removedMemberships[0].assignments.insert(
        QStringLiteral("clock"),
        QStringList{QStringLiteral("clock-a")});
    check(hasDiagnosticCode(
              resolveMeshResizeAssignments(
                  confirmationPlan, {}, staleMembership).diagnostics,
              QStringLiteral("mesh.resize_stale_membership_confirmation")),
          QStringLiteral("a same-Router confirmation with changed assignments is stale"));

    MeshResizeImpactConfirmation staleOverride = exactConfirmation;
    staleOverride.removedEdgeOverrides[0].policy = QStringLiteral("changed");
    check(hasDiagnosticCode(
              resolveMeshResizeAssignments(
                  confirmationPlan, {}, staleOverride).diagnostics,
              QStringLiteral("mesh.resize_stale_override_confirmation")),
          QStringLiteral("a same-edge/type confirmation with changed policy is stale"));

    MeshResizeImpactConfirmation extraConfirmation = exactConfirmation;
    extraConfirmation.removedMemberships.append(
        confirmableShrink.domainMemberships.constFirst());
    check(hasDiagnosticCode(
              resolveMeshResizeAssignments(
                  confirmationPlan, {}, extraConfirmation).diagnostics,
              QStringLiteral("mesh.resize_extra_membership_confirmation")),
          QStringLiteral("confirmation cannot authorize an unrelated extra membership deletion"));

    NocDesign emptyReshape = design;
    emptyReshape.topology = TopologySpec{QStringLiteral("mesh"), 2, 2};
    emptyReshape.domainMemberships.clear();
    const MeshResizePlan reshape = buildMeshResizePlan(
        emptyReshape, package, 1, 3);
    check(!reshape.hasBlockingDiagnostics()
              && reshape.newRouters.size() == 1
              && reshape.newRouters.constFirst().element.id
                  == QStringLiteral("r-2-0")
              && reshape.removedRouters.size() == 2
              && reshape.removedRouters.contains(
                  ElementRef{ElementKind::Router, QStringLiteral("r-0-1")})
              && reshape.removedRouters.contains(
                  ElementRef{ElementKind::Router, QStringLiteral("r-1-1")}),
          QStringLiteral("reshape planning uses stable set differences instead of growth-only coordinate tricks"));

    QVector<DomainMembership> invalidMemberships = explicitClockChoices(grow);
    invalidMemberships[0].assignments.insert(
        QStringLiteral("endpoint-security"),
        QStringList{QStringLiteral("endpoint-secure")});
    invalidMemberships[0].assignments.insert(
        QStringLiteral("unknown-type"),
        QStringList{QStringLiteral("clock-a")});
    invalidMemberships[0].assignments.insert(
        QStringLiteral("power"),
        QStringList{QStringLiteral("clock-a")});
    invalidMemberships[0].assignments.insert(
        QStringLiteral("clock"),
        QStringList{QStringLiteral("clock-a"), QStringLiteral("clock-b")});
    invalidMemberships.append(DomainMembership{
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep")}, {}});
    invalidMemberships.append(invalidMemberships.at(1));
    const MeshResizeAssignmentResolution invalidResolution =
        resolveMeshResizeAssignments(grow, invalidMemberships);
    check(!invalidResolution.success()
              && invalidResolution.newRouterMemberships.isEmpty()
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.not_applicable"))
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.unknown_type"))
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.domain_type_mismatch"))
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.cardinality"))
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.invalid_element_kind"))
              && hasDiagnosticCode(
                  invalidResolution.diagnostics,
                  QStringLiteral("domain_assignment.duplicate_element")),
          QStringLiteral("assignment resolution validates the complete submission before returning any mutation"));

    PackageDefinition tightPackage = package;
    tightPackage.mesh.maximumRows = 1;
    const MeshResizePlan outOfRange = buildMeshResizePlan(
        design, tightPackage, 2, 2);
    check(outOfRange.hasBlockingDiagnostics()
              && hasDiagnosticCode(outOfRange.diagnostics,
                                   QStringLiteral("mesh.rows_out_of_range")),
          QStringLiteral("planning exposes Package Mesh limits before mutation"));

    QTextStream(stdout)
        << (failures == 0 ? "Mesh resize plan tests passed"
                          : QStringLiteral("%1 Mesh resize plan test(s) failed")
                                .arg(failures))
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
