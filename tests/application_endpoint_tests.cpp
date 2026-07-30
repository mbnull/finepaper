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

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics,
                       const QString& code) {
    return std::any_of(
        diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
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
              && sameDesign(result.design, original)
              && hasDiagnosticCode(result.diagnostics, code),
          message);
}

const EndpointInstance* findEndpoint(const NocDesign& design,
                                     const QString& id) {
    const auto endpoint = std::find_if(
        design.endpoints.cbegin(), design.endpoints.cend(),
        [&](const EndpointInstance& value) { return value.id == id; });
    return endpoint == design.endpoints.cend() ? nullptr : &(*endpoint);
}

const ElementConfiguration* findConfiguration(
    const NocDesign& design,
    const QString& endpointId,
    const QString& propertySet) {
    const ElementRef attachment{
        ElementKind::EndpointAttachment, endpointId};
    const auto configuration = std::find_if(
        design.elementConfigurations.cbegin(),
        design.elementConfigurations.cend(),
        [&](const ElementConfiguration& value) {
            return value.element == attachment
                && value.propertySet == propertySet;
        });
    return configuration == design.elementConfigurations.cend()
        ? nullptr : &(*configuration);
}

QJsonObject parameter(const QString& id,
                      const QString& type,
                      const QJsonValue& defaultValue,
                      std::optional<double> minimum = std::nullopt,
                      std::optional<double> maximum = std::nullopt,
                      const QJsonArray& values = {}) {
    QJsonObject result{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), type},
        {QStringLiteral("default"), defaultValue}
    };
    if (minimum) {
        result.insert(QStringLiteral("minimum"), *minimum);
    }
    if (maximum) {
        result.insert(QStringLiteral("maximum"), *maximum);
    }
    if (!values.isEmpty()) {
        result.insert(QStringLiteral("values"), values);
    }
    return result;
}

QJsonObject propertySet(const QString& id,
                        const QJsonArray& endpointTypes,
                        const QString& propertyId,
                        int defaultValue) {
    QJsonObject result{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), id},
        {QStringLiteral("appliesTo"), QJsonArray{
            QStringLiteral("endpoint-attachment")
        }},
        {QStringLiteral("properties"), QJsonArray{
            parameter(propertyId,
                      QStringLiteral("integer"),
                      defaultValue,
                      0,
                      256)
        }}
    };
    if (!endpointTypes.isEmpty()) {
        result.insert(QStringLiteral("endpointTypes"), endpointTypes);
    }
    return result;
}

