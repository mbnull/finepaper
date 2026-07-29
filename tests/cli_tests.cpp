#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

constexpr int kIoError = 7;
constexpr int kProcessTimeoutMilliseconds = 30000;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

struct CommandResult {
    bool started = false;
    bool finished = false;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

CommandResult runCommand(const QString& executable, const QStringList& arguments) {
    CommandResult result;
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    result.started = process.waitForStarted();
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    result.finished = process.waitForFinished(kProcessTimeoutMilliseconds);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(3000);
    }
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

QJsonObject parseJsonOutput(const CommandResult& result, const QString& context) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        result.standardOutput.trimmed(), &error);
    check(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("%1 writes one JSON object to stdout; stdout=%2 stderr=%3")
              .arg(context,
                   QString::fromUtf8(result.standardOutput),
                   QString::fromUtf8(result.standardError)));
    return document.isObject() ? document.object() : QJsonObject{};
}

bool hasDiagnosticCode(const QJsonObject& result, const QString& code) {
    const QJsonArray diagnostics = result.value(QStringLiteral("diagnostics")).toArray();
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const QJsonValue& value) {
        return value.isObject()
            && value.toObject().value(QStringLiteral("code")).toString() == code;
    });
}

QString finepaperExecutable() {
#ifdef Q_OS_WIN
    constexpr auto executableName = "finepaper.exe";
#else
    constexpr auto executableName = "finepaper";
#endif
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QString::fromLatin1(executableName));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const QString executable = finepaperExecutable();
    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    const QString requestPath = QDir(projectRoot).filePath(
        QStringLiteral("examples/mesh-2x2.request.json"));
    const QString packageRoot = QDir(projectRoot).filePath(QStringLiteral("packages"));

    check(QFileInfo(executable).isExecutable(),
          QStringLiteral("finepaper CLI is built beside finepaper-cli-tests"));
    check(QFileInfo(requestPath).isFile(), QStringLiteral("CLI request fixture exists"));
    check(QFileInfo(packageRoot).isDir(), QStringLiteral("reference Package root exists"));

    QTemporaryDir temporaryDirectory;
    check(temporaryDirectory.isValid(), QStringLiteral("temporary CLI test directory is available"));
    if (!temporaryDirectory.isValid()) {
        return 1;
    }

    const QString blockingParent = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("blocking-parent"));
    QFile blockingFile(blockingParent);
    check(blockingFile.open(QIODevice::WriteOnly),
          QStringLiteral("ordinary file can block creation of a child output path"));
    if (blockingFile.isOpen()) {
        blockingFile.write("not a directory\n");
        blockingFile.close();
    }
    const QString invalidDesignOutput = QDir(blockingParent).filePath(
        QStringLiteral("design.fpnoc"));
    const CommandResult createResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("create"),
            QStringLiteral("--input"),
            requestPath,
            QStringLiteral("--output"),
            invalidDesignOutput,
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--json")
        });
    check(createResult.started && createResult.finished
              && createResult.exitStatus == QProcess::NormalExit,
          QStringLiteral("design create finishes normally when its output cannot be written"));
    check(createResult.exitCode == kIoError,
          QStringLiteral("design create output failure returns IO exit code 7"));

    const QString resultDirectory = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("result-is-a-directory"));
    check(QDir().mkpath(resultDirectory),
          QStringLiteral("existing directory can be used as an invalid result file"));

    const QString validDesignPath = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("valid-design.fpnoc"));
    const CommandResult validCreateResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("create"),
            QStringLiteral("--input"),
            requestPath,
            QStringLiteral("--output"),
            validDesignPath,
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--json")
        });
    check(validCreateResult.started && validCreateResult.finished
              && validCreateResult.exitStatus == QProcess::NormalExit
              && validCreateResult.exitCode == 0
              && QFileInfo(validDesignPath).isFile(),
          QStringLiteral("design create prepares a valid design for generate testing"));

    const QString designGenerationOutput = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("design-generation-output"));
    const CommandResult generateResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("generate"),
            validDesignPath,
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--output"),
            designGenerationOutput,
            QStringLiteral("--result"),
            resultDirectory,
            QStringLiteral("--json")
        });
    check(generateResult.started && generateResult.finished
              && generateResult.exitStatus == QProcess::NormalExit,
          QStringLiteral("design generate finishes when its result copy cannot be written"));
    check(generateResult.exitCode == kIoError,
          QStringLiteral("design generate result-copy failure returns IO exit code 7"));
    const QJsonObject generateJson = parseJsonOutput(
        generateResult, QStringLiteral("design generate --json"));
    check(!generateJson.value(QStringLiteral("success")).toBool(true),
          QStringLiteral("design generate result-copy failure reports success=false"));
    check(hasDiagnosticCode(generateJson, QStringLiteral("json.write_failed")),
          QStringLiteral("design generate result-copy failure reports json.write_failed"));

    const QString generationOutput = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("generation-output"));
    const CommandResult runResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("run"),
            requestPath,
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--output"),
            generationOutput,
            QStringLiteral("--result"),
            resultDirectory,
            QStringLiteral("--json")
        });
    check(runResult.started && runResult.finished
              && runResult.exitStatus == QProcess::NormalExit,
          QStringLiteral("run finishes normally when its result copy cannot be written"));
    check(runResult.exitCode == kIoError,
          QStringLiteral("run result-copy failure returns IO exit code 7"));

    const QJsonObject runJson = parseJsonOutput(runResult, QStringLiteral("run --json"));
    check(!runJson.value(QStringLiteral("success")).toBool(true),
          QStringLiteral("run result-copy failure reports success=false"));
    check(hasDiagnosticCode(runJson, QStringLiteral("json.write_failed")),
          QStringLiteral("run result-copy failure reports json.write_failed"));

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-cli-tests passed" << Qt::endl;
        return 0;
    }
    return 1;
}
