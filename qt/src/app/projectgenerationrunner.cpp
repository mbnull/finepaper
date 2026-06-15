// ProjectGenerationRunner implementation.
#include "app/projectgenerationrunner.h"

#include "app/generationartifacts.h"
#include "app/projectflowsupport.h"
#include "ipcraft/ipcraftbuiltinvalidator.h"
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"
#include "validation/validationresult.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

QString generationSchemaName() {
    return QStringLiteral("ipcraft.generation.manifest.v1");
}

QString defaultOutputRootForProject(const QString& projectPath) {
    return QFileInfo(projectPath).absoluteDir().filePath(QStringLiteral("generated"));
}

QString generationDesignName(const ProjectGenerationRequest& request) {
    const QString requested = request.designName.trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString fromProjectPath = QFileInfo(request.projectPath).completeBaseName().trimmed();
    return fromProjectPath.isEmpty() ? QStringLiteral("design") : fromProjectPath;
}

QString withInstanceContext(const ProjectIpInstanceRecord& instance, const QString& message) {
    return QStringLiteral("Instance '%1' (%2): %3")
        .arg(instance.instanceId, instance.ipcoreId, message);
}

bool isSafeInstanceOutputKey(const QString& instanceId) {
    const QString trimmed = instanceId.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

QString normalizedInstanceOutputKey(const QString& instanceId) {
    return instanceId.toCaseFolded();
}

QString flowRunRootForInstance(const QString& outputRoot, const QString& instanceId) {
    return QDir(outputRoot).filePath(QStringLiteral(".runs/%1").arg(instanceId));
}

QString canonicalOrAbsolutePath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool sameDirectoryPath(const QString& lhs, const QString& rhs) {
    return canonicalOrAbsolutePath(lhs) == canonicalOrAbsolutePath(rhs);
}

bool isReservedInstanceOutputKey(const QString& instanceId) {
    return normalizedInstanceOutputKey(instanceId)
        == QStringLiteral("project-snapshot.fpproj");
}

ProjectGenerationResult requestFailure(const QString& message) {
    ProjectGenerationResult result;
    result.error = message;
    result.errors.append(message);
    return result;
}

ProjectGenerationResult builtInValidationFailure(
    const IpcraftBuiltInValidator::Result& validation) {
    QStringList errors;
    for (const ValidationResult& diagnostic : validation.diagnostics) {
        if (diagnostic.severity() == ValidationSeverity::Error) {
            errors.append(diagnostic.message());
        }
    }

    return requestFailure(QStringLiteral("Built-in validation failed:\n%1")
                              .arg(errors.join(QStringLiteral("\n"))));
}

const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId) {
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const IpCatalogEntry& entry) {
        return entry.id == ipcoreId;
    });
    return it == entries.cend() ? nullptr : &(*it);
}

QString writeJsonFile(const QString& path, const QJsonDocument& document) {
    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
        return QStringLiteral("Could not create directory: %1").arg(fileInfo.absolutePath());
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("Could not open file for writing: %1").arg(path);
    }
    const QByteArray content = document.toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size()) {
        return QStringLiteral("Could not write file: %1").arg(path);
    }
    if (!file.commit()) {
        return QStringLiteral("Could not commit file: %1").arg(path);
    }
    return {};
}

QStringList generatedFiles(const QString& outputDirectory) {
    QStringList files;
    const QDir base(outputDirectory);
    QDirIterator iterator(outputDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString relativePath = base.relativeFilePath(path);
        if (relativePath == QStringLiteral("generation-manifest.json")
            || relativePath == QStringLiteral("stdout.log")
            || relativePath == QStringLiteral("stderr.log")
            || relativePath.startsWith(QStringLiteral("inputs/"))) {
            continue;
        }
        files.append(relativePath);
    }
    files.sort();
    return files;
}

QJsonArray stringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

void appendUniquePath(QStringList& paths, const QString& path) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return;
    }
    const QString absolutePath = QFileInfo(trimmedPath).absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

bool isSourceTreeWithFrameworkTool(const QDir& dir) {
    return QFileInfo(dir.filePath(QStringLiteral("qt/xmake.lua"))).isFile()
           && QFileInfo(dir.filePath(QStringLiteral("ipcraft_generator/bin/ipcraft-generate"))).isFile();
}

