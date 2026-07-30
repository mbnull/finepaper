#include "gui/domain_presentation.h"

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

PackageDefinition presentationPackage() {
    DomainTypeDefinition securityZones;
    securityZones.id = QStringLiteral("security-zone");
    securityZones.label = QStringLiteral("Security zones");
    securityZones.appliesTo = {
        ElementKind::Router,
        ElementKind::Endpoint
    };
    securityZones.cardinality = DomainCardinality::Multiple;

    DomainTypeDefinition fabricTier;
    fabricTier.id = QStringLiteral("fabric-tier");
    fabricTier.label = QStringLiteral("Fabric tier");
    fabricTier.appliesTo = {ElementKind::Router};
    fabricTier.cardinality = DomainCardinality::Single;

    DomainTypeDefinition power;
    power.id = QStringLiteral("power");
    power.label = QStringLiteral("Power domains");
    power.appliesTo = {ElementKind::Router, ElementKind::Endpoint};
    power.cardinality = DomainCardinality::Single;

    PackageDefinition package;
    package.id = QStringLiteral("test.domain-presentation");
    package.version = QStringLiteral("1.0.0");
    package.domainTypes = {securityZones, fabricTier, power};
    return package;
}

NocDesign presentationDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain-presentation-test");
    design.name = QStringLiteral("Domain presentation test");
    design.package = PackageReference{
        QStringLiteral("test.domain-presentation"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-a"),
            QStringLiteral("initiator"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}
        },
        EndpointInstance{
            QStringLiteral("ep-unassigned"),
            QStringLiteral("target"),
            EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
            {}
        }
    };
    design.domains = {
        DomainDefinition{
            QStringLiteral("zone-a"),
            QStringLiteral("security-zone"),
            QStringLiteral("Trusted"),
            {}
        },
        DomainDefinition{
            QStringLiteral("zone-b"),
            QStringLiteral("security-zone"),
            QStringLiteral("Shared"),
            {}
        },
        DomainDefinition{
            QStringLiteral("zone-c"),
            QStringLiteral("security-zone"),
            QStringLiteral("Restricted"),
            {}
        },
        DomainDefinition{
            QStringLiteral("tier-backbone"),
            QStringLiteral("fabric-tier"),
            QStringLiteral("Backbone"),
            {}
        },
        DomainDefinition{
            QStringLiteral("pd-main"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            {}
        },
        DomainDefinition{
            QStringLiteral("pd-aux"),
            QStringLiteral("power"),
            QStringLiteral("Auxiliary power"),
            {}
        }
    };
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{
                        QStringLiteral("zone-b"),
                        QStringLiteral("zone-a"),
                        QStringLiteral("zone-a")
                    }
                },
                {
                    QStringLiteral("power"),
                    QStringList{QStringLiteral("pd-main")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{
                        QStringLiteral("zone-c"),
                        QStringLiteral("zone-b")
                    }
                },
                {
                    QStringLiteral("power"),
                    QStringList{QStringLiteral("pd-aux")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep-a")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{QStringLiteral("zone-a")}
                },
                {
                    QStringLiteral("power"),
                    QStringList{QStringLiteral("pd-main")}
                }
            }
        }
    };

    const QString routerLink = linkId(
        QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
    design.crossingPolicies = {
        DomainCrossingPolicy{
            QStringLiteral("power-transition"),
            QStringLiteral("power"),
            QStringLiteral("pd-main"),
            QStringLiteral("pd-aux"),
            QJsonObject{{QStringLiteral("latencyCycles"), 4}}
        }
    };
    design.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{ElementKind::RouterLink, routerLink},
            QStringLiteral("security-zone"),
            QStringLiteral("security-bridge"),
            QJsonObject{
                {QStringLiteral("mode"), QStringLiteral("isolation")}
            }
        },
        DomainEdgeOverride{
            ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep-a")},
            QStringLiteral("security-zone"),
            QStringLiteral("endpoint-guard"),
            QJsonObject{{QStringLiteral("strength"), 2}}
        },
        DomainEdgeOverride{
            ElementRef{ElementKind::RouterLink, routerLink},
            QStringLiteral("power"),
            QStringLiteral("power-transition"),
            QJsonObject{{QStringLiteral("isolationCells"), 2}}
        }
    };
    return design;
}

