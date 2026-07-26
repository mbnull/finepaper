#include "application/application.h"
#include "application/runtime_settings.h"
#include "package/package.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTextStream>

#include <algorithm>
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
    return resolveRuntimeLocations(optionValues(arguments, QStringLiteral("--package-root"))).packageRoots;
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
        << "  finepaper package check PACKAGE_PATH [--json]\n"
        << "  finepaper design create --input REQUEST --output DESIGN [--package-root PATH] [--json]\n"
        << "  finepaper design validate DESIGN [--package-root PATH] [--json]\n"
        << "  finepaper design generate DESIGN --output ROOT [--result FILE] [--package-root PATH] [--json]\n"
        << "  finepaper run REQUEST --output ROOT [--result FILE] [--design-output FILE] [--package-root PATH] [--json]\n";
}

bool initializeApplication(FinepaperApplication& application,
                           const QStringList& arguments,
                           QVector<Diagnostic>* diagnostics) {
    *diagnostics = application.reloadPackages(packageRoots(arguments));
    return !hasErrors(*diagnostics);
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
        const PackageLoadResult result = loadPackage(arguments.at(3));
        const QJsonObject output{
            {QStringLiteral("success"), result.success},
            {QStringLiteral("package"),
             result.package ? packageToJson(*result.package) : QJsonObject()},
            {QStringLiteral("diagnostics"), diagnosticsToJson(result.diagnostics)}
        };
        if (hasOption(arguments, QStringLiteral("--json"))) {
            printJson(output);
        } else {
            printDiagnostics(result.diagnostics);
            QTextStream(stdout) << (result.success ? "Package is valid" : "Package is invalid")
                                << Qt::endl;
        }
        return result.success ? kSuccess : kPackageError;
    }
    if (action != QStringLiteral("list")) {
        printUsage();
        return kInputError;
    }

    FinepaperApplication application;
    QVector<Diagnostic> diagnostics;
    const bool initialized = initializeApplication(application, arguments, &diagnostics);
    QJsonArray packages;
    for (const PackageDefinition& package : application.packages()) {
        packages.append(packageToJson(package));
    }
    const QJsonObject output{
        {QStringLiteral("success"), initialized},
        {QStringLiteral("packages"), packages},
        {QStringLiteral("diagnostics"), diagnosticsToJson(diagnostics)}
    };
    if (hasOption(arguments, QStringLiteral("--json"))) {
        printJson(output);
    } else {
        printDiagnostics(diagnostics);
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
    if (result.success &&
        !application.saveDesignFile(outputPath, result.design, &result.diagnostics)) {
        result.success = false;
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
    return hasErrors(result.diagnostics) ? kDesignValidationError : kIoError;
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
    const QJsonObject output = generationResultToJson(result);
    const QString resultPath = optionValue(arguments, QStringLiteral("--result"));
    if (!resultPath.isEmpty()) {
        saveJsonObject(resultPath, output, &result.diagnostics);
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
    return result.success ? kSuccess : kGenerationError;
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
        return kDesignValidationError;
    }
    const QString designOutput = optionValue(arguments, QStringLiteral("--design-output"));
    if (!designOutput.isEmpty() &&
        !application.saveDesignFile(designOutput, design.design, &design.diagnostics)) {
        printDiagnostics(design.diagnostics);
        return kIoError;
    }

    GenerationResult result = application.generate(design.design, GenerationOptions{outputRoot});
    const QJsonObject output = generationResultToJson(result);
    const QString resultPath = optionValue(arguments, QStringLiteral("--result"));
    if (!resultPath.isEmpty()) {
        saveJsonObject(resultPath, output, &result.diagnostics);
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
    return result.success ? kSuccess : kGenerationError;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
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