void appendSourceTreeFrameworkToolPath(QStringList& paths, const QDir& applicationDir) {
    QDir candidate = applicationDir;
    while (true) {
        if (isSourceTreeWithFrameworkTool(candidate)) {
            appendUniquePath(paths, candidate.filePath(QStringLiteral("ipcraft_generator/bin")));
            return;
        }
        if (!candidate.cdUp()) {
            return;
        }
    }
}

QJsonDocument manifestDocument(const ProjectGenerationRequest& request,
                               const QString& designName,
                               const IpCatalogEntry& entry,
                               const ProjectGenerationInstanceResult& result) {
    QJsonObject project;
    project.insert(QStringLiteral("path"), request.projectPath);
    project.insert(QStringLiteral("name"), designName);

    QJsonObject ipcore;
    ipcore.insert(QStringLiteral("id"), entry.id);
    ipcore.insert(QStringLiteral("name"), entry.name);
    ipcore.insert(QStringLiteral("version"), entry.version);
    ipcore.insert(QStringLiteral("kind"), entry.kind);

    QJsonObject instance;
    instance.insert(QStringLiteral("id"), result.instance.instanceId);
    instance.insert(QStringLiteral("ipcore"), result.instance.ipcoreId);
    instance.insert(QStringLiteral("schema"), result.instance.schema);
    instance.insert(QStringLiteral("state"), result.instance.state);

    QJsonObject input;
    input.insert(QStringLiteral("path"), result.inputPath);
    input.insert(QStringLiteral("schema"), result.inputSchema);

    QJsonObject output;
    output.insert(QStringLiteral("directory"), result.outputDirectory);

    QJsonObject process;
    process.insert(QStringLiteral("flow"), QStringLiteral("generate"));
    process.insert(QStringLiteral("exit_code"), result.exitCode);
    process.insert(QStringLiteral("exit_status"), result.exitStatus);
    process.insert(QStringLiteral("stdout"), result.standardOutput);
    process.insert(QStringLiteral("stderr"), result.standardError);

    QJsonObject artifacts;
    artifacts.insert(QStringLiteral("emitted_inputs"), result.inputPath);
    artifacts.insert(QStringLiteral("manifest"), result.manifestPath);
    artifacts.insert(QStringLiteral("files"), stringArray(result.artifactPaths));

    QJsonObject root;
    root.insert(QStringLiteral("schema"), generationSchemaName());
    root.insert(QStringLiteral("success"), result.success);
    root.insert(QStringLiteral("error"), result.error);
    root.insert(QStringLiteral("project"), project);
    root.insert(QStringLiteral("ipcore"), ipcore);
    root.insert(QStringLiteral("instance"), instance);
    root.insert(QStringLiteral("input"), input);
    root.insert(QStringLiteral("output"), output);
    root.insert(QStringLiteral("process"), process);
    root.insert(QStringLiteral("artifacts"), artifacts);
    return QJsonDocument(root);
}

QString writeManifest(const ProjectGenerationRequest& request,
                      const QString& designName,
                      const IpCatalogEntry& entry,
                      const ProjectGenerationInstanceResult& result) {
    return writeJsonFile(result.manifestPath,
                         manifestDocument(request, designName, entry, result));
}

struct PackageFlowContext {
    bool ok = false;
    ipcraft::PackageSpec package;
    QString packageRoot;
    QString error;
};

PackageFlowContext packageFlowContextForEntry(const IpCatalogEntry& entry) {
    PackageFlowContext context;
    const QString packageRoot = !entry.packageManifest.packageRootPath.trimmed().isEmpty()
        ? entry.packageManifest.packageRootPath
        : entry.runtimeRootPath;
    if (packageRoot.trimmed().isEmpty()) {
        context.error = QStringLiteral("Package root is not available.");
        return context;
    }

    const ipcraft::PackageSpecReadResult specResult =
        ipcraft::PackageSpecReader().readPackageRoot(packageRoot);
    if (!specResult.ok) {
        QStringList messages;
        for (const ipcraft::Diagnostic& diagnostic : specResult.diagnostics.records) {
            messages.append(diagnostic.message);
        }
        context.error = messages.isEmpty()
            ? QStringLiteral("Package spec could not be read.")
            : messages.join(QStringLiteral("\n"));
        return context;
    }

    const bool hasGenerateFlow =
        std::any_of(specResult.spec.flows.constBegin(),
                    specResult.spec.flows.constEnd(),
                    [](const QJsonValue& flowValue) {
                        return flowValue.isObject() &&
                               flowValue.toObject().value(QStringLiteral("id")).toString()
                                   == QStringLiteral("generate");
                    });
    if (!hasGenerateFlow) {
        context.error = QStringLiteral("Package does not declare a generate flow.");
        return context;
    }

    context.ok = true;
    context.package = specResult.spec;
    context.packageRoot = packageRoot;
    return context;
}

