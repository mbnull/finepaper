#include "gui/domain_manager_projection.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

PackageDefinition genericPackage() {
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
    fabricTier.required = true;

    PackageDefinition package;
    package.id = QStringLiteral("test.domain-manager-projection");
    package.version = QStringLiteral("1.0.0");
    package.domainTypes = {securityZones, fabricTier};
    return package;
}

NocDesign genericDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain-manager-projection");
    design.package = PackageReference{
        QStringLiteral("test.domain-manager-projection"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-a"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}
        },
        EndpointInstance{
            QStringLiteral("ep-unassigned"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
            {}
        }
    };
    design.domains = {
        DomainDefinition{
            QStringLiteral("zone-c"),
            QStringLiteral("security-zone"),
            QStringLiteral("Restricted"),
            {}
        },
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
            QStringLiteral("tier-core"),
            QStringLiteral("fabric-tier"),
            QStringLiteral("Core"),
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
                    QStringLiteral("fabric-tier"),
                    QStringList{QStringLiteral("tier-core")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{
                        QStringLiteral("zone-a"),
                        QStringLiteral("zone-b")
                    }
                },
                {
                    QStringLiteral("fabric-tier"),
                    QStringList{QStringLiteral("tier-core")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep-a")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{
                        QStringLiteral("zone-c"),
                        QStringLiteral("zone-a")
                    }
                }
            }
        }
    };
    return design;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const PackageDefinition package = genericPackage();
    const NocDesign design = genericDesign();
    const ElementRef router0{ElementKind::Router, QStringLiteral("r-0-0")};
    const ElementRef router1{ElementKind::Router, QStringLiteral("r-1-0")};
    const ElementRef endpoint{ElementKind::Endpoint, QStringLiteral("ep-a")};
    const ElementRef unassignedEndpoint{
        ElementKind::Endpoint, QStringLiteral("ep-unassigned")};
    const ResolvedDesign resolved = resolveDesign(design);

    const QVector<ElementRef> allSecurityElements =
        buildDomainAssignmentSelection(
            resolved,
            *package.domainType(QStringLiteral("security-zone")),
            DomainAssignmentSelectionScope::AllEligible);
    check(allSecurityElements
              == QVector<ElementRef>{router0, router1, endpoint,
                                     unassignedEndpoint},
          QStringLiteral("all-eligible selection follows stable Router then Endpoint order"));
    check(buildDomainAssignmentSelection(
              resolved,
              *package.domainType(QStringLiteral("security-zone")),
              DomainAssignmentSelectionScope::AssignedToDomain,
              QStringLiteral("zone-a"))
              == QVector<ElementRef>{router0, router1, endpoint},
          QStringLiteral("Domain member selection returns every matching applicable element"));
    check(buildDomainAssignmentSelection(
              resolved,
              *package.domainType(QStringLiteral("security-zone")),
              DomainAssignmentSelectionScope::Unassigned)
              == QVector<ElementRef>{unassignedEndpoint},
          QStringLiteral("unassigned selection finds applicable elements without membership"));
    check(buildDomainAssignmentSelection(
              resolved,
              *package.domainType(QStringLiteral("fabric-tier")),
              DomainAssignmentSelectionScope::AllEligible)
              == QVector<ElementRef>{router0, router1},
          QStringLiteral("selection helpers honor Package appliesTo without fixed Domain names"));

    const DomainAssignmentAggregate commonMultiple =
        buildDomainAssignmentAggregate(
            design,
            package,
            QVector<ElementRef>{router0, router1},
            QStringLiteral("security-zone"));
    check(commonMultiple.state == DomainAssignmentAggregateState::Common
              && commonMultiple.cardinality == DomainCardinality::Multiple
              && commonMultiple.totalElements == 2
              && commonMultiple.eligibleElements == 2,
          QStringLiteral("equal normalized multiple assignments aggregate as Common"));
    check(commonMultiple.commonAssignments
              == QStringList{QStringLiteral("zone-a"), QStringLiteral("zone-b")},
          QStringLiteral("duplicate assignments are normalized as a sorted set"));
    check(commonMultiple.domainIds
              == QStringList{
                  QStringLiteral("zone-a"),
                  QStringLiteral("zone-b"),
                  QStringLiteral("zone-c")},
          QStringLiteral("generic Domain choices are sorted independently of design order"));
    check(commonMultiple.presence(QStringLiteral("zone-a"))
                  == DomainAssignmentPresence::All
              && commonMultiple.presence(QStringLiteral("zone-b"))
                  == DomainAssignmentPresence::All
              && commonMultiple.presence(QStringLiteral("zone-c"))
                  == DomainAssignmentPresence::None,
          QStringLiteral("Common multiple assignments expose per-Domain presence"));

    const DomainAssignmentAggregate mixed = buildDomainAssignmentAggregate(
        design,
        package,
        QVector<ElementRef>{router0, endpoint},
        QStringLiteral("security-zone"));
    check(mixed.state == DomainAssignmentAggregateState::Mixed
              && mixed.commonAssignments
                  == QStringList{QStringLiteral("zone-a")},
          QStringLiteral("different assignment sets are Mixed and retain their intersection"));
    check(mixed.presence(QStringLiteral("zone-a"))
                  == DomainAssignmentPresence::All
              && mixed.presence(QStringLiteral("zone-b"))
                  == DomainAssignmentPresence::Some
              && mixed.presence(QStringLiteral("zone-c"))
                  == DomainAssignmentPresence::Some,
          QStringLiteral("Mixed assignments distinguish All and Some presence"));

    const DomainAssignmentAggregate unassigned = buildDomainAssignmentAggregate(
        design,
        package,
        QVector<ElementRef>{unassignedEndpoint},
        QStringLiteral("security-zone"));
    check(unassigned.state == DomainAssignmentAggregateState::Unassigned
              && unassigned.eligibleElements == 1
              && unassigned.commonAssignments.isEmpty(),
          QStringLiteral("an eligible element without an assignment is Unassigned"));
    check(unassigned.presence(QStringLiteral("zone-a"))
                  == DomainAssignmentPresence::None
              && unassigned.presence(QStringLiteral("zone-b"))
                  == DomainAssignmentPresence::None
              && unassigned.presence(QStringLiteral("zone-c"))
                  == DomainAssignmentPresence::None,
          QStringLiteral("Unassigned selection reports None for every Domain"));

    const DomainAssignmentAggregate commonSingle =
        buildDomainAssignmentAggregate(
            design,
            package,
            QVector<ElementRef>{router0, router1},
            QStringLiteral("fabric-tier"));
    check(commonSingle.state == DomainAssignmentAggregateState::Common
              && commonSingle.cardinality == DomainCardinality::Single
              && commonSingle.required
              && commonSingle.commonAssignments
                  == QStringList{QStringLiteral("tier-core")}
              && commonSingle.presence(QStringLiteral("tier-core"))
                  == DomainAssignmentPresence::All,
          QStringLiteral("single-cardinality Package types use the same generic aggregate"));

    const DomainAssignmentAggregate partiallyEligible =
        buildDomainAssignmentAggregate(
            design,
            package,
            QVector<ElementRef>{router0, endpoint},
            QStringLiteral("fabric-tier"));
    check(partiallyEligible.totalElements == 2
              && partiallyEligible.eligibleElements == 1
              && partiallyEligible.eligibleElementRefs
                  == QVector<ElementRef>{router0}
              && partiallyEligible.state == DomainAssignmentAggregateState::Common,
          QStringLiteral("appliesTo filters Endpoint selections for a Router-only type"));

    const ElementRef link{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};
    const ElementRef attachment{
        ElementKind::EndpointAttachment, QStringLiteral("ep-a")};
    const ElementRef unknownRouter{
        ElementKind::Router, QStringLiteral("r-99-99")};
    const ElementRef unknownEndpoint{
        ElementKind::Endpoint, QStringLiteral("missing-endpoint")};
    const DomainAssignmentAggregate eligibility =
        buildDomainAssignmentAggregate(
            design,
            package,
            QVector<ElementRef>{
                router0,
                router0,
                endpoint,
                link,
                attachment,
                unknownRouter,
                unknownEndpoint
            },
            QStringLiteral("security-zone"));
    check(eligibility.totalElements == 6
              && eligibility.eligibleElements == 2
              && eligibility.eligibleElementRefs
                  == QVector<ElementRef>{router0, endpoint},
          QStringLiteral("selection duplicates are removed and edges or stale elements are skipped"));

    const DomainAssignmentAggregate noEligible = buildDomainAssignmentAggregate(
        design,
        package,
        QVector<ElementRef>{link, attachment},
        QStringLiteral("security-zone"));
    check(noEligible.state == DomainAssignmentAggregateState::NoEligible
              && noEligible.totalElements == 2
              && noEligible.eligibleElements == 0,
          QStringLiteral("a known type with only derived edge selections is NoEligible"));

    const DomainAssignmentAggregate unavailable =
        buildDomainAssignmentAggregate(
            design,
            package,
            QVector<ElementRef>{router0, router0},
            QStringLiteral("not-a-package-type"));
    check(unavailable.state == DomainAssignmentAggregateState::Unavailable
              && unavailable.totalElements == 1
              && unavailable.eligibleElements == 0
              && unavailable.domainIds.isEmpty(),
          QStringLiteral("an unknown Package type is Unavailable after selection normalization"));

    if (failures == 0) {
        QTextStream(stdout) << "All Domain Manager projection tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