const DomainLegendEntry* legendEntry(const DomainPresentationSnapshot& snapshot,
                                     const QString& domainId) {
    const auto iterator = std::find_if(
        snapshot.legend.cbegin(), snapshot.legend.cend(),
        [&](const DomainLegendEntry& entry) { return entry.id == domainId; });
    return iterator == snapshot.legend.cend() ? nullptr : &(*iterator);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const PackageDefinition package = presentationPackage();
    const ResolvedDesign resolved = resolveDesign(presentationDesign());
    const QString activeType = QStringLiteral("security-zone");
    const DomainPresentationSnapshot snapshot = buildDomainPresentationSnapshot(
        resolved, package, activeType);

    check(snapshot.status == DomainPresentationStatus::Ready
              && snapshot.activeDomainType == activeType
              && snapshot.domainTypeLabel == QStringLiteral("Security zones"),
          QStringLiteral("an arbitrary Package Domain type becomes the active layer"));
    check(snapshot == buildDomainPresentationSnapshot(resolved, package, activeType),
          QStringLiteral("the same Package and Design produce an identical snapshot"));

    const QColor zoneAColor = domainPresentationColor(
        activeType, QStringLiteral("zone-a"));
    check(zoneAColor.isValid()
              && zoneAColor == domainPresentationColor(
                  activeType, QStringLiteral("zone-a")),
          QStringLiteral("Domain colors are valid and stable across calls"));

    const DomainElementPresentation* routerA = snapshot.element(
        ElementRef{ElementKind::Router, QStringLiteral("r-0-0")});
    check(routerA
              && routerA->state == DomainAssignmentDisplayState::Multiple
              && routerA->domainIds
                  == QStringList{
                      QStringLiteral("zone-a"),
                      QStringLiteral("zone-b")
                  }
              && routerA->colors.size() == 2
              && !routerA->primaryColor().isValid(),
          QStringLiteral("multiple assignments are sorted, deduplicated, and retained"));

    const DomainElementPresentation* endpointA = snapshot.element(
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep-a")});
    check(endpointA
              && endpointA->state == DomainAssignmentDisplayState::Assigned
              && endpointA->domainIds
                  == QStringList{QStringLiteral("zone-a")}
              && endpointA->primaryColor() == zoneAColor,
          QStringLiteral("a single Endpoint assignment exposes its stable primary color"));

    const DomainElementPresentation* unassignedEndpoint = snapshot.element(
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep-unassigned")});
    check(unassignedEndpoint
              && unassignedEndpoint->state
                  == DomainAssignmentDisplayState::Unassigned
              && unassignedEndpoint->domainIds.isEmpty()
              && unassignedEndpoint->colors.isEmpty(),
          QStringLiteral("an applicable element without an assignment is unassigned"));

    const DomainPresentationSnapshot tierSnapshot = buildDomainPresentationSnapshot(
        resolved, package, QStringLiteral("fabric-tier"));
    const DomainElementPresentation* tierRouter = tierSnapshot.element(
        ElementRef{ElementKind::Router, QStringLiteral("r-0-0")});
    const DomainElementPresentation* tierEndpoint = tierSnapshot.element(
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep-a")});
    check(tierRouter
              && tierRouter->state == DomainAssignmentDisplayState::Unassigned
              && tierEndpoint
              && tierEndpoint->state
                  == DomainAssignmentDisplayState::NotApplicable,
          QStringLiteral("Package appliesTo distinguishes unassigned from not applicable"));

    check(snapshot.legend.size() == 3
              && snapshot.legend[0].id == QStringLiteral("zone-a")
              && snapshot.legend[1].id == QStringLiteral("zone-b")
              && snapshot.legend[2].id == QStringLiteral("zone-c"),
          QStringLiteral("Legend entries use a deterministic Domain id order"));
    const DomainLegendEntry* zoneA = legendEntry(
        snapshot, QStringLiteral("zone-a"));
    const DomainLegendEntry* zoneB = legendEntry(
        snapshot, QStringLiteral("zone-b"));
    const DomainLegendEntry* zoneC = legendEntry(
        snapshot, QStringLiteral("zone-c"));
    check(zoneA && zoneA->name == QStringLiteral("Trusted")
              && zoneA->color == zoneAColor
              && zoneA->memberCount == 2
              && zoneA->crossingCount == 1,
          QStringLiteral("Legend counts members and changed boundaries for zone-a"));
    check(zoneB && zoneB->memberCount == 2 && zoneB->crossingCount == 2,
          QStringLiteral("a Domain shared across a Router Link is not over-counted there"));
    check(zoneC && zoneC->memberCount == 1 && zoneC->crossingCount == 2,
          QStringLiteral("Legend crossing counts include Link and Attachment boundaries"));

    const ElementRef linkEdge{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
    };
    const DomainCrossingPresentation* linkCrossing = snapshot.crossing(linkEdge);
    check(linkCrossing
              && linkCrossing->fromDomainIds
                  == QStringList{
                      QStringLiteral("zone-a"),
                      QStringLiteral("zone-b")
                  }
              && linkCrossing->toDomainIds
                  == QStringList{
                      QStringLiteral("zone-b"),
                      QStringLiteral("zone-c")
                  }
              && linkCrossing->accentDomainIds
                  == QStringList{
                      QStringLiteral("zone-a"),
                      QStringLiteral("zone-c")
                  }
              && linkCrossing->fromColors.size() == 2
              && linkCrossing->toColors.size() == 2
              && linkCrossing->accentColors.size() == 2
              && linkCrossing->primaryAccent == zoneAColor,
          QStringLiteral("Router Link crossing presentation preserves both assignment sets"));
    check(linkCrossing && linkCrossing->overridePolicy
              && *linkCrossing->overridePolicy
                  == QStringLiteral("security-bridge")
              && linkCrossing->overrideProperties.value(QStringLiteral("mode"))
                  == QStringLiteral("isolation"),
          QStringLiteral("Router Link crossing exposes its edge override"));
    check(linkCrossing
              && !linkCrossing->defaultPolicy
              && linkCrossing->defaultProperties.isEmpty(),
          QStringLiteral("presentation preserves the absence of a default for set-valued crossings"));

    const DomainPresentationSnapshot powerSnapshot =
        buildDomainPresentationSnapshot(
            resolved, package, QStringLiteral("power"));
    const DomainCrossingPresentation* powerCrossing =
        powerSnapshot.crossing(linkEdge);
    check(powerCrossing
              && powerCrossing->fromDomainIds
                  == QStringList{QStringLiteral("pd-main")}
              && powerCrossing->toDomainIds
                  == QStringList{QStringLiteral("pd-aux")}
              && powerCrossing->defaultPolicy
              && *powerCrossing->defaultPolicy
                  == QStringLiteral("power-transition")
              && powerCrossing->defaultProperties
                  == QJsonObject{{QStringLiteral("latencyCycles"), 4}}
              && powerCrossing->overridePolicy
              && *powerCrossing->overridePolicy
                  == QStringLiteral("power-transition")
              && powerCrossing->overrideProperties
                  == QJsonObject{{QStringLiteral("isolationCells"), 2}},
          QStringLiteral("presentation distinguishes resolved defaults from edge-local overrides"));

    const DomainCrossingPresentation* attachmentCrossing = snapshot.crossing(
        ElementRef{
            ElementKind::EndpointAttachment,
            QStringLiteral("ep-a")
        });
    check(attachmentCrossing
              && attachmentCrossing->fromDomainIds
                  == QStringList{
                      QStringLiteral("zone-a"),
                      QStringLiteral("zone-b")
                  }
              && attachmentCrossing->toDomainIds
                  == QStringList{QStringLiteral("zone-a")}
              && attachmentCrossing->accentDomainIds
                  == QStringList{QStringLiteral("zone-b")}
              && attachmentCrossing->overridePolicy
              && *attachmentCrossing->overridePolicy
                  == QStringLiteral("endpoint-guard")
              && attachmentCrossing->overrideProperties.value(
                  QStringLiteral("strength")).toInt() == 2,
          QStringLiteral("Endpoint Attachment crossing retains sets and override data"));

    PackageDefinition packageWithoutType = package;
    packageWithoutType.domainTypes.erase(
        packageWithoutType.domainTypes.begin());
    const DomainPresentationSnapshot unavailable = buildDomainPresentationSnapshot(
        resolved, packageWithoutType, activeType);
    const DomainElementPresentation* unavailableRouter = unavailable.element(
        ElementRef{ElementKind::Router, QStringLiteral("r-0-0")});
    check(unavailable.status == DomainPresentationStatus::MissingPackageType
              && unavailable.domainTypeLabel == activeType
              && unavailableRouter
              && unavailableRouter->state
                  == DomainAssignmentDisplayState::Unavailable
              && unavailableRouter->domainIds
                  == QStringList{
                      QStringLiteral("zone-a"),
                      QStringLiteral("zone-b")
                  }
              && unavailable.legend.size() == 3
              && unavailable.crossing(linkEdge),
          QStringLiteral("missing Package type is explicit without discarding Design evidence"));

    if (failures == 0) {
        QTextStream(stdout) << "Domain presentation tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures
                        << " Domain presentation test(s) failed" << Qt::endl;
    return 1;
}
