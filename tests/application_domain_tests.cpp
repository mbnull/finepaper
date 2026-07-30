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

QJsonObject explicitConfigurationPackageManifest() {
    QJsonObject manifest = packageManifest();
    manifest.insert(
        QStringLiteral("id"), QStringLiteral("test.explicit-domain-configuration"));
    manifest.insert(
        QStringLiteral("name"), QStringLiteral("Explicit Domain Configuration test"));

    QJsonArray domainTypes = manifest.value(QStringLiteral("domainTypes")).toArray();
    QJsonObject clockType = domainTypes[1].toObject();
    QJsonArray properties = clockType.value(QStringLiteral("properties")).toArray();
    QJsonObject powerReference = properties[1].toObject();
    powerReference.insert(QStringLiteral("required"), true);
    powerReference.remove(QStringLiteral("default"));
    properties[1] = powerReference;
    clockType.insert(QStringLiteral("properties"), properties);

    QJsonArray relations = clockType.value(QStringLiteral("relations")).toArray();
    QJsonObject poweredBy = relations[0].toObject();
    poweredBy.insert(QStringLiteral("required"), true);
    relations[0] = poweredBy;
    clockType.insert(QStringLiteral("relations"), relations);
    domainTypes[1] = clockType;
    manifest.insert(QStringLiteral("domainTypes"), domainTypes);
    return manifest;
}

QJsonObject versionOnePackageManifest() {
    QJsonObject manifest = packageManifest();
    manifest.insert(QStringLiteral("formatVersion"), 1);
    manifest.insert(
        QStringLiteral("id"), QStringLiteral("test.v1-domain-configuration"));
    manifest.insert(
        QStringLiteral("name"), QStringLiteral("V1 Domain Configuration test"));
    manifest.remove(QStringLiteral("domainTypes"));
    return manifest;
}

QJsonObject requestForPackage(const QString& packageId) {
    QJsonObject request = createRequest();
    QJsonObject package = request.value(QStringLiteral("package")).toObject();
    package.insert(QStringLiteral("id"), packageId);
    request.insert(QStringLiteral("package"), package);
    return request;
}

DomainConfiguration singleDomainConfiguration() {
    DomainConfiguration configuration;
    configuration.domains = {
        DomainDefinition{
            QStringLiteral("power-explicit"),
            QStringLiteral("power"),
            QStringLiteral("Explicit power"),
            QJsonObject{{QStringLiteral("voltageMv"), 900}}
        },
        DomainDefinition{
            QStringLiteral("clock-left"),
            QStringLiteral("clock"),
            QStringLiteral("Left clock"),
            QJsonObject{
                {QStringLiteral("frequencyMHz"), 800},
                {QStringLiteral("powerDomain"), QStringLiteral("power-explicit")}
            }
        }
    };
    configuration.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("power-explicit")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("clock-left")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("power-explicit")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("clock-left")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("power-explicit")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("clock-left")}}
            }
        }
    };
    configuration.domainRelations.append(DomainRelation{
        QStringLiteral("poweredBy"),
        QStringLiteral("clock-left"),
        QStringLiteral("power-explicit"),
        QJsonObject{{QStringLiteral("latency"), 1}}
    });
    return configuration;
}

