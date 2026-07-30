#include "package/package.h"
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

QJsonObject baseManifest(int formatVersion) {
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), formatVersion},
        {QStringLiteral("id"), QStringLiteral("test.package-domains")},
        {QStringLiteral("name"), QStringLiteral("Package Domain test")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 4},
                {QStringLiteral("default"), 2}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 4},
                {QStringLiteral("default"), 2}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{}},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 1},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("package-domain-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
            {QStringLiteral("supportsValidate"), false},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
}

QJsonArray completeDomainTypes() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("clock")},
            {QStringLiteral("label"), QStringLiteral("Clock Domain")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("router"), QStringLiteral("endpoint")
            }},
            {QStringLiteral("cardinality"), QStringLiteral("multiple")},
            {QStringLiteral("required"), true},
            {QStringLiteral("properties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("frequencyMHz")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("maximum"), 5000},
                    {QStringLiteral("default"), 800}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("backupPowers")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("multiple"), true},
                    {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
                }
            }},
            {QStringLiteral("relations"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("synchronizedWith")},
                    {QStringLiteral("label"), QStringLiteral("Synchronized with")},
                    {QStringLiteral("targetTypes"), QJsonArray{QStringLiteral("clock")}},
                    {QStringLiteral("cardinality"), QStringLiteral("multiple")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("properties"), QJsonArray{
                        QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("phaseOffset")},
                            {QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("default"), 0}
                        }
                    }}
                }
            }},
            {QStringLiteral("crossingProperties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("synchronizerStages")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("default"), 2}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("isolationDomains")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("multiple"), true},
                    {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
                }
            }}
        },
        QJsonObject{
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
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("default"), 900}
                }
            }},
            {QStringLiteral("relations"), QJsonArray{}},
            {QStringLiteral("crossingProperties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("requiresIsolation")},
                    {QStringLiteral("type"), QStringLiteral("boolean")},
                    {QStringLiteral("default"), true}
                }
            }}
        }
    };
}

