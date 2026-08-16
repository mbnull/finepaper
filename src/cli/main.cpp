#include "application/application.h"
#include "application/runtime_settings.h"
#include "package/package.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace {

using namespace finepaper;

constexpr int kSuccess = 0;
constexpr int kInputError = 2;
constexpr int kPackageError = 3;
constexpr int kDesignValidationError = 4;
constexpr int kPackageValidationError = 5;
constexpr int kGenerationError = 6;
constexpr int kIoError = 7;
constexpr int kInternalError = 8;

QString optionValue(const QStringList& arguments, const QString& option) {
    for (qsizetype index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments.at(index) == option) {
            return arguments.at(index + 1);
        }
    }
    return {};
}

QStringList optionValues(const QStringList& arguments, const QString& option) {
    QStringList values;
    for (qsizetype index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments.at(index) == option) {
            values.append(arguments.at(index + 1));
        }
    }
    return values;
}

bool hasOption(const QStringList& arguments, const QString& option) {
    return arguments.contains(option);
}

QStringList packageRoots(const QStringList& arguments) {
    const QStringList configuredRoots = QSettings().value(
        installedPackageRootsSetting).toStringList();
    return resolveRuntimeLocations(
        optionValues(arguments, QStringLiteral("--package-root")),
        configuredRoots).packageRoots;
}

void printJson(const QJsonObject& object) {
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}

void printDiagnostics(const QVector<Diagnostic>& diagnostics) {
    QTextStream stream(stderr);
    for (const Diagnostic& diagnostic : diagnostics) {
        stream << diagnostic.severity << ": " << diagnostic.code;
        if (!diagnostic.path.isEmpty()) {
            stream << " " << diagnostic.path;
        }
        stream << ": " << diagnostic.message << Qt::endl;
    }
}

bool hasPackageResolutionFailure(const QVector<Diagnostic>& diagnostics) {
    return std::any_of(
        diagnostics.cbegin(), diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == QStringLiteral("package.not_found");
        });
}

int failureExitCode(const QVector<Diagnostic>& diagnostics, int fallback) {
    return hasPackageResolutionFailure(diagnostics) ? kPackageError : fallback;
}

QJsonObject packageToJson(const PackageDefinition& package) {
    return QJsonObject{
        {QStringLiteral("id"), package.id},
        {QStringLiteral("name"), package.name},
        {QStringLiteral("version"), package.version},
        {QStringLiteral("root"), package.rootPath},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), package.mesh.minimumRows},
                {QStringLiteral("max"), package.mesh.maximumRows},
                {QStringLiteral("default"), package.mesh.defaultRows}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), package.mesh.minimumColumns},
                {QStringLiteral("max"), package.mesh.maximumColumns},
                {QStringLiteral("default"), package.mesh.defaultColumns}
            }}
        }},
        {QStringLiteral("hasEngine"), package.engine.has_value()}
    };
}

QJsonObject designResultToJson(const DesignResult& result) {
    QJsonObject object{
        {QStringLiteral("success"), result.success},
        {QStringLiteral("diagnostics"), diagnosticsToJson(result.diagnostics)}
    };
    if (result.success) {
        object.insert(QStringLiteral("design"), designToJson(result.design));
    }
    return object;
}

void printUsage() {
    QTextStream(stdout)
        << "Finepaper runtime NoC tool\n\n"
        << "Commands:\n"
        << "  finepaper package list [--package-root PATH] [--json]\n"
        << "  finepaper package check PACKAGE_PATH [--smoke-generate --output ROOT] [--json]\n"
        << "  finepaper design create --input REQUEST --output DESIGN [--package-root PATH] [--json]\n"
        << "  finepaper design validate DESIGN [--package-root PATH] [--json]\n"
        << "  finepaper design generate DESIGN --output ROOT [--result FILE] [--package-root PATH] [--json]\n"
        << "  finepaper run REQUEST --output ROOT [--result FILE] [--design-output FILE] [--package-root PATH] [--json]\n";
}

bool initializeApplication(FinepaperApplication& application,
                           const QStringList& arguments,
                           QVector<Diagnostic>* diagnostics) {
    PackageCatalogReloadResult reload =
        application.reloadPackages(packageRoots(arguments));
    *diagnostics = std::move(reload.diagnostics);
    return reload.committed();
}

