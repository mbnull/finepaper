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

constexpr int kPackageError = 3;
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

bool readJsonObjectFile(const QString& path, QJsonObject* object) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    *object = document.object();
    return true;
}

bool writeJsonObjectFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray contents = QJsonDocument(object).toJson();
    return file.write(contents) == contents.size();
}

void setMissingPackageReference(QJsonObject& object) {
    object.insert(
        QStringLiteral("package"),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("example.rejected-target")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        });
}

void checkPackageResolutionFailure(const CommandResult& result,
                                   const QString& context) {
    const QJsonObject output = parseJsonOutput(result, context);
    check(result.started
              && result.finished
              && result.exitStatus == QProcess::NormalExit
              && result.exitCode == kPackageError
              && !output.value(QStringLiteral("success")).toBool(true)
              && hasDiagnosticCode(output, QStringLiteral("package.not_found")),
          QStringLiteral("%1 returns Package exit code 3 for an unresolved target")
              .arg(context));
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
    // CLI intentionally consumes the same installed-Package user setting as
    // the GUI. Keep this process-level integration test independent of the
    // developer account that launches it.
    qputenv("XDG_CONFIG_HOME", temporaryDirectory.path().toUtf8());

    const QString conformancePackage = QDir(projectRoot).filePath(
        QStringLiteral("tests/fixtures/complex-engine"));
    const CommandResult packageCheckResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("package"),
            QStringLiteral("check"),
            conformancePackage,
            QStringLiteral("--json")
        });
    const QJsonObject packageCheck = parseJsonOutput(
        packageCheckResult, QStringLiteral("Package conformance validation"));
    check(packageCheckResult.started && packageCheckResult.finished
              && packageCheckResult.exitStatus == QProcess::NormalExit
              && packageCheckResult.exitCode == 0
              && packageCheck.value(QStringLiteral("success")).toBool()
              && packageCheck.value(QStringLiteral("validation"))
                     .toObject().value(QStringLiteral("success")).toBool()
              && hasDiagnosticCode(
                  packageCheck, QStringLiteral("mock.engine_used")),
          QStringLiteral(
              "package check enters FinepaperApplication and runs the minimal Package validator"));

    const QString smokeOutput = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("package-check-smoke"));
    const CommandResult packageSmokeResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("package"),
            QStringLiteral("check"),
            conformancePackage,
            QStringLiteral("--smoke-generate"),
            QStringLiteral("--output"),
            smokeOutput,
            QStringLiteral("--json")
        });
    const QJsonObject packageSmoke = parseJsonOutput(
        packageSmokeResult, QStringLiteral("Package smoke generation"));
    const QJsonObject smokeGeneration = packageSmoke.value(
        QStringLiteral("generation")).toObject();
    const QJsonArray smokeArtifacts = smokeGeneration.value(
        QStringLiteral("artifacts")).toArray();
    check(packageSmokeResult.started && packageSmokeResult.finished
              && packageSmokeResult.exitStatus == QProcess::NormalExit
              && packageSmokeResult.exitCode == 0
              && packageSmoke.value(QStringLiteral("success")).toBool()
              && smokeGeneration.value(QStringLiteral("success")).toBool()
              && smokeArtifacts.size() == 1
              && hasDiagnosticCode(
                  packageSmoke, QStringLiteral("mock.engine_used"))
              && hasDiagnosticCode(
                  packageSmoke, QStringLiteral("mock.generator_used"))
              && QFileInfo(smokeGeneration.value(
                     QStringLiteral("outputDirectory")).toString()
                         + QStringLiteral("/complex_top.sv")).isFile(),
          QStringLiteral(
              "package check optionally smoke-generates and validates its result and artifact paths"));

    const QString missingPackageRoot = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("missing-package-root"));
    const QString invalidPackageRoot = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("invalid-package"));
    check(QDir().mkpath(invalidPackageRoot),
          QStringLiteral("invalid CLI Package root is created"));
    QFile invalidManifest(
        QDir(invalidPackageRoot).filePath(QStringLiteral("package.json")));
    const bool invalidManifestOpened = invalidManifest.open(
        QIODevice::WriteOnly | QIODevice::Truncate);
    check(invalidManifestOpened,
          QStringLiteral("invalid CLI Package manifest is opened for writing"));
    if (invalidManifestOpened) {
        check(invalidManifest.write("{}\n") == 3,
              QStringLiteral("invalid CLI Package manifest is written"));
        invalidManifest.close();
    }
    const CommandResult packageListResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("package"),
            QStringLiteral("list"),
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--package-root"),
            QDir(packageRoot).filePath(QStringLiteral("finepaper-noc")),
            QStringLiteral("--package-root"),
            missingPackageRoot,
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    const QJsonObject packageList = parseJsonOutput(
        packageListResult, QStringLiteral("resilient Package list"));
    check(packageListResult.started && packageListResult.finished
              && packageListResult.exitStatus == QProcess::NormalExit
              && packageListResult.exitCode == 0
              && packageList.value(QStringLiteral("success")).toBool()
              && packageList.value(QStringLiteral("catalogCommitted")).toBool()
              && !packageList.value(QStringLiteral("catalogFatal")).toBool()
              && packageList.value(QStringLiteral("acceptedPackageCount")).toInt() == 2
              && packageList.value(QStringLiteral("rejectedPackageCount")).toInt() == 1
              && packageList.value(QStringLiteral("packages")).toArray().size() == 2
              && hasDiagnosticCode(packageList, QStringLiteral("package.root_missing")),
          QStringLiteral("CLI keeps valid Packages selectable when roots overlap or go missing"));

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
            QStringLiteral("--package-root"),
            invalidPackageRoot,
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
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    check(validCreateResult.started && validCreateResult.finished
              && validCreateResult.exitStatus == QProcess::NormalExit
              && validCreateResult.exitCode == 0
              && QFileInfo(validDesignPath).isFile()
              && validCreateResult.standardError.contains("error: package."),
          QStringLiteral("design create uses valid Packages and reports isolated candidates on stderr"));

    QJsonObject missingPackageRequest;
    const QString missingPackageRequestPath = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("missing-package.request.json"));
    const bool missingRequestLoaded = readJsonObjectFile(
        requestPath, &missingPackageRequest);
    if (missingRequestLoaded) {
        setMissingPackageReference(missingPackageRequest);
    }
    check(missingRequestLoaded
              && writeJsonObjectFile(missingPackageRequestPath, missingPackageRequest),
          QStringLiteral("request with an unresolved target Package is available"));

    QJsonObject missingPackageDesign;
    const QString missingPackageDesignPath = QDir(temporaryDirectory.path()).filePath(
        QStringLiteral("missing-package.fpnoc"));
    const bool missingDesignLoaded = readJsonObjectFile(
        validDesignPath, &missingPackageDesign);
    if (missingDesignLoaded) {
        setMissingPackageReference(missingPackageDesign);
    }
    check(missingDesignLoaded
              && writeJsonObjectFile(missingPackageDesignPath, missingPackageDesign),
          QStringLiteral("design with an unresolved target Package is available"));

    const CommandResult missingCreateResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("create"),
            QStringLiteral("--input"),
            missingPackageRequestPath,
            QStringLiteral("--output"),
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral("missing-package-create.fpnoc")),
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    checkPackageResolutionFailure(
        missingCreateResult, QStringLiteral("design create"));

    const CommandResult missingValidateResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("validate"),
            missingPackageDesignPath,
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    checkPackageResolutionFailure(
        missingValidateResult, QStringLiteral("design validate"));

    const CommandResult missingGenerateResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("design"),
            QStringLiteral("generate"),
            missingPackageDesignPath,
            QStringLiteral("--output"),
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral("missing-package-generate")),
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    checkPackageResolutionFailure(
        missingGenerateResult, QStringLiteral("design generate"));

    const CommandResult missingRunResult = runCommand(
        executable,
        QStringList{
            QStringLiteral("run"),
            missingPackageRequestPath,
            QStringLiteral("--output"),
            QDir(temporaryDirectory.path()).filePath(
                QStringLiteral("missing-package-run")),
            QStringLiteral("--package-root"),
            packageRoot,
            QStringLiteral("--package-root"),
            invalidPackageRoot,
            QStringLiteral("--json")
        });
    checkPackageResolutionFailure(missingRunResult, QStringLiteral("run"));

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
