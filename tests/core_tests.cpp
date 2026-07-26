#include "application/application.h"
#include "application/runtime_settings.h"
#include "execution/process.h"
#include "noc/model.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);

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
    const QVector<Diagnostic> packageDiagnostics = finepaper.reloadPackages(
        QStringList{QDir(projectRoot).filePath(QStringLiteral("packages"))});
    check(!hasErrors(packageDiagnostics), QStringLiteral("reference Package loads"));
    check(finepaper.packages().size() == 1, QStringLiteral("exactly one reference Package loads"));

    const DesignResult created = finepaper.createDesign(request());
    check(created.success, QStringLiteral("request creates a valid NocDesign"));
    check(created.design.parameters.value(QStringLiteral("dataWidth")).toInt() == 64,
          QStringLiteral("Package defaults are materialized at creation"));
    check(created.design.endpoints.at(0).parameters.value(QStringLiteral("bufferDepth")).toInt() == 16,
          QStringLiteral("endpoint defaults are materialized at creation"));

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

    const ValidationResult validation = finepaper.validate(created.design, true);
    check(validation.success, QStringLiteral("reference Package validates through its process boundary"));

    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-core-test-XXXXXX"));
    check(outputRoot.isValid(), QStringLiteral("temporary output directory is available"));
    if (outputRoot.isValid()) {
        const GenerationResult generation = finepaper.generate(
            created.design, GenerationOptions{outputRoot.path()});
        check(generation.success, QStringLiteral("reference Package generates RTL"));
        check(!generation.artifacts.isEmpty(), QStringLiteral("generation reports artifacts"));
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
    check(multiPackageApplication.packages().size() == 2,
          QStringLiteral("catalog exposes simple and Engine-backed Packages together"));

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
    }
#endif

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-tests passed" << Qt::endl;
        return 0;
    }
    return 1;
}
