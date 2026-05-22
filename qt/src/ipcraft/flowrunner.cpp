#include "ipcraft/flowrunner.h"

#include "ipcraft/jsonhelpers.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

namespace {

constexpr int kDefaultTimeoutMs = 60000;
constexpr int kMaxTimeoutMs = 24 * 60 * 60 * 1000;
constexpr qint64 kDefaultCaptureLimitBytes = 1048576;
constexpr qint64 kMaxCaptureLimitBytes = 16 * 1024 * 1024;

void insertString(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

ipcraft::DiagnosticLocation documentLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ipcraft::DiagnosticLocation fileLocation(const QString& file) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("file");
    location.file = file;
    return location;
}

ipcraft::Diagnostic diagnostic(const QString& ruleId,
                               const QString& message,
                               QVector<ipcraft::DiagnosticLocation> locations,
                               QJsonObject details = {},
                               const QString& severity = QStringLiteral("error")) {
    ipcraft::Diagnostic record;
    record.severity = severity;
    record.source = QStringLiteral("core");
    record.ruleId = ruleId;
    record.category = QStringLiteral("flow");
    record.message = message;
    record.details = std::move(details);
    record.locations = std::move(locations);
    return record;
}

void addFlowDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                       const QString& ruleId,
                       const QString& message,
                       const QString& path,
                       QJsonObject details = {}) {
    diagnostics.records.append(
        diagnostic(ruleId, message, {documentLocation(path)}, std::move(details)));
}

void appendDiagnostics(ipcraft::DiagnosticStore& target,
                       const ipcraft::DiagnosticStore& source) {
    for (const ipcraft::Diagnostic& diagnostic : source.records) {
        target.records.append(diagnostic);
    }
}

QString childPath(const QString& base, const QString& key) {
    return base + QLatin1Char('.') + key;
}

QString indexPath(qsizetype index) {
    return QStringLiteral("$.flows[].steps[%1]").arg(index);
}

QString slashPath(QString path) {
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

QString portablePath(QString path) {
    path = slashPath(path);
    return QDir::cleanPath(path);
}

bool isWindowsAbsolutePath(const QString& path) {
    return path.size() >= 3 &&
           path.at(0).isLetter() &&
           path.at(1) == QLatin1Char(':') &&
           path.at(2) == QLatin1Char('/');
}

bool hasTraversalSegment(const QString& relativePath) {
    return slashPath(relativePath)
        .split(QLatin1Char('/'), Qt::SkipEmptyParts)
        .contains(QStringLiteral(".."));
}

QString canonicalOrAbsoluteRoot(const QString& rootPath) {
    const QFileInfo rootInfo(rootPath);
    const QString canonical = rootInfo.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? rootInfo.absoluteFilePath() : canonical);
}

bool pathInsideRoot(const QString& rootPath, const QString& path) {
    const QString root = canonicalOrAbsoluteRoot(rootPath);
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    const QString clean = QDir::cleanPath(canonical);
    return clean == root || clean.startsWith(root + QLatin1Char('/'));
}

bool existingAncestorsStayInsideRoot(const QString& rootPath, const QString& relativePath) {
    const QString root = canonicalOrAbsoluteRoot(rootPath);
    QString currentPath = root;
    const QStringList segments =
        slashPath(relativePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        const QFileInfo nextInfo(QDir(currentPath).filePath(segment));
        if (!nextInfo.exists()) {
            const QString projected = QDir::cleanPath(QDir(currentPath).filePath(segment));
            return projected == root || projected.startsWith(root + QLatin1Char('/'));
        }
        const QString canonical = nextInfo.canonicalFilePath();
        if (canonical.isEmpty()) {
            return false;
        }
        currentPath = QDir::cleanPath(canonical);
        if (currentPath != root && !currentPath.startsWith(root + QLatin1Char('/'))) {
            return false;
        }
    }
    return currentPath == root || currentPath.startsWith(root + QLatin1Char('/'));
}

