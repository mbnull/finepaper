#include "application/application.h"
#include "application/runtime_settings.h"
#include "execution/process.h"
#include "noc/model.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

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

QJsonObject objectWithStringField(const QJsonArray& values,
                                  const QString& field,
                                  const QString& expected) {
    const auto value = std::find_if(
        values.cbegin(), values.cend(), [&](const QJsonValue& candidate) {
            return candidate.isObject()
                && candidate.toObject().value(field).toString() == expected;
        });
    return value == values.cend() ? QJsonObject{} : value->toObject();
}

bool preparePackageFixture(const QString& packageRoot,
                           const QString& generatorSource,
                           const QJsonObject& manifest) {
    const QString executable = QDir(packageRoot).filePath(QStringLiteral("runtime/bin/generate"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    if (!QFileInfo::exists(executable)) {
        if (!QFile::copy(generatorSource, executable)) {
            return false;
        }
        if (!QFile::setPermissions(
                executable,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                    QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                    QFileDevice::ReadOther | QFileDevice::ExeOther)) {
            return false;
        }
    }
    return saveJsonObject(QDir(packageRoot).filePath(QStringLiteral("package.json")), manifest);
}

QJsonObject request() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("test_mesh")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("finepaper.noc")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("z_cpu")},
                {QStringLiteral("type"), QStringLiteral("master")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("a_cpu")},
                {QStringLiteral("type"), QStringLiteral("master")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("memory")},
                {QStringLiteral("type"), QStringLiteral("slave")},
                {QStringLiteral("router"), QJsonArray{1, 1}},
                {QStringLiteral("parameters"), QJsonObject{
                    {QStringLiteral("dataWidth"), 128}
                }}
            }
        }}
    };
}

QJsonObject configurableRequest() {
    QJsonObject configurable = request();
    configurable.insert(QStringLiteral("name"),
                        QStringLiteral("configurable_mesh"));
    QJsonObject package = configurable.value(
        QStringLiteral("package")).toObject();
    package.insert(QStringLiteral("version"), QStringLiteral("3.0.0"));
    configurable.insert(QStringLiteral("package"), package);
    QJsonArray endpoints = configurable.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject memory = endpoints[2].toObject();
    QJsonObject memoryParameters = memory.value(
        QStringLiteral("parameters")).toObject();
    memoryParameters.insert(QStringLiteral("bufferDepth"), 37);
    memoryParameters.insert(QStringLiteral("qosEnabled"), true);
    memory.insert(QStringLiteral("parameters"), memoryParameters);
    endpoints[2] = memory;
    configurable.insert(QStringLiteral("endpoints"), endpoints);
    return configurable;
}

QJsonObject complexRequest() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("complex_demo")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.complex-engine")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("host")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            }
        }},
        {QStringLiteral("packageData"), QJsonObject{
            {QStringLiteral("vendorTopology"), QStringLiteral("opaque-to-core")}
        }}
    };
}

QJsonObject explicitSlotRequest() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("explicit_slot_demo")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.explicit-slots")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("device_0")},
                {QStringLiteral("type"), QStringLiteral("device")},
                {QStringLiteral("router"), QJsonArray{0, 0}},
                {QStringLiteral("slot"), QStringLiteral("local1")}
            }
        }}
    };
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);

    const PackageOperationResult invalidOperationResult = parsePackageOperationResult(
        QJsonObject{
            {QStringLiteral("success"), QStringLiteral("yes")},
            {QStringLiteral("diagnostics"), QJsonObject{}},
            {QStringLiteral("artifacts"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("a")},
                            {QStringLiteral("type"), QStringLiteral("rtl")},
                            {QStringLiteral("path"), QStringLiteral("a.sv")},
                            {QStringLiteral("primary"), QStringLiteral("true")}}
            }}},
        QStringLiteral("/tmp/result.json"),
        QStringLiteral("test"),
        ArtifactResultPolicy::Required);
    check(!invalidOperationResult.protocolValid
              && hasDiagnosticCode(invalidOperationResult.diagnostics,
                                   QStringLiteral("operation.invalid_result")),
          QStringLiteral("Package operation results are parsed with a strict typed contract"));

    const JsonObjectLoadResult referenceManifest = loadJsonObject(
        QDir(projectRoot).filePath(QStringLiteral("packages/finepaper-noc/package.json")));
    check(referenceManifest.success, QStringLiteral("reference Package manifest is readable"));
    QTemporaryDir manifestFixture(QStringLiteral("/tmp/finepaper-manifest-test-XXXXXX"));
    check(manifestFixture.isValid(), QStringLiteral("temporary Package fixture is available"));
    const QString generatorSource = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc/runtime/bin/generate"));
    if (referenceManifest.success && manifestFixture.isValid()) {
        QJsonObject invalidManifest = referenceManifest.object;
        invalidManifest.insert(QStringLiteral("endpointTypes"), QJsonObject{});
        invalidManifest.insert(QStringLiteral("engine"), QStringLiteral("invalid"));
        QJsonObject attachment = invalidManifest.value(QStringLiteral("attachment")).toObject();
        attachment.insert(QStringLiteral("maxPerRouter"), 0);
        attachment.insert(QStringLiteral("slotMode"), 7);
        invalidManifest.insert(QStringLiteral("attachment"), attachment);
        QJsonObject generator = invalidManifest.value(QStringLiteral("generator")).toObject();
        generator.insert(QStringLiteral("supportsValidate"), QStringLiteral("true"));
        generator.insert(QStringLiteral("timeoutSeconds"),
                         kMaximumPackageTimeoutSeconds + 1);
        invalidManifest.insert(QStringLiteral("generator"), generator);
        QJsonObject mesh = invalidManifest.value(QStringLiteral("mesh")).toObject();
        QJsonObject rows = mesh.value(QStringLiteral("rows")).toObject();
        rows.insert(QStringLiteral("min"), 4);
        rows.insert(QStringLiteral("default"), 2);
        rows.insert(QStringLiteral("max"), kMaximumMeshDimension + 1);
        mesh.insert(QStringLiteral("rows"), rows);
        invalidManifest.insert(QStringLiteral("mesh"), mesh);
        invalidManifest.insert(QStringLiteral("parameters"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badInteger")},
                        {QStringLiteral("type"), QStringLiteral("integer")},
                        {QStringLiteral("default"), 1.5},
                        {QStringLiteral("minimum"), 10},
                        {QStringLiteral("maximum"), 1}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badEnum")},
                        {QStringLiteral("type"), QStringLiteral("enum")},
                        {QStringLiteral("default"), QStringLiteral("missing")},
                        {QStringLiteral("values"), QJsonArray{QStringLiteral("a"),
                                                              QStringLiteral("a"), 3}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badType")},
                        {QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("default"), QJsonObject{}}}
        });
        check(preparePackageFixture(manifestFixture.path(), generatorSource, invalidManifest),
              QStringLiteral("strict Package fixture is prepared"));
        const PackageLoadResult invalidPackage = loadPackage(manifestFixture.path());
        check(!invalidPackage.success &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_endpoint_types")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_engine")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_attachment_capacity")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_timeout")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_mesh_rows")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.mesh_projection_too_large")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_parameter_type")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_parameter_default")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.duplicate_parameter_value")),
              QStringLiteral("Package manifest validation fails closed across typed and semantic fields"));
    }

