// ProjectGenerationRunner tests project-level generation across all IP instances.
#include "app/projectgenerationrunner.h"

#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcore/ipcatalogservice.h"
#include "project/projectreader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

QString readText(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "failed to read test file");
    return QString::fromUtf8(file.readAll());
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "failed to read JSON file");
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    require(document.isObject(), "JSON file should contain an object");
    return document.object();
}

bool usesCommandInputFileName(const QString& path) {
    return path.contains(QStringLiteral("command-input")) ||
           path.contains(QStringLiteral("ipcraft-input"));
}

QString commandInputPath(const QString& outputDirectory) {
    return QDir(outputDirectory).filePath(QStringLiteral("command-input.json"));
}

void makeExecutable(const QString& path) {
    QFile file(path);
    require(file.setPermissions(QFile::ReadOwner |
                                QFile::WriteOwner |
                                QFile::ExeOwner |
                                QFile::ReadGroup |
                                QFile::ExeGroup |
                                QFile::ReadOther |
                                QFile::ExeOther),
            "failed to make test generator executable");
}

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   const QString& ipcoreId,
                                   const QString& instanceId,
                                   const QString& artifactId) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    module->setParameter(QStringLiteral("external_id"), artifactId);
    module->addPort(Port(QStringLiteral("out"),
                         Port::Direction::Output,
                         QStringLiteral("bus"),
                         QStringLiteral("Out")));
    return module;
}

ProjectIpInstanceRecord instanceRecord(const QString& ipcoreId,
                                       const QString& instanceId,
                                       const QString& marker) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.schema = ipcoreId + QStringLiteral("-state-v1");
    record.state = QJsonObject{
        {QStringLiteral("marker"), marker}
    };
    return record;
}

IpCatalogEntry catalogEntry(const QString& ipcoreId, const QString& generatorPath) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.sourceRootPath = QFileInfo(generatorPath).absolutePath();
    entry.generator.command = generatorPath;
    entry.generator.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.generator.args = {
        QStringLiteral("{input}"),
        QStringLiteral("{output}")
    };
    return entry;
}

IpcraftInterfaceDescriptor manifestInterface(const QString& id) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.label = id;
    descriptor.modes = {QStringLiteral("initiator")};
    descriptor.ipxactBusInterface = id;
    return descriptor;
}

IpcraftModuleDescriptor manifestModule(const QString& id) {
    IpcraftModuleDescriptor descriptor;
    descriptor.id = id;
    descriptor.name = id;
    descriptor.interfaces = {manifestInterface(QStringLiteral("out"))};
    return descriptor;
}

IpcraftCommandDescriptor manifestCommand(const QString& name, const QString& executablePath) {
    IpcraftCommandDescriptor command;
    command.name = name;
    command.executablePath = executablePath;
    command.resolvedExecutablePath = executablePath;
    command.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    command.args = {
        QStringLiteral("{input}"),
        QStringLiteral("{output}")
    };
    return command;
}

IpCatalogEntry ipcraftCatalogEntry(const QString& ipcoreId, const QString& generatorPath) {
    IpCatalogEntry entry = catalogEntry(ipcoreId, generatorPath);
    entry.packageId = ipcoreId;
    entry.moduleTypes = {QStringLiteral("Tile")};
    entry.packageManifest.schema = QStringLiteral("ipcraft.manifest.v1");
    entry.packageManifest.id = ipcoreId;
    entry.packageManifest.name = ipcoreId;
    entry.packageManifest.version = QStringLiteral("1.0");
    entry.packageManifest.packageRootPath = QFileInfo(generatorPath).absolutePath();
    entry.packageManifest.modules = {manifestModule(QStringLiteral("Tile"))};
    entry.packageManifest.commands.insert(QStringLiteral("generate"),
                                          manifestCommand(QStringLiteral("generate"), generatorPath));
    entry.generator.inputFormat = QStringLiteral("ipcraft.noc.project.v1");
    return entry;
}

QString createCopyingGenerator(const QDir& dir) {
    const QString path = dir.filePath(QStringLiteral("copy-generator.sh"));
    writeFile(path,
              QByteArrayLiteral("#!/bin/sh\n"
                                "set -eu\n"
                                "input=\"$1\"\n"
                                "output=\"$2\"\n"
                                "mkdir -p \"$output\"\n"
                                "cp \"$input\" \"$output/artifact.sv\"\n"
                                "printf '\\n// generated by test generator\\n' >> \"$output/artifact.sv\"\n"));
    makeExecutable(path);
    return path;
}