bool validRelativePath(const QString& rootPath,
                       const QString& relativePath,
                       const QString& path,
                       ipcraft::DiagnosticStore& diagnostics,
                       const QString& ruleId = QStringLiteral("flow.command_policy_violation")) {
    const QString raw = slashPath(relativePath);
    const QString normalized = portablePath(relativePath);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(normalized) ||
        isWindowsAbsolutePath(normalized) ||
        hasTraversalSegment(raw) ||
        !existingAncestorsStayInsideRoot(rootPath, normalized)) {
        addFlowDiagnostic(diagnostics,
                          ruleId,
                          QStringLiteral("Flow path must stay inside its allowed root."),
                          path);
        return false;
    }
    return true;
}

bool writeBytes(const QString& runRoot,
                const QString& relativePath,
                const QByteArray& bytes,
                const QString& path,
                ipcraft::DiagnosticStore& diagnostics) {
    const QString normalizedPath = portablePath(relativePath);
    if (!validRelativePath(runRoot, normalizedPath, path, diagnostics)) {
        return false;
    }
    const QString absolutePath = QDir(runRoot).filePath(normalizedPath);
    const QFileInfo fileInfo(absolutePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Could not create flow output directory."),
                          path);
        return false;
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Could not open flow output file."),
                          path);
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Could not write flow output file."),
                          path);
        return false;
    }
    return true;
}

QString stringValue(const QJsonObject& object, std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

std::optional<qint64> strictPositiveIntegerValue(const QJsonObject& object,
                                                 const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double doubleValue = value.toDouble();
    const qint64 intValue = static_cast<qint64>(doubleValue);
    if (intValue <= 0 || static_cast<double>(intValue) != doubleValue) {
        return std::nullopt;
    }
    return intValue;
}

QStringList stringArray(const QJsonValue& value) {
    QStringList values;
    if (!value.isArray()) {
        return values;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            values.append(item.toString());
        }
    }
    return values;
}

QString resolvePackageRoot(const ipcraft::FlowRunRequest& request) {
    return request.packageRoot.trimmed().isEmpty()
        ? request.package.packageRootPath
        : request.packageRoot;
}

QString expandPlaceholder(QString value, const ipcraft::FlowRunRequest& request,
                          const ipcraft::FlowRunResult& result) {
    const QString outputRoot = request.outputRoot.trimmed().isEmpty()
        ? QDir(result.runRoot).filePath(QStringLiteral("out"))
        : request.outputRoot;
    const QString packageManifest =
        QDir(resolvePackageRoot(request)).filePath(QStringLiteral("ipcraft.json"));
    value.replace(QStringLiteral("{run_dir}"), result.runRoot);
    value.replace(QStringLiteral("{out}"), outputRoot);
    value.replace(QStringLiteral("{package.manifest}"), packageManifest);
    value.replace(QStringLiteral("{inputs.manifest}"),
                  QDir(result.runRoot).filePath(QStringLiteral("inputs/manifest.json")));
    return value;
}

QStringList expandedArguments(const QJsonValue& value,
                              const ipcraft::FlowRunRequest& request,
                              const ipcraft::FlowRunResult& result) {
    QStringList arguments;
    for (const QString& argument : stringArray(value)) {
        arguments.append(expandPlaceholder(argument, request, result));
    }
    return arguments;
}

QJsonObject findFlow(const ipcraft::PackageSpec& package, const QString& flowId) {
    for (const QJsonValue& flowValue : package.flows) {
        if (!flowValue.isObject()) {
            continue;
        }
        const QJsonObject flow = flowValue.toObject();
        if (flow.value(QStringLiteral("id")).toString() == flowId) {
            return flow;
        }
    }
    return {};
}