#ifdef Q_OS_UNIX
    QTemporaryDir executableFixture(QStringLiteral("/tmp/finepaper-executable-test-XXXXXX"));
    if (referenceManifest.success && executableFixture.isValid()) {
        QJsonObject escapedManifest = referenceManifest.object;
        const QString linkedExecutable = QDir(executableFixture.path()).filePath(
            QStringLiteral("runtime/bin/generate"));
        check(QDir().mkpath(QFileInfo(linkedExecutable).absolutePath()) &&
                  QFile(QStringLiteral("/bin/true")).link(linkedExecutable) &&
                  saveJsonObject(QDir(executableFixture.path()).filePath(
                                     QStringLiteral("package.json")),
                                 escapedManifest),
              QStringLiteral("escaped executable fixture is prepared"));
        const PackageLoadResult escapedPackage = loadPackage(executableFixture.path());
        check(!escapedPackage.success &&
                  hasDiagnosticCode(escapedPackage.diagnostics,
                                    QStringLiteral("package.executable_escape")),
              QStringLiteral("Package executable symlinks cannot escape the Package root"));
    }
#endif

    const RuntimeLocations locations = resolveRuntimeLocations(
        QStringList{QDir(projectRoot).filePath(QStringLiteral("packages"))}, projectRoot);
    check(locations.packageRoots == QStringList{QDir(projectRoot).filePath(QStringLiteral("packages"))},
          QStringLiteral("explicit Package roots use the shared runtime resolver"));
    check(locations.defaultOutputRoot == QDir(projectRoot).filePath(QStringLiteral("output")),
          QStringLiteral("default output root comes from the shared runtime resolver"));
    RuntimeLocations installedLocations = locations;
    const QString fixtureRoot = QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"));
    appendPackageRoots(installedLocations, QStringList{fixtureRoot, fixtureRoot}, projectRoot);
    check(installedLocations.packageRoots.size() == 2 &&
              installedLocations.packageRoots.contains(fixtureRoot),
          QStringLiteral("manually added Package roots merge centrally without duplicates"));

    FinepaperApplication finepaper;
    const QString bundledPackageRoot = QDir(projectRoot).filePath(
        QStringLiteral("packages"));
    const QString bundledPackagePath = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc"));
    const QVector<Diagnostic> packageDiagnostics = finepaper.reloadPackages(
        QStringList{bundledPackageRoot});
    check(!hasErrors(packageDiagnostics), QStringLiteral("reference Packages load"));
    check(finepaper.packages().size() == 2
              && finepaper.packages().at(0).key()
                  == QStringLiteral("finepaper.noc@1.0.0")
              && finepaper.packages().at(1).key()
                  == QStringLiteral("finepaper.noc@3.0.0"),
          QStringLiteral("V1 compatibility and V3 configurable Packages load together"));
    const auto configurablePackage = std::find_if(
        finepaper.packages().cbegin(), finepaper.packages().cend(),
        [](const PackageDefinition& package) {
            return package.key() == QStringLiteral("finepaper.noc@3.0.0");
        });
    const EndpointTypeDefinition* masterType =
        configurablePackage != finepaper.packages().cend()
        ? configurablePackage->endpointType(QStringLiteral("master"))
        : nullptr;
    const ParameterDefinition* qosParameter = nullptr;
    if (masterType) {
        const auto parameter = std::find_if(
            masterType->parameters.cbegin(), masterType->parameters.cend(),
            [](const ParameterDefinition& value) {
                return value.id == QStringLiteral("qosEnabled");
            });
        if (parameter != masterType->parameters.cend()) {
            qosParameter = &(*parameter);
        }
    }
    check(qosParameter
              && qosParameter->category == QStringLiteral("Traffic")
              && qosParameter->advanced
              && !qosParameter->description.isEmpty(),
          QStringLiteral(
              "bundled V3 Endpoint schemas expose Package-owned presentation metadata"));

    QTemporaryDir duplicatePackageFixture(
        QStringLiteral("/tmp/finepaper-duplicate-package-XXXXXX"));
    check(referenceManifest.success && duplicatePackageFixture.isValid()
              && preparePackageFixture(duplicatePackageFixture.path(),
                                       generatorSource,
                                       referenceManifest.object),
          QStringLiteral("duplicate Package fixture is prepared"));
    if (duplicatePackageFixture.isValid()) {
        FinepaperApplication resilientCatalog;
        const QString missingPackageRoot = QDir(duplicatePackageFixture.path()).filePath(
            QStringLiteral("missing-root"));
        const QVector<Diagnostic> discoveryDiagnostics = resilientCatalog.reloadPackages(
            QStringList{bundledPackageRoot,
                        bundledPackagePath,
                        duplicatePackageFixture.path(),
                        missingPackageRoot});
        check(!hasErrors(discoveryDiagnostics)
                  && resilientCatalog.packages().size() == 2
                  && resilientCatalog.packages().at(0).id == QStringLiteral("finepaper.noc")
                  && hasDiagnosticCode(discoveryDiagnostics,
                                       QStringLiteral("package.duplicate_ignored"))
                  && hasDiagnosticCode(discoveryDiagnostics,
                                       QStringLiteral("package.root_missing")),
              QStringLiteral("duplicate and missing roots do not hide valid NoC Packages"));
    }
    if (manifestFixture.isValid()) {
        const QVector<Diagnostic> failedReload = finepaper.reloadPackages(
            QStringList{manifestFixture.path()});
        check(hasErrors(failedReload) && finepaper.packages().size() == 2 &&
                  finepaper.packages().at(0).id == QStringLiteral("finepaper.noc"),
              QStringLiteral("failed Package reload preserves the previous catalog snapshot"));
    }

    const DesignResult created = finepaper.createDesign(request());
    check(created.success, QStringLiteral("request creates a valid NocDesign"));
    check(created.design.parameters.value(QStringLiteral("dataWidth")).toInt() == 64,
          QStringLiteral("Package defaults are materialized at creation"));
    check(created.design.endpoints.at(0).parameters.value(QStringLiteral("bufferDepth")).toInt() == 16,
          QStringLiteral("endpoint defaults are materialized at creation"));

    QJsonObject numericNameRequest = request();
    numericNameRequest.insert(QStringLiteral("name"), QStringLiteral("123 Demo-NoC"));
    numericNameRequest.remove(QStringLiteral("id"));
    const DesignResult numericNameDesign = finepaper.createDesign(numericNameRequest);
    check(numericNameDesign.success &&
              numericNameDesign.design.id == QStringLiteral("noc_123_demo_noc"),
          QStringLiteral("generated design IDs are safe when display names start with digits"));

    QJsonObject fractionalTopologyRequest = request();
    QJsonObject fractionalTopology = fractionalTopologyRequest.value(
        QStringLiteral("topology")).toObject();
    fractionalTopology.insert(QStringLiteral("rows"), 1.5);
    fractionalTopologyRequest.insert(QStringLiteral("topology"), fractionalTopology);
    const DesignResult fractionalTopologyDesign = finepaper.createDesign(
        fractionalTopologyRequest);
    check(!fractionalTopologyDesign.success &&
              hasDiagnosticCode(fractionalTopologyDesign.diagnostics,
                                QStringLiteral("create.expected_integer")),
          QStringLiteral("createDesign rejects fractional topology dimensions"));

    QJsonObject invalidEndpointsRequest = request();
    invalidEndpointsRequest.insert(QStringLiteral("endpoints"), QJsonObject{});
    const DesignResult invalidEndpointsDesign = finepaper.createDesign(invalidEndpointsRequest);
    check(!invalidEndpointsDesign.success &&
              hasDiagnosticCode(invalidEndpointsDesign.diagnostics,
                                QStringLiteral("create.expected_array")),
          QStringLiteral("createDesign rejects a non-array endpoints field"));

    QJsonObject automaticSlotRequest = request();
    QJsonArray automaticSlotEndpoints = automaticSlotRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject automaticSlotEndpoint = automaticSlotEndpoints.at(0).toObject();
    automaticSlotEndpoint.insert(QStringLiteral("slot"), QStringLiteral("0"));
    automaticSlotEndpoints[0] = automaticSlotEndpoint;
    automaticSlotRequest.insert(QStringLiteral("endpoints"), automaticSlotEndpoints);
    const DesignResult automaticSlotDesign = finepaper.createDesign(automaticSlotRequest);
    check(!automaticSlotDesign.success &&
              hasDiagnosticCode(automaticSlotDesign.diagnostics,
                                QStringLiteral("endpoint.automatic_slot_persisted")),
          QStringLiteral("automatic Package source designs cannot persist derived slots"));

    NocDesign duplicateAutomaticSlots = created.design;
    duplicateAutomaticSlots.endpoints[0].attachment.slot = QStringLiteral("same");
    duplicateAutomaticSlots.endpoints[1].attachment.slot = QStringLiteral("same");
    const ValidationResult duplicateAutomaticValidation = finepaper.validate(
        duplicateAutomaticSlots, false);
    check(!duplicateAutomaticValidation.success &&
              hasDiagnosticCode(duplicateAutomaticValidation.diagnostics,
                                QStringLiteral("endpoint.duplicate_slot")),
          QStringLiteral("duplicate persisted slots are rejected in automatic mode"));

    NocDesign staleSlotDesign = created.design;
    staleSlotDesign.endpoints[2].attachment.slot = QStringLiteral("stale");
    const DesignResult clearedSlotMove = finepaper.moveEndpoint(
        staleSlotDesign, QStringLiteral("memory"), RouterPosition{0, 1}, std::nullopt);
    check(clearedSlotMove.success &&
              !clearedSlotMove.design.endpoints.at(2).attachment.slot,
          QStringLiteral("moving with no slot clears stale automatic slot state"));

    NocDesign oversizedDesign = created.design;
    oversizedDesign.topology.rows = kMaximumMeshDimension;
    oversizedDesign.topology.columns = kMaximumMeshDimension;
    const QVector<Diagnostic> oversizedDiagnostics = validateDesignStructure(oversizedDesign);
    check(hasDiagnosticCode(oversizedDiagnostics,
                            QStringLiteral("topology.projection_too_large")) &&
              projectTopology(oversizedDesign).routers.isEmpty(),
          QStringLiteral("oversized topology is rejected and never projected"));

    NocDesign unsafeIdentifierDesign = created.design;
    unsafeIdentifierDesign.id = QStringLiteral("unsafe-id");
    const ValidationResult unsafeIdentifierValidation = finepaper.validate(
        unsafeIdentifierDesign, true);
    check(!unsafeIdentifierValidation.success,
          QStringLiteral("Package process validation rejects unsafe HDL design identifiers"));

    const NocDesign resolved = withResolvedAutomaticSlots(created.design);
    check(resolved.endpoints.at(0).attachment.slot == QStringLiteral("1"),
          QStringLiteral("automatic slot assignment is stable by endpoint id"));
    check(resolved.endpoints.at(1).attachment.slot == QStringLiteral("0"),
          QStringLiteral("automatic slot assignment is stable by endpoint id"));

    const TopologyProjection projection = projectTopology(resolved);
    check(projection.routers.size() == 4, QStringLiteral("2x2 Mesh projects four Routers"));
    check(projection.links.size() == 4, QStringLiteral("2x2 Mesh projects four Links"));
    check(projection.routers.at(0).id == QStringLiteral("r-0-0"),
          QStringLiteral("Mesh Router IDs are deterministic"));

    const DesignLoadResult reloaded = designFromJson(designToJson(created.design));
    check(reloaded.success, QStringLiteral("NocDesign JSON round-trips"));
    check(reloaded.design.endpoints.size() == created.design.endpoints.size(),
          QStringLiteral("NocDesign JSON preserves Endpoint attachments"));

    const DesignResult rejectedResize = finepaper.resizeMesh(created.design, 1, 1);
    check(!rejectedResize.success, QStringLiteral("Mesh resize does not silently detach Endpoints"));
    check(hasDiagnosticCode(rejectedResize.diagnostics,
                            QStringLiteral("mesh.resize_would_detach_endpoint")),
          QStringLiteral("unsafe Mesh resize reports its reason"));

    const DesignResult moved = finepaper.moveEndpoint(
        created.design, QStringLiteral("memory"), RouterPosition{0, 1});
    check(moved.success && moved.design.endpoints.at(2).attachment.router == RouterPosition{0, 1},
          QStringLiteral("Endpoint movement uses the shared application operation"));

    const DesignResult rejectedMove = finepaper.moveEndpoint(
        created.design, QStringLiteral("memory"), RouterPosition{99, 99});
    check(!rejectedMove.success
              && designToJson(rejectedMove.design) == designToJson(created.design),
          QStringLiteral("failed Endpoint movement returns the unchanged Design"));

    QJsonObject invalidParameters = created.design.parameters;
    invalidParameters.insert(QStringLiteral("dataWidth"), QStringLiteral("wide"));
    const DesignResult rejectedParameterUpdate = finepaper.updateParameters(
        created.design, invalidParameters);
    check(!rejectedParameterUpdate.success
              && designToJson(rejectedParameterUpdate.design) == designToJson(created.design),
          QStringLiteral("failed parameter updates return the unchanged Design"));

    const ValidationResult validation = finepaper.validate(created.design, true);
    check(validation.success, QStringLiteral("reference Package validates through its process boundary"));

    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-core-test-XXXXXX"));
    check(outputRoot.isValid(), QStringLiteral("temporary output directory is available"));
    if (outputRoot.isValid()) {
        const GenerationResult generation = finepaper.generate(
            created.design, GenerationOptions{outputRoot.path()});
        check(generation.success, QStringLiteral("reference Package generates RTL"));
        check(!generation.artifacts.isEmpty(), QStringLiteral("generation reports artifacts"));
        check(std::none_of(
                  generation.artifacts.cbegin(),
                  generation.artifacts.cend(),
                  [](const Artifact& artifact) {
                      return artifact.type == QStringLiteral("constraints")
                          || artifact.path.endsWith(
                              QStringLiteral("_domain_constraints.json"));
                  }),
              QStringLiteral(
                  "V1 compatibility generation does not invent Domain constraints"));
        const auto primary = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(), [](const Artifact& artifact) {
                return artifact.primary;
            });
        check(primary != generation.artifacts.cend(), QStringLiteral("generation reports a primary artifact"));
        if (primary != generation.artifacts.cend()) {
            check(QFileInfo(QDir(generation.outputDirectory).filePath(primary->path)).isFile(),
                  QStringLiteral("primary artifact exists below the run output directory"));
        }
    }

    const DesignResult configurableCreated = finepaper.createDesign(
        configurableRequest());
    check(configurableCreated.success
              && configurableCreated.design.formatVersion == 3
              && configurableCreated.design.domains.size() == 2
              && configurableCreated.design.domainMemberships.size() == 7,
          QStringLiteral(
              "bundled V3 Package materializes Package-declared clock/power defaults and assignments"));
    const DesignResult routerConfigured = configurableCreated.success
        ? finepaper.setElementConfiguration(
              configurableCreated.design,
              ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
              QStringLiteral("router.microarchitecture"),
              QJsonObject{
                  {QStringLiteral("routingAlgorithm"), QStringLiteral("yx")},
                  {QStringLiteral("virtualChannels"), 4},
                  {QStringLiteral("bufferDepth"), 16}})
        : DesignResult{};
    check(routerConfigured.success
              && routerConfigured.design.elementConfigurations.size() == 1,
          QStringLiteral(
              "bundled V3 Package accepts sparse Router microarchitecture overrides"));
    QTemporaryDir configurableOutput(
        QStringLiteral("/tmp/finepaper-configurable-core-test-XXXXXX"));
    if (routerConfigured.success && configurableOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            routerConfigured.design,
            GenerationOptions{configurableOutput.path()});
        const auto primary = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) { return artifact.primary; });
        const auto intent = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.path.endsWith(
                    QStringLiteral("_design_intent.json"));
            });
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_constraints.json"));
            });
        QString primaryText;
        if (primary != generation.artifacts.cend()) {
            QFile primaryFile(QDir(generation.outputDirectory).filePath(
                primary->path));
            if (primaryFile.open(QIODevice::ReadOnly)) {
                primaryText = QString::fromUtf8(primaryFile.readAll());
            }
        }
        DesignLoadResult intentDesign;
        if (intent != generation.artifacts.cend()) {
            intentDesign = loadDesign(
                QDir(generation.outputDirectory).filePath(intent->path));
        }
        JsonObjectLoadResult defaultConstraints;
        if (constraints != generation.artifacts.cend()) {
            defaultConstraints = loadJsonObject(
                QDir(generation.outputDirectory).filePath(constraints->path));
        }
        const auto intentMemory = intentDesign.success
            ? std::find_if(
                  intentDesign.design.endpoints.cbegin(),
                  intentDesign.design.endpoints.cend(),
                  [](const EndpointInstance& endpoint) {
                      return endpoint.id == QStringLiteral("memory");
                  })
            : intentDesign.design.endpoints.cend();
        check(generation.success
                  && primaryText.contains(
                      QStringLiteral("route=yx vc=4 depth=16"))
                  && primaryText.contains(
                      QStringLiteral(
                          "data_width=128 buffer_depth=37 qos=enabled"))
                  && intentDesign.success
                  && intentDesign.design.elementConfigurations
                      == routerConfigured.design.elementConfigurations
                  && intentMemory != intentDesign.design.endpoints.cend()
                  && intentMemory->parameters.value(
                         QStringLiteral("bufferDepth")).toInt() == 37
                  && intentMemory->parameters.value(
                         QStringLiteral("qosEnabled")).toBool()
                  && defaultConstraints.success
                  && defaultConstraints.object.value(
                         QStringLiteral("format")).toString()
                      == QStringLiteral("finepaper.noc-domain-constraints")
                  && defaultConstraints.object.value(
                         QStringLiteral("boundarySemantics")).toObject().value(
                         QStringLiteral("scope")).toString()
                      == QStringLiteral("bidirectional-physical-edge")
                  && defaultConstraints.object.value(
                         QStringLiteral("instances")).toArray().size() == 2
                  && defaultConstraints.object.value(
                         QStringLiteral("meshCrossings")).toArray().isEmpty(),
              QStringLiteral(
                  "bundled V3 Generator consumes Router, Endpoint, and default Domain intent"));
    }

    NocDesign crossingDesign;
    if (routerConfigured.success) {
        crossingDesign = routerConfigured.design;
        crossingDesign.domains.append(DomainDefinition{
            QStringLiteral("clock-io"),
            QStringLiteral("clock"),
            QStringLiteral("I/O clock"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 500}}
        });
        crossingDesign.domains.append(DomainDefinition{
            QStringLiteral("power-low"),
            QStringLiteral("power"),
            QStringLiteral("Low-voltage island"),
            QJsonObject{
                {QStringLiteral("voltageMv"), 750},
                {QStringLiteral("retention"), true}
            }
        });
        for (DomainMembership& membership : crossingDesign.domainMemberships) {
            const bool alternateRouter = membership.element.kind == ElementKind::Router
                && (membership.element.id == QStringLiteral("r-1-0")
                    || membership.element.id == QStringLiteral("r-1-1"));
            const bool alternateEndpoint = membership.element.kind == ElementKind::Endpoint
                && membership.element.id == QStringLiteral("memory");
            if (alternateRouter || alternateEndpoint) {
                membership.assignments.insert(
                    QStringLiteral("clock"),
                    QStringList{QStringLiteral("clock-io")});
                membership.assignments.insert(
                    QStringLiteral("power"),
                    QStringList{QStringLiteral("power-low")});
            }
        }
        crossingDesign.domainRelations.append(DomainRelation{
            QStringLiteral("derived-from"),
            QStringLiteral("clock-io"),
            QStringLiteral("clock-main"),
            QJsonObject{{QStringLiteral("divider"), 2}}
        });
        crossingDesign.crossingPolicies = {
            DomainCrossingPolicy{
                QStringLiteral("clock-main-to-io"),
                QStringLiteral("clock"),
                QStringLiteral("clock-main"),
                QStringLiteral("clock-io"),
                QJsonObject{
                    {QStringLiteral("implementation"), QStringLiteral("async-fifo")},
                    {QStringLiteral("synchronizerStages"), 3}
                }
            },
            DomainCrossingPolicy{
                QStringLiteral("power-main-to-low"),
                QStringLiteral("power"),
                QStringLiteral("power-main"),
                QStringLiteral("power-low"),
                QJsonObject{
                    {QStringLiteral("isolation"), true},
                    {QStringLiteral("levelShift"), QStringLiteral("down")}
                }
            }
        };
        crossingDesign.edgeOverrides.append(DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("power"),
            QStringLiteral("power-main-to-low"),
            QJsonObject{{QStringLiteral("isolation"), false}}
        });
    }

    const ValidationResult crossingValidation = routerConfigured.success
        ? finepaper.validate(crossingDesign, true)
        : ValidationResult{};
    check(crossingValidation.success,
          QStringLiteral(
              "bundled V3 runtime validates Domain relations and resolvable Mesh crossings"));

    QTemporaryDir crossingOutput(
        QStringLiteral("/tmp/finepaper-domain-crossing-test-XXXXXX"));
    check(crossingOutput.isValid(),
          QStringLiteral("Domain crossing output directory is available"));
    QJsonObject crossingConstraints;
    QString crossingConstraintsText;
    if (crossingValidation.success && crossingOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            crossingDesign, GenerationOptions{crossingOutput.path()});
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_constraints.json"));
            });
        if (constraints != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                constraints->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            crossingConstraints = loaded.object;
            QFile constraintsFile(path);
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                crossingConstraintsText = QString::fromUtf8(
                    constraintsFile.readAll());
            }
        }
        const QJsonObject clockInstance = objectWithStringField(
            crossingConstraints.value(QStringLiteral("instances")).toArray(),
            QStringLiteral("id"),
            QStringLiteral("clock-io"));
        QJsonObject overriddenCrossing;
        const QString overriddenLink = linkId(
            QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
        for (const QJsonValue& value : crossingConstraints.value(
                 QStringLiteral("meshCrossings")).toArray()) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("domainType")).toString()
                    == QStringLiteral("power")
                && candidate.value(QStringLiteral("edge")).toObject().value(
                       QStringLiteral("id")).toString() == overriddenLink) {
                overriddenCrossing = candidate;
                break;
            }
        }
        const QJsonObject resolution = overriddenCrossing.value(
            QStringLiteral("resolution")).toObject();
        check(generation.success
                  && !crossingConstraints.isEmpty()
                  && clockInstance.value(QStringLiteral("members"))
                         .toArray().size() == 3
                  && crossingConstraints.value(QStringLiteral("relations"))
                         .toArray().size() == 1
                  && crossingConstraints.value(QStringLiteral("policies"))
                         .toArray().size() == 2
                  && crossingConstraints.value(QStringLiteral("overrides"))
                         .toArray().size() == 1
                  && crossingConstraints.value(
                         QStringLiteral("boundarySemantics")).toObject().value(
                         QStringLiteral("policyOrientation")).toString()
                      == QStringLiteral("canonical-edge-endpoints")
                  && crossingConstraints.value(QStringLiteral("meshCrossings"))
                         .toArray().size() == 4
                  && resolution.value(QStringLiteral("source")).toString()
                      == QStringLiteral("override")
                  && resolution.value(QStringLiteral("policy")).toString()
                      == QStringLiteral("power-main-to-low")
                  && resolution.value(QStringLiteral("overrideProperties"))
                         .toObject().contains(QStringLiteral("isolation"))
                  && !resolution.value(QStringLiteral("overrideProperties"))
                          .toObject().value(QStringLiteral("isolation")).toBool()
                  && resolution.value(QStringLiteral("effectiveProperties"))
                         .toObject().value(QStringLiteral("levelShift")).toString()
                      == QStringLiteral("down")
                  && !resolution.value(QStringLiteral("effectiveProperties"))
                          .toObject().value(QStringLiteral("isolation")).toBool(),
              QStringLiteral(
                  "Domain constraints compile instances, members, relations, policies, overrides, and Mesh crossings"));
    }

    NocDesign reorderedDomainDesign = crossingDesign;
    std::reverse(reorderedDomainDesign.domains.begin(),
                 reorderedDomainDesign.domains.end());
    std::reverse(reorderedDomainDesign.domainMemberships.begin(),
                 reorderedDomainDesign.domainMemberships.end());
    std::reverse(reorderedDomainDesign.domainRelations.begin(),
                 reorderedDomainDesign.domainRelations.end());
    std::reverse(reorderedDomainDesign.crossingPolicies.begin(),
                 reorderedDomainDesign.crossingPolicies.end());
    std::reverse(reorderedDomainDesign.edgeOverrides.begin(),
                 reorderedDomainDesign.edgeOverrides.end());
    QTemporaryDir reorderedDomainOutput(
        QStringLiteral("/tmp/finepaper-domain-order-test-XXXXXX"));
    check(reorderedDomainOutput.isValid(),
          QStringLiteral("Domain determinism output directory is available"));
    if (crossingValidation.success && reorderedDomainOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            reorderedDomainDesign,
            GenerationOptions{reorderedDomainOutput.path()});
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints");
            });
        QString reorderedText;
        if (constraints != generation.artifacts.cend()) {
            QFile constraintsFile(QDir(generation.outputDirectory).filePath(
                constraints->path));
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                reorderedText = QString::fromUtf8(constraintsFile.readAll());
            }
        }
        check(generation.success
                  && !crossingConstraintsText.isEmpty()
                  && reorderedText == crossingConstraintsText,
              QStringLiteral(
                  "Domain constraints are deterministic across equivalent input ordering"));
    }

    NocDesign changedDomainDesign = crossingDesign;
    for (DomainDefinition& domain : changedDomainDesign.domains) {
        if (domain.id == QStringLiteral("clock-io")) {
            domain.properties.insert(QStringLiteral("frequencyMHz"), 625);
        }
    }
    QTemporaryDir changedDomainOutput(
        QStringLiteral("/tmp/finepaper-domain-change-test-XXXXXX"));
    check(changedDomainOutput.isValid(),
          QStringLiteral("changed Domain output directory is available"));
    if (crossingValidation.success && changedDomainOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            changedDomainDesign,
            GenerationOptions{changedDomainOutput.path()});
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints");
            });
        QString changedText;
        QJsonObject changedConstraints;
        if (constraints != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                constraints->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            changedConstraints = loaded.object;
            QFile constraintsFile(path);
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                changedText = QString::fromUtf8(constraintsFile.readAll());
            }
        }
        const QJsonObject changedClock = objectWithStringField(
            changedConstraints.value(QStringLiteral("instances")).toArray(),
            QStringLiteral("id"),
            QStringLiteral("clock-io"));
        check(generation.success
                  && !crossingConstraintsText.isEmpty()
                  && crossingConstraintsText != changedText
                  && changedClock.value(QStringLiteral("properties"))
                         .toObject().value(QStringLiteral("frequencyMHz")).toInt()
                      == 625,
              QStringLiteral(
                  "changing a Domain property deterministically changes the compiled constraints artifact"));
    }

    NocDesign missingStrategyDesign = crossingDesign;
    if (!missingStrategyDesign.crossingPolicies.isEmpty()) {
        missingStrategyDesign.crossingPolicies.removeFirst();
    }
    const ValidationResult missingStrategyValidation = routerConfigured.success
        ? finepaper.validate(missingStrategyDesign, true)
        : ValidationResult{};
    check(!missingStrategyValidation.success
              && hasDiagnosticCode(
                  missingStrategyValidation.diagnostics,
                  QStringLiteral("finepaper_noc.adapter_failed")),
          QStringLiteral(
              "bundled V3 runtime rejects a Mesh Domain crossing without a directed strategy"));

    QTemporaryDir strictDomainRun(
        QStringLiteral("/tmp/finepaper-domain-strict-fields-XXXXXX"));
    check(strictDomainRun.isValid(),
          QStringLiteral("strict Domain validation directory is available"));
    if (routerConfigured.success && strictDomainRun.isValid()) {
        const QStringList domainArrayFields{
            QStringLiteral("domains"),
            QStringLiteral("domainMemberships"),
            QStringLiteral("domainRelations"),
            QStringLiteral("crossingPolicies"),
            QStringLiteral("edgeOverrides")
        };
        bool allRejected = true;
        for (const QString& field : domainArrayFields) {
            QJsonObject invalidDesign = designToJson(crossingDesign);
            invalidDesign.insert(field, QJsonObject{});
            const QString designPath = QDir(strictDomainRun.path()).filePath(
                field + QStringLiteral("-design.json"));
            const QString resultPath = QDir(strictDomainRun.path()).filePath(
                field + QStringLiteral("-result.json"));
            const bool saved = saveJsonObject(designPath, invalidDesign);
            const ProcessResult process = saved
                ? runProcess(
                      QDir(projectRoot).filePath(
                          QStringLiteral(
                              "packages/finepaper-noc-v3/runtime/bin/generate")),
                      QStringList{
                          QStringLiteral("validate"),
                          QStringLiteral("--design"),
                          designPath,
                          QStringLiteral("--result"),
                          resultPath
                      },
                      strictDomainRun.path(),
                      30'000)
                : ProcessResult{};
            const JsonObjectLoadResult result = loadJsonObject(resultPath);
            allRejected = allRejected
                && saved
                && process.started
                && process.exitCode != 0
                && result.success
                && !result.object.value(QStringLiteral("success")).toBool();
        }
        check(allRejected,
              QStringLiteral(
                  "bundled V3 runtime strictly parses every Domain data plane as an array"));
    }

    QTemporaryDir invalidStrategyRun(
        QStringLiteral("/tmp/finepaper-domain-invalid-strategy-XXXXXX"));
    check(invalidStrategyRun.isValid(),
          QStringLiteral("invalid Domain strategy directory is available"));
    if (routerConfigured.success && invalidStrategyRun.isValid()) {
        QJsonObject invalidStrategy = designToJson(crossingDesign);
        QJsonArray policies = invalidStrategy.value(
            QStringLiteral("crossingPolicies")).toArray();
        for (qsizetype index = 0; index < policies.size(); ++index) {
            QJsonObject policy = policies.at(index).toObject();
            if (policy.value(QStringLiteral("id")).toString()
                != QStringLiteral("power-main-to-low")) {
                continue;
            }
            policy.insert(QStringLiteral("from"), QStringLiteral("power-low"));
            policy.insert(QStringLiteral("to"), QStringLiteral("power-main"));
            policies[index] = policy;
        }
        invalidStrategy.insert(QStringLiteral("crossingPolicies"), policies);
        const QString designPath = QDir(invalidStrategyRun.path()).filePath(
            QStringLiteral("design.json"));
        const QString resultPath = QDir(invalidStrategyRun.path()).filePath(
            QStringLiteral("result.json"));
        const bool saved = saveJsonObject(designPath, invalidStrategy);
        const ProcessResult process = saved
            ? runProcess(
                  QDir(projectRoot).filePath(
                      QStringLiteral(
                          "packages/finepaper-noc-v3/runtime/bin/generate")),
                  QStringList{
                      QStringLiteral("validate"),
                      QStringLiteral("--design"),
                      designPath,
                      QStringLiteral("--result"),
                      resultPath
                  },
                  invalidStrategyRun.path(),
                  30'000)
            : ProcessResult{};
        const JsonObjectLoadResult result = loadJsonObject(resultPath);
        const QJsonArray diagnostics = result.object.value(
            QStringLiteral("diagnostics")).toArray();
        const QString message = diagnostics.isEmpty()
            ? QString()
            : diagnostics.at(0).toObject().value(
                  QStringLiteral("message")).toString();
        check(saved
                  && process.started
                  && process.exitCode != 0
                  && result.success
                  && !result.object.value(QStringLiteral("success")).toBool()
                  && message.contains(QStringLiteral("does not match")),
              QStringLiteral(
                  "bundled V3 runtime rejects an override policy with the reverse canonical edge orientation"));
    }

    FinepaperApplication complexApplication;
    const QVector<Diagnostic> complexPackageDiagnostics = complexApplication.reloadPackages(
        QStringList{QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"))});
    check(!hasErrors(complexPackageDiagnostics), QStringLiteral("complex Engine fixture Package loads"));
    const DesignResult complexDesign = complexApplication.createDesign(complexRequest());
    check(complexDesign.success, QStringLiteral("complex Package can retain opaque packageData"));
    const ValidationResult complexValidation = complexApplication.validate(complexDesign.design, true);
    check(complexValidation.success, QStringLiteral("complex Package validation succeeds through its Engine"));
    check(hasDiagnosticCode(complexValidation.diagnostics, QStringLiteral("mock.engine_used")),
          QStringLiteral("Engine, not the Generator, performs complex Package validation"));
    NocDesign rejectedComplexDesign = complexDesign.design;
    rejectedComplexDesign.packageData.insert(QStringLiteral("forceEngineError"), true);
    const ValidationResult rejectedComplexValidation = complexApplication.validate(
        rejectedComplexDesign, true);
    check(!rejectedComplexValidation.success
              && hasDiagnosticCode(rejectedComplexValidation.diagnostics,
                                   QStringLiteral("mock.engine_rejected")),
          QStringLiteral("structured Engine diagnostics survive a non-zero process exit"));
    QTemporaryDir complexOutput(QStringLiteral("/tmp/finepaper-engine-test-XXXXXX"));
    if (complexOutput.isValid()) {
        const GenerationResult complexGeneration = complexApplication.generate(
            complexDesign.design, GenerationOptions{complexOutput.path()});
        check(complexGeneration.success, QStringLiteral("complex Package still uses its Generator for artifacts"));
        check(complexGeneration.artifacts.size() == 1 && complexGeneration.artifacts.at(0).primary,
              QStringLiteral("complex Generator returns a contained primary artifact"));
    }

    FinepaperApplication multiPackageApplication;
    const QVector<Diagnostic> multiPackageDiagnostics = multiPackageApplication.reloadPackages(
        QStringList{
            QDir(projectRoot).filePath(QStringLiteral("packages")),
            QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"))
        });
    check(!hasErrors(multiPackageDiagnostics),
          QStringLiteral("multiple Package roots load without a shared build step"));
    check(multiPackageApplication.packages().size() == 4,
          QStringLiteral("catalog exposes V1/V3 automatic, explicit-slot and Engine-backed Packages together"));
    const auto explicitPackage = std::find_if(
        multiPackageApplication.packages().cbegin(),
        multiPackageApplication.packages().cend(),
        [](const PackageDefinition& package) {
            return package.id == QStringLiteral("test.explicit-slots");
        });
    check(explicitPackage != multiPackageApplication.packages().cend()
              && explicitPackage->attachment.slotMode == AttachmentSlotMode::Explicit
              && explicitPackage->attachment.positions.size() == 2
              && explicitPackage->attachment.positions.at(1).id == QStringLiteral("local1"),
          QStringLiteral("explicit Package exposes centrally declared attachment positions"));
    const DesignResult explicitDesign = multiPackageApplication.createDesign(
        explicitSlotRequest());
    check(explicitDesign.success
              && explicitDesign.design.endpoints.at(0).attachment.slot
                     == QStringLiteral("local1"),
          QStringLiteral("explicit attachment position persists in NocDesign"));
    QJsonObject nestedExplicitRequest = explicitSlotRequest();
    QJsonArray nestedExplicitEndpoints = nestedExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject nestedExplicitEndpoint = nestedExplicitEndpoints.at(0).toObject();
    nestedExplicitEndpoint.remove(QStringLiteral("slot"));
    nestedExplicitEndpoint.insert(
        QStringLiteral("attachment"),
        QJsonObject{{QStringLiteral("slot"), QStringLiteral("local1")}});
    nestedExplicitEndpoints[0] = nestedExplicitEndpoint;
    nestedExplicitRequest.insert(QStringLiteral("endpoints"), nestedExplicitEndpoints);
    const DesignResult nestedExplicitDesign = multiPackageApplication.createDesign(
        nestedExplicitRequest);
    check(nestedExplicitDesign.success &&
              nestedExplicitDesign.design.endpoints.at(0).attachment.router ==
                  RouterPosition{0, 0},
          QStringLiteral("a nested slot keeps the endpoint's top-level Router attachment"));

    QJsonObject duplicateExplicitRequest = explicitSlotRequest();
    QJsonArray duplicateExplicitEndpoints = duplicateExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject duplicateExplicitEndpoint = duplicateExplicitEndpoints.at(0).toObject();
    duplicateExplicitEndpoint.insert(QStringLiteral("id"), QStringLiteral("device_1"));
    duplicateExplicitEndpoints.append(duplicateExplicitEndpoint);
    duplicateExplicitRequest.insert(QStringLiteral("endpoints"), duplicateExplicitEndpoints);
    const DesignResult duplicateExplicitDesign = multiPackageApplication.createDesign(
        duplicateExplicitRequest);
    check(!duplicateExplicitDesign.success &&
              hasDiagnosticCode(duplicateExplicitDesign.diagnostics,
                                QStringLiteral("endpoint.duplicate_slot")),
          QStringLiteral("duplicate attachment slots are rejected in explicit mode"));
    QJsonObject invalidExplicitRequest = explicitSlotRequest();
    QJsonArray invalidEndpoints = invalidExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject invalidEndpoint = invalidEndpoints.at(0).toObject();
    invalidEndpoint.insert(QStringLiteral("slot"), QStringLiteral("undeclared"));
    invalidEndpoints[0] = invalidEndpoint;
    invalidExplicitRequest.insert(QStringLiteral("endpoints"), invalidEndpoints);
    const DesignResult invalidExplicitDesign = multiPackageApplication.createDesign(
        invalidExplicitRequest);
    check(!invalidExplicitDesign.success
              && hasDiagnosticCode(invalidExplicitDesign.diagnostics,
                                   QStringLiteral("endpoint.unknown_slot")),
          QStringLiteral("shared application validation rejects undeclared explicit slots"));

#ifdef Q_OS_UNIX
    QTemporaryDir processOutput(QStringLiteral("/tmp/finepaper-process-test-XXXXXX"));
    if (processOutput.isValid()) {
        const QString marker = QDir(processOutput.path()).filePath(QStringLiteral("child-survived"));
        const ProcessResult timedOut = runProcess(
            QStringLiteral("/bin/sh"),
            QStringList{QStringLiteral("-c"),
                        QStringLiteral("(sleep 1; touch %1) & wait").arg(marker)},
            processOutput.path(),
            30);
        check(timedOut.timedOut, QStringLiteral("Process runner reports a timeout"));
        QThread::msleep(1200);
        check(!QFileInfo::exists(marker),
              QStringLiteral("timeout terminates Package child processes with their parent"));

        const QString stubbornMarker = QDir(processOutput.path()).filePath(
            QStringLiteral("stubborn-child-survived"));
        const ProcessResult stubbornTimeout = runProcess(
            QStringLiteral("/bin/sh"),
            QStringList{
                QStringLiteral("-c"),
                QStringLiteral("(trap '' TERM; sleep 1; touch %1) & wait").arg(stubbornMarker)
            },
            processOutput.path(),
            30);
        check(stubbornTimeout.timedOut,
              QStringLiteral("Process runner reports a timeout for a stubborn descendant"));
        QThread::msleep(1200);
        check(!QFileInfo::exists(stubbornMarker),
              QStringLiteral("timeout force-kills descendants that ignore termination"));
    }
#endif

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-tests passed" << Qt::endl;
        return 0;
    }
    return 1;
}
