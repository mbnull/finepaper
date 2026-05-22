// Ipcraft V1 FlowRunner process security contract tests.
#include "graph/graph.h"
#include "ipcraft/flowrunner.h"
#include "project/ipinstancestate.h"
#include "validation/projectvalidationrunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId && diagnostic.source == QStringLiteral("core")) {
            return true;
        }
    }
    return false;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "file should open for reading");
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    const QFileInfo info(path);
    require(QDir().mkpath(info.absolutePath()), "file directory should be created");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "file should open for writing");
    require(file.write(bytes) == bytes.size(), "file should write fully");
}

void writeExecutable(const QString& path, const QByteArray& script) {
    const QFileInfo info(path);
    require(QDir().mkpath(info.absolutePath()), "script directory should be created");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "script should open");
    require(file.write(script) == script.size(), "script should write fully");
    file.close();
    require(QFile::setPermissions(path,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther),
            "script should be executable");
}

QJsonObject execFlow(const QJsonObject& command) {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("generate")},
        {QStringLiteral("steps"),
         QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("exec")},
                                {QStringLiteral("command"), command}}}}};
}

ipcraft::PackageSpec packageWithFlow(const QString& packageRoot, const QJsonObject& flow) {
    ipcraft::PackageSpec package;
    package.id = QStringLiteral("vendor.example.flow");
    package.version = QStringLiteral("1.0.0");
    package.packageRootPath = packageRoot;
    package.flows = QJsonArray{flow};
    return package;
}

ipcraft::FlowRunRequest requestFor(QTemporaryDir& runRoot,
                                   QTemporaryDir& packageRoot,
                                   const QJsonObject& flow) {
    ipcraft::FlowRunRequest request;
    request.projectId = QStringLiteral("project_0");
    request.instanceId = QStringLiteral("ip0");
    request.flowId = QStringLiteral("generate");
    request.runId = QStringLiteral("run0");
    request.runRoot = runRoot.path();
    request.packageRoot = packageRoot.path();
    request.package = packageWithFlow(packageRoot.path(), flow);
    return request;
}

QJsonObject commandFor(const QString& executable,
                       QJsonArray args = {},
                       QJsonObject capture = {}) {
    if (capture.isEmpty()) {
        capture.insert(QStringLiteral("stdout"), QStringLiteral("stdout.log"));
        capture.insert(QStringLiteral("stderr"), QStringLiteral("stderr.log"));
    }
    return QJsonObject{{QStringLiteral("executable"), executable},
                       {QStringLiteral("args"), args},
                       {QStringLiteral("capture"), capture}};
}

void testExecMissingExecutableReturnsDiagnostic() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");

    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/missing-generator")))));

    require(!result.ok, "missing executable should fail flow run");
    require(hasRule(result.diagnostics, QStringLiteral("flow.executable_missing")),
            "missing executable should emit flow.executable_missing");
}

void testExecNonzeroExitCapturesStdoutStderr() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/fail.sh")),
                    "#!/bin/sh\n"
                    "echo standard-output\n"
                    "echo standard-error >&2\n"
                    "exit 7\n");

    QJsonObject capture;
    capture.insert(QStringLiteral("stdout"), QStringLiteral("logs/stdout.log"));
    capture.insert(QStringLiteral("stderr"), QStringLiteral("logs/stderr.log"));
    capture.insert(QStringLiteral("max_bytes"), 1024);
    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/fail.sh"), {}, capture))));

    require(!result.ok, "nonzero executable should fail flow run");
    require(hasRule(result.diagnostics, QStringLiteral("flow.exec_failed")),
            "nonzero executable should emit flow.exec_failed");
    require(readFile(QDir(runRoot.path()).filePath(QStringLiteral("logs/stdout.log")))
                .contains("standard-output"),
            "stdout should be captured");
    require(readFile(QDir(runRoot.path()).filePath(QStringLiteral("logs/stderr.log")))
                .contains("standard-error"),
            "stderr should be captured");
}