bool resolvePackageExecutable(const ipcraft::FlowRunRequest& request,
                              const QJsonObject& command,
                              const QString& path,
                              ipcraft::DiagnosticStore& diagnostics,
                              QString* executablePath) {
    const QString executable = stringValue(command, {QStringLiteral("executable")});
    if (executable.isEmpty()) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.executable_missing"),
                          QStringLiteral("Flow executable is required."),
                          path);
        return false;
    }
    const QString normalized = portablePath(executable);
    if (QDir::isAbsolutePath(normalized) ||
        isWindowsAbsolutePath(normalized) ||
        hasTraversalSegment(executable)) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow executable must be package-relative."),
                          path);
        return false;
    }

    const QString packageRoot = resolvePackageRoot(request);
    if (packageRoot.trimmed().isEmpty()) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Package root is required for package-local executables."),
                          path);
        return false;
    }
    if (!validRelativePath(packageRoot, normalized, path, diagnostics)) {
        return false;
    }

    const QString absolutePath = QDir(packageRoot).filePath(normalized);
    QFileInfo executableInfo(absolutePath);
    if (!executableInfo.isFile() || !pathInsideRoot(packageRoot, absolutePath)) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.executable_missing"),
                          QStringLiteral("Flow executable is missing."),
                          path,
                          QJsonObject{{QStringLiteral("executable"), executable}});
        return false;
    }
    const QString canonical = executableInfo.canonicalFilePath();
    *executablePath = canonical.isEmpty() ? executableInfo.absoluteFilePath() : canonical;
    return true;
}

bool resolveFrameworkToolExecutable(const ipcraft::FlowRunRequest& request,
                                    const QString& frameworkTool,
                                    const QString& path,
                                    ipcraft::DiagnosticStore& diagnostics,
                                    QString* executablePath) {
    const QString normalizedTool = portablePath(frameworkTool);
    if (normalizedTool.isEmpty() ||
        QDir::isAbsolutePath(normalizedTool) ||
        isWindowsAbsolutePath(normalizedTool) ||
        hasTraversalSegment(frameworkTool) ||
        normalizedTool.contains(QLatin1Char('/'))) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Framework tool name is not allowed."),
                          path);
        return false;
    }

    for (const QString& searchPath : request.frameworkToolSearchPaths) {
        const QFileInfo rootInfo(searchPath);
        if (!rootInfo.isDir()) {
            continue;
        }
        const QString canonicalRoot = canonicalOrAbsoluteRoot(rootInfo.absoluteFilePath());
        const QString candidate = QDir(canonicalRoot).filePath(normalizedTool);
        QFileInfo candidateInfo(candidate);
        if (!candidateInfo.isFile() ||
            !candidateInfo.isExecutable() ||
            !pathInsideRoot(canonicalRoot, candidate)) {
            continue;
        }
        const QString canonical = candidateInfo.canonicalFilePath();
        *executablePath = canonical.isEmpty() ? candidateInfo.absoluteFilePath() : canonical;
        return true;
    }

    addFlowDiagnostic(diagnostics,
                      QStringLiteral("flow.executable_missing"),
                      QStringLiteral("Framework tool executable is missing."),
                      path,
                      QJsonObject{{QStringLiteral("framework_tool"), frameworkTool}});
    return false;
}

bool resolveExecutable(const ipcraft::FlowRunRequest& request,
                       const QJsonObject& command,
                       const QString& stepPath,
                       ipcraft::DiagnosticStore& diagnostics,
                       QString* executablePath) {
    const QString executable = stringValue(command, {QStringLiteral("executable")});
    const QString frameworkTool = stringValue(command, {QStringLiteral("framework_tool")});
    if (!frameworkTool.isEmpty()) {
        if (!executable.isEmpty()) {
            addFlowDiagnostic(diagnostics,
                              QStringLiteral("flow.command_policy_violation"),
                              QStringLiteral("Flow command must not declare both executable and framework_tool."),
                              childPath(stepPath, QStringLiteral("command")));
            return false;
        }
        return resolveFrameworkToolExecutable(request,
                                              frameworkTool,
                                              childPath(stepPath, QStringLiteral("command.framework_tool")),
                                              diagnostics,
                                              executablePath);
    }
    return resolvePackageExecutable(request,
                                    command,
                                    childPath(stepPath, QStringLiteral("command.executable")),
                                    diagnostics,
                                    executablePath);
}