QString createFailingGenerator(const QDir& dir) {
    const QString path = dir.filePath(QStringLiteral("failing-generator.sh"));
    writeFile(path,
              QByteArrayLiteral("#!/bin/sh\n"
                                "echo 'generator failed intentionally' >&2\n"
                                "exit 7\n"));
    makeExecutable(path);
    return path;
}

QString createSlowGenerator(const QDir& dir) {
    const QString path = dir.filePath(QStringLiteral("slow-generator.sh"));
    writeFile(path,
              QByteArrayLiteral("#!/bin/sh\n"
                                "sleep 5\n"
                                "exit 0\n"));
    makeExecutable(path);
    return path;
}

void testGeneratesEveryProjectInstanceIntoSeparateOutputDirectories() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("alpha_runtime"),
                                       QStringLiteral("AlphaTile"),
                                       QStringLiteral("finepaper.alpha"),
                                       QStringLiteral("alpha_0"),
                                       QStringLiteral("alpha_tile"))),
            "alpha module should add");
    require(graph.addModule(makeModule(QStringLiteral("beta_runtime"),
                                       QStringLiteral("BetaTile"),
                                       QStringLiteral("finepaper.beta"),
                                       QStringLiteral("beta_0"),
                                       QStringLiteral("beta_tile"))),
            "beta module should add");

    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("alpha_0"),
                       QStringLiteral("alpha-marker")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("beta_0"),
                       QStringLiteral("beta-marker"))
    };

    const QString projectPath = root.filePath(QStringLiteral("project/demo_design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.designName = QStringLiteral("demo_design");
    request.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath),
        catalogEntry(QStringLiteral("finepaper.beta"), generatorPath)
    };
    request.instances = instances;

    ProjectGenerationRunner runner;
    const ProjectGenerationResult result = runner.generate(request);
    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 2, "both project instances should be generated");

    const QDir generatedDir(QFileInfo(projectPath).absoluteDir().filePath(QStringLiteral("generated")));
    const QString alphaOutput = generatedDir.filePath(QStringLiteral("alpha_0"));
    const QString betaOutput = generatedDir.filePath(QStringLiteral("beta_0"));
    require(QFileInfo::exists(QDir(alphaOutput).filePath(QStringLiteral("artifact.sv"))),
            "alpha artifact should be written under generated/alpha_0");
    require(QFileInfo::exists(QDir(betaOutput).filePath(QStringLiteral("artifact.sv"))),
            "beta artifact should be written under generated/beta_0");
    require(QFileInfo::exists(commandInputPath(alphaOutput)),
            "alpha command input JSON should exist");
    require(QFileInfo::exists(commandInputPath(betaOutput)),
            "beta command input JSON should exist");
    require(QFileInfo::exists(QDir(alphaOutput).filePath(QStringLiteral("generation-manifest.json"))),
            "alpha generation manifest should exist");
    require(QFileInfo::exists(QDir(betaOutput).filePath(QStringLiteral("generation-manifest.json"))),
            "beta generation manifest should exist");
    require(QFileInfo::exists(generatedDir.filePath(QStringLiteral("project-snapshot.fpproj"))),
            "project generation snapshot should exist");

    const QString alphaArtifact = readText(QDir(alphaOutput).filePath(QStringLiteral("artifact.sv")));
    const QString betaArtifact = readText(QDir(betaOutput).filePath(QStringLiteral("artifact.sv")));
    require(alphaArtifact.contains(QStringLiteral("alpha_0")),
            "alpha artifact should contain alpha instance input");
    require(alphaArtifact.contains(QStringLiteral("AlphaTile")),
            "alpha artifact should contain alpha module input");
    require(!alphaArtifact.contains(QStringLiteral("beta_0")) &&
                !alphaArtifact.contains(QStringLiteral("BetaTile")),
            "alpha artifact should not include beta instance or module input");
    require(betaArtifact.contains(QStringLiteral("beta_0")),
            "beta artifact should contain beta instance input");
    require(betaArtifact.contains(QStringLiteral("BetaTile")),
            "beta artifact should contain beta module input");
    require(!betaArtifact.contains(QStringLiteral("alpha_0")) &&
                !betaArtifact.contains(QStringLiteral("AlphaTile")),
            "beta artifact should not include alpha instance or module input");

    const ProjectReadResult snapshot =
        ProjectReader::readFile(generatedDir.filePath(QStringLiteral("project-snapshot.fpproj")));
    require(snapshot.success, snapshot.error.toLocal8Bit().constData());
    require(snapshot.document.ipcoreState.size() == 2,
            "project snapshot should include all generation-time IP instance state");
}

void testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    Graph graph;

    ProjectGenerationRequest unsafeRequest;
    unsafeRequest.graph = &graph;
    unsafeRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    unsafeRequest.catalogEntries = {catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath)};
    unsafeRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("../escape"),
                       QStringLiteral("unsafe-marker"))
    };

    const ProjectGenerationResult unsafeResult = ProjectGenerationRunner().generate(unsafeRequest);
    require(!unsafeResult.success, "unsafe instance id should fail generation");
    require(unsafeResult.error.contains(QStringLiteral("Unsafe IP instance id")),
            "unsafe instance id error should be explicit");
    require(!QFileInfo::exists(root.filePath(QStringLiteral("escape"))),
            "unsafe instance id should not create output outside generated root");

    ProjectGenerationRequest duplicateRequest;
    duplicateRequest.graph = &graph;
    duplicateRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    duplicateRequest.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath),
        catalogEntry(QStringLiteral("finepaper.beta"), generatorPath)
    };
    duplicateRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("shared_0"),
                       QStringLiteral("alpha-marker")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("shared_0"),
                       QStringLiteral("beta-marker"))
    };

    const ProjectGenerationResult duplicateResult = ProjectGenerationRunner().generate(duplicateRequest);
    require(!duplicateResult.success, "duplicate instance output keys should fail generation");
    require(duplicateResult.error.contains(QStringLiteral("Duplicate IP instance output id")),
            "duplicate instance output key error should be explicit");

    ProjectGenerationRequest caseFoldedDuplicateRequest;
    caseFoldedDuplicateRequest.graph = &graph;
    caseFoldedDuplicateRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    caseFoldedDuplicateRequest.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath),
        catalogEntry(QStringLiteral("finepaper.beta"), generatorPath)
    };
    caseFoldedDuplicateRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("shared_1"),
                       QStringLiteral("alpha-marker")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("SHARED_1"),
                       QStringLiteral("beta-marker"))
    };

    const ProjectGenerationResult caseFoldedDuplicateResult =
        ProjectGenerationRunner().generate(caseFoldedDuplicateRequest);
    require(!caseFoldedDuplicateResult.success,
            "case-folded duplicate instance output keys should fail generation");
    require(caseFoldedDuplicateResult.error.contains(QStringLiteral("Duplicate IP instance output id")),
            "case-folded duplicate output key error should be explicit");

    ProjectGenerationRequest reservedSnapshotRequest;
    reservedSnapshotRequest.graph = &graph;
    reservedSnapshotRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    reservedSnapshotRequest.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath)
    };
    reservedSnapshotRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("project-snapshot.fpproj"),
                       QStringLiteral("snapshot-marker"))
    };

    const ProjectGenerationResult reservedSnapshotResult =
        ProjectGenerationRunner().generate(reservedSnapshotRequest);
    require(!reservedSnapshotResult.success,
            "reserved snapshot output id should fail generation");
    require(reservedSnapshotResult.error.contains(QStringLiteral("Reserved IP instance output id")),
            "reserved snapshot output id error should be explicit");
}

void testGenerationFailureAndTimeoutFailWholeResult() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    Graph graph;

    ProjectGenerationRequest failingRequest;
    failingRequest.graph = &graph;
    failingRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    failingRequest.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.fail"), createFailingGenerator(root))
    };
    failingRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.fail"),
                       QStringLiteral("fail_0"),
                       QStringLiteral("fail-marker"))
    };

    const ProjectGenerationResult failingResult = ProjectGenerationRunner().generate(failingRequest);
    require(!failingResult.success, "nonzero generator should fail the whole generation result");
    require(failingResult.error.contains(QStringLiteral("exit code 7")),
            "nonzero generator failure should include exit code");

    ProjectGenerationRequest timeoutRequest;
    timeoutRequest.graph = &graph;
    timeoutRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    timeoutRequest.generatorTimeoutMs = 50;
    timeoutRequest.catalogEntries = {
        catalogEntry(QStringLiteral("finepaper.slow"), createSlowGenerator(root))
    };
    timeoutRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.slow"),
                       QStringLiteral("slow_0"),
                       QStringLiteral("slow-marker"))
    };

    const ProjectGenerationResult timeoutResult = ProjectGenerationRunner().generate(timeoutRequest);
    require(!timeoutResult.success, "timed-out generator should fail the whole generation result");
    require(timeoutResult.error.contains(QStringLiteral("timed out")),
            "timed-out generator failure should be explicit");
}