QString diagnosticMessageForFlowFailure(const ipcraft::FlowRunResult& flowResult) {
    for (const ipcraft::Diagnostic& diagnostic : flowResult.diagnostics.records) {
        if (diagnostic.ruleId == QStringLiteral("flow.timeout")) {
            const int elapsedMilliseconds =
                diagnostic.details.value(QStringLiteral("timeout_ms")).toInt();
            return elapsedMilliseconds > 0
                ? QStringLiteral("Generator timed out after %1 ms.").arg(elapsedMilliseconds)
                : QStringLiteral("Generator timed out.");
        }
        if (diagnostic.ruleId == QStringLiteral("flow.exec_failed")) {
            const int exitCode = diagnostic.details.value(QStringLiteral("exit_code")).toInt(-1);
            return QStringLiteral("Generator failed (exit code %1).").arg(exitCode);
        }
        if (diagnostic.ruleId == QStringLiteral("flow.executable_missing")) {
            return QStringLiteral("Failed to start IP core generator.");
        }
    }
    if (!flowResult.diagnostics.records.isEmpty()) {
        return flowResult.diagnostics.records.first().message;
    }
    return QStringLiteral("Generator flow failed.");
}

int exitCodeForFlowResult(const ipcraft::FlowRunResult& flowResult) {
    for (const ipcraft::Diagnostic& diagnostic : flowResult.diagnostics.records) {
        if (diagnostic.ruleId == QStringLiteral("flow.exec_failed")) {
            return diagnostic.details.value(QStringLiteral("exit_code")).toInt(-1);
        }
    }
    return flowResult.ok ? 0 : -1;
}

QString readTextFileIfPresent(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString copyFileReplacing(const QString& sourcePath, const QString& destinationPath) {
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile()) {
        return QStringLiteral("Source file is missing: %1").arg(sourcePath);
    }

    const QFileInfo destinationInfo(destinationPath);
    if (!destinationInfo.absoluteDir().exists() &&
        !QDir().mkpath(destinationInfo.absolutePath())) {
        return QStringLiteral("Could not create directory: %1")
            .arg(destinationInfo.absolutePath());
    }

    const QString sourceAbsolute = sourceInfo.absoluteFilePath();
    const QString destinationAbsolute = destinationInfo.absoluteFilePath();
    if (sourceAbsolute == destinationAbsolute) {
        return {};
    }

    if (destinationInfo.exists()) {
        if (!destinationInfo.isFile()) {
            return QStringLiteral("Destination is not a file: %1").arg(destinationPath);
        }
        if (!QFile::remove(destinationPath)) {
            return QStringLiteral("Could not replace file: %1").arg(destinationPath);
        }
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        return QStringLiteral("Could not copy file from %1 to %2")
            .arg(sourcePath, destinationPath);
    }
    return {};
}

QString copyDirectoryReplacing(const QString& sourceDirectoryPath,
                               const QString& destinationDirectoryPath) {
    const QFileInfo sourceInfo(sourceDirectoryPath);
    if (!sourceInfo.isDir()) {
        return QStringLiteral("Source directory is missing: %1").arg(sourceDirectoryPath);
    }

    QDir destinationDirectory(destinationDirectoryPath);
    if (destinationDirectory.exists() && !destinationDirectory.removeRecursively()) {
        return QStringLiteral("Could not replace directory: %1")
            .arg(destinationDirectoryPath);
    }
    if (!QDir().mkpath(destinationDirectoryPath)) {
        return QStringLiteral("Could not create directory: %1").arg(destinationDirectoryPath);
    }

    const QDir sourceDirectory(sourceDirectoryPath);
    QDirIterator iterator(sourceDirectoryPath, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceFilePath = iterator.next();
        const QString relativePath = sourceDirectory.relativeFilePath(sourceFilePath);
        const QString destinationFilePath =
            QDir(destinationDirectoryPath).filePath(relativePath);
        const QString copyError = copyFileReplacing(sourceFilePath, destinationFilePath);
        if (!copyError.isEmpty()) {
            return copyError;
        }
    }
    return {};
}