QJsonObject packageManifest() {
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("id"), QStringLiteral("test.endpoint-lifecycle")},
        {QStringLiteral("name"), QStringLiteral("Endpoint lifecycle test")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 2},
                {QStringLiteral("default"), 1}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 2},
                {QStringLiteral("default"), 2}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client")},
                {QStringLiteral("label"), QStringLiteral("Client")},
                {QStringLiteral("parameters"), QJsonArray{
                    parameter(QStringLiteral("protocol"),
                              QStringLiteral("enum"),
                              QStringLiteral("axi"),
                              std::nullopt,
                              std::nullopt,
                              QJsonArray{QStringLiteral("axi")}),
                    parameter(QStringLiteral("width"),
                              QStringLiteral("integer"), 32, 8, 64),
                    parameter(QStringLiteral("queueDepth"),
                              QStringLiteral("integer"), 4, 1, 32),
                    parameter(QStringLiteral("enabled"),
                              QStringLiteral("boolean"), true),
                    parameter(QStringLiteral("clientOnly"),
                              QStringLiteral("boolean"), false)
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("memory")},
                {QStringLiteral("label"), QStringLiteral("Memory")},
                {QStringLiteral("parameters"), QJsonArray{
                    parameter(QStringLiteral("protocol"),
                              QStringLiteral("enum"),
                              QStringLiteral("chi"),
                              std::nullopt,
                              std::nullopt,
                              QJsonArray{QStringLiteral("chi")}),
                    parameter(QStringLiteral("width"),
                              QStringLiteral("integer"), 64, 32, 256),
                    parameter(QStringLiteral("queueDepth"),
                              QStringLiteral("integer"), 8, 1, 16),
                    parameter(QStringLiteral("enabled"),
                              QStringLiteral("boolean"), false),
                    parameter(QStringLiteral("banks"),
                              QStringLiteral("integer"), 8, 1, 32)
                }}
            }
        }},
        {QStringLiteral("domainTypes"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("clock")},
                {QStringLiteral("label"), QStringLiteral("Clock")},
                {QStringLiteral("appliesTo"), QJsonArray{
                    QStringLiteral("router"), QStringLiteral("endpoint")
                }},
                {QStringLiteral("cardinality"), QStringLiteral("single")},
                {QStringLiteral("required"), true},
                {QStringLiteral("defaultInstance"), QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("clock-main")},
                    {QStringLiteral("name"), QStringLiteral("Main clock")},
                    {QStringLiteral("properties"), QJsonObject{}}
                }},
                {QStringLiteral("properties"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("frequencyMHz")},
                        {QStringLiteral("type"), QStringLiteral("integer")},
                        {QStringLiteral("required"), true},
                        {QStringLiteral("default"), 1000},
                        {QStringLiteral("minimum"), 1}
                    }
                }},
                {QStringLiteral("relations"), QJsonArray{}},
                {QStringLiteral("crossingProperties"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("stages")},
                        {QStringLiteral("type"), QStringLiteral("integer")},
                        {QStringLiteral("required"), true},
                        {QStringLiteral("default"), 2},
                        {QStringLiteral("minimum"), 1},
                        {QStringLiteral("maximum"), 8}
                    }
                }}
            }
        }},
        {QStringLiteral("runtimeCapabilities"), QJsonObject{
            {QStringLiteral("domainConfiguration"), QJsonObject{
                {QStringLiteral("domains"), true},
                {QStringLiteral("memberships"), true},
                {QStringLiteral("relations"), true},
                {QStringLiteral("crossingPolicies"), true},
                {QStringLiteral("edgeOverrides"), true}
            }}
        }},
        {QStringLiteral("elementPropertySets"), QJsonArray{
            propertySet(QStringLiteral("generic-attachment"),
                        {},
                        QStringLiteral("latency"),
                        1),
            propertySet(QStringLiteral("client-attachment"),
                        QJsonArray{QStringLiteral("client")},
                        QStringLiteral("width"),
                        32)
        }},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 8},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("endpoint-lifecycle-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
            {QStringLiteral("supportsValidate"), false},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
}

bool prepareFixture(
    const QString& packageRoot,
    const QJsonObject& manifest = packageManifest()) {
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    QFile file(executable);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write("#!/bin/sh\nexit 0\n") < 0) {
        return false;
    }
    file.close();
    if (!QFile::setPermissions(
            executable,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                | QFileDevice::ExeOther)) {
        return false;
    }
    return saveJsonObject(
        QDir(packageRoot).filePath(QStringLiteral("package.json")),
        manifest);
}