int packageCommand(const QStringList& arguments) {
    if (arguments.size() < 3) {
        printUsage();
        return kInputError;
    }
    const QString action = arguments.at(2);
    if (action == QStringLiteral("check")) {
        if (arguments.size() < 4) {
            printUsage();
            return kInputError;
        }
        const bool smokeGeneration = hasOption(
            arguments, QStringLiteral("--smoke-generate"));
        const QString smokeOutputRoot = optionValue(
            arguments, QStringLiteral("--output"));
        if (smokeGeneration && smokeOutputRoot.isEmpty()) {
            printUsage();
            return kInputError;
        }
        FinepaperApplication application;
        const PackageCheckResult result = application.checkPackage(
            arguments.at(3),
            PackageCheckOptions{smokeGeneration, smokeOutputRoot});
        QJsonObject output{
            {QStringLiteral("success"), result.success},
            {QStringLiteral("package"),
             result.package ? packageToJson(*result.package) : QJsonObject()},
            {QStringLiteral("smokeGenerationRequested"), smokeGeneration},
            {QStringLiteral("diagnostics"), diagnosticsToJson(result.diagnostics)}
        };
        if (result.validation) {
            output.insert(
                QStringLiteral("validation"),
                validationResultToJson(*result.validation));
        }
        if (result.generation) {
            output.insert(
                QStringLiteral("generation"),
                generationResultToJson(*result.generation));
        }
        if (hasOption(arguments, QStringLiteral("--json"))) {
            printJson(output);
        } else {
            printDiagnostics(result.diagnostics);
            QTextStream(stdout) << (result.success
                                      ? "Package conformance check passed"
                                      : "Package conformance check failed")
                                << Qt::endl;
        }
        return result.success ? kSuccess : kPackageError;
    }
    if (action != QStringLiteral("list")) {
        printUsage();
        return kInputError;
    }

    FinepaperApplication application;
    const PackageCatalogReloadResult reload =
        application.reloadPackages(packageRoots(arguments));
    const bool initialized = reload.committed();
    QJsonArray packages;
    for (const PackageDefinition& package : application.packages()) {
        packages.append(packageToJson(package));
    }
    const QJsonObject output{
        {QStringLiteral("success"), initialized},
        {QStringLiteral("catalogCommitted"), reload.committed()},
        {QStringLiteral("catalogFatal"), reload.catalogFatal()},
        {QStringLiteral("acceptedPackageCount"),
         static_cast<qint64>(reload.acceptedCount)},
        {QStringLiteral("rejectedPackageCount"),
         static_cast<qint64>(reload.rejectedCount)},
        {QStringLiteral("packages"), packages},
        {QStringLiteral("diagnostics"), diagnosticsToJson(reload.diagnostics)}
    };
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(output);
    } else {
        printDiagnostics(reload.diagnostics);
        for (const PackageDefinition& package : application.packages()) {
            QTextStream(stdout) << package.key() << "  " << package.name << Qt::endl;
        }
    }
    return initialized ? kSuccess : kPackageError;
}

int designCreate(const QStringList& arguments, FinepaperApplication& application) {
    const QString inputPath = optionValue(arguments, QStringLiteral("--input"));
    const QString outputPath = optionValue(arguments, QStringLiteral("--output"));
    if (inputPath.isEmpty() || outputPath.isEmpty()) {
        printUsage();
        return kInputError;
    }
    const JsonObjectLoadResult request = loadJsonObject(inputPath);
    if (!request.success) {
        printDiagnostics(request.diagnostics);
        if (hasOption(arguments, QStringLiteral("--json"))) {
            printJson(QJsonObject{
                {QStringLiteral("success"), false},
                {QStringLiteral("diagnostics"), diagnosticsToJson(request.diagnostics)}
            });
        }
        return kInputError;
    }
    DesignResult result = application.createDesign(request.object);
    bool outputWriteFailed = false;
    if (result.success &&
        !application.saveDesignFile(outputPath, result.design, &result.diagnostics)) {
        result.success = false;
        outputWriteFailed = true;
    }
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(designResultToJson(result));
    } else {
        printDiagnostics(result.diagnostics);
        if (result.success) {
            QTextStream(stdout) << "Created " << outputPath << Qt::endl;
        }
    }
    if (result.success) {
        return kSuccess;
    }
    if (outputWriteFailed) {
        return kIoError;
    }
    return failureExitCode(result.diagnostics, kDesignValidationError);
}

int designValidate(const QStringList& arguments, FinepaperApplication& application) {
    if (arguments.size() < 4) {
        printUsage();
        return kInputError;
    }
    const DesignResult loaded = application.loadDesignFile(arguments.at(3));
    ValidationResult result;
    if (!loaded.success) {
        result.diagnostics = loaded.diagnostics;
    } else {
        result = application.validate(loaded.design, true);
    }
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(validationResultToJson(result));
    } else {
        printDiagnostics(result.diagnostics);
        QTextStream(stdout) << (result.success ? "Design is valid" : "Design is invalid")
                            << Qt::endl;
    }
    if (result.success) {
        return kSuccess;
    }
    if (hasPackageResolutionFailure(result.diagnostics)) {
        return kPackageError;
    }
    const bool packageFailure = std::any_of(
        result.diagnostics.cbegin(),
        result.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.source == QStringLiteral("generator") ||
                   diagnostic.source == QStringLiteral("engine");
        });
    return packageFailure ? kPackageValidationError : kDesignValidationError;
}