QString materializeEmittedInputs(const QString& flowRunRoot,
                                 const QString& publicInputPath) {
    const QString runInputDirectory = QDir(flowRunRoot).filePath(QStringLiteral("inputs"));
    const QString runInputPath = QDir(runInputDirectory).filePath(QStringLiteral("manifest.json"));
    if (QFileInfo(runInputDirectory).isDir()) {
        if (!QFileInfo(runInputPath).isFile()) {
            return QStringLiteral("Emitted inputs manifest was not produced.");
        }
        return copyDirectoryReplacing(runInputDirectory, QFileInfo(publicInputPath).absolutePath());
    }
    if (QFileInfo(publicInputPath).isFile()) {
        return {};
    }
    return QStringLiteral("Emitted inputs manifest was not produced.");
}

QString validateEmittedInputsManifest(const QString& manifestPath) {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Emitted inputs manifest could not be opened: %1")
            .arg(manifestPath);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QStringLiteral("Emitted inputs manifest is not valid JSON: %1")
            .arg(parseError.errorString());
    }
    if (!document.isObject()) {
        return QStringLiteral("Emitted inputs manifest root must be an object.");
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() != ipcraft::schemaids::emittedInputsV1) {
        return QStringLiteral("Emitted inputs manifest schema must be %1.")
            .arg(ipcraft::schemaids::emittedInputsV1);
    }
    return {};
}

const GenerationFlowProvider* generationFlowProviderFor(
    const std::vector<std::unique_ptr<GenerationFlowProvider>>& providers,
    const GenerationFlowRequest& request) {
    for (auto it = providers.crbegin(); it != providers.crend(); ++it) {
        if (*it && (*it)->canRun(request)) {
            return it->get();
        }
    }
    return nullptr;
}