void testExecTimeoutReturnsDiagnosticAndAttemptsCleanup() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/slow.sh")),
                    "#!/bin/sh\n"
                    "sleep 5\n");

    QJsonObject command = commandFor(QStringLiteral("tools/slow.sh"));
    command.insert(QStringLiteral("timeout_ms"), 100);
    QElapsedTimer timer;
    timer.start();
    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, execFlow(command)));

    require(!result.ok, "timeout should fail flow run");
    require(hasRule(result.diagnostics, QStringLiteral("flow.timeout")),
            "timeout should emit flow.timeout");
    require(timer.elapsed() < 5000, "timeout should not wait for script completion");
}

void testExecTimeoutAttemptsProcessGroupCleanup() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    const QString markerPath = QDir(runRoot.path()).filePath(QStringLiteral("child-survived"));
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/spawn-child.sh")),
                    "#!/bin/sh\n"
                    "( sleep 1; printf survived > \"$1\" ) &\n"
                    "sleep 5\n");

    QJsonObject command = commandFor(QStringLiteral("tools/spawn-child.sh"),
                                     QJsonArray{markerPath});
    command.insert(QStringLiteral("timeout_ms"), 100);
    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, execFlow(command)));
    QThread::msleep(1300);

    require(!result.ok, "timeout should fail flow run");
    require(hasRule(result.diagnostics, QStringLiteral("flow.timeout")),
            "timeout should emit flow.timeout");
    require(!QFileInfo::exists(markerPath),
            "timeout cleanup should attempt to kill child process group");
}

void testExecRejectsNativeCommandPolicyOverride() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    const QString absoluteScript =
        QDir(packageRoot.path()).filePath(QStringLiteral("tools/absolute.sh"));
    writeExecutable(absoluteScript, "#!/bin/sh\nexit 0\n");

    QJsonObject command = commandFor(absoluteScript);
    command.insert(QStringLiteral("native"),
                   QJsonObject{{QStringLiteral("allow_absolute_executable"), true}});
    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, execFlow(command)));

    require(!result.ok, "native command policy overrides should not allow absolute executables");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "absolute executable should emit flow.command_policy_violation");
}

void testExecResolvesFrameworkToolFromApplicationPolicy() {
    QTemporaryDir runRoot;
    QTemporaryDir toolsRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(toolsRoot.isValid(), "tools root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(toolsRoot.path()).filePath(QStringLiteral("ipcraft-test-tool")),
                    "#!/bin/sh\n"
                    "printf framework-ok\n");

    QJsonObject command =
        commandFor(QString{}, {}, QJsonObject{{QStringLiteral("stdout"), QStringLiteral("stdout.log")},
                                             {QStringLiteral("stderr"), QStringLiteral("stderr.log")}});
    command.remove(QStringLiteral("executable"));
    command.insert(QStringLiteral("framework_tool"), QStringLiteral("ipcraft-test-tool"));
    ipcraft::FlowRunRequest request = requestFor(runRoot, packageRoot, execFlow(command));
    request.frameworkToolSearchPaths = {toolsRoot.path()};

    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(request);

    require(result.ok, "framework_tool should run when resolved from application policy");
    require(readFile(QDir(runRoot.path()).filePath(QStringLiteral("stdout.log"))) ==
                QByteArrayLiteral("framework-ok"),
            "framework tool stdout should be captured");
}

void testCaptureMaxBytesRejectsUnboundedValue() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/noop.sh")),
                    "#!/bin/sh\nexit 0\n");

    QJsonObject capture;
    capture.insert(QStringLiteral("stdout"), QStringLiteral("stdout.log"));
    capture.insert(QStringLiteral("stderr"), QStringLiteral("stderr.log"));
    capture.insert(QStringLiteral("max_bytes"), 999999999);
    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/noop.sh"), {}, capture))));

    require(!result.ok, "oversized capture max_bytes should fail flow policy");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "oversized capture max_bytes should emit flow.command_policy_violation");
}

