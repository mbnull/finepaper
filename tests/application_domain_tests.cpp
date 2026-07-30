#include "application/application.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
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

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics, const QString& code) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

bool sameDesign(const NocDesign& lhs, const NocDesign& rhs) {
    return designToJson(lhs) == designToJson(rhs);
}

void checkAtomicFailure(const DesignResult& result,
                        const NocDesign& original,
                        const QString& code,
                        const QString& message) {
    check(!result.success
              && hasDiagnosticCode(result.diagnostics, code)
              && sameDesign(result.design, original),
          message);
}

const DomainDefinition* findDomain(const NocDesign& design, const QString& id) {
    const auto domain = std::find_if(
        design.domains.cbegin(), design.domains.cend(), [&](const DomainDefinition& value) {
            return value.id == id;
        });
    return domain == design.domains.cend() ? nullptr : &(*domain);
}

const DomainMembership* findMembership(const NocDesign& design,
                                       ElementKind kind,
                                       const QString& id) {
    const auto membership = std::find_if(
        design.domainMemberships.cbegin(),
        design.domainMemberships.cend(),
        [&](const DomainMembership& value) {
            return value.element == ElementRef{kind, id};
        });
    return membership == design.domainMemberships.cend() ? nullptr : &(*membership);
}

bool hasAssignment(const NocDesign& design,
                   ElementKind kind,
                   const QString& elementId,
                   const QString& domainType,
                   const QStringList& domainIds) {
    const DomainMembership* membership = findMembership(design, kind, elementId);
    return membership && membership->assignments.value(domainType) == domainIds;
}

bool prepareFixture(const QString& packageRoot) {
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    QFile file(executable);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write("#!/bin/sh\nexit 0\n") < 0) {
        return false;
    }
    file.close();
    return QFile::setPermissions(
        executable,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther);
}

QJsonObject packageManifest() {
    const QJsonObject powerType{
        {QStringLiteral("id"), QStringLiteral("power")},
        {QStringLiteral("label"), QStringLiteral("Power Domain")},
        {QStringLiteral("appliesTo"), QJsonArray{
            QStringLiteral("router"), QStringLiteral("endpoint")
        }},
        {QStringLiteral("cardinality"), QStringLiteral("single")},
        {QStringLiteral("required"), true},
        {QStringLiteral("properties"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("voltageMv")},
                {QStringLiteral("type"), QStringLiteral("integer")},
                {QStringLiteral("required"), true},
                {QStringLiteral("minimum"), 700},
                {QStringLiteral("maximum"), 1100},
                {QStringLiteral("default"), 900}
            }
        }},
        {QStringLiteral("relations"), QJsonArray{}},
        {QStringLiteral("crossingProperties"), QJsonArray{}}
    };
    const QJsonObject clockType{
        {QStringLiteral("id"), QStringLiteral("clock")},
        {QStringLiteral("label"), QStringLiteral("Clock Domain")},
        {QStringLiteral("appliesTo"), QJsonArray{
            QStringLiteral("router"), QStringLiteral("endpoint")
        }},
        {QStringLiteral("cardinality"), QStringLiteral("single")},
        {QStringLiteral("required"), true},
        {QStringLiteral("properties"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("frequencyMHz")},
                {QStringLiteral("type"), QStringLiteral("integer")},
                {QStringLiteral("required"), true},
                {QStringLiteral("minimum"), 100},
                {QStringLiteral("maximum"), 2000},
                {QStringLiteral("default"), 800}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("powerDomain")},
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
            }
        }},
        {QStringLiteral("relations"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("poweredBy")},
                {QStringLiteral("targetTypes"), QJsonArray{QStringLiteral("power")}},
                {QStringLiteral("cardinality"), QStringLiteral("single")},
                {QStringLiteral("required"), false},
                {QStringLiteral("properties"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("latency")},
                        {QStringLiteral("type"), QStringLiteral("integer")},
                        {QStringLiteral("required"), true},
                        {QStringLiteral("minimum"), 0},
                        {QStringLiteral("maximum"), 10},
                        {QStringLiteral("default"), 1}
                    }
                }}
            }
        }},
        {QStringLiteral("crossingProperties"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("stages")},
                {QStringLiteral("type"), QStringLiteral("integer")},
                {QStringLiteral("required"), true},
                {QStringLiteral("minimum"), 1},
                {QStringLiteral("maximum"), 4},
                {QStringLiteral("default"), 2}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("isolationPower")},
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
            }
        }}
    };
    const QJsonObject tagType{
        {QStringLiteral("id"), QStringLiteral("tag")},
        {QStringLiteral("label"), QStringLiteral("Router Tag")},
        {QStringLiteral("appliesTo"), QJsonArray{QStringLiteral("router")}},
        {QStringLiteral("cardinality"), QStringLiteral("multiple")},
        {QStringLiteral("required"), false},
        {QStringLiteral("properties"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("color")},
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("required"), true},
                {QStringLiteral("default"), QStringLiteral("blue")}
            }
        }},
        {QStringLiteral("relations"), QJsonArray{}},
        {QStringLiteral("crossingProperties"), QJsonArray{}}
    };

    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("id"), QStringLiteral("test.application-domains")},
        {QStringLiteral("name"), QStringLiteral("Application Domain test")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 3},
                {QStringLiteral("default"), 1}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 3},
                {QStringLiteral("default"), 2}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client")},
                {QStringLiteral("label"), QStringLiteral("Client")},
                {QStringLiteral("parameters"), QJsonArray{}}
            }
        }},
        {QStringLiteral("domainTypes"), QJsonArray{powerType, clockType, tagType}},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 8},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("application-domain-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
            {QStringLiteral("supportsValidate"), false},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
}