ProjectGenerationInstanceResult generateInstance(const ProjectGenerationRequest& request,
                                                 const QString& outputRoot,
                                                 const QString& designName,
                                                 const ProjectIpInstanceRecord& instance,
                                                 const QStringList& frameworkToolSearchPaths,
                                                 const std::vector<std::unique_ptr<GenerationFlowProvider>>&
                                                     generationFlowProviders) {
    ProjectGenerationInstanceResult result;
    result.instance = instance;
    result.ipcoreId = instance.ipcoreId;
    result.instanceId = instance.instanceId;
    result.outputDirectory = QDir(outputRoot).filePath(instance.instanceId);
    result.inputPath =
        QDir(result.outputDirectory).filePath(QStringLiteral("inputs/manifest.json"));
    result.inputSchema = ipcraft::schemaids::emittedInputsV1;
    result.manifestPath = QDir(result.outputDirectory).filePath(QStringLiteral("generation-manifest.json"));

    if (!isSafeInstanceOutputKey(instance.instanceId)) {
        result.error = withInstanceContext(
            instance,
            QStringLiteral("Unsafe IP instance id for generation output: %1").arg(instance.instanceId));
        return result;
    }
    if (isReservedInstanceOutputKey(instance.instanceId)) {
        result.error = withInstanceContext(
            instance,
            QStringLiteral("Reserved IP instance output id: %1").arg(instance.instanceId));
        return result;
    }

    QDir outputDirectory(result.outputDirectory);
    if (outputDirectory.exists() && !outputDirectory.removeRecursively()) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("Could not clear output directory: %1")
                                               .arg(result.outputDirectory));
        return result;
    }
    if (!QDir().mkpath(result.outputDirectory)) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("Could not create output directory: %1")
                                               .arg(result.outputDirectory));
        return result;
    }

    const QString flowRunRoot = flowRunRootForInstance(outputRoot, instance.instanceId);
    QDir flowRunDirectory(flowRunRoot);
    if (flowRunDirectory.exists() && !flowRunDirectory.removeRecursively()) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("Could not clear flow run directory: %1")
                                               .arg(flowRunRoot));
        return result;
    }
    if (!QDir().mkpath(flowRunRoot)) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("Could not create flow run directory: %1")
                                               .arg(flowRunRoot));
        return result;
    }

    const IpCatalogEntry* entry = findCatalogEntry(request.catalogEntries, instance.ipcoreId);
    if (!entry) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("IP core '%1' is not available in the catalog.")
                                               .arg(instance.ipcoreId));
        return result;
    }

    const PackageFlowContext flowContext = packageFlowContextForEntry(*entry);
    if (!flowContext.ok) {
        result.error = withInstanceContext(instance, flowContext.error);
        result.artifactPaths = generatedFiles(result.outputDirectory);
        const QString manifestError = writeManifest(request, designName, *entry, result);
        if (!manifestError.isEmpty()) {
            result.error += QStringLiteral(" Manifest error: %1").arg(manifestError);
        }
        return result;
    }

    ipcraft::FlowRunRequest flowRequest;
    flowRequest.projectId = designName;
    flowRequest.instanceId = instance.instanceId;
    flowRequest.flowId = QStringLiteral("generate");
    flowRequest.runId = instance.instanceId;
    flowRequest.runRoot = flowRunRoot;
    flowRequest.outputRoot = result.outputDirectory;
    flowRequest.packageRoot = flowContext.packageRoot;
    flowRequest.package = flowContext.package;
    flowRequest.config = ipcraft::ConfigBundle::fromJson(instance.config);
    flowRequest.graphConfig = ProjectFlowSupport::graphConfigForInstance(instance);
    flowRequest.frameworkToolSearchPaths = frameworkToolSearchPaths;

    const GenerationFlowRequest generationFlowRequest{flowRequest, result.outputDirectory};
    const GenerationFlowProvider* flowProvider =
        generationFlowProviderFor(generationFlowProviders, generationFlowRequest);
    if (!flowProvider) {
        result.error = withInstanceContext(instance,
                                           QStringLiteral("No generation flow provider is available."));
        result.artifactPaths = generatedFiles(result.outputDirectory);
        const QString manifestError = writeManifest(request, designName, *entry, result);
        if (!manifestError.isEmpty()) {
            result.error += QStringLiteral(" Manifest error: %1").arg(manifestError);
        }
        return result;
    }

    const ipcraft::FlowRunResult flowResult = flowProvider->run(generationFlowRequest);
    if (!sameDirectoryPath(flowResult.runRoot, flowRunRoot)) {
        result.exitCode = exitCodeForFlowResult(flowResult);
        result.exitStatus = QStringLiteral("failed");
        result.error = withInstanceContext(
            instance,
            QStringLiteral("Generation flow returned an unexpected run root."));
        result.artifactPaths = generatedFiles(result.outputDirectory);
        const QString manifestError = writeManifest(request, designName, *entry, result);
        if (!manifestError.isEmpty()) {
            result.error += QStringLiteral(" Manifest error: %1").arg(manifestError);
        }
        return result;
    }
    result.standardOutput = readTextFileIfPresent(QDir(flowResult.runRoot).filePath(QStringLiteral("stdout.log")));
    result.standardError = readTextFileIfPresent(QDir(flowResult.runRoot).filePath(QStringLiteral("stderr.log")));
    result.exitCode = exitCodeForFlowResult(flowResult);
    result.exitStatus = flowResult.ok ? QStringLiteral("normal") : QStringLiteral("failed");

    if (!flowResult.ok) {
        result.error = withInstanceContext(instance, diagnosticMessageForFlowFailure(flowResult));
    } else {
        result.success = true;
    }

    const QString inputManifestError =
        materializeEmittedInputs(flowResult.runRoot, result.inputPath);
    if (!inputManifestError.isEmpty()) {
        result.success = false;
        const QString contextualInputError =
            withInstanceContext(instance, inputManifestError);
        result.error = result.error.isEmpty()
            ? contextualInputError
            : result.error + QStringLiteral(" ") + contextualInputError;
    } else {
        const QString validationError = validateEmittedInputsManifest(result.inputPath);
        if (!validationError.isEmpty()) {
            result.success = false;
            const QString contextualInputError =
                withInstanceContext(instance, validationError);
            result.error = result.error.isEmpty()
                ? contextualInputError
                : result.error + QStringLiteral(" ") + contextualInputError;
        }
    }

    result.artifactPaths = generatedFiles(result.outputDirectory);
    const QString manifestError = writeManifest(request, designName, *entry, result);
    if (!manifestError.isEmpty()) {
        result.success = false;
        const QString contextualManifestError = withInstanceContext(instance, manifestError);
        result.error = result.error.isEmpty()
            ? contextualManifestError
            : result.error + QStringLiteral(" ") + contextualManifestError;
    }

    return result;
}

} // namespace

ProjectGenerationRunner::ProjectGenerationRunner()
    : m_frameworkToolSearchPaths(defaultFrameworkToolSearchPaths()) {
    addGenerationFlowProvider(std::make_unique<PackageGenerationFlowProvider>());
}