void testTimeoutPolicyRejectsBeforeProcessStart() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    const QString markerPath = QDir(runRoot.path()).filePath(QStringLiteral("process-started"));
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/mark-start.sh")),
                    "#!/bin/sh\n"
                    "printf started > \"$1\"\n");

    QJsonObject command = commandFor(QStringLiteral("tools/mark-start.sh"),
                                     QJsonArray{markerPath});
    command.insert(QStringLiteral("timeout_ms"), 86400001);
    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, execFlow(command)));

    require(!result.ok, "oversized timeout_ms should fail flow policy");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "oversized timeout_ms should emit flow.command_policy_violation");
    require(!QFileInfo::exists(markerPath),
            "invalid timeout policy must be rejected before the executable starts");
}

void testDuplicateCapturePathsAreRejected() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/noop.sh")),
                    "#!/bin/sh\nexit 0\n");

    QJsonObject capture;
    capture.insert(QStringLiteral("stdout"), QStringLiteral("same.log"));
    capture.insert(QStringLiteral("stderr"), QStringLiteral("./same.log"));
    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/noop.sh"), {}, capture))));

    require(!result.ok, "duplicate capture paths should fail flow policy");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "duplicate capture paths should emit flow.command_policy_violation");
}

void testParseDiagnosticsWithoutParserFailsStructurally() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    QJsonObject flow{
        {QStringLiteral("id"), QStringLiteral("generate")},
        {QStringLiteral("steps"),
         QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("parse_diagnostics")}}}}};

    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, flow));

    require(!result.ok, "parse_diagnostics without a parser should fail structurally");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "parse_diagnostics without a parser should emit flow.command_policy_violation");
}

void testRunFlowUsesRunDirectoryCwdByDefault() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/pwd.sh")),
                    "#!/bin/sh\npwd\n");

    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/pwd.sh")))));

    require(result.ok, "pwd flow should succeed");
    const QString stdoutText =
        QString::fromUtf8(readFile(QDir(runRoot.path()).filePath(QStringLiteral("stdout.log"))))
            .trimmed();
    require(QFileInfo(stdoutText).canonicalFilePath() ==
                QFileInfo(runRoot.path()).canonicalFilePath(),
            "default exec cwd should be run root");
}

void testRunFlowExpandsInputsManifestPlaceholder() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/check-input.sh")),
                    "#!/bin/sh\n"
                    "test -f \"$1\" || exit 9\n"
                    "grep -q 'ipcraft.emitted-inputs.v1' \"$1\" || exit 10\n");

    QJsonObject flow{
        {QStringLiteral("id"), QStringLiteral("generate")},
        {QStringLiteral("steps"),
         QJsonArray{
             QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}},
             QJsonObject{{QStringLiteral("kind"), QStringLiteral("exec")},
                         {QStringLiteral("command"),
                          commandFor(QStringLiteral("tools/check-input.sh"),
                                     QJsonArray{QStringLiteral("{inputs.manifest}")})}}}}};
    ipcraft::FlowRunRequest request = requestFor(runRoot, packageRoot, flow);
    request.package.emitters = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("params.json")}}};
    request.config.parameters.insert(QStringLiteral("width"), 64);

    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(request);

    require(result.ok, "exec args should expand {inputs.manifest} after emit_inputs");
    require(QFileInfo(QDir(runRoot.path()).filePath(QStringLiteral("inputs/manifest.json"))).isFile(),
            "emitted inputs manifest should be written under run root");
}