DomainConfiguration splitDomainConfiguration() {
    DomainConfiguration configuration = singleDomainConfiguration();
    configuration.domains.append(DomainDefinition{
        QStringLiteral("clock-right"),
        QStringLiteral("clock"),
        QStringLiteral("Right clock"),
        QJsonObject{
            {QStringLiteral("frequencyMHz"), 600},
            {QStringLiteral("powerDomain"), QStringLiteral("power-explicit")}
        }
    });
    configuration.domainMemberships[1].assignments.insert(
        QStringLiteral("clock"), QStringList{QStringLiteral("clock-right")});
    configuration.domainRelations.append(DomainRelation{
        QStringLiteral("poweredBy"),
        QStringLiteral("clock-right"),
        QStringLiteral("power-explicit"),
        QJsonObject{{QStringLiteral("latency"), 2}}
    });
    configuration.crossingPolicies.append(DomainCrossingPolicy{
        QStringLiteral("left_to_right"),
        QStringLiteral("clock"),
        QStringLiteral("clock-left"),
        QStringLiteral("clock-right"),
        QJsonObject{{QStringLiteral("stages"), 2}}
    });
    configuration.edgeOverrides.append(DomainEdgeOverride{
        ElementRef{
            ElementKind::RouterLink,
            linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
        },
        QStringLiteral("clock"),
        QStringLiteral("left_to_right"),
        QJsonObject{{QStringLiteral("stages"), 3}}
    });
    return configuration;
}

QJsonObject domainConfigurationToJson(const DomainConfiguration& configuration) {
    return domain_configuration::toJson(configuration);
}