bool resolveCwd(const QString& runRoot,
                const QJsonObject& command,
                const QString& path,
                ipcraft::DiagnosticStore& diagnostics,
                QString* cwd) {
    const QString requested = stringValue(command, {QStringLiteral("cwd")});
    if (requested.isEmpty() || requested == QStringLiteral("run_dir")) {
        *cwd = canonicalOrAbsoluteRoot(runRoot);
        return true;
    }
    const QString normalized = portablePath(requested);
    if (!validRelativePath(runRoot, normalized, path, diagnostics)) {
        return false;
    }
    const QString absolutePath = QDir(runRoot).filePath(normalized);
    if (!QDir().mkpath(absolutePath) || !pathInsideRoot(runRoot, absolutePath)) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow cwd must stay inside the run directory."),
                          path);
        return false;
    }
    *cwd = QFileInfo(absolutePath).canonicalFilePath();
    return true;
}

struct CapturePolicy {
    QString stdoutPath = QStringLiteral("stdout.log");
    QString stderrPath = QStringLiteral("stderr.log");
    qint64 maxBytes = kDefaultCaptureLimitBytes;
};

std::optional<CapturePolicy> capturePolicy(const QJsonObject& command,
                                           const QString& stepPath,
                                           ipcraft::DiagnosticStore& diagnostics) {
    CapturePolicy policy;
    const QJsonObject capture = command.value(QStringLiteral("capture")).toObject();
    const QString stdoutPath = stringValue(capture, {QStringLiteral("stdout")});
    const QString stderrPath = stringValue(capture, {QStringLiteral("stderr")});
    if (!stdoutPath.isEmpty()) {
        policy.stdoutPath = stdoutPath;
    }
    if (!stderrPath.isEmpty()) {
        policy.stderrPath = stderrPath;
    }
    const std::optional<qint64> requestedMax =
        strictPositiveIntegerValue(capture, QStringLiteral("max_bytes"));
    policy.maxBytes = requestedMax.value_or(kDefaultCaptureLimitBytes);
    if (policy.maxBytes > kMaxCaptureLimitBytes) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow capture limit exceeds the application maximum."),
                          childPath(stepPath, QStringLiteral("command.capture.max_bytes")),
                          QJsonObject{{QStringLiteral("max_bytes"), policy.maxBytes},
                                      {QStringLiteral("max_allowed"), kMaxCaptureLimitBytes}});
        return std::nullopt;
    }
    return policy;
}

std::optional<int> timeoutMs(const QJsonObject& command,
                             const QString& stepPath,
                             ipcraft::DiagnosticStore& diagnostics) {
    const std::optional<qint64> requested =
        strictPositiveIntegerValue(command, QStringLiteral("timeout_ms"));
    const qint64 timeout = requested.value_or(kDefaultTimeoutMs);
    if (timeout > kMaxTimeoutMs) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow timeout exceeds the application maximum."),
                          childPath(stepPath, QStringLiteral("command.timeout_ms")),
                          QJsonObject{{QStringLiteral("timeout_ms"), timeout},
                                      {QStringLiteral("max_allowed"), kMaxTimeoutMs}});
        return std::nullopt;
    }
    return static_cast<int>(timeout);
}

void appendLimited(QByteArray& target,
                   const QByteArray& chunk,
                   qint64 maxBytes,
                   bool* truncated) {
    if (chunk.isEmpty()) {
        return;
    }
    const qint64 remaining = maxBytes - target.size();
    if (remaining > 0) {
        target.append(chunk.left(remaining));
    }
    if (chunk.size() > remaining) {
        *truncated = true;
    }
}

void addTruncationDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                             const QString& streamName,
                             qint64 limit,
                             const QString& capturePath,
                             const QString& commandPath) {
    QJsonObject details;
    details.insert(QStringLiteral("stream"), streamName);
    details.insert(QStringLiteral("limit"), limit);
    details.insert(QStringLiteral("capture"), capturePath);
    diagnostics.records.append(diagnostic(
        QStringLiteral("flow.output_truncated"),
        QStringLiteral("Flow process output was truncated."),
        {documentLocation(commandPath), fileLocation(capturePath)},
        details,
        QStringLiteral("warning")));
}

QProcessEnvironment sanitizedEnvironment(const QJsonObject& command) {
    QProcessEnvironment inherited = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment environment;
    static const QStringList kBaseAllowList{
        QStringLiteral("PATH"),
        QStringLiteral("SystemRoot"),
        QStringLiteral("WINDIR")
    };
    QSet<QString> allowed(kBaseAllowList.cbegin(), kBaseAllowList.cend());
    const QJsonObject env = command.value(QStringLiteral("env")).toObject();
    for (const QString& key : stringArray(env.value(QStringLiteral("allow")))) {
        if (!key.trimmed().isEmpty()) {
            allowed.insert(key);
        }
    }
    for (const QString& key : allowed) {
        if (inherited.contains(key)) {
            environment.insert(key, inherited.value(key));
        }
    }
    return environment;
}

bool runExecStep(const ipcraft::FlowRunRequest& request,
                 const QJsonObject& step,
                 const QString& stepPath,
                 ipcraft::FlowRunResult& result) {
    const QJsonObject command = step.value(QStringLiteral("command")).toObject();
    if (command.contains(QStringLiteral("native"))) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Native command policy overrides are not allowed."),
                          childPath(stepPath, QStringLiteral("command.native")));
        return false;
    }

    QString executablePath;
    if (!resolveExecutable(request, command, stepPath, result.diagnostics, &executablePath)) {
        return false;
    }

    QString cwd;
    if (!resolveCwd(result.runRoot,
                    command,
                    childPath(stepPath, QStringLiteral("command.cwd")),
                    result.diagnostics,
                    &cwd)) {
        return false;
    }

    const std::optional<CapturePolicy> captureResult =
        capturePolicy(command, stepPath, result.diagnostics);
    if (!captureResult.has_value()) {
        return false;
    }
    const CapturePolicy capture = *captureResult;
    if (portablePath(capture.stdoutPath) == portablePath(capture.stderrPath)) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow stdout and stderr capture paths must be distinct."),
                          childPath(stepPath, QStringLiteral("command.capture")));
        return false;
    }
    if (!validRelativePath(result.runRoot,
                           capture.stdoutPath,
                           childPath(stepPath, QStringLiteral("command.capture.stdout")),
                           result.diagnostics) ||
        !validRelativePath(result.runRoot,
                           capture.stderrPath,
                           childPath(stepPath, QStringLiteral("command.capture.stderr")),
                           result.diagnostics)) {
        return false;
    }

    const std::optional<int> timeout = timeoutMs(command, stepPath, result.diagnostics);
    if (!timeout.has_value()) {
        return false;
    }

    QProcess process;
#ifdef Q_OS_UNIX
    process.setUnixProcessParameters(
        QProcess::UnixProcessFlag::CreateNewSession);
