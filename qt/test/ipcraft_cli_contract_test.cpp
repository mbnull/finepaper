// Ipcraft CLI public contract smoke tests.
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString findCliBinary() {
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral("ipcraft-cli"));
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ipcraft-cli");
}

struct CliRun {
    int exitCode = -1;
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
    QJsonObject json;
};

CliRun runCli(const QStringList& arguments) {
    QProcess process;
    process.start(findCliBinary(), arguments);
    require(process.waitForStarted(), "ipcraft-cli should start");
    require(process.waitForFinished(10000), "ipcraft-cli should finish");
    CliRun run;
    run.exitCode = process.exitCode();
    run.stdoutBytes = process.readAllStandardOutput();
    run.stderrBytes = process.readAllStandardError();
    const QJsonDocument document = QJsonDocument::fromJson(run.stdoutBytes);
    require(document.isObject(), "ipcraft-cli should write a JSON object to stdout");
    run.json = document.object();
    return run;
}

void requireCliEnvelope(const QJsonObject& object, bool ok) {
    require(object.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::cliResultV1,
            "CLI result schema should be ipcraft.cli.result.v1");
    require(object.value(QStringLiteral("ok")).toBool() == ok,
            "CLI ok flag should match expected value");
    const QJsonObject diagnostics = object.value(QStringLiteral("diagnostics")).toObject();
    require(diagnostics.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::diagnosticsV1,
            "CLI diagnostics schema should be diagnostics v1");
    require(diagnostics.value(QStringLiteral("records")).isArray(),
            "CLI diagnostics records should be an array");
}

bool hasRule(const QJsonObject& object, const QString& ruleId) {
    const QJsonArray records =
        object.value(QStringLiteral("diagnostics")).toObject()
            .value(QStringLiteral("records")).toArray();
    for (const QJsonValue& value : records) {
        if (value.toObject().value(QStringLiteral("rule_id")).toString() == ruleId) {
            return true;
        }
    }
    return false;
}

QJsonObject projectJson() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::projectV1},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("project_0")},
            {QStringLiteral("name"), QStringLiteral("CLI Contract")}
        }},
        {QStringLiteral("instances"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("ip0")},
                {QStringLiteral("package"), QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("vendor.example.simple")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")}
                }},
                {QStringLiteral("config"), QJsonObject{
                    {QStringLiteral("parameters"), QJsonObject{
                        {QStringLiteral("width"), 32}
                    }}
                }}
            }
        }},
        {QStringLiteral("composition"), QJsonObject{
            {QStringLiteral("connections"), QJsonArray{}},
            {QStringLiteral("external_ports"), QJsonArray{}}
        }},
        {QStringLiteral("layout"), QJsonObject{}},
        {QStringLiteral("diagnostics"), QJsonObject{
            {QStringLiteral("schema"), ipcraft::schemaids::diagnosticsV1},
            {QStringLiteral("records"), QJsonArray{}}
        }},
        {QStringLiteral("artifacts"), QJsonObject{}},
        {QStringLiteral("migration"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };
}

QJsonObject graphConfigJson() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")},
                        {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };
}

QJsonObject packageJson(QJsonArray emitters = {}, QJsonArray flows = {}, QJsonArray artifacts = {}) {
    QJsonArray extensions{
        QStringLiteral("ipcraft.config.params"),
        QStringLiteral("ipcraft.emitters"),
        QStringLiteral("ipcraft.flows"),
        QStringLiteral("ipcraft.artifacts"),
        QStringLiteral("ipcraft.graph_config")
    };
    QJsonObject object{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), QStringLiteral("vendor.example.simple")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("name"), QStringLiteral("Simple")},
        {QStringLiteral("extensions"), extensions},
        {QStringLiteral("config_schema"), QJsonObject{
            {QStringLiteral("parameters"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("width")},
                            {QStringLiteral("type"), QStringLiteral("int")}}
            }}
        }}
    };
    if (!emitters.isEmpty()) {
        object.insert(QStringLiteral("emitters"), emitters);
    }
    if (!flows.isEmpty()) {
        object.insert(QStringLiteral("flows"), flows);
    }
    if (!artifacts.isEmpty()) {
        object.insert(QStringLiteral("artifacts"), artifacts);
    }
    return object;
}

QString writeJson(const QString& path, const QJsonObject& object) {
    QFileInfo info(path);
    require(QDir().mkpath(info.absolutePath()), "fixture directory should be created");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture should open");
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size(), "fixture should write");
    return path;
}

void writeExecutable(const QString& path, const QByteArray& script) {
    writeJson(QFileInfo(path).absoluteDir().filePath(QStringLiteral(".keep.json")), QJsonObject{});
    QFile::remove(QFileInfo(path).absoluteDir().filePath(QStringLiteral(".keep.json")));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "script should open");
    require(file.write(script) == script.size(), "script should write");
    file.close();
    require(QFile::setPermissions(path,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther),
            "script should be executable");
}