bool hasDomainConfiguration(const NocDesign& design,
                            const DomainConfiguration& configuration) {
    return design.domains == configuration.domains
        && design.domainMemberships == configuration.domainMemberships
        && design.domainRelations == configuration.domainRelations
        && design.crossingPolicies == configuration.crossingPolicies
        && design.edgeOverrides == configuration.edgeOverrides;
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

    const DesignResult addedTagC = application.addDomain(
        twoTags,
        domain(QStringLiteral("tag-c"), QStringLiteral("tag"), QStringLiteral("Tag C")));
    const NocDesign threeTags = addedTagC.success ? addedTagC.design : twoTags;
    const DesignResult assignedLeftTags = application.assignDomainsToElements(
        threeTags,
        QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
        QStringLiteral("tag"),
        QStringList{QStringLiteral("tag-a"), QStringLiteral("tag-c")});
    const DesignResult assignedRightTags = application.assignDomainsToElements(
        assignedLeftTags.success ? assignedLeftTags.design : threeTags,
        QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-1-0")}},
        QStringLiteral("tag"),
        QStringList{QStringLiteral("tag-b"), QStringLiteral("tag-c")});
    const NocDesign mixedTags = assignedRightTags.success
        ? assignedRightTags.design
        : threeTags;
    check(addedTagC.success && assignedLeftTags.success && assignedRightTags.success,
          QStringLiteral("mixed assignment patch fixture is prepared"));

    DomainAssignmentPatch mixedPatch;
    mixedPatch.ensurePresent = {
        QStringLiteral(" tag-a "), QStringLiteral("tag-a")};
    mixedPatch.ensureAbsent = {
        QStringLiteral("tag-b"), QStringLiteral(" tag-b ")};
    const DesignResult patchedMixed = application.patchDomainAssignments(
        mixedTags,
        QVector<ElementRef>{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}
        },
        QStringLiteral(" tag "),
        mixedPatch);
    check(patchedMixed.success
              && hasAssignment(patchedMixed.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-0"),
                               QStringLiteral("tag"),
                               QStringList{QStringLiteral("tag-a"),
                                           QStringLiteral("tag-c")})
              && hasAssignment(patchedMixed.design,
                               ElementKind::Router,
                               QStringLiteral("r-1-0"),
                               QStringLiteral("tag"),
                               QStringList{QStringLiteral("tag-a"),
                                           QStringLiteral("tag-c")})
              && hasAssignment(patchedMixed.design,
                               ElementKind::Router,
                               QStringLiteral("r-1-0"),
                               QStringLiteral("clock"),
                               QStringList{QStringLiteral("clock-default")}),
          QStringLiteral("mixed add/remove patches normalize input, deduplicate elements, and preserve untouched assignments"));

    DomainAssignmentPatch replacementPatch;
    replacementPatch.replacement = QStringList{
        QStringLiteral(" tag-b "),
        QStringLiteral("tag-a"),
        QStringLiteral("tag-b")
    };
    const DesignResult replacedMixed = application.patchDomainAssignments(
        mixedTags,
        QVector<ElementRef>{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")}
        },
        QStringLiteral("tag"),
        replacementPatch);
    check(replacedMixed.success
              && hasAssignment(replacedMixed.design,
                               ElementKind::Router,
                               QStringLiteral("r-0-0"),
                               QStringLiteral("tag"),
                               QStringList{QStringLiteral("tag-a"),
                                           QStringLiteral("tag-b")})
              && hasAssignment(replacedMixed.design,
                               ElementKind::Router,
                               QStringLiteral("r-1-0"),
                               QStringLiteral("tag"),
                               QStringList{QStringLiteral("tag-a"),
                                           QStringLiteral("tag-b")}),
          QStringLiteral("replacement patches atomically replace and normalize every selected assignment"));

    DomainAssignmentPatch clearOptionalPatch;
    clearOptionalPatch.replacement = QStringList{};
    const DesignResult clearedOptional = application.patchDomainAssignments(
        mixedTags,
        QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
        QStringLiteral("tag"),
        clearOptionalPatch);
    const DomainMembership* clearedMembership = findMembership(
        clearedOptional.design, ElementKind::Router, QStringLiteral("r-0-0"));
    check(clearedOptional.success
              && clearedMembership
              && !clearedMembership->assignments.contains(QStringLiteral("tag"))
              && clearedMembership->assignments.contains(QStringLiteral("clock")),
          QStringLiteral("an empty replacement removes only the patched assignment and retains other membership data"));

    const DesignResult noOpPatch = application.patchDomainAssignments(
        mixedTags,
        QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
        QStringLiteral("tag"),
        DomainAssignmentPatch{});
    check(noOpPatch.success && sameDesign(noOpPatch.design, mixedTags),
          QStringLiteral("an empty assignment patch is a semantic no-op"));

    DomainAssignmentPatch singleCardinalityPatch;
    singleCardinalityPatch.ensurePresent = {QStringLiteral("clock-alt")};
    checkAtomicFailure(
        application.patchDomainAssignments(
            multiClock,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("clock"),
            singleCardinalityPatch),
        multiClock,
        QStringLiteral("domain_assignment.cardinality"),
        QStringLiteral("a patch that violates single cardinality is rejected atomically"));

    DomainAssignmentPatch requiredPatch;
    requiredPatch.replacement = QStringList{};
    checkAtomicFailure(
        application.patchDomainAssignments(
            base,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("clock"),
            requiredPatch),
        base,
        QStringLiteral("domain_assignment.required"),
        QStringLiteral("a patch cannot clear a required assignment"));

    DomainAssignmentPatch unknownDomainPatch;
    unknownDomainPatch.ensureAbsent = {QStringLiteral("tag-missing")};
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("tag"),
            unknownDomainPatch),
        threeTags,
        QStringLiteral("domain_assignment.unknown_domain"),
        QStringLiteral("unknown Domains are rejected even when an ensure-absent patch would otherwise be a no-op"));

    DomainAssignmentPatch knownTagPatch;
    knownTagPatch.ensurePresent = {QStringLiteral("tag-a")};
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("unknown-type"),
            knownTagPatch),
        threeTags,
        QStringLiteral("domain_assignment.unknown_type"),
        QStringLiteral("unknown Domain types are rejected before mutation"));
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-9-9")}},
            QStringLiteral("tag"),
            knownTagPatch),
        threeTags,
        QStringLiteral("domain_assignment.unknown_element"),
        QStringLiteral("unknown elements are rejected before mutation"));
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))}},
            QStringLiteral("tag"),
            knownTagPatch),
        threeTags,
        QStringLiteral("domain_assignment.not_applicable"),
        QStringLiteral("derived edges cannot be used as Domain membership elements"));

    DomainAssignmentPatch conflictingPatch;
    conflictingPatch.ensurePresent = {QStringLiteral("tag-a")};
    conflictingPatch.replacement = QStringList{QStringLiteral("tag-b")};
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("tag"),
            conflictingPatch),
        threeTags,
        QStringLiteral("domain_assignment.patch_conflict"),
        QStringLiteral("replacement and incremental assignment changes are mutually exclusive"));

    DomainAssignmentPatch overlappingPatch;
    overlappingPatch.ensurePresent = {QStringLiteral("tag-a")};
    overlappingPatch.ensureAbsent = {QStringLiteral(" tag-a ")};
    checkAtomicFailure(
        application.patchDomainAssignments(
            threeTags,
            QVector<ElementRef>{ElementRef{ElementKind::Router, QStringLiteral("r-0-0")}},
            QStringLiteral("tag"),
            overlappingPatch),
        threeTags,
        QStringLiteral("domain_assignment.patch_overlap"),
        QStringLiteral("the same Domain cannot be requested present and absent"));

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

    QTemporaryDir configurationFixture(
        QStringLiteral("/tmp/finepaper-domain-configuration-test-XXXXXX"));
    const bool configurationFixtureReady = configurationFixture.isValid()
        && prepareFixture(configurationFixture.path())
        && saveJsonObject(
            QDir(configurationFixture.path()).filePath(QStringLiteral("package.json")),
            explicitConfigurationPackageManifest());
    check(configurationFixtureReady,
          QStringLiteral("the explicit Domain configuration Package fixture is prepared"));
    if (configurationFixtureReady) {
        FinepaperApplication configurationApplication;
        const QVector<Diagnostic> configurationPackageDiagnostics =
            configurationApplication.reloadPackages(
                QStringList{configurationFixture.path()});
        check(!hasErrors(configurationPackageDiagnostics),
              QStringLiteral("the explicit Domain configuration Package loads"));

        const DomainConfiguration singleConfiguration = singleDomainConfiguration();
        const QJsonObject singleConfigurationJson = domainConfigurationToJson(
            singleConfiguration);
        const QStringList configurationFields{
            QStringLiteral("domains"),
            QStringLiteral("domainMemberships"),
            QStringLiteral("domainRelations"),
            QStringLiteral("crossingPolicies"),
            QStringLiteral("edgeOverrides")
        };
        check(std::all_of(
                  configurationFields.cbegin(),
                  configurationFields.cend(),
                  [&](const QString& field) {
                      return singleConfigurationJson.value(field).isArray();
                  }),
              QStringLiteral("a Domain configuration serializes all five arrays explicitly"));

        QJsonObject explicitRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        explicitRequest.insert(
            QStringLiteral("domainConfiguration"), singleConfigurationJson);
        const DesignResult explicitlyCreated = configurationApplication.createDesign(
            explicitRequest);
        check(explicitlyCreated.success
                  && hasDomainConfiguration(
                      explicitlyCreated.design, singleConfiguration)
                  && !findDomain(
                      explicitlyCreated.design, QStringLiteral("power-default"))
                  && !findDomain(
                      explicitlyCreated.design, QStringLiteral("clock-default")),
              QStringLiteral("createDesign uses an explicit complete Domain configuration without materializing defaults"));

        if (explicitlyCreated.success) {
            const DomainConfiguration splitConfiguration = splitDomainConfiguration();
            const DesignResult replaced =
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, splitConfiguration);
            check(replaced.success
                      && hasDomainConfiguration(replaced.design, splitConfiguration)
                      && replaced.design.topology.rows
                          == explicitlyCreated.design.topology.rows
                      && replaced.design.topology.columns
                          == explicitlyCreated.design.topology.columns
                      && replaced.design.endpoints.size()
                          == explicitlyCreated.design.endpoints.size(),
                  QStringLiteral("replaceDomainConfiguration atomically installs a complete interdependent configuration"));

            DomainConfiguration missingRelation = splitConfiguration;
            missingRelation.domainRelations.removeLast();
            checkAtomicFailure(
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, missingRelation),
                explicitlyCreated.design,
                QStringLiteral("domain_relation.required"),
                QStringLiteral("replaceDomainConfiguration rejects a missing required relation atomically"));

            DomainConfiguration unknownReference = splitConfiguration;
            unknownReference.domains[2].properties.insert(
                QStringLiteral("powerDomain"), QStringLiteral("missing-power"));
            checkAtomicFailure(
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, unknownReference),
                explicitlyCreated.design,
                QStringLiteral("domain_property.unknown_reference"),
                QStringLiteral("replaceDomainConfiguration validates required Domain references atomically"));

            DomainConfiguration invalidPolicy = splitConfiguration;
            invalidPolicy.crossingPolicies[0].properties.insert(
                QStringLiteral("stages"), 9);
            checkAtomicFailure(
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, invalidPolicy),
                explicitlyCreated.design,
                QStringLiteral("domain_property.above_maximum"),
                QStringLiteral("replaceDomainConfiguration validates crossing policy schemas atomically"));

            DomainConfiguration duplicateDomain = splitConfiguration;
            duplicateDomain.domains.append(duplicateDomain.domains[0]);
            checkAtomicFailure(
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, duplicateDomain),
                explicitlyCreated.design,
                QStringLiteral("domain.duplicate_id"),
                QStringLiteral("replaceDomainConfiguration rejects structural errors atomically"));

            DomainConfiguration unknownRouter = splitConfiguration;
            unknownRouter.domainMemberships[1].element.id = QStringLiteral("r-9-9");
            checkAtomicFailure(
                configurationApplication.replaceDomainConfiguration(
                    explicitlyCreated.design, unknownRouter),
                explicitlyCreated.design,
                QStringLiteral("domain_membership.unknown_element"),
                QStringLiteral("Domain configuration cannot create Routers outside the Mesh"));
        }

        for (const QString& field : configurationFields) {
            QJsonObject missingConfiguration = singleConfigurationJson;
            missingConfiguration.remove(field);
            QJsonObject missingFieldRequest = requestForPackage(
                QStringLiteral("test.explicit-domain-configuration"));
            missingFieldRequest.insert(
                QStringLiteral("domainConfiguration"), missingConfiguration);
            check(!configurationApplication.createDesign(missingFieldRequest).success,
                  QStringLiteral("createDesign rejects domainConfiguration without %1")
                      .arg(field));
        }

        QJsonObject nonObjectRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        nonObjectRequest.insert(
            QStringLiteral("domainConfiguration"), QStringLiteral("invalid"));
        check(!configurationApplication.createDesign(nonObjectRequest).success,
              QStringLiteral("createDesign rejects a non-object domainConfiguration"));

        QJsonObject unknownFieldConfiguration = singleConfigurationJson;
        unknownFieldConfiguration.insert(QStringLiteral("domainPolices"), QJsonArray{});
        QJsonObject unknownFieldRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        unknownFieldRequest.insert(
            QStringLiteral("domainConfiguration"), unknownFieldConfiguration);
        const DesignResult unknownFieldCreate =
            configurationApplication.createDesign(unknownFieldRequest);
        check(!unknownFieldCreate.success
                  && hasDiagnosticCode(
                      unknownFieldCreate.diagnostics,
                      QStringLiteral("create.unknown_field")),
              QStringLiteral("createDesign rejects unknown domainConfiguration fields"));

        DomainConfiguration unknownRouterConfiguration = singleConfiguration;
        unknownRouterConfiguration.domainMemberships[1].element.id =
            QStringLiteral("r-9-9");
        QJsonObject unknownRouterRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        unknownRouterRequest.insert(
            QStringLiteral("domainConfiguration"),
            domainConfigurationToJson(unknownRouterConfiguration));
        const DesignResult unknownRouterCreate =
            configurationApplication.createDesign(unknownRouterRequest);
        check(!unknownRouterCreate.success
                  && hasDiagnosticCode(
                      unknownRouterCreate.diagnostics,
                      QStringLiteral("domain_membership.unknown_element")),
              QStringLiteral("createDesign rejects Domain memberships for non-Mesh Routers"));

        DomainConfiguration missingRequiredRelation = singleConfiguration;
        missingRequiredRelation.domainRelations.clear();
        QJsonObject missingRelationRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        missingRelationRequest.insert(
            QStringLiteral("domainConfiguration"),
            domainConfigurationToJson(missingRequiredRelation));
        const DesignResult missingRelationCreate =
            configurationApplication.createDesign(missingRelationRequest);
        check(!missingRelationCreate.success
                  && hasDiagnosticCode(
                      missingRelationCreate.diagnostics,
                      QStringLiteral("domain_relation.required")),
              QStringLiteral("createDesign validates required relation schemas in explicit configurations"));

        DomainConfiguration invalidPropertyConfiguration = singleConfiguration;
        invalidPropertyConfiguration.domains[1].properties.insert(
            QStringLiteral("frequencyMHz"), 50);
        QJsonObject invalidPropertyRequest = requestForPackage(
            QStringLiteral("test.explicit-domain-configuration"));
        invalidPropertyRequest.insert(
            QStringLiteral("domainConfiguration"),
            domainConfigurationToJson(invalidPropertyConfiguration));
        const DesignResult invalidPropertyCreate =
            configurationApplication.createDesign(invalidPropertyRequest);
        check(!invalidPropertyCreate.success
                  && hasDiagnosticCode(
                      invalidPropertyCreate.diagnostics,
                      QStringLiteral("domain_property.below_minimum")),
              QStringLiteral("createDesign validates Package property schemas in explicit configurations"));

        QTemporaryDir versionOneFixture(
            QStringLiteral("/tmp/finepaper-v1-domain-configuration-test-XXXXXX"));
        const bool versionOneFixtureReady = versionOneFixture.isValid()
            && prepareFixture(versionOneFixture.path())
            && saveJsonObject(
                QDir(versionOneFixture.path()).filePath(QStringLiteral("package.json")),
                versionOnePackageManifest());
        check(versionOneFixtureReady,
              QStringLiteral("the V1 Domain configuration Package fixture is prepared"));
        if (versionOneFixtureReady) {
            FinepaperApplication versionOneApplication;
            const QVector<Diagnostic> versionOnePackageDiagnostics =
                versionOneApplication.reloadPackages(
                    QStringList{versionOneFixture.path()});
            check(!hasErrors(versionOnePackageDiagnostics),
                  QStringLiteral("the V1 Package fixture loads"));
            QJsonObject versionOneRequest = requestForPackage(
                QStringLiteral("test.v1-domain-configuration"));
            versionOneRequest.insert(
                QStringLiteral("domainConfiguration"), singleConfigurationJson);
            check(!versionOneApplication.createDesign(versionOneRequest).success,
                  QStringLiteral("a V1 Package rejects an explicit Domain configuration"));

            const DesignResult versionOneDesign = versionOneApplication.createDesign(
                requestForPackage(QStringLiteral("test.v1-domain-configuration")));
            check(versionOneDesign.success,
                  QStringLiteral("the V1 fixture creates a baseline Design"));
            if (versionOneDesign.success) {
                checkAtomicFailure(
                    versionOneApplication.replaceDomainConfiguration(
                        versionOneDesign.design, DomainConfiguration{}),
                    versionOneDesign.design,
                    QStringLiteral("domain_configuration.requires_v2"),
                    QStringLiteral("replaceDomainConfiguration is explicitly V2-only"));
            }
        }
    }

    if (failures == 0) {
        QTextStream(stdout) << "Application Domain tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Application Domain test(s) failed" << Qt::endl;
    return 1;
}