QJsonObject createRequest() {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("endpoint_lifecycle")},
        {QStringLiteral("name"), QStringLiteral("Endpoint Lifecycle")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.endpoint-lifecycle")},
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
                {QStringLiteral("router"), QJsonArray{0, 0}},
                {QStringLiteral("parameters"), QJsonObject{
                    {QStringLiteral("width"), 16},
                    {QStringLiteral("queueDepth"), 12}
                }}
            }
        }}
    };
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication qtApplication(argc, argv);
    QTemporaryDir fixture(QStringLiteral(
        "/tmp/finepaper-application-endpoint-test-XXXXXX"));
    check(fixture.isValid() && prepareFixture(fixture.path()),
          QStringLiteral("Endpoint lifecycle Package fixture is available"));
    if (!fixture.isValid()) {
        return 1;
    }

    FinepaperApplication application;
    const QVector<Diagnostic> packageDiagnostics =
        application.reloadPackages(QStringList{fixture.path()});
    check(!hasErrors(packageDiagnostics),
          QStringLiteral("Endpoint lifecycle Package loads"));

    QTemporaryDir legacyFixture(QStringLiteral(
        "/tmp/finepaper-legacy-attachment-test-XXXXXX"));
    QJsonObject legacyManifest = packageManifest();
    legacyManifest.insert(
        QStringLiteral("id"), QStringLiteral("test.legacy-explicit"));
    legacyManifest.insert(
        QStringLiteral("attachment"),
        QJsonObject{
            {QStringLiteral("maxPerRouter"), 2},
            {QStringLiteral("slotMode"), QStringLiteral("explicit")},
        });
    check(legacyFixture.isValid()
              && prepareFixture(legacyFixture.path(), legacyManifest),
          QStringLiteral("legacy explicit-slot Package fixture is available"));
    FinepaperApplication legacyApplication;
    const QVector<Diagnostic> legacyPackageDiagnostics =
        legacyApplication.reloadPackages(QStringList{legacyFixture.path()});
    QJsonObject legacyRequest = createRequest();
    QJsonObject legacyPackage =
        legacyRequest.value(QStringLiteral("package")).toObject();
    legacyPackage.insert(
        QStringLiteral("id"), QStringLiteral("test.legacy-explicit"));
    legacyRequest.insert(QStringLiteral("package"), legacyPackage);
    QJsonArray legacyEndpoints =
        legacyRequest.value(QStringLiteral("endpoints")).toArray();
    QJsonObject legacyEndpoint = legacyEndpoints.at(0).toObject();
    legacyEndpoint.insert(QStringLiteral("slot"), QStringLiteral("0"));
    legacyEndpoints[0] = legacyEndpoint;
    legacyRequest.insert(QStringLiteral("endpoints"), legacyEndpoints);
    const DesignResult validLegacyDesign =
        legacyApplication.createDesign(legacyRequest);
    legacyEndpoint.insert(
        QStringLiteral("slot"), QStringLiteral("hardcodedTrick"));
    legacyEndpoints[0] = legacyEndpoint;
    legacyRequest.insert(QStringLiteral("endpoints"), legacyEndpoints);
    const DesignResult invalidLegacyDesign =
        legacyApplication.createDesign(legacyRequest);
    check(!hasErrors(legacyPackageDiagnostics)
              && validLegacyDesign.success
              && !invalidLegacyDesign.success
              && hasDiagnosticCode(
                  invalidLegacyDesign.diagnostics,
                  QStringLiteral("endpoint.unknown_slot")),
          QStringLiteral(
              "Application and GUI share numeric legacy explicit slots"));

    QTemporaryDir oversizedFixture(QStringLiteral(
        "/tmp/finepaper-oversized-attachment-test-XXXXXX"));
    QJsonObject oversizedManifest = packageManifest();
    oversizedManifest.insert(
        QStringLiteral("id"), QStringLiteral("test.oversized-attachment"));
    oversizedManifest.insert(
        QStringLiteral("attachment"),
        QJsonObject{
            {QStringLiteral("maxPerRouter"),
             kMaximumEndpointAttachmentsPerRouter + 1},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")},
        });
    check(oversizedFixture.isValid()
              && prepareFixture(oversizedFixture.path(), oversizedManifest),
          QStringLiteral("oversized attachment Package fixture is available"));
    FinepaperApplication oversizedApplication;
    const QVector<Diagnostic> oversizedDiagnostics =
        oversizedApplication.reloadPackages(
            QStringList{oversizedFixture.path()});
    check(hasErrors(oversizedDiagnostics)
              && hasDiagnosticCode(
                  oversizedDiagnostics,
                  QStringLiteral("package.invalid_attachment_capacity")),
          QStringLiteral(
              "Package loading rejects attachment capacities that could exhaust the GUI"));

    const DesignResult created = application.createDesign(createRequest());
    const EndpointInstance* createdEndpoint = findEndpoint(created.design, QStringLiteral("ep0"));
    check(created.success && createdEndpoint
              && createdEndpoint->parameters
                  == QJsonObject{
                      {QStringLiteral("protocol"), QStringLiteral("axi")},
                      {QStringLiteral("width"), 16},
                      {QStringLiteral("queueDepth"), 12},
                      {QStringLiteral("enabled"), true},
                      {QStringLiteral("clientOnly"), false}},
          QStringLiteral(
              "creation merges Package defaults with explicitly provided Endpoint values"));

    QJsonObject unknownCreateRequest = createRequest();
    QJsonArray unknownCreateEndpoints =
        unknownCreateRequest.value(QStringLiteral("endpoints")).toArray();
    QJsonObject unknownCreateEndpoint = unknownCreateEndpoints.at(0).toObject();
    QJsonObject unknownCreateParameters =
        unknownCreateEndpoint.value(QStringLiteral("parameters")).toObject();
    unknownCreateParameters.insert(QStringLiteral("hardcodedTrick"), true);
    unknownCreateEndpoint.insert(QStringLiteral("parameters"), unknownCreateParameters);
    unknownCreateEndpoints[0] = unknownCreateEndpoint;
    unknownCreateRequest.insert(QStringLiteral("endpoints"), unknownCreateEndpoints);
    const DesignResult rejectedCreate = application.createDesign(unknownCreateRequest);
    check(!rejectedCreate.success
              && hasDiagnosticCode(rejectedCreate.diagnostics,
                                   QStringLiteral("parameter.unknown")),
          QStringLiteral("creation rejects Endpoint values outside the Package schema"));

    if (!created.success || !createdEndpoint) {
        return 1;
    }

    const QJsonObject editedParameters{
        {QStringLiteral("protocol"), QStringLiteral("axi")},
        {QStringLiteral("width"), 48},
        {QStringLiteral("queueDepth"), 7},
        {QStringLiteral("enabled"), false},
        {QStringLiteral("clientOnly"), true}
    };
    const DesignResult edited = application.updateEndpointParameters(
        created.design, QStringLiteral("ep0"), editedParameters);
    check(edited.success
              && findEndpoint(edited.design, QStringLiteral("ep0"))->parameters
                  == editedParameters,
          QStringLiteral("post-create Endpoint parameter editing replaces the complete effective object"));

    QJsonObject missingParameter = editedParameters;
    missingParameter.remove(QStringLiteral("width"));
    checkAtomicFailure(
        application.updateEndpointParameters(
            edited.design, QStringLiteral("ep0"), missingParameter),
        edited.design,
        QStringLiteral("parameter.missing"),
        QStringLiteral("incomplete Endpoint parameter edits fail atomically"));
    QJsonObject invalidParameter = editedParameters;
    invalidParameter.insert(QStringLiteral("width"), 512);
    checkAtomicFailure(
        application.updateEndpointParameters(
            edited.design, QStringLiteral("ep0"), invalidParameter),
        edited.design,
        QStringLiteral("parameter.above_maximum"),
        QStringLiteral("out-of-range Endpoint parameter edits fail atomically"));
    checkAtomicFailure(
        application.updateEndpointParameters(
            edited.design, QStringLiteral("missing"), editedParameters),
        edited.design,
        QStringLiteral("endpoint.not_found"),
        QStringLiteral("Endpoint parameter editing rejects an unknown Endpoint atomically"));

    const EndpointTypeChangePlan sameType = application.planEndpointTypeChange(
        edited.design,
        QStringLiteral("ep0"),
        QStringLiteral("client"),
        EndpointParameterMigration::PreserveCompatible);
    check(!sameType.canApply()
              && hasDiagnosticCode(sameType.diagnostics,
                                   QStringLiteral("endpoint.type_change_same_type")),
          QStringLiteral("same-type requests direct callers to ordinary parameter editing"));
    const EndpointTypeChangePlan invalidMigration =
        application.planEndpointTypeChange(
            edited.design,
            QStringLiteral("ep0"),
            QStringLiteral("memory"),
            static_cast<EndpointParameterMigration>(99));
    check(!invalidMigration.canApply()
              && hasDiagnosticCode(
                  invalidMigration.diagnostics,
                  QStringLiteral("endpoint.invalid_parameter_migration")),
          QStringLiteral("unknown migration enum values fail closed"));

    EndpointInstance addedEndpoint = {
        QStringLiteral("memory1"),
        QStringLiteral("memory"),
        EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
        QJsonObject{{QStringLiteral("banks"), 4}}
    };
    const DesignResult added = application.addEndpoint(
        created.design, addedEndpoint);
    const EndpointInstance* materializedAdded = findEndpoint(
        added.design, QStringLiteral("memory1"));
    check(added.success && materializedAdded
              && materializedAdded->parameters.value(QStringLiteral("protocol"))
                     == QStringLiteral("chi")
              && materializedAdded->parameters.value(QStringLiteral("width")).toInt()
                     == 64
              && materializedAdded->parameters.value(QStringLiteral("banks")).toInt()
                     == 4,
          QStringLiteral("addEndpoint also materializes target-type defaults before strict validation"));
    EndpointInstance invalidAdded = addedEndpoint;
    invalidAdded.id = QStringLiteral("memory-invalid");
    invalidAdded.parameters.insert(QStringLiteral("hardcodedTrick"), true);
    checkAtomicFailure(
        application.addEndpoint(created.design, invalidAdded),
        created.design,
        QStringLiteral("parameter.unknown"),
        QStringLiteral("addEndpoint rejects undeclared Endpoint parameters atomically"));

    const DesignResult sameAttachment = application.moveEndpoint(
        created.design,
        QStringLiteral("ep0"),
        RouterPosition{0, 0},
        std::nullopt);
    check(sameAttachment.success
              && sameDesign(sameAttachment.design, created.design),
          QStringLiteral("moving to the unchanged automatic attachment is a stable no-op"));
    checkAtomicFailure(
        application.moveEndpoint(
            created.design,
            QStringLiteral("missing"),
            RouterPosition{1, 0},
            std::nullopt),
        created.design,
        QStringLiteral("endpoint.not_found"),
        QStringLiteral("moveEndpoint rejects an unknown Endpoint atomically"));
    checkAtomicFailure(
        application.moveEndpoint(
            created.design,
            QStringLiteral("ep0"),
            RouterPosition{9, 9},
            std::nullopt),
        created.design,
        QStringLiteral("endpoint.router_out_of_range"),
        QStringLiteral("moveEndpoint cannot target a Router outside the derived Mesh"));

    DesignResult filledRouter = created;
    for (int index = 1; index < 8 && filledRouter.success; ++index) {
        filledRouter = application.addEndpoint(
            filledRouter.design,
            EndpointInstance{
                QStringLiteral("capacity_%1").arg(index),
                QStringLiteral("client"),
                EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
                {},
            });
    }
    check(filledRouter.success && filledRouter.design.endpoints.size() == 8,
          QStringLiteral("Application accepts exactly the Package attachment capacity"));
    if (filledRouter.success) {
        checkAtomicFailure(
            application.addEndpoint(
                filledRouter.design,
                EndpointInstance{
                    QStringLiteral("capacity_overflow"),
                    QStringLiteral("client"),
                    EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
                    {},
                }),
            filledRouter.design,
            QStringLiteral("endpoint.router_capacity"),
            QStringLiteral("Application rejects over-capacity attachment atomically"));
    }

    const DesignResult genericConfigured = application.setElementConfiguration(
        created.design,
        ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")},
        QStringLiteral("generic-attachment"),
        QJsonObject{{QStringLiteral("latency"), 3}});
    const DesignResult clientConfigured = genericConfigured.success
        ? application.setElementConfiguration(
              genericConfigured.design,
              ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")},
              QStringLiteral("client-attachment"),
              QJsonObject{{QStringLiteral("width"), 64}})
        : DesignResult{};
    check(clientConfigured.success,
          QStringLiteral("type-change fixture has generic and client-only attachment configurations"));

    const DesignResult addedClock = clientConfigured.success
        ? application.addDomain(
              clientConfigured.design,
              DomainDefinition{
                  QStringLiteral("clock-alt"),
                  QStringLiteral("clock"),
                  QStringLiteral("Alternate clock"),
                  QJsonObject{{QStringLiteral("frequencyMHz"), 500}}})
        : DesignResult{};
    const DesignResult assignedClock = addedClock.success
        ? application.assignDomainsToElements(
              addedClock.design,
              QVector<ElementRef>{ElementRef{
                  ElementKind::Endpoint, QStringLiteral("ep0")}},
              QStringLiteral("clock"),
              QStringList{QStringLiteral("clock-alt")})
        : DesignResult{};
    NocDesign crossingDesign = assignedClock.design;
    if (assignedClock.success) {
        crossingDesign.crossingPolicies.append(DomainCrossingPolicy{
            QStringLiteral("cdc"),
            QStringLiteral("clock"),
            QStringLiteral("clock-main"),
            QStringLiteral("clock-alt"),
            QJsonObject{{QStringLiteral("stages"), 2}}
        });
        crossingDesign.edgeOverrides.append(DomainEdgeOverride{
            ElementRef{
                ElementKind::EndpointAttachment, QStringLiteral("ep0")},
            QStringLiteral("clock"),
            QStringLiteral("cdc"),
            QJsonObject{{QStringLiteral("stages"), 3}}
        });
    }
    check(assignedClock.success
              && application.validate(crossingDesign, false).success,
          QStringLiteral("type-change fixture has a valid retained Endpoint Domain crossing"));

    const EndpointTypeChangePlan preservePlan =
        application.planEndpointTypeChange(
            crossingDesign,
            QStringLiteral("ep0"),
            QStringLiteral("memory"),
            EndpointParameterMigration::PreserveCompatible,
            QJsonObject{{QStringLiteral("banks"), 16}});
    check(preservePlan.canApply()
              && preservePlan.requiresImpactConfirmation()
              && preservePlan.removedAttachmentConfigurations.size() == 1
              && preservePlan.removedAttachmentConfigurations.constFirst().propertySet
                     == QStringLiteral("client-attachment")
              && preservePlan.retainedDomainMemberships.size() == 1
              && preservePlan.parameters
                  == QJsonObject{
                      {QStringLiteral("protocol"), QStringLiteral("chi")},
                      {QStringLiteral("width"), 64},
                      {QStringLiteral("queueDepth"), 12},
                      {QStringLiteral("enabled"), true},
                      {QStringLiteral("banks"), 16}},
          QStringLiteral(
              "PreserveCompatible keeps valid shared values, resets incompatible enum/range values, applies defaults and then the explicit patch"));

    const DesignResult unknownPatch = application.changeEndpointType(
        crossingDesign,
        QStringLiteral("ep0"),
        QStringLiteral("memory"),
        EndpointParameterMigration::PreserveCompatible,
        QJsonObject{{QStringLiteral("hardcodedTrick"), true}});
    checkAtomicFailure(
        unknownPatch,
        crossingDesign,
        QStringLiteral("parameter.unknown"),
        QStringLiteral("unknown type-change patches fail atomically"));

    checkAtomicFailure(
        application.changeEndpointType(
            crossingDesign,
            QStringLiteral("ep0"),
            QStringLiteral("memory"),
            EndpointParameterMigration::PreserveCompatible,
            QJsonObject{{QStringLiteral("banks"), 16}}),
        crossingDesign,
        QStringLiteral(
            "endpoint.type_change_missing_attachment_configuration_confirmation"),
        QStringLiteral("type changes cannot silently discard incompatible attachment state"));

    EndpointTypeChangeImpactConfirmation staleConfirmation;
    staleConfirmation.removedAttachmentConfigurations =
        preservePlan.removedAttachmentConfigurations;
    if (!staleConfirmation.removedAttachmentConfigurations.isEmpty()) {
        staleConfirmation.removedAttachmentConfigurations[0].properties.insert(
            QStringLiteral("width"), 128);
    }
    checkAtomicFailure(
        application.changeEndpointType(
            crossingDesign,
            QStringLiteral("ep0"),
            QStringLiteral("memory"),
            EndpointParameterMigration::PreserveCompatible,
            QJsonObject{{QStringLiteral("banks"), 16}},
            staleConfirmation),
        crossingDesign,
        QStringLiteral(
            "endpoint.type_change_stale_attachment_configuration_confirmation"),
        QStringLiteral("stale attachment impact confirmations fail atomically"));

    EndpointTypeChangeImpactConfirmation extraConfirmation;
    extraConfirmation.removedAttachmentConfigurations =
        preservePlan.removedAttachmentConfigurations;
    if (const ElementConfiguration* generic = findConfiguration(
            crossingDesign,
            QStringLiteral("ep0"),
            QStringLiteral("generic-attachment"))) {
        extraConfirmation.removedAttachmentConfigurations.append(*generic);
    }
    checkAtomicFailure(
        application.changeEndpointType(
            crossingDesign,
            QStringLiteral("ep0"),
            QStringLiteral("memory"),
            EndpointParameterMigration::PreserveCompatible,
            QJsonObject{{QStringLiteral("banks"), 16}},
            extraConfirmation),
        crossingDesign,
        QStringLiteral(
            "endpoint.type_change_extra_attachment_configuration_confirmation"),
        QStringLiteral("extra attachment deletion confirmations fail atomically"));

    const DesignResult refreshedClientConfiguration =
        application.setElementConfiguration(
            crossingDesign,
            ElementRef{
                ElementKind::EndpointAttachment, QStringLiteral("ep0")},
            QStringLiteral("client-attachment"),
            QJsonObject{{QStringLiteral("width"), 128}});
    EndpointTypeChangeImpactConfirmation oldPreviewConfirmation;
    oldPreviewConfirmation.removedAttachmentConfigurations =
        preservePlan.removedAttachmentConfigurations;
    if (refreshedClientConfiguration.success) {
        checkAtomicFailure(
            application.changeEndpointType(
                refreshedClientConfiguration.design,
                QStringLiteral("ep0"),
                QStringLiteral("memory"),
                EndpointParameterMigration::PreserveCompatible,
                QJsonObject{{QStringLiteral("banks"), 16}},
                oldPreviewConfirmation),
            refreshedClientConfiguration.design,
            QStringLiteral(
                "endpoint.type_change_stale_attachment_configuration_confirmation"),
            QStringLiteral(
                "Application rebuilds the plan against current state so a stale UI preview cannot delete a changed configuration"));
    }

    QJsonObject parametersAfterPreview = createdEndpoint->parameters;
    parametersAfterPreview.insert(QStringLiteral("queueDepth"), 15);
    const DesignResult editedAfterPreview = application.updateEndpointParameters(
        crossingDesign, QStringLiteral("ep0"), parametersAfterPreview);
    const DesignResult rebasedAfterParameterChange = editedAfterPreview.success
        ? application.changeEndpointType(
              editedAfterPreview.design,
              QStringLiteral("ep0"),
              QStringLiteral("memory"),
              EndpointParameterMigration::PreserveCompatible,
              QJsonObject{{QStringLiteral("banks"), 16}},
              oldPreviewConfirmation)
        : DesignResult{};
    const EndpointInstance* rebasedEndpoint = findEndpoint(
        rebasedAfterParameterChange.design, QStringLiteral("ep0"));
    check(rebasedAfterParameterChange.success && rebasedEndpoint
              && rebasedEndpoint->parameters
                     .value(QStringLiteral("queueDepth")).toInt() == 15
              && !findConfiguration(
                  rebasedAfterParameterChange.design,
                  QStringLiteral("ep0"),
                  QStringLiteral("client-attachment"))
              && findConfiguration(
                  rebasedAfterParameterChange.design,
                  QStringLiteral("ep0"),
                  QStringLiteral("generic-attachment")),
          QStringLiteral(
              "Application rebuilds migration from current Endpoint parameters while deleting only the exactly confirmed attachment record"));

    EndpointTypeChangeImpactConfirmation exactConfirmation;
    exactConfirmation.removedAttachmentConfigurations =
        preservePlan.removedAttachmentConfigurations;
    const DesignResult changed = application.changeEndpointType(
        crossingDesign,
        QStringLiteral("ep0"),
        QStringLiteral("memory"),
        EndpointParameterMigration::PreserveCompatible,
        QJsonObject{{QStringLiteral("banks"), 16}},
        exactConfirmation);
    const EndpointInstance* changedEndpoint = findEndpoint(
        changed.design, QStringLiteral("ep0"));
    check(changed.success && changedEndpoint
              && changedEndpoint->type == QStringLiteral("memory")
              && changedEndpoint->parameters == preservePlan.parameters
              && !findConfiguration(changed.design,
                                    QStringLiteral("ep0"),
                                    QStringLiteral("client-attachment"))
              && findConfiguration(changed.design,
                                   QStringLiteral("ep0"),
                                   QStringLiteral("generic-attachment"))
              && changed.design.domainMemberships
                     == crossingDesign.domainMemberships
              && changed.design.edgeOverrides == crossingDesign.edgeOverrides,
          QStringLiteral(
              "confirmed type changes remove only incompatible attachment configs and retain Domain memberships and edge overrides"));

    if (changed.success) {
        checkAtomicFailure(
            application.changeEndpointType(
                changed.design,
                QStringLiteral("ep0"),
                QStringLiteral("memory"),
                EndpointParameterMigration::PreserveCompatible,
                {},
                oldPreviewConfirmation),
            changed.design,
            QStringLiteral("endpoint.type_change_same_type"),
            QStringLiteral(
                "a stale UI action is rejected after the Endpoint type has already changed and cannot delete more state"));
    }

    const EndpointTypeChangePlan resetPlan = application.planEndpointTypeChange(
        crossingDesign,
        QStringLiteral("ep0"),
        QStringLiteral("memory"),
        EndpointParameterMigration::ResetToDefaults);
    check(resetPlan.canApply()
              && resetPlan.parameters
                  == QJsonObject{
                      {QStringLiteral("protocol"), QStringLiteral("chi")},
                      {QStringLiteral("width"), 64},
                      {QStringLiteral("queueDepth"), 8},
                      {QStringLiteral("enabled"), false},
                      {QStringLiteral("banks"), 8}},
          QStringLiteral("ResetToDefaults ignores otherwise compatible source values"));

    if (failures == 0) {
        QTextStream(stdout) << "application-endpoint-tests passed" << Qt::endl;
        return 0;
    }
    return 1;
}