void setupProjectAndPackage(QTemporaryDir& root,
                            QString* projectPath,
                            QString* packageRoot,
                            QJsonArray emitters = {},
                            QJsonArray flows = {},
                            QJsonArray artifacts = {}) {
    QDir dir(root.path());
    *projectPath = writeJson(dir.filePath(QStringLiteral("project.json")), projectJson());
    *packageRoot = dir.filePath(QStringLiteral("packages/simple"));
    writeJson(QDir(*packageRoot).filePath(QStringLiteral("ipcraft.json")),
              packageJson(std::move(emitters), std::move(flows), std::move(artifacts)));
}

void testInspectProjectReturnsJsonResult() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot);

    const CliRun run = runCli({QStringLiteral("inspect-project"), projectPath});
    require(run.exitCode == 0, "inspect-project should exit 0");
    requireCliEnvelope(run.json, true);
    require(run.json.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("project")).toObject()
                .value(QStringLiteral("id")).toString() == QStringLiteral("project_0"),
            "inspect-project should include project id");
}

void testValidateProjectIsStaticAndDoesNotCreateRunFiles() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot, {}, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("generate")},
                    {QStringLiteral("steps"), QJsonArray{
                        QJsonObject{{QStringLiteral("kind"), QStringLiteral("exec")},
                                    {QStringLiteral("command"), QJsonObject{
                                        {QStringLiteral("executable"), QStringLiteral("tools/missing")}
                                    }}}
                    }}}
    });

    const CliRun run = runCli({QStringLiteral("validate-project"), projectPath,
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode == 0, "static validate-project should exit 0 for valid static input");
    requireCliEnvelope(run.json, true);
    require(!QFileInfo(QDir(root.path()).filePath(QStringLiteral("run"))).exists(),
            "validate-project should not create run files");
}

void testEmitInputsReturnsManifest() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/params.json")}}
    });

    const QString outDir = QDir(root.path()).filePath(QStringLiteral("emitted"));
    const CliRun run = runCli({QStringLiteral("emit-inputs"), projectPath,
                               QStringLiteral("--instance"), QStringLiteral("ip0"),
                               QStringLiteral("--out"), outDir,
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode == 0, "emit-inputs should exit 0");
    requireCliEnvelope(run.json, true);
    require(run.json.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("manifest")).toObject()
                .value(QStringLiteral("schema")).toString() == ipcraft::schemaids::emittedInputsV1,
            "emit-inputs should return emitted manifest");
}

void testRunFlowReportsMissingExecutable() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot, {}, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("generate")},
                    {QStringLiteral("steps"), QJsonArray{
                        QJsonObject{{QStringLiteral("kind"), QStringLiteral("exec")},
                                    {QStringLiteral("command"), QJsonObject{
                                        {QStringLiteral("executable"), QStringLiteral("tools/missing")}
                                    }}}
                    }}}
    });

    const CliRun run = runCli({QStringLiteral("run-flow"), projectPath,
                               QStringLiteral("--flow"), QStringLiteral("generate"),
                               QStringLiteral("--instance"), QStringLiteral("ip0"),
                               QStringLiteral("--out"), QDir(root.path()).filePath(QStringLiteral("run")),
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode != 0, "missing executable run-flow should fail");
    requireCliEnvelope(run.json, false);
    require(hasRule(run.json, QStringLiteral("flow.executable_missing")),
            "run-flow should surface flow.executable_missing");
}

void testRunFlowEmitsInstanceGraphConfig() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QDir dir(root.path());

    QJsonObject project = projectJson();
    QJsonArray instances = project.value(QStringLiteral("instances")).toArray();
    QJsonObject instance = instances.first().toObject();
    instance.insert(QStringLiteral("graph_config"), graphConfigJson());
    instances.replace(0, instance);
    project.insert(QStringLiteral("instances"), instances);

    const QString projectPath = writeJson(dir.filePath(QStringLiteral("project.json")), project);
    const QString packageRoot = dir.filePath(QStringLiteral("packages/simple"));
    writeJson(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")),
              packageJson(QJsonArray{
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("graph")},
                              {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
                              {QStringLiteral("path"), QStringLiteral("graph/config.json")}}
              }, QJsonArray{
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("generate")},
                              {QStringLiteral("steps"), QJsonArray{
                                  QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}}
                              }}}
              }));

    const QString outDir = dir.filePath(QStringLiteral("run"));
    const CliRun run = runCli({QStringLiteral("run-flow"), projectPath,
                               QStringLiteral("--flow"), QStringLiteral("generate"),
                               QStringLiteral("--instance"), QStringLiteral("ip0"),
                               QStringLiteral("--out"), outDir,
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode == 0, "run-flow should pass graph_config through emit_inputs");
    requireCliEnvelope(run.json, true);
    require(QFileInfo(QDir(outDir).filePath(QStringLiteral("ip0/inputs/graph/config.json"))).isFile(),
            "run-flow should emit instance graph_config files");
}