void testGenerationManifestExcludesStaleArtifacts() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    Graph graph;
    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    const QString stalePath =
        root.filePath(QStringLiteral("project/generated/alpha_0/stale.sv"));
    require(QDir().mkpath(QFileInfo(stalePath).absolutePath()),
            "failed to create stale artifact directory");
    writeFile(stalePath, QByteArrayLiteral("stale"));

    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {catalogEntry(QStringLiteral("finepaper.alpha"), generatorPath)};
    request.instances = {
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("alpha_0"),
                       QStringLiteral("alpha-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);
    require(result.success, result.error.toLocal8Bit().constData());
    require(!QFileInfo::exists(stalePath),
            "generation should clear stale artifacts before running generator");

    const QString manifestPath =
        root.filePath(QStringLiteral("project/generated/alpha_0/generation-manifest.json"));
    const QJsonObject manifest = readJsonObject(manifestPath);
    const QJsonArray files =
        manifest.value(QStringLiteral("artifacts")).toObject().value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : files) {
        require(value.toString() != QStringLiteral("stale.sv"),
                "manifest should not report stale artifacts from previous runs");
    }
}

void testGenerationRequiresSavedProjectPath() {
    Graph graph;
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = QString();

    ProjectGenerationRunner runner;
    const ProjectGenerationResult result = runner.generate(request);
    require(!result.success, "generation without project path should fail");
    require(result.error == QStringLiteral("Save the project before generation."),
            "generation without project path should ask user to save project first");
}

void testGenerateExportsIpcraftSchemaForPackageCommand() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    const QString ipcoreId = QStringLiteral("org.example.alpha");
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("alpha_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("alpha_0"),
                                       QStringLiteral("alpha_tile"))),
            "ipcraft package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {ipcraftCatalogEntry(ipcoreId, generatorPath)};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1, "one ipcraft package instance should be generated");
    const QJsonObject input = readJsonObject(result.instances.first().inputPath);
    require(input.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("ipcraft.noc.project.v1"),
            "ipcraft package generator should receive ipcraft NoC project schema");
    const QJsonObject manifest =
        readJsonObject(root.filePath(QStringLiteral("project/generated/alpha_0/generation-manifest.json")));
    require(manifest.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ipcraft.generation.manifest.v1"),
            "generation manifest should use the public ipcraft generation manifest schema");
    require(manifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString() == QStringLiteral("ipcraft.noc.project.v1"),
            "generation manifest should record the exported ipcraft schema");
}

void testIpcraftCommandInputPathUsesCommandInputWording() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    const QString ipcoreId = QStringLiteral("org.example.path");
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("path_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("path_0"),
                                       QStringLiteral("path_tile"))),
            "ipcraft package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {ipcraftCatalogEntry(ipcoreId, generatorPath)};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("path_0"), QStringLiteral("path-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1, "one ipcraft package instance should be generated");
    const ProjectGenerationInstanceResult instance = result.instances.first();
    require(usesCommandInputFileName(instance.inputPath),
            "generated command input path should contain command-input or ipcraft-input");
    require(!instance.inputPath.contains(QStringLiteral(".fpproj")),
            "generated command input path should not look like a saved project file");

    const QJsonObject manifest = readJsonObject(instance.manifestPath);
    const QString manifestInputPath =
        manifest.value(QStringLiteral("input")).toObject().value(QStringLiteral("path")).toString();
    require(usesCommandInputFileName(manifestInputPath),
            "generation manifest input path should use command input wording");
    require(!manifestInputPath.contains(QStringLiteral(".fpproj")),
            "generation manifest input path should not point at a saved project file");
    const QString artifactInputPath =
        manifest.value(QStringLiteral("artifacts")).toObject()
            .value(QStringLiteral("command_input")).toString();
    require(usesCommandInputFileName(artifactInputPath),
            "generation manifest artifact path should use command input wording");
    require(!artifactInputPath.contains(QStringLiteral(".fpproj")),
            "generation manifest artifact path should not point at a saved project file");

    const QJsonArray arguments =
        manifest.value(QStringLiteral("process")).toObject().value(QStringLiteral("arguments")).toArray();
    require(!arguments.isEmpty(), "generation manifest should record command arguments");
    require(usesCommandInputFileName(arguments.first().toString()),
            "recorded generator command input argument should use command input wording");
    require(!arguments.first().toString().contains(QStringLiteral(".fpproj")),
            "recorded generator command input argument should not look like a saved project file");
}