#endif
    process.setProgram(executablePath);
    process.setArguments(expandedArguments(command.value(QStringLiteral("args")),
                                           request,
                                           result));
    process.setWorkingDirectory(cwd);
    process.setProcessEnvironment(sanitizedEnvironment(command));
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted()) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.executable_missing"),
                          QStringLiteral("Flow executable could not be started."),
                          childPath(stepPath, QStringLiteral("command.executable")),
                          QJsonObject{{QStringLiteral("executable"), executablePath}});
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
    bool stdoutTruncated = false;
    bool stderrTruncated = false;
    bool timedOut = false;
    while (process.state() != QProcess::NotRunning) {
        const qint64 remaining = *timeout - timer.elapsed();
        if (remaining <= 0) {
            timedOut = true;
            break;
        }
        process.waitForReadyRead(std::min<qint64>(remaining, 25));
        appendLimited(stdoutBytes, process.readAllStandardOutput(), capture.maxBytes, &stdoutTruncated);
        appendLimited(stderrBytes, process.readAllStandardError(), capture.maxBytes, &stderrTruncated);
    }

    if (timedOut) {
#ifdef Q_OS_UNIX
        const qint64 pid = process.processId();
        if (pid > 0) {
            ::kill(static_cast<pid_t>(-pid), SIGKILL);
        }
#endif
        process.kill();
        process.waitForFinished(1000);
    } else {
        process.waitForFinished(0);
    }
    appendLimited(stdoutBytes, process.readAllStandardOutput(), capture.maxBytes, &stdoutTruncated);
    appendLimited(stderrBytes, process.readAllStandardError(), capture.maxBytes, &stderrTruncated);

    const bool stdoutWritten = writeBytes(result.runRoot,
                                          capture.stdoutPath,
                                          stdoutBytes,
                                          childPath(stepPath, QStringLiteral("command.capture.stdout")),
                                          result.diagnostics);
    const bool stderrWritten = writeBytes(result.runRoot,
                                          capture.stderrPath,
                                          stderrBytes,
                                          childPath(stepPath, QStringLiteral("command.capture.stderr")),
                                          result.diagnostics);

    if (stdoutTruncated) {
        addTruncationDiagnostic(result.diagnostics,
                                QStringLiteral("stdout"),
                                capture.maxBytes,
                                portablePath(capture.stdoutPath),
                                stepPath);
    }
    if (stderrTruncated) {
        addTruncationDiagnostic(result.diagnostics,
                                QStringLiteral("stderr"),
                                capture.maxBytes,
                                portablePath(capture.stderrPath),
                                stepPath);
    }

    if (timedOut) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.timeout"),
                          QStringLiteral("Flow process timed out."),
                          childPath(stepPath, QStringLiteral("command.timeout_ms")),
                          QJsonObject{{QStringLiteral("timeout_ms"), *timeout}});
        return false;
    }

    if (!stdoutWritten || !stderrWritten) {
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.exec_failed"),
                          QStringLiteral("Flow process exited with failure."),
                          stepPath,
                          QJsonObject{{QStringLiteral("exit_code"), process.exitCode()}});
        return false;
    }

    return true;
}

} // namespace