void testRunFlowRequiresInstanceOrAllInstancesForInstanceScopedFlow() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot, {}, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("generate")},
                    {QStringLiteral("steps"), QJsonArray{}}}
    });

    const CliRun run = runCli({QStringLiteral("run-flow"), projectPath,
                               QStringLiteral("--flow"), QStringLiteral("generate"),
                               QStringLiteral("--out"), QDir(root.path()).filePath(QStringLiteral("run")),
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode != 0, "run-flow should require instance targeting");
    requireCliEnvelope(run.json, false);
    require(hasRule(run.json, QStringLiteral("cli.instance_scope_required")),
            "missing instance targeting should emit cli.instance_scope_required");
}

void testRunFlowRejectsUnsafeInstanceOutputPath() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QDir dir(root.path());
    QJsonObject project = projectJson();
    QJsonArray instances = project.value(QStringLiteral("instances")).toArray();
    QJsonObject instance = instances.first().toObject();
    instance.insert(QStringLiteral("id"), QStringLiteral("../escaped"));
    instances.replace(0, instance);
    project.insert(QStringLiteral("instances"), instances);

    const QString projectPath = writeJson(dir.filePath(QStringLiteral("project.json")), project);
    const QString packageRoot = dir.filePath(QStringLiteral("packages/simple"));
    writeJson(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")),
              packageJson({}, QJsonArray{
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("generate")},
                              {QStringLiteral("steps"), QJsonArray{}}}
              }));

    const QString outDir = dir.filePath(QStringLiteral("run"));
    const CliRun run = runCli({QStringLiteral("run-flow"), projectPath,
                               QStringLiteral("--flow"), QStringLiteral("generate"),
                               QStringLiteral("--instance"), QStringLiteral("../escaped"),
                               QStringLiteral("--out"), outDir,
                               QStringLiteral("--packages"), packageRoot});
    require(run.exitCode != 0, "unsafe run-flow instance id should fail");
    requireCliEnvelope(run.json, false);
    require(hasRule(run.json, QStringLiteral("cli.path_escape")),
            "unsafe run-flow instance id should emit cli.path_escape");
    require(!QFileInfo(dir.filePath(QStringLiteral("escaped"))).exists(),
            "unsafe instance id must not create a run directory outside --out");
}

void testCollectArtifactsReturnsArtifactIndex() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QDir dir(root.path());
    writeJson(dir.filePath(QStringLiteral("spec.json")),
              packageJson({}, {}, QJsonArray{
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("log")},
                              {QStringLiteral("type"), QStringLiteral("log")},
                              {QStringLiteral("glob"), QStringLiteral("logs/*.log")}}
              }));
    writeJson(dir.filePath(QStringLiteral("run/logs/output.log")),
              QJsonObject{{QStringLiteral("ok"), true}});

    const CliRun run = runCli({QStringLiteral("collect-artifacts"),
                               dir.filePath(QStringLiteral("run")),
                               QStringLiteral("--spec"), dir.filePath(QStringLiteral("spec.json"))});
    require(run.exitCode == 0, "collect-artifacts should exit 0");
    requireCliEnvelope(run.json, true);
    require(run.json.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("artifacts")).toObject()
                .value(QStringLiteral("records")).toArray().size() == 1,
            "collect-artifacts should return one record");
}

void testMigrateProjectRequiresExplicitTarget() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot);

    const CliRun run = runCli({QStringLiteral("migrate-project"), projectPath});
    require(run.exitCode != 0, "migrate-project without --to should fail");
    requireCliEnvelope(run.json, false);
    require(hasRule(run.json, QStringLiteral("migration.target_required")),
            "migrate-project without --to should emit migration.target_required");
}

void testMigrateProjectReturnsProjectUnderResultProject() {
    QTemporaryDir root;
    require(root.isValid(), "temp root should be valid");
    QString projectPath;
    QString packageRoot;
    setupProjectAndPackage(root, &projectPath, &packageRoot);

    const CliRun run = runCli({QStringLiteral("migrate-project"), projectPath,
                               QStringLiteral("--to"), ipcraft::schemaids::projectV1});
    require(run.exitCode == 0, "migrate-project to current schema should exit 0");
    requireCliEnvelope(run.json, true);
    require(run.json.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("project")).toObject()
                .value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1,
            "migrate-project should put migrated project under result.project");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testInspectProjectReturnsJsonResult();
        testValidateProjectIsStaticAndDoesNotCreateRunFiles();
        testEmitInputsReturnsManifest();
        testRunFlowReportsMissingExecutable();
        testRunFlowEmitsInstanceGraphConfig();
        testRunFlowRequiresInstanceOrAllInstancesForInstanceScopedFlow();
        testRunFlowRejectsUnsafeInstanceOutputPath();
        testCollectArtifactsReturnsArtifactIndex();
        testMigrateProjectRequiresExplicitTarget();
        testMigrateProjectReturnsProjectUnderResultProject();
    } catch (const std::exception& error) {
        std::cerr << "ipcraft_cli_contract_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_cli_contract_test passed\n";
    return 0;
}