ProjectGenerationRunner::ProjectGenerationRunner(QStringList frameworkToolSearchPaths)
    : m_frameworkToolSearchPaths(std::move(frameworkToolSearchPaths)) {
    addGenerationFlowProvider(std::make_unique<PackageGenerationFlowProvider>());
}

ProjectGenerationRunner::~ProjectGenerationRunner() = default;

QStringList ProjectGenerationRunner::defaultFrameworkToolSearchPaths() {
    QStringList paths;

    const QDir application(QCoreApplication::applicationDirPath());
    appendSourceTreeFrameworkToolPath(paths, application);
    appendUniquePath(paths, application.filePath(QStringLiteral("ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../../ipcraft_generator/bin")));

    appendUniquePath(paths, QStringLiteral("/usr/local/libexec/finepaper"));
    appendUniquePath(paths, QStringLiteral("/usr/local/bin"));
    return paths;
}

QStringList ProjectGenerationRunner::frameworkToolSearchPaths() const {
    return m_frameworkToolSearchPaths;
}

void ProjectGenerationRunner::setFrameworkToolSearchPaths(QStringList searchPaths) {
    m_frameworkToolSearchPaths = std::move(searchPaths);
}

void ProjectGenerationRunner::addGenerationFlowProvider(
    std::unique_ptr<GenerationFlowProvider> provider) {
    constexpr std::size_t kMaxGenerationFlowProviders = 1024;
    if (provider && m_generationFlowProviders.size() < kMaxGenerationFlowProviders) {
        m_generationFlowProviders.push_back(std::move(provider));
    }
}

ProjectGenerationResult ProjectGenerationRunner::generate(const ProjectGenerationRequest& request) const {
    IpcraftBuiltInValidator builtInValidator;
    const IpcraftBuiltInValidator::Result builtInResult =
        builtInValidator.validate(nullptr,
                                  request.catalogEntries,
                                  request.instances,
                                  IpcraftBuiltInValidator::CommandPurpose::Generate);
    if (builtInResult.hasErrors()) {
        return builtInValidationFailure(builtInResult);
    }

    if (request.projectPath.trimmed().isEmpty()) {
        return requestFailure(QStringLiteral("Save the project before generation."));
    }

    QSet<QString> outputKeys;
    for (const ProjectIpInstanceRecord& instance : request.instances) {
        if (!isSafeInstanceOutputKey(instance.instanceId)) {
            return requestFailure(QStringLiteral("Unsafe IP instance id for generation output: %1")
                                      .arg(instance.instanceId));
        }
        if (isReservedInstanceOutputKey(instance.instanceId)) {
            return requestFailure(QStringLiteral("Reserved IP instance output id: %1")
                                      .arg(instance.instanceId));
        }

        const QString outputKey = normalizedInstanceOutputKey(instance.instanceId);
        if (outputKeys.contains(outputKey)) {
            return requestFailure(QStringLiteral("Duplicate IP instance output id: %1")
                                      .arg(instance.instanceId));
        }
        outputKeys.insert(outputKey);
    }

    ProjectGenerationResult result;
    result.outputRoot = request.outputRoot.trimmed().isEmpty()
        ? defaultOutputRootForProject(request.projectPath)
        : request.outputRoot;

    if (!QDir().mkpath(result.outputRoot)) {
        const QString error =
            QStringLiteral("Could not create generation output root: %1").arg(result.outputRoot);
        return requestFailure(error);
    }

    const QString designName = generationDesignName(request);
    bool allInstancesSucceeded = true;
    for (const ProjectIpInstanceRecord& instance : request.instances) {
        ProjectGenerationInstanceResult instanceResult =
            generateInstance(request,
                             result.outputRoot,
                             designName,
                             instance,
                             m_frameworkToolSearchPaths,
                             m_generationFlowProviders);
        if (!instanceResult.success) {
            allInstancesSucceeded = false;
            result.errors.append(instanceResult.error);
        }
        result.instances.append(instanceResult);
    }

    const GeneratedProjectSnapshotResult snapshot =
        writeGeneratedProjectSnapshotInOutputRoot(request.projectDesign,
                                                  result.outputRoot,
                                                  designName,
                                                  request.instances);
    result.snapshotPath = snapshot.path;
    if (!snapshot.success) {
        allInstancesSucceeded = false;
        result.errors.append(QStringLiteral("Project snapshot: %1").arg(snapshot.error));
    }

    result.success = allInstancesSucceeded;
    result.error = result.errors.join(QStringLiteral("\n"));
    return result;
}