int designGenerate(const QStringList& arguments, FinepaperApplication& application) {
    if (arguments.size() < 4) {
        printUsage();
        return kInputError;
    }
    const QString outputRoot = optionValue(arguments, QStringLiteral("--output"));
    if (outputRoot.isEmpty()) {
        printUsage();
        return kInputError;
    }
    const DesignResult loaded = application.loadDesignFile(arguments.at(3));
    GenerationResult result;
    if (!loaded.success) {
        result.diagnostics = loaded.diagnostics;
    } else {
        result = application.generate(loaded.design, GenerationOptions{outputRoot});
    }
    const QString resultPath = optionValue(arguments, QStringLiteral("--result"));
    bool resultWriteFailed = false;
    if (!resultPath.isEmpty() &&
        !saveJsonObject(resultPath, generationResultToJson(result), &result.diagnostics)) {
        result.success = false;
        resultWriteFailed = true;
    }
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(generationResultToJson(result));
    } else {
        printDiagnostics(result.diagnostics);
        if (result.success) {
            QTextStream(stdout) << "Generated artifacts in " << result.outputDirectory
                                << Qt::endl;
        }
    }
    if (resultWriteFailed) {
        return kIoError;
    }
    return result.success
        ? kSuccess
        : failureExitCode(result.diagnostics, kGenerationError);
}

int designCommand(const QStringList& arguments) {
    if (arguments.size() < 3) {
        printUsage();
        return kInputError;
    }
    FinepaperApplication application;
    QVector<Diagnostic> diagnostics;
    if (!initializeApplication(application, arguments, &diagnostics)) {
        printDiagnostics(diagnostics);
        if (hasOption(arguments, QStringLiteral("--json"))) {
            printJson(QJsonObject{
                {QStringLiteral("success"), false},
                {QStringLiteral("diagnostics"), diagnosticsToJson(diagnostics)}
            });
        }
        return kPackageError;
    }
    if (!diagnostics.isEmpty()) {
        printDiagnostics(diagnostics);
    }
    const QString action = arguments.at(2);
    if (action == QStringLiteral("create")) {
        return designCreate(arguments, application);
    }
    if (action == QStringLiteral("validate")) {
        return designValidate(arguments, application);
    }
    if (action == QStringLiteral("generate")) {
        return designGenerate(arguments, application);
    }
    printUsage();
    return kInputError;
}

int runCommand(const QStringList& arguments) {
    if (arguments.size() < 3) {
        printUsage();
        return kInputError;
    }
    const QString outputRoot = optionValue(arguments, QStringLiteral("--output"));
    if (outputRoot.isEmpty()) {
        printUsage();
        return kInputError;
    }
    FinepaperApplication application;
    QVector<Diagnostic> diagnostics;
    if (!initializeApplication(application, arguments, &diagnostics)) {
        printDiagnostics(diagnostics);
        return kPackageError;
    }
    if (!diagnostics.isEmpty()) {
        printDiagnostics(diagnostics);
    }
    const JsonObjectLoadResult request = loadJsonObject(arguments.at(2));
    if (!request.success) {
        printDiagnostics(request.diagnostics);
        return kInputError;
    }
    DesignResult design = application.createDesign(request.object);
    if (!design.success) {
        printDiagnostics(design.diagnostics);
        if (hasOption(arguments, QStringLiteral("--json"))) {
            printJson(designResultToJson(design));
        }
        return failureExitCode(design.diagnostics, kDesignValidationError);
    }
    const QString designOutput = optionValue(arguments, QStringLiteral("--design-output"));
    if (!designOutput.isEmpty() &&
        !application.saveDesignFile(designOutput, design.design, &design.diagnostics)) {
        printDiagnostics(design.diagnostics);
        return kIoError;
    }

    GenerationResult result = application.generate(design.design, GenerationOptions{outputRoot});
    const QString resultPath = optionValue(arguments, QStringLiteral("--result"));
    bool resultWriteFailed = false;
    if (!resultPath.isEmpty() &&
        !saveJsonObject(resultPath, generationResultToJson(result), &result.diagnostics)) {
        result.success = false;
        resultWriteFailed = true;
    }
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(generationResultToJson(result));
    } else {
        printDiagnostics(result.diagnostics);
        if (result.success) {
            QTextStream(stdout) << "Generated artifacts in " << result.outputDirectory
                                << Qt::endl;
        }
    }
    if (resultWriteFailed) {
        return kIoError;
    }
    return result.success
        ? kSuccess
        : failureExitCode(result.diagnostics, kGenerationError);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Finepaper"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper"));
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2 ||
        arguments.at(1) == QStringLiteral("--help") ||
        arguments.at(1) == QStringLiteral("-h")) {
        printUsage();
        return arguments.size() < 2 ? kInputError : kSuccess;
    }

    const QString command = arguments.at(1);
    if (command == QStringLiteral("package")) {
        return packageCommand(arguments);
    }
    if (command == QStringLiteral("design")) {
        return designCommand(arguments);
    }
    if (command == QStringLiteral("run")) {
        return runCommand(arguments);
    }
    printUsage();
    return kInputError;
}