PackageLoadResult loadManifest(const QString& packageRoot, const QJsonObject& manifest) {
    const QString path = QDir(packageRoot).filePath(QStringLiteral("package.json"));
    QVector<Diagnostic> diagnostics;
    if (!saveJsonObject(path, manifest, &diagnostics)) {
        PackageLoadResult result;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    return loadPackage(packageRoot);
}

void expectFailure(const QString& packageRoot,
                   const QJsonObject& manifest,
                   const QString& code,
                   const QString& description) {
    const PackageLoadResult result = loadManifest(packageRoot, manifest);
    check(!result.success && hasDiagnosticCode(result.diagnostics, code),
          description + QStringLiteral(" reports ") + code);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    QTemporaryDir fixture(QStringLiteral("/tmp/finepaper-package-domain-test-XXXXXX"));
    check(fixture.isValid() && prepareFixture(fixture.path()),
          QStringLiteral("the Package Domain fixture is available"));
    if (!fixture.isValid()) {
        return 1;
    }

    const QJsonObject v1Manifest = baseManifest(1);
    const PackageLoadResult v1Result = loadManifest(fixture.path(), v1Manifest);
    check(v1Result.success && v1Result.package && v1Result.package->domainTypes.isEmpty(),
          QStringLiteral("Package V1 succeeds without domainTypes"));

    QJsonObject v1WithDomains = v1Manifest;
    v1WithDomains.insert(QStringLiteral("domainTypes"), QJsonArray{});
    expectFailure(fixture.path(),
                  v1WithDomains,
                  QStringLiteral("package.domain_types_require_v2"),
                  QStringLiteral("Package V1 with domainTypes"));

    const QJsonObject v2MissingDomains = baseManifest(2);
    expectFailure(fixture.path(),
                  v2MissingDomains,
                  QStringLiteral("package.invalid_domain_types"),
                  QStringLiteral("Package V2 without domainTypes"));

    QJsonObject v2EmptyDomains = baseManifest(2);
    v2EmptyDomains.insert(QStringLiteral("domainTypes"), QJsonArray{});
    const PackageLoadResult v2EmptyResult = loadManifest(fixture.path(), v2EmptyDomains);
    check(v2EmptyResult.success && v2EmptyResult.package
              && v2EmptyResult.package->domainTypes.isEmpty(),
          QStringLiteral("Package V2 accepts an explicit empty domainTypes array"));

    QJsonObject completeManifest = baseManifest(2);
    completeManifest.insert(QStringLiteral("domainTypes"), completeDomainTypes());
    const PackageLoadResult completeResult = loadManifest(fixture.path(), completeManifest);
    check(completeResult.success && completeResult.package,
          QStringLiteral("a complete Package Domain schema loads successfully"));
    if (completeResult.package) {
        const DomainTypeDefinition* clock = completeResult.package->domainType(
            QStringLiteral("clock"));
        check(clock != nullptr, QStringLiteral("Domain types are addressable by id"));
        if (clock) {
            check(clock->required && clock->cardinality == DomainCardinality::Multiple,
                  QStringLiteral("Domain required and multiple cardinality are parsed"));
            check(clock->appliesTo.contains(ElementKind::Router)
                      && clock->appliesTo.contains(ElementKind::Endpoint),
                  QStringLiteral("Domain element applicability is parsed"));

            const auto reference = std::find_if(
                clock->properties.cbegin(),
                clock->properties.cend(),
                [](const DomainPropertyDefinition& property) {
                    return property.id == QStringLiteral("backupPowers");
                });
            check(reference != clock->properties.cend()
                      && reference->required
                      && reference->multiple
                      && reference->referenceDomainType == QStringLiteral("power")
                      && !reference->hasDefault,
                  QStringLiteral("required, multiple Domain references are parsed"));

            check(clock->relations.size() == 1
                      && clock->relations[0].id == QStringLiteral("synchronizedWith")
                      && clock->relations[0].required
                      && clock->relations[0].cardinality == DomainCardinality::Multiple
                      && clock->relations[0].targetTypes
                          == QStringList{QStringLiteral("clock")}
                      && clock->relations[0].properties.size() == 1,
                  QStringLiteral("Domain relation schemas and their properties are parsed"));
            check(clock->crossingProperties.size() == 2
                      && clock->crossingProperties[0].id
                          == QStringLiteral("synchronizerStages")
                      && clock->crossingProperties[0].required
                      && clock->crossingProperties[1].referenceDomainType
                          == QStringLiteral("power"),
                  QStringLiteral("Domain crossing properties are parsed"));
        }
    }

    QJsonObject duplicateTypeManifest = completeManifest;
    QJsonArray duplicateTypes = duplicateTypeManifest.value(
        QStringLiteral("domainTypes")).toArray();
    duplicateTypes.append(duplicateTypes[0]);
    duplicateTypeManifest.insert(QStringLiteral("domainTypes"), duplicateTypes);
    expectFailure(fixture.path(),
                  duplicateTypeManifest,
                  QStringLiteral("package.duplicate_domain_type"),
                  QStringLiteral("duplicated Domain type ids"));

    QJsonObject duplicatePropertyManifest = completeManifest;
    QJsonArray duplicatePropertyTypes = duplicatePropertyManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithDuplicateProperty = duplicatePropertyTypes[0].toObject();
    QJsonArray duplicateProperties = clockWithDuplicateProperty.value(
        QStringLiteral("properties")).toArray();
    duplicateProperties.append(duplicateProperties[0]);
    clockWithDuplicateProperty.insert(QStringLiteral("properties"), duplicateProperties);
    duplicatePropertyTypes[0] = clockWithDuplicateProperty;
    duplicatePropertyManifest.insert(QStringLiteral("domainTypes"), duplicatePropertyTypes);
    expectFailure(fixture.path(),
                  duplicatePropertyManifest,
                  QStringLiteral("package.duplicate_domain_property"),
                  QStringLiteral("duplicated Domain property ids"));

    QJsonObject unknownPropertyReferenceManifest = completeManifest;
    QJsonArray unknownPropertyTypes = unknownPropertyReferenceManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithUnknownPropertyReference = unknownPropertyTypes[0].toObject();
    QJsonArray unknownReferenceProperties = clockWithUnknownPropertyReference.value(
        QStringLiteral("properties")).toArray();
    QJsonObject unknownReferenceProperty = unknownReferenceProperties[1].toObject();
    unknownReferenceProperty.insert(
        QStringLiteral("referenceDomainType"), QStringLiteral("missing"));
    unknownReferenceProperties[1] = unknownReferenceProperty;
    clockWithUnknownPropertyReference.insert(
        QStringLiteral("properties"), unknownReferenceProperties);
    unknownPropertyTypes[0] = clockWithUnknownPropertyReference;
    unknownPropertyReferenceManifest.insert(
        QStringLiteral("domainTypes"), unknownPropertyTypes);
    expectFailure(fixture.path(),
                  unknownPropertyReferenceManifest,
                  QStringLiteral("package.unknown_domain_property_reference"),
                  QStringLiteral("an unknown Domain property reference"));

    QJsonObject unknownRelationTargetManifest = completeManifest;
    QJsonArray unknownRelationTypes = unknownRelationTargetManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithUnknownRelationTarget = unknownRelationTypes[0].toObject();
    QJsonArray unknownTargetRelations = clockWithUnknownRelationTarget.value(
        QStringLiteral("relations")).toArray();
    QJsonObject unknownTargetRelation = unknownTargetRelations[0].toObject();
    unknownTargetRelation.insert(
        QStringLiteral("targetTypes"), QJsonArray{QStringLiteral("missing")});
    unknownTargetRelations[0] = unknownTargetRelation;
    clockWithUnknownRelationTarget.insert(
        QStringLiteral("relations"), unknownTargetRelations);
    unknownRelationTypes[0] = clockWithUnknownRelationTarget;
    unknownRelationTargetManifest.insert(
        QStringLiteral("domainTypes"), unknownRelationTypes);
    expectFailure(fixture.path(),
                  unknownRelationTargetManifest,
                  QStringLiteral("package.unknown_domain_relation_target"),
                  QStringLiteral("an unknown Domain relation target"));

    QJsonObject invalidDefaultManifest = completeManifest;
    QJsonArray invalidDefaultTypes = invalidDefaultManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithInvalidDefault = invalidDefaultTypes[0].toObject();
    QJsonArray invalidDefaultProperties = clockWithInvalidDefault.value(
        QStringLiteral("properties")).toArray();
    QJsonObject invalidDefaultProperty = invalidDefaultProperties[1].toObject();
    invalidDefaultProperty.insert(
        QStringLiteral("default"), QJsonArray{QStringLiteral("pd_main")});
    invalidDefaultProperties[1] = invalidDefaultProperty;
    clockWithInvalidDefault.insert(
        QStringLiteral("properties"), invalidDefaultProperties);
    invalidDefaultTypes[0] = clockWithInvalidDefault;
    invalidDefaultManifest.insert(QStringLiteral("domainTypes"), invalidDefaultTypes);
    expectFailure(fixture.path(),
                  invalidDefaultManifest,
                  QStringLiteral("package.domain_reference_default_unsupported"),
                  QStringLiteral("a Domain reference property with an instance-id default"));

    QJsonObject invalidMultipleDefaultManifest = completeManifest;
    QJsonArray invalidMultipleDefaultTypes = invalidMultipleDefaultManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithInvalidMultipleDefault =
        invalidMultipleDefaultTypes[0].toObject();
    QJsonArray invalidMultipleDefaultProperties =
        clockWithInvalidMultipleDefault.value(QStringLiteral("properties")).toArray();
    QJsonObject invalidMultipleDefaultProperty =
        invalidMultipleDefaultProperties[1].toObject();
    invalidMultipleDefaultProperty.remove(QStringLiteral("referenceDomainType"));
    invalidMultipleDefaultProperty.insert(
        QStringLiteral("default"), QStringLiteral("not-an-array"));
    invalidMultipleDefaultProperties[1] = invalidMultipleDefaultProperty;
    clockWithInvalidMultipleDefault.insert(
        QStringLiteral("properties"), invalidMultipleDefaultProperties);
    invalidMultipleDefaultTypes[0] = clockWithInvalidMultipleDefault;
    invalidMultipleDefaultManifest.insert(
        QStringLiteral("domainTypes"), invalidMultipleDefaultTypes);
    expectFailure(fixture.path(),
                  invalidMultipleDefaultManifest,
                  QStringLiteral("package.invalid_parameter_default"),
                  QStringLiteral("a multiple Domain property with a scalar default"));

    if (failures == 0) {
        QTextStream(stdout) << "Package Domain tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Package Domain test(s) failed" << Qt::endl;
    return 1;
}