void testGenerateExportsIpcraftProjectForEveryInstance() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    const QString ipcoreId = QStringLiteral("org.example.alpha");
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("alpha_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("alpha_0"),
                                       QStringLiteral("alpha_tile"))),
            "first ipcraft package module should add");
    require(graph.addModule(makeModule(QStringLiteral("beta_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("beta_0"),
                                       QStringLiteral("beta_tile"))),
            "second ipcraft package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {ipcraftCatalogEntry(ipcoreId, generatorPath)};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker")),
        instanceRecord(ipcoreId, QStringLiteral("beta_0"), QStringLiteral("beta-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 2,
            "generate should run once for every project IP instance");

    const QString alphaRoot = root.filePath(QStringLiteral("project/generated/alpha_0"));
    const QString betaRoot = root.filePath(QStringLiteral("project/generated/beta_0"));
    const QJsonObject alphaInput = readJsonObject(commandInputPath(alphaRoot));
    const QJsonObject betaInput = readJsonObject(commandInputPath(betaRoot));
    require(alphaInput.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ipcraft.noc.project.v1"),
            "first package instance should receive the ipcraft NoC project schema");
    require(betaInput.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ipcraft.noc.project.v1"),
            "second package instance should receive the ipcraft NoC project schema");
    require(alphaInput.value(QStringLiteral("project")).toObject()
                .value(QStringLiteral("instance")).toObject()
                .value(QStringLiteral("id")).toString() == QStringLiteral("alpha_0"),
            "first package input should identify the first project instance");
    require(betaInput.value(QStringLiteral("project")).toObject()
                .value(QStringLiteral("instance")).toObject()
                .value(QStringLiteral("id")).toString() == QStringLiteral("beta_0"),
            "second package input should identify the second project instance");

    const QJsonArray alphaInstances = alphaInput.value(QStringLiteral("instances")).toArray();
    const QJsonArray betaInstances = betaInput.value(QStringLiteral("instances")).toArray();
    require(alphaInstances.size() == 1 &&
                alphaInstances.first().toObject().value(QStringLiteral("id")).toString()
                    == QStringLiteral("alpha_tile"),
            "first package input should include only the first instance modules");
    require(betaInstances.size() == 1 &&
                betaInstances.first().toObject().value(QStringLiteral("id")).toString()
                    == QStringLiteral("beta_tile"),
            "second package input should include only the second instance modules");

    const QJsonObject alphaManifest =
        readJsonObject(QDir(alphaRoot).filePath(QStringLiteral("generation-manifest.json")));
    const QJsonObject betaManifest =
        readJsonObject(QDir(betaRoot).filePath(QStringLiteral("generation-manifest.json")));
    require(alphaManifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString()
                    == QStringLiteral("ipcraft.noc.project.v1"),
            "first generation manifest should record the ipcraft schema");
    require(betaManifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString()
                    == QStringLiteral("ipcraft.noc.project.v1"),
            "second generation manifest should record the ipcraft schema");
}

void testGenerateRunsBuiltInValidationBeforeCommand() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString generatorPath = createCopyingGenerator(root);
    const QString ipcoreId = QStringLiteral("finepaper.alpha");
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("alpha_runtime"),
                                       QStringLiteral("MissingTile"),
                                       ipcoreId,
                                       QStringLiteral("alpha_0"),
                                       QStringLiteral("alpha_tile"))),
            "invalid package-owned module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {ipcraftCatalogEntry(ipcoreId, generatorPath)};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(!result.success, "built-in validation error should stop generation");
    require(result.error.contains(QStringLiteral("Built-in validation failed")),
            "generation error should report built-in validation failure");
    require(result.error.contains(QStringLiteral("MissingTile")),
            "generation error should include the built-in validation diagnostic");
    require(!QFileInfo::exists(root.filePath(QStringLiteral("project/generated/alpha_0/artifact.sv"))),
            "generator should not run after a built-in validation error");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testGeneratesEveryProjectInstanceIntoSeparateOutputDirectories();
        testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys();
        testGenerationFailureAndTimeoutFailWholeResult();
        testGenerationManifestExcludesStaleArtifacts();
        testGenerationRequiresSavedProjectPath();
        testGenerateExportsIpcraftSchemaForPackageCommand();
        testIpcraftCommandInputPathUsesCommandInputWording();
        testGenerateExportsIpcraftProjectForEveryInstance();
        testGenerateRunsBuiltInValidationBeforeCommand();
    } catch (const std::exception& error) {
        std::cerr << "projectgenerationrunner_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectgenerationrunner_test passed\n";
    return 0;
}