QJsonObject createRequest() {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("application_domain")},
        {QStringLiteral("name"), QStringLiteral("Application Domain")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.application-domains")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 1},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("ep0")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            }
        }}
    };
}

EndpointInstance endpoint(const QString& id, RouterPosition router) {
    return EndpointInstance{
        id,
        QStringLiteral("client"),
        EndpointAttachment{router, std::nullopt},
        QJsonObject{}
    };
}

DomainDefinition domain(const QString& id,
                        const QString& type,
                        const QString& name,
                        const QJsonObject& properties = {}) {
    return DomainDefinition{id, type, name, properties};
}

DesignResult addClockDomain(const FinepaperApplication& application,
                            const NocDesign& design,
                            const QString& id = QStringLiteral("clock-alt")) {
    return application.addDomain(
        design,
        domain(id, QStringLiteral("clock"), QStringLiteral("Alternate clock")));
}

NocDesign crossingDesign(const FinepaperApplication& application,
                         const NocDesign& base,
                         ElementKind edgeKind,
                         const QString& edgeId,
                         const ElementRef& assignedElement) {
    const DesignResult added = addClockDomain(application, base);
    if (!added.success) {
        return base;
    }
    const DesignResult assigned = application.assignDomainsToElements(
        added.design,
        QVector<ElementRef>{assignedElement},
        QStringLiteral("clock"),
        QStringList{QStringLiteral("clock-alt")});
    if (!assigned.success) {
        return base;
    }
    NocDesign design = assigned.design;
    design.domainRelations.append(DomainRelation{
        QStringLiteral("poweredBy"),
        QStringLiteral("clock-default"),
        QStringLiteral("power-default"),
        QJsonObject{{QStringLiteral("latency"), 1}}
    });
    design.crossingPolicies.append(DomainCrossingPolicy{
        QStringLiteral("cdc"),
        QStringLiteral("clock"),
        QStringLiteral("clock-default"),
        QStringLiteral("clock-alt"),
        QJsonObject{{QStringLiteral("stages"), 2}}
    });
    design.edgeOverrides.append(DomainEdgeOverride{
        ElementRef{edgeKind, edgeId},
        QStringLiteral("clock"),
        QStringLiteral("cdc"),
        QJsonObject{{QStringLiteral("stages"), 3}}
    });
    return design;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication qtApplication(argc, argv);

    QTemporaryDir fixture(QStringLiteral("/tmp/finepaper-application-domain-test-XXXXXX"));
    const bool fixtureReady = fixture.isValid() && prepareFixture(fixture.path())
        && saveJsonObject(
            QDir(fixture.path()).filePath(QStringLiteral("package.json")),
            packageManifest());
    check(fixtureReady, QStringLiteral("the application Domain Package fixture is prepared"));
    if (!fixtureReady) {
        return 1;
    }

    FinepaperApplication application;
    const QVector<Diagnostic> packageDiagnostics = application.reloadPackages(
        QStringList{fixture.path()});
    check(!hasErrors(packageDiagnostics) && application.packages().size() == 1,
          QStringLiteral("the V2 application Domain Package loads"));

    const DesignResult created = application.createDesign(createRequest());
    check(created.success && created.design.formatVersion == 2,
          QStringLiteral("createDesign creates a V2 Design for a V2 Package"));
    if (!created.success) {
        return 1;
    }
    const NocDesign base = created.design;
    const DomainDefinition* defaultPower = findDomain(base, QStringLiteral("power-default"));
    const DomainDefinition* defaultClock = findDomain(base, QStringLiteral("clock-default"));
    check(base.domains.size() == 2
              && defaultPower
              && defaultPower->properties.value(QStringLiteral("voltageMv")).toInt() == 900
              && defaultClock
              && defaultClock->properties.value(QStringLiteral("frequencyMHz")).toInt() == 800
              && !defaultClock->properties.contains(QStringLiteral("powerDomain")),
          QStringLiteral("createDesign materializes required Domains and property defaults"));
    check(base.domainMemberships.size() == 3
              && hasAssignment(base,
                               ElementKind::Router,
                               QStringLiteral("r-0-0"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-default")})
              && hasAssignment(base,
                               ElementKind::Router,
                               QStringLiteral("r-1-0"),
                               QStringLiteral("power"),
                               QStringList{QStringLiteral("power-default")})
              && hasAssignment(base,
                               ElementKind::Endpoint,
                               QStringLiteral("ep0"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-default")})
              && !findDomain(base, QStringLiteral("tag-default")),
          QStringLiteral("createDesign assigns every Router and Endpoint to applicable required Domains"));

    checkAtomicFailure(
        application.addDomain(
            base,
            domain(QStringLiteral("clock-bad-type"),
                   QStringLiteral("clock"),
                   QStringLiteral("Bad clock"),
                   QJsonObject{{QStringLiteral("frequencyMHz"), QStringLiteral("fast")}})),
        base,
        QStringLiteral("domain_property.invalid_type"),
        QStringLiteral("Domain property type failures are atomic"));
    checkAtomicFailure(
        application.addDomain(
            base,
            domain(QStringLiteral("clock-bad-range"),
                   QStringLiteral("clock"),
                   QStringLiteral("Bad clock"),
                   QJsonObject{{QStringLiteral("frequencyMHz"), 99}})),
        base,
        QStringLiteral("domain_property.below_minimum"),
        QStringLiteral("Domain property range failures are atomic"));
    checkAtomicFailure(
        application.addDomain(
            base,
            domain(QStringLiteral("clock-bad-reference"),
                   QStringLiteral("clock"),
                   QStringLiteral("Bad clock"),
                   QJsonObject{{QStringLiteral("powerDomain"), QStringLiteral("missing")}})),
        base,
        QStringLiteral("domain_property.unknown_reference"),
        QStringLiteral("unknown Domain property references are rejected atomically"));
    checkAtomicFailure(
        application.addDomain(
            base,
            domain(QStringLiteral("clock-wrong-reference"),
                   QStringLiteral("clock"),
                   QStringLiteral("Bad clock"),
                   QJsonObject{{QStringLiteral("powerDomain"),
                                QStringLiteral("clock-default")}})),
        base,
        QStringLiteral("domain_property.reference_type_mismatch"),
        QStringLiteral("wrong-type Domain property references are rejected atomically"));

    const DesignResult addedClock = addClockDomain(application, base);
    check(addedClock.success
              && findDomain(addedClock.design, QStringLiteral("clock-alt"))
              && findDomain(addedClock.design, QStringLiteral("clock-alt"))
                     ->properties.value(QStringLiteral("frequencyMHz")).toInt() == 800
              && !findDomain(addedClock.design, QStringLiteral("clock-alt"))
                      ->properties.contains(QStringLiteral("powerDomain")),
          QStringLiteral("addDomain atomically merges Package defaults"));
    const NocDesign multiClock = addedClock.success ? addedClock.design : base;
    if (addedClock.success) {
        DomainDefinition updatedClock = *findDomain(
            multiClock, QStringLiteral("clock-alt"));
        updatedClock.name = QStringLiteral(" Updated clock ");
        updatedClock.properties.insert(QStringLiteral("frequencyMHz"), 1000);
        const DesignResult updated = application.updateDomain(
            multiClock, QStringLiteral("clock-alt"), updatedClock);
        check(updated.success
                  && findDomain(updated.design, QStringLiteral("clock-alt"))->name
                      == QStringLiteral("Updated clock")
                  && findDomain(updated.design, QStringLiteral("clock-alt"))
                         ->properties.value(QStringLiteral("frequencyMHz")).toInt() == 1000,
              QStringLiteral("updateDomain atomically replaces mutable Domain state"));

        DomainDefinition changedIdentity = updatedClock;
        changedIdentity.id = QStringLiteral("clock-renamed");
        checkAtomicFailure(
            application.updateDomain(
                multiClock, QStringLiteral("clock-alt"), changedIdentity),
            multiClock,
            QStringLiteral("domain.update_identity_forbidden"),
            QStringLiteral("updateDomain rejects identity changes without partial mutation"));
    }

    const DesignResult addedTag = application.addDomain(
        base,
        domain(QStringLiteral("tag-a"),
               QStringLiteral("tag"),
               QStringLiteral("Tag A")));
    check(addedTag.success, QStringLiteral("an optional Domain can be added"));
    if (addedTag.success) {
        const DesignResult removedTag = application.removeDomain(
            addedTag.design, QStringLiteral("tag-a"));
        check(removedTag.success
                  && !findDomain(removedTag.design, QStringLiteral("tag-a")),
              QStringLiteral("removeDomain atomically removes an unreferenced optional Domain"));
    }
    checkAtomicFailure(
        application.removeDomain(base, QStringLiteral("power-default")),
        base,
        QStringLiteral("domain.remove_referenced"),
        QStringLiteral("removeDomain preserves a Design when the Domain is referenced"));

    const DesignResult addedTagA = application.addDomain(
        base,
        domain(QStringLiteral("tag-a"), QStringLiteral("tag"), QStringLiteral("Tag A")));
    const DesignResult addedTagB = application.addDomain(
        addedTagA.success ? addedTagA.design : base,
        domain(QStringLiteral("tag-b"), QStringLiteral("tag"), QStringLiteral("Tag B")));
    const NocDesign twoTags = addedTagB.success ? addedTagB.design : base;
    const DesignResult assignedTags = application.assignDomainsToElements(
        twoTags,
        QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
        QStringLiteral("tag"),
        QStringList{QStringLiteral("tag-a"), QStringLiteral("tag-b")});
    check(assignedTags.success
              && hasAssignment(assignedTags.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-0"),
                               QStringLiteral("tag"),
                               QStringList{QStringLiteral("tag-a"), QStringLiteral("tag-b")}),
          QStringLiteral("multiple-cardinality Domains accept multiple assignments"));
    checkAtomicFailure(
        application.assignDomainsToElements(
            twoTags,
            QVector<ElementRef>{ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")}},
            QStringLiteral("tag"),
            QStringList{QStringLiteral("tag-a")}),
        twoTags,
        QStringLiteral("domain_assignment.not_applicable"),
        QStringLiteral("appliesTo violations preserve the original Design"));
    checkAtomicFailure(
        application.assignDomainsToElements(
            multiClock,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("clock"),
            QStringList{QStringLiteral("clock-default"), QStringLiteral("clock-alt")}),
        multiClock,
        QStringLiteral("domain_assignment.cardinality"),
        QStringLiteral("single-cardinality Domains reject multiple assignments atomically"));
    checkAtomicFailure(
        application.clearDomainAssignment(
            base,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("clock")),
        base,
        QStringLiteral("domain_assignment.required"),
        QStringLiteral("clearing a required assignment is rejected and returns the original Design"));

    const DesignResult automaticEndpoint = application.addEndpoint(
        base,
        endpoint(QStringLiteral("auto_ep"), RouterPosition{0, 0}));
    check(automaticEndpoint.success
              && hasAssignment(automaticEndpoint.design,
                               ElementKind::Endpoint,
                               QStringLiteral("auto_ep"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-default")})
              && hasAssignment(automaticEndpoint.design,
                               ElementKind::Endpoint,
                               QStringLiteral("auto_ep"),
                               QStringLiteral("power"),
                               QStringList{QStringLiteral("power-default")}),
          QStringLiteral("addEndpoint automatically fills required single-instance assignments"));
    const DesignResult explicitEndpoint = application.addEndpoint(
        multiClock,
        endpoint(QStringLiteral("explicit_ep"), RouterPosition{1, 0}),
        QHash<QString, QStringList>{
            {QStringLiteral("clock"), QStringList{QStringLiteral("clock-alt")}}
        });
    check(explicitEndpoint.success
              && hasAssignment(explicitEndpoint.design,
                               ElementKind::Endpoint,
                               QStringLiteral("explicit_ep"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-alt")})
              && hasAssignment(explicitEndpoint.design,
                               ElementKind::Endpoint,
                               QStringLiteral("explicit_ep"),
                               QStringLiteral("power"),
                               QStringList{QStringLiteral("power-default")}),
          QStringLiteral("addEndpoint combines explicit choices with unique automatic choices"));
    checkAtomicFailure(
        application.addEndpoint(
            multiClock,
            endpoint(QStringLiteral("ambiguous_ep"), RouterPosition{0, 0})),
        multiClock,
        QStringLiteral("domain_assignment.required_choice"),
        QStringLiteral("addEndpoint rejects an omitted choice when multiple instances exist"));

    const NocDesign endpointCrossing = crossingDesign(
        application,
        base,
        ElementKind::EndpointAttachment,
        QStringLiteral("ep0"),
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")});
    check(application.validate(endpointCrossing, false).success,
          QStringLiteral("the Endpoint cleanup fixture is valid before removal"));
    const DesignResult removedEndpoint = application.removeEndpoint(
        endpointCrossing, QStringLiteral("ep0"));
    check(removedEndpoint.success
              && std::none_of(
                  removedEndpoint.design.endpoints.cbegin(),
                  removedEndpoint.design.endpoints.cend(),
                  [](const EndpointInstance& value) {
                      return value.id == QStringLiteral("ep0");
                  })
              && !findMembership(
                  removedEndpoint.design, ElementKind::Endpoint, QStringLiteral("ep0"))
              && std::none_of(
                  removedEndpoint.design.edgeOverrides.cbegin(),
                  removedEndpoint.design.edgeOverrides.cend(),
                  [](const DomainEdgeOverride& value) {
                      return value.edge
                          == ElementRef{
                              ElementKind::EndpointAttachment,
                              QStringLiteral("ep0")};
                  }),
          QStringLiteral("removeEndpoint cleans up its membership and attachment override"));

    const DesignResult automaticGrow = application.resizeMesh(base, 2, 2);
    check(automaticGrow.success
              && hasAssignment(automaticGrow.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-1"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-default")})
              && hasAssignment(automaticGrow.design,
                               ElementKind::Router,
                               QStringLiteral("r-1-1"),
                               QStringLiteral("power"),
                               QStringList{QStringLiteral("power-default")}),
          QStringLiteral("resizeMesh auto-assigns unique required Domains to new Routers"));
    const QVector<DomainMembership> explicitNewRouters{
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-1")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clock-alt")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-1")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clock-default")}}
            }
        }
    };
    const DesignResult explicitGrow = application.resizeMesh(
        multiClock, 2, 2, explicitNewRouters);
    check(explicitGrow.success
              && hasAssignment(explicitGrow.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-1"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-alt")})
              && hasAssignment(explicitGrow.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-1"),
                               QStringLiteral("power"),
                               QStringList{QStringLiteral("power-default")}),
          QStringLiteral("resizeMesh combines explicit Router choices with unique automatic choices"));
    checkAtomicFailure(
        application.resizeMesh(multiClock, 2, 2),
        multiClock,
        QStringLiteral("domain_assignment.required_choice"),
        QStringLiteral("resizeMesh rejects ambiguous new Router assignments atomically"));

    const QString routerLinkId = linkId(
        QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
    const NocDesign linkCrossing = crossingDesign(
        application,
        base,
        ElementKind::RouterLink,
        routerLinkId,
        ElementRef{ElementKind::Router, QStringLiteral("r-1-0")});
    check(application.validate(linkCrossing, false).success,
          QStringLiteral("declared relation, policy, and crossing override schemas validate"));

    NocDesign invalidRelation = linkCrossing;
    invalidRelation.domainRelations[0].type = QStringLiteral("unknown");
    const ValidationResult invalidRelationValidation = application.validate(
        invalidRelation, false);
    check(!invalidRelationValidation.success
              && hasDiagnosticCode(invalidRelationValidation.diagnostics,
                                   QStringLiteral("domain_relation.unknown_schema")),
          QStringLiteral("undeclared Domain relation schemas are rejected"));
    NocDesign invalidRelationTarget = linkCrossing;
    invalidRelationTarget.domainRelations[0].to = QStringLiteral("clock-alt");
    const ValidationResult invalidRelationTargetValidation = application.validate(
        invalidRelationTarget, false);
    check(!invalidRelationTargetValidation.success
              && hasDiagnosticCode(invalidRelationTargetValidation.diagnostics,
                                   QStringLiteral("domain_relation.invalid_target_type")),
          QStringLiteral("Domain relation target type constraints are enforced"));
    NocDesign invalidPolicyProperty = linkCrossing;
    invalidPolicyProperty.crossingPolicies[0].properties.insert(
        QStringLiteral("stages"), 5);
    const ValidationResult invalidPolicyValidation = application.validate(
        invalidPolicyProperty, false);
    check(!invalidPolicyValidation.success
              && hasDiagnosticCode(invalidPolicyValidation.diagnostics,
                                   QStringLiteral("domain_property.above_maximum")),
          QStringLiteral("crossing policy properties use the Package schema"));
    NocDesign invalidOverrideProperty = linkCrossing;
    invalidOverrideProperty.edgeOverrides[0].properties.insert(
        QStringLiteral("stages"), QStringLiteral("three"));
    const ValidationResult invalidOverrideValidation = application.validate(
        invalidOverrideProperty, false);
    check(!invalidOverrideValidation.success
              && hasDiagnosticCode(invalidOverrideValidation.diagnostics,
                                   QStringLiteral("domain_property.invalid_type")),
          QStringLiteral("edge override properties use partial Package schema validation"));
    NocDesign mismatchedPolicy = linkCrossing;
    std::swap(mismatchedPolicy.crossingPolicies[0].from,
              mismatchedPolicy.crossingPolicies[0].to);
    const ValidationResult mismatchedPolicyValidation = application.validate(
        mismatchedPolicy, false);
    check(!mismatchedPolicyValidation.success
              && hasDiagnosticCode(mismatchedPolicyValidation.diagnostics,
                                   QStringLiteral("domain_edge_override.policy_pair_mismatch")),
          QStringLiteral("edge overrides must select a policy matching the directed crossing"));

    const DesignResult rejectedShrink = application.resizeMesh(linkCrossing, 1, 1);
    check(!rejectedShrink.success
              && hasDiagnosticCode(
                  rejectedShrink.diagnostics,
                  QStringLiteral("mesh.resize_would_remove_domain_membership"))
              && hasDiagnosticCode(
                  rejectedShrink.diagnostics,
                  QStringLiteral("mesh.resize_would_remove_edge_override"))
              && sameDesign(rejectedShrink.design, linkCrossing),
          QStringLiteral("resizeMesh rejects shrinking away Router memberships and Link overrides"));

    if (failures == 0) {
        QTextStream(stdout) << "Application Domain tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Application Domain test(s) failed" << Qt::endl;
    return 1;
}