void testRunFlowExpandsPackageManifestPlaceholder() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    const QString manifestPath = QDir(packageRoot.path()).filePath(QStringLiteral("ipcraft.json"));
    writeFile(manifestPath, QByteArrayLiteral("{\"schema\":\"ipcraft.package.v1\"}\n"));
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/check-manifest.sh")),
                    "#!/bin/sh\n"
                    "test \"$1\" = \"$2\" || exit 11\n"
                    "test -f \"$1\" || exit 12\n");

    const QJsonObject flow = execFlow(
        commandFor(QStringLiteral("tools/check-manifest.sh"),
                   QJsonArray{QStringLiteral("{package.manifest}"), manifestPath}));
    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, flow));

    require(result.ok, "exec args should expand {package.manifest}");
}

void testValidateProjectDoesNotRunFlow() {
    QTemporaryDir root;
    require(root.isValid(), "temporary root should be valid");
    const QString markerPath = QDir(root.path()).filePath(QStringLiteral("drc-ran"));
    const QString scriptPath = QDir(root.path()).filePath(QStringLiteral("drc.sh"));
    writeExecutable(scriptPath,
                    QString("#!/bin/sh\nprintf ran > '%1'\nexit 0\n")
                        .arg(markerPath)
                        .toUtf8());

    Graph graph;
    IpCatalogEntry entry;
    entry.id = QStringLiteral("vendor.example.flow");
    entry.drc.command = scriptPath;
    ProjectIpInstanceRecord instance;
    instance.ipcoreId = entry.id;
    instance.instanceId = QStringLiteral("ip0");

    const QList<ValidationResult> ignored =
        ProjectValidationRunner().validate(&graph, QList<IpCatalogEntry>{entry}, {instance});

    require(!QFileInfo::exists(markerPath),
            "default project validation must not execute external DRC/flow commands");
}

void testStdoutStderrCaptureTruncationReturnsDiagnostic() {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/noisy.sh")),
                    "#!/bin/sh\n"
                    "printf 1234567890\n"
                    "printf abcdefghij >&2\n");

    QJsonObject capture;
    capture.insert(QStringLiteral("stdout"), QStringLiteral("stdout.log"));
    capture.insert(QStringLiteral("stderr"), QStringLiteral("stderr.log"));
    capture.insert(QStringLiteral("max_bytes"), 4);
    const ipcraft::FlowRunResult result = ipcraft::FlowRunner::runFlow(
        requestFor(runRoot,
                   packageRoot,
                   execFlow(commandFor(QStringLiteral("tools/noisy.sh"), {}, capture))));

    require(result.ok, "output truncation alone should not fail a successful process");
    require(hasRule(result.diagnostics, QStringLiteral("flow.output_truncated")),
            "capture truncation should emit flow.output_truncated");
    require(readFile(QDir(runRoot.path()).filePath(QStringLiteral("stdout.log"))) ==
                QByteArrayLiteral("1234"),
            "stdout should be deterministically truncated to max bytes");
    require(readFile(QDir(runRoot.path()).filePath(QStringLiteral("stderr.log"))) ==
                QByteArrayLiteral("abcd"),
            "stderr should be deterministically truncated to max bytes");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testExecMissingExecutableReturnsDiagnostic();
        testExecNonzeroExitCapturesStdoutStderr();
        testExecTimeoutReturnsDiagnosticAndAttemptsCleanup();
        testExecTimeoutAttemptsProcessGroupCleanup();
        testExecRejectsNativeCommandPolicyOverride();
        testExecResolvesFrameworkToolFromApplicationPolicy();
        testCaptureMaxBytesRejectsUnboundedValue();
        testTimeoutPolicyRejectsBeforeProcessStart();
        testDuplicateCapturePathsAreRejected();
        testParseDiagnosticsWithoutParserFailsStructurally();
        testRunFlowUsesRunDirectoryCwdByDefault();
        testRunFlowExpandsInputsManifestPlaceholder();
        testRunFlowExpandsPackageManifestPlaceholder();
        testValidateProjectDoesNotRunFlow();
        testStdoutStderrCaptureTruncationReturnsDiagnostic();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_flowrunner_test passed\n";
    return 0;
}