namespace ipcraft {

FlowRunResult FlowRunner::runFlow(const FlowRunRequest& request) {
    FlowRunResult result;
    result.flowId = request.flowId;
    result.runId = request.runId.trimmed().isEmpty()
        ? QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"))
        : request.runId;
    result.runRoot = request.runRoot.trimmed().isEmpty()
        ? QDir::current().filePath(result.runId)
        : request.runRoot;

    if (!QDir().mkpath(result.runRoot)) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Could not create flow run directory."),
                          QStringLiteral("$.run_root"));
        result.ok = false;
        return result;
    }
    result.runRoot = canonicalOrAbsoluteRoot(result.runRoot);

    const QJsonObject flow = findFlow(request.package, request.flowId);
    if (flow.isEmpty()) {
        addFlowDiagnostic(result.diagnostics,
                          QStringLiteral("flow.unknown_flow"),
                          QStringLiteral("Requested flow is not declared by the package."),
                          QStringLiteral("$.flow"));
        result.ok = false;
        return result;
    }

    bool hardFailure = false;
    const QJsonArray steps = flow.value(QStringLiteral("steps")).toArray();
    for (qsizetype index = 0; index < steps.size(); ++index) {
        const QString stepPath = indexPath(index);
        const QJsonValue stepValue = steps.at(index);
        if (!stepValue.isObject()) {
            addFlowDiagnostic(result.diagnostics,
                              QStringLiteral("flow.command_policy_violation"),
                              QStringLiteral("Flow step must be an object."),
                              stepPath);
            hardFailure = true;
            continue;
        }

        const QJsonObject step = stepValue.toObject();
        const QString kind = stringValue(step, {QStringLiteral("kind")});
        if (kind == QStringLiteral("exec")) {
            if (!runExecStep(request, step, stepPath, result)) {
                hardFailure = true;
            }
        } else if (kind == QStringLiteral("emit_inputs")) {
            PackageInputBuildRequest emitRequest;
            emitRequest.projectId = request.projectId;
            emitRequest.instanceId = request.instanceId;
            emitRequest.runId = result.runId;
            emitRequest.outputRoot = QDir(result.runRoot).filePath(QStringLiteral("inputs"));
            emitRequest.package = request.package;
            emitRequest.packageRoot = resolvePackageRoot(request);
            emitRequest.config = request.config;
            emitRequest.composition = request.composition;
            emitRequest.graphConfig = request.graphConfig;
            const PackageInputBuildResult emitResult =
                PackageInputBuilder::emitInputs(emitRequest);
            result.inputsManifest = emitResult.manifest;
            appendDiagnostics(result.diagnostics, emitResult.manifest.diagnostics);
            if (!emitResult.ok) {
                hardFailure = true;
            }
            if (!writeBytes(result.runRoot,
                            QStringLiteral("inputs/manifest.json"),
                            toDeterministicJson(result.inputsManifest.toJson()),
                            stepPath,
                            result.diagnostics)) {
                hardFailure = true;
            }
        } else if (kind == QStringLiteral("collect_artifacts")) {
            ArtifactCollectRequest collectRequest;
            collectRequest.runRoot = result.runRoot;
            collectRequest.outputRoot = request.outputRoot;
            collectRequest.flowRunId = result.runId;
            collectRequest.sourceInstanceId = request.instanceId;
            collectRequest.package = request.package;
            const ArtifactCollectResult collectResult =
                ArtifactCollector::collect(collectRequest);
            result.artifacts = collectResult.index;
            appendDiagnostics(result.diagnostics, collectResult.diagnostics);
            if (!collectResult.ok) {
                hardFailure = true;
            }
        } else if (kind == QStringLiteral("parse_diagnostics")) {
            addFlowDiagnostic(result.diagnostics,
                              QStringLiteral("flow.command_policy_violation"),
                              QStringLiteral("Flow diagnostic parsing is not available without a declared parser."),
                              stepPath);
            hardFailure = true;
        } else if (kind == QStringLiteral("plugin_hook")) {
            addFlowDiagnostic(result.diagnostics,
                              QStringLiteral("flow.plugin_unavailable"),
                              QStringLiteral("Flow plugin hooks are not available in this runtime."),
                              stepPath);
            hardFailure = true;
        } else {
            addFlowDiagnostic(result.diagnostics,
                              QStringLiteral("flow.command_policy_violation"),
                              QStringLiteral("Flow step kind is not supported."),
                              childPath(stepPath, QStringLiteral("kind")));
            hardFailure = true;
        }
    }

    result.state.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.flow-run-state.v1"));
    result.state.insert(QStringLiteral("flow_id"), result.flowId);
    result.state.insert(QStringLiteral("run_id"), result.runId);
    result.state.insert(QStringLiteral("run_root"), result.runRoot);
    result.state.insert(QStringLiteral("diagnostics"), result.diagnostics.toJson());
    result.state.insert(QStringLiteral("artifacts"), result.artifacts.toJson());
    result.ok = !hardFailure;
    return result;
}

} // namespace ipcraft
