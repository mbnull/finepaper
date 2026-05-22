// ProjectGenerationRunner tests project-level generation across all IP instances.
#include "app/projectgenerationrunner.h"

#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcraft/schemaids.h"
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

bool usesEmittedInputsManifestPath(const QString& path) {
    const QString normalized = QDir::fromNativeSeparators(path);
    return normalized.endsWith(QStringLiteral("inputs/manifest.json"));
}

QString emittedInputsManifestPath(const QString& outputDirectory) {
    return QDir(outputDirectory).filePath(QStringLiteral("inputs/manifest.json"));
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

QString repositoryPathFromApplicationDir(const QString& relativePath) {
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        const QFileInfo info(dir.filePath(relativePath));
        if (info.exists()) {
            return info.absoluteFilePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath(relativePath))
        .absoluteFilePath();
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
    record.id = instanceId;
    record.package = ProjectPackageRef{ipcoreId, QStringLiteral("1.0.0")};
    record.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("marker"), marker}}}
    };
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    return record;
}

QJsonArray jsonStringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray packageExtensions() {
    return jsonStringArray({
        QStringLiteral("ipcraft.config.params"),
        QStringLiteral("ipcraft.graph_config"),
        QStringLiteral("ipcraft.emitters"),
        QStringLiteral("ipcraft.flows"),
        QStringLiteral("ipcraft.artifacts")
    });
}

QJsonObject flowCapture() {
    return QJsonObject{
        {QStringLiteral("stdout"), QStringLiteral("stdout.log")},
        {QStringLiteral("stderr"), QStringLiteral("stderr.log")},
        {QStringLiteral("max_bytes"), 1048576}
    };
}

QJsonObject flowCommand(const QString& executable,
                        const QStringList& args,
                        int timeoutMs = 300000) {
    return QJsonObject{
        {QStringLiteral("executable"), executable},
        {QStringLiteral("args"), jsonStringArray(args)},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("timeout_ms"), timeoutMs},
        {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
        {QStringLiteral("capture"), flowCapture()}
    };
}

QJsonObject frameworkToolCommand(const QStringList& args,
                                 int timeoutMs = 300000) {
    return QJsonObject{
        {QStringLiteral("framework_tool"), QStringLiteral("ipcraft-generate")},
        {QStringLiteral("args"), jsonStringArray(args)},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("timeout_ms"), timeoutMs},
        {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
        {QStringLiteral("capture"), flowCapture()}
    };
}

QJsonArray packageEmitters() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("graph_config")},
            {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
            {QStringLiteral("path"), QStringLiteral("graph_config.json")}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("parameters")},
            {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
            {QStringLiteral("path"), QStringLiteral("parameters.json")}
        }
    };
}

QJsonArray packageFlows(const QJsonObject& command) {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("generate")},
            {QStringLiteral("label"), QStringLiteral("Generate")},
            {QStringLiteral("steps"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}},
                 QJsonObject{
                     {QStringLiteral("kind"), QStringLiteral("exec")},
                     {QStringLiteral("command"), command}
                 },
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("collect_artifacts")}}
             }}
        }
    };
}

QJsonArray packageArtifacts(const QString& artifactGlob = QStringLiteral("artifact.sv")) {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("primary")},
            {QStringLiteral("type"), QStringLiteral("rtl")},
            {QStringLiteral("glob"), artifactGlob},
            {QStringLiteral("primary"), true}
        }
    };
}

void writePackageSpec(const QString& packageRoot,
                      const QString& packageId,
                      const QJsonObject& command,
                      const QString& artifactGlob = QStringLiteral("artifact.sv")) {
    require(QDir().mkpath(packageRoot), "package root should be created");
    const QJsonObject spec{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), packageId},
        {QStringLiteral("name"), packageId},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), packageExtensions()},
        {QStringLiteral("graph_config"),
         QJsonObject{
             {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
             {QStringLiteral("objects"), QJsonArray{}},
             {QStringLiteral("relationships"), QJsonArray{}},
             {QStringLiteral("properties"), QJsonObject{}},
             {QStringLiteral("native"), QJsonObject{}}
         }},
        {QStringLiteral("emitters"), packageEmitters()},
        {QStringLiteral("flows"), packageFlows(command)},
        {QStringLiteral("artifacts"), packageArtifacts(artifactGlob)}
    };
    writeFile(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")),
              QJsonDocument(spec).toJson(QJsonDocument::Indented));
}

IpcraftModuleDescriptor manifestModule(const QString& id);

IpCatalogEntry flowCatalogEntry(const QString& ipcoreId,
                                const QString& packageRoot,
                                const QString& moduleType) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.packageId = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0.0");
    entry.kind = QStringLiteral("ipcraft");
    entry.runtimeRootPath = packageRoot;
    entry.sourceRootPath = packageRoot;
    entry.moduleTypes = {moduleType};
    entry.packageManifest.schema = ipcraft::schemaids::packageV1;
    entry.packageManifest.id = ipcoreId;
    entry.packageManifest.name = ipcoreId;
    entry.packageManifest.version = QStringLiteral("1.0.0");
    entry.packageManifest.packageRootPath = packageRoot;
    entry.packageManifest.modules = {manifestModule(moduleType)};
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

QString createPackageTool(const QString& packageRoot,
                          const QString& fileName,
                          const QByteArray& content) {
    const QDir packageDir(packageRoot);
    require(QDir().mkpath(packageDir.filePath(QStringLiteral("tools"))),
            "package tools directory should be created");
    const QString path = packageDir.filePath(QStringLiteral("tools/%1").arg(fileName));
    writeFile(path, content);
    makeExecutable(path);
    return QStringLiteral("tools/%1").arg(fileName);
}

QString createCopyingGenerator(const QString& packageRoot) {
    return createPackageTool(
        packageRoot,
        QStringLiteral("copy-generator.sh"),
        QByteArrayLiteral("#!/bin/sh\n"
                          "set -eu\n"
                          "input=''\n"
                          "output=''\n"
                          "while [ \"$#\" -gt 0 ]; do\n"
                          "  case \"$1\" in\n"
                          "    --input) input=\"$2\"; shift 2 ;;\n"
                          "    --out) output=\"$2\"; shift 2 ;;\n"
                          "    *) echo \"unexpected argument: $1\" >&2; exit 2 ;;\n"
                          "  esac\n"
                          "done\n"
                          "test -f \"$input\"\n"
                          "test -f \"$output/inputs/graph_config.json\"\n"
                          "test -f \"$output/inputs/parameters.json\"\n"
                          "mkdir -p \"$output\"\n"
                          "{\n"
                          "  printf 'input=%s\\n' \"$input\"\n"
                          "  cat \"$input\"\n"
                          "  cat \"$output/inputs/graph_config.json\"\n"
                          "  cat \"$output/inputs/parameters.json\"\n"
                          "  printf '\\n// generated by test generator\\n'\n"
                          "} > \"$output/artifact.sv\"\n"));
}

QString createFailingGenerator(const QString& packageRoot) {
    return createPackageTool(packageRoot,
                             QStringLiteral("failing-generator.sh"),
                             QByteArrayLiteral("#!/bin/sh\n"
                                               "echo 'generator failed intentionally' >&2\n"
                                               "exit 7\n"));
}

QString createSlowGenerator(const QString& packageRoot) {
    return createPackageTool(packageRoot,
                             QStringLiteral("slow-generator.sh"),
                             QByteArrayLiteral("#!/bin/sh\n"
                                               "sleep 5\n"
                                               "exit 0\n"));
}

IpCatalogEntry createCopyingFlowPackage(const QDir& root,
                                        const QString& packageId,
                                        const QString& moduleType) {
    const QString packageRoot =
        root.filePath(QStringLiteral("packages/%1").arg(packageId));
    const QString tool = createCopyingGenerator(packageRoot);
    writePackageSpec(packageRoot,
                     packageId,
                     flowCommand(tool,
                                 {QStringLiteral("--input"),
                                  QStringLiteral("{inputs.manifest}"),
                                  QStringLiteral("--out"),
                                  QStringLiteral("{out}")}));
    return flowCatalogEntry(packageId, packageRoot, moduleType);
}

IpCatalogEntry createFailingFlowPackage(const QDir& root,
                                        const QString& packageId,
                                        const QString& moduleType) {
    const QString packageRoot =
        root.filePath(QStringLiteral("packages/%1").arg(packageId));
    const QString tool = createFailingGenerator(packageRoot);
    writePackageSpec(packageRoot,
                     packageId,
                     flowCommand(tool,
                                 {QStringLiteral("--input"),
                                  QStringLiteral("{inputs.manifest}"),
                                  QStringLiteral("--out"),
                                  QStringLiteral("{out}")}));
    return flowCatalogEntry(packageId, packageRoot, moduleType);
}

IpCatalogEntry createSlowFlowPackage(const QDir& root,
                                     const QString& packageId,
                                     const QString& moduleType,
                                     int timeoutMs) {
    const QString packageRoot =
        root.filePath(QStringLiteral("packages/%1").arg(packageId));
    const QString tool = createSlowGenerator(packageRoot);
    writePackageSpec(packageRoot,
                     packageId,
                     flowCommand(tool,
                                 {QStringLiteral("--input"),
                                  QStringLiteral("{inputs.manifest}"),
                                  QStringLiteral("--out"),
                                  QStringLiteral("{out}")},
                                 timeoutMs));
    return flowCatalogEntry(packageId, packageRoot, moduleType);
}

IpCatalogEntry createFrameworkToolFlowPackage(const QDir& root,
                                              const QString& packageId,
                                              const QString& moduleType,
                                              const QString& artifactGlob) {
    const QString packageRoot =
        root.filePath(QStringLiteral("packages/%1").arg(packageId));
    writePackageSpec(packageRoot,
                     packageId,
                     frameworkToolCommand({QStringLiteral("--manifest"),
                                           QStringLiteral("{package.manifest}"),
                                           QStringLiteral("--input"),
                                           QStringLiteral("{inputs.manifest}"),
                                           QStringLiteral("--output"),
                                           QStringLiteral("{out}")}),
                     artifactGlob);
    return flowCatalogEntry(packageId, packageRoot, moduleType);
}

QString createFrameworkGenerateTool(const QDir& dir) {
    const QString path = dir.filePath(QStringLiteral("ipcraft-generate"));
    writeFile(path,
              QByteArrayLiteral("#!/bin/sh\n"
                                "set -eu\n"
                                "manifest=''\n"
                                "input=''\n"
                                "output=''\n"
                                "while [ \"$#\" -gt 0 ]; do\n"
                                "  case \"$1\" in\n"
                                "    --manifest) manifest=\"$2\"; shift 2 ;;\n"
                                "    --input) input=\"$2\"; shift 2 ;;\n"
                                "    --output) output=\"$2\"; shift 2 ;;\n"
                                "    *) echo \"unexpected argument: $1\" >&2; exit 2 ;;\n"
                                "  esac\n"
                                "done\n"
                                "test -f \"$manifest\"\n"
                                "test -f \"$input\"\n"
                                "mkdir -p \"$output\"\n"
                                "{\n"
                                "  printf 'manifest=%s\\n' \"$manifest\"\n"
                                "  printf 'input=%s\\n' \"$input\"\n"
                                "  printf 'output=%s\\n' \"$output\"\n"
                                "} > \"$output/framework-tool-ran.txt\"\n"));
    makeExecutable(path);
    return path;
}

bool hasManifestFile(const QJsonObject& manifest,
                     const QString& id,
                     const QString& kind,
                     const QString& path) {
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (const QJsonValue& fileValue : files) {
        if (!fileValue.isObject()) {
            continue;
        }
        const QJsonObject file = fileValue.toObject();
        if (file.value(QStringLiteral("id")).toString() == id &&
            file.value(QStringLiteral("kind")).toString() == kind &&
            file.value(QStringLiteral("path")).toString() == path) {
            return true;
        }
    }
    return false;
}

void testDefaultFrameworkToolSearchPathsIgnoreCurrentWorkingDirectory() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("project/ipcraft_generator/bin")),
            "project-local framework tool directory should be created");
    require(root.mkpath(QStringLiteral("ipcraft_generator/bin")),
            "parent framework tool directory should be created");

    const QString previousCurrentPath = QDir::currentPath();
    const QString projectPath = root.filePath(QStringLiteral("project"));
    require(QDir::setCurrent(projectPath), "current directory should switch to test project");
    const QStringList searchPaths = ProjectGenerationRunner::defaultFrameworkToolSearchPaths();
    require(QDir::setCurrent(previousCurrentPath), "current directory should be restored");

    const QString projectLocalToolPath =
        QFileInfo(QDir(projectPath).filePath(QStringLiteral("ipcraft_generator/bin"))).absoluteFilePath();
    const QString parentLocalToolPath =
        QFileInfo(root.filePath(QStringLiteral("ipcraft_generator/bin"))).absoluteFilePath();
    require(!searchPaths.contains(projectLocalToolPath),
            "default framework tool paths should not include CWD-local tool directories");
    require(!searchPaths.contains(parentLocalToolPath),
            "default framework tool paths should not include CWD-parent tool directories");
}

void testDefaultFrameworkToolSearchPathsFindRepositoryToolFromApplicationDir() {
    const QString toolDirectory =
        repositoryPathFromApplicationDir(QStringLiteral("ipcraft_generator/bin"));
    require(QFileInfo(QDir(toolDirectory).filePath(QStringLiteral("ipcraft-generate"))).isFile(),
            "repository ipcraft-generate tool should exist for default search path coverage");

    const QStringList searchPaths = ProjectGenerationRunner::defaultFrameworkToolSearchPaths();

    require(searchPaths.contains(toolDirectory),
            "default framework tool paths should include repo tool directory from application dir layout");
}

void testGeneratesEveryProjectInstanceIntoSeparateOutputDirectories() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

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
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
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
    require(QFileInfo::exists(emittedInputsManifestPath(alphaOutput)),
            "alpha emitted inputs manifest should exist");
    require(QFileInfo::exists(emittedInputsManifestPath(betaOutput)),
            "beta emitted inputs manifest should exist");
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
    require(snapshot.document.instances.size() == 2,
            "project snapshot should include all generation-time IP instance state");
}

void testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    Graph graph;

    ProjectGenerationRequest unsafeRequest;
    unsafeRequest.graph = &graph;
    unsafeRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    unsafeRequest.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
    };
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
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
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
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
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
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
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
    require(graph.addModule(makeModule(QStringLiteral("fail_runtime"),
                                       QStringLiteral("FailTile"),
                                       QStringLiteral("finepaper.fail"),
                                       QStringLiteral("fail_0"),
                                       QStringLiteral("fail_tile"))),
            "failing package module should add");

    ProjectGenerationRequest failingRequest;
    failingRequest.graph = &graph;
    failingRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    failingRequest.catalogEntries = {
        createFailingFlowPackage(root, QStringLiteral("finepaper.fail"), QStringLiteral("FailTile"))
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

    Graph timeoutGraph;
    ProjectGenerationRequest timeoutRequest;
    timeoutRequest.graph = &timeoutGraph;
    timeoutRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    require(timeoutGraph.addModule(makeModule(QStringLiteral("slow_runtime"),
                                              QStringLiteral("SlowTile"),
                                              QStringLiteral("finepaper.slow"),
                                              QStringLiteral("slow_0"),
                                              QStringLiteral("slow_tile"))),
            "slow package module should add");
    timeoutRequest.catalogEntries = {
        createSlowFlowPackage(root,
                              QStringLiteral("finepaper.slow"),
                              QStringLiteral("SlowTile"),
                              50)
    };
    timeoutRequest.instances = {
        instanceRecord(QStringLiteral("finepaper.slow"),
                       QStringLiteral("slow_0"),
                       QStringLiteral("slow-marker"))
    };

    const ProjectGenerationResult timeoutResult = ProjectGenerationRunner().generate(timeoutRequest);
    require(!timeoutResult.success, "timed-out generator should fail the whole generation result");
    require(timeoutResult.error.contains(QStringLiteral("timed out")),
            timeoutResult.error.toLocal8Bit().constData());
}

void testGenerationManifestExcludesStaleArtifacts() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("alpha_runtime"),
                                       QStringLiteral("AlphaTile"),
                                       QStringLiteral("finepaper.alpha"),
                                       QStringLiteral("alpha_0"),
                                       QStringLiteral("alpha_tile"))),
            "alpha module should add");
    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    const QString stalePath =
        root.filePath(QStringLiteral("project/generated/alpha_0/stale.sv"));
    require(QDir().mkpath(QFileInfo(stalePath).absolutePath()),
            "failed to create stale artifact directory");
    writeFile(stalePath, QByteArrayLiteral("stale"));

    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
    };
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

void testGenerateEmitsPackageInputsForFlow() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
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
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1, "one ipcraft package instance should be generated");
    const QJsonObject input = readJsonObject(result.instances.first().inputPath);
    require(input.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::emittedInputsV1,
            "package flow should receive an emitted inputs manifest");
    require(input.value(QStringLiteral("instance")).toString() == QStringLiteral("alpha_0"),
            "emitted inputs manifest should identify the generated instance");
    require(input.value(QStringLiteral("package")).toObject()
                .value(QStringLiteral("id")).toString() == ipcoreId,
            "emitted inputs manifest should identify the package");
    require(input.value(QStringLiteral("diagnostics")).toObject()
                .value(QStringLiteral("records")).toArray().isEmpty(),
            "successful emitted inputs manifest should not contain diagnostics");
    require(hasManifestFile(input,
                            QStringLiteral("graph_config"),
                            QStringLiteral("graph_config"),
                            QStringLiteral("graph_config.json")),
            "emitted inputs manifest should record graph_config input");
    require(hasManifestFile(input,
                            QStringLiteral("parameters"),
                            QStringLiteral("parameters"),
                            QStringLiteral("parameters.json")),
            "emitted inputs manifest should record parameters input");

    const QJsonObject manifest =
        readJsonObject(root.filePath(QStringLiteral("project/generated/alpha_0/generation-manifest.json")));
    require(manifest.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ipcraft.generation.manifest.v1"),
            "generation manifest should use the public ipcraft generation manifest schema");
    require(manifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString() == ipcraft::schemaids::emittedInputsV1,
            "generation manifest should record the emitted inputs schema");
}

void testPackageFlowInputPathUsesEmittedInputsManifest() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
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
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("path_0"), QStringLiteral("path-marker"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1, "one ipcraft package instance should be generated");
    const ProjectGenerationInstanceResult instance = result.instances.first();
    require(usesEmittedInputsManifestPath(instance.inputPath),
            "generated package flow input should be inputs/manifest.json");
    require(!instance.inputPath.contains(QStringLiteral(".fpproj")),
            "generated flow input path should not look like a saved project file");

    const QJsonObject manifest = readJsonObject(instance.manifestPath);
    const QString manifestInputPath =
        manifest.value(QStringLiteral("input")).toObject().value(QStringLiteral("path")).toString();
    require(usesEmittedInputsManifestPath(manifestInputPath),
            "generation manifest input path should point to emitted inputs manifest");
    require(!manifestInputPath.contains(QStringLiteral(".fpproj")),
            "generation manifest input path should not point at a saved project file");
    const QString artifactInputPath =
        manifest.value(QStringLiteral("artifacts")).toObject()
            .value(QStringLiteral("emitted_inputs")).toString();
    require(usesEmittedInputsManifestPath(artifactInputPath),
            "generation manifest artifacts should record emitted inputs manifest");
    require(!artifactInputPath.contains(QStringLiteral(".fpproj")),
            "generation manifest artifact path should not point at a saved project file");

    require(manifest.value(QStringLiteral("process")).toObject()
                .value(QStringLiteral("flow")).toString() == QStringLiteral("generate"),
            "generation manifest should record the package flow id");
}

void testGenerateRunsFrameworkToolFromInjectedSearchPath() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("framework-tools")), "framework tool directory should be created");
    const QDir toolsDir(root.filePath(QStringLiteral("framework-tools")));
    const QString toolPath = createFrameworkGenerateTool(toolsDir);

    const QString ipcoreId = QStringLiteral("org.example.framework");
    const IpCatalogEntry entry =
        createFrameworkToolFlowPackage(root,
                                       ipcoreId,
                                       QStringLiteral("Tile"),
                                       QStringLiteral("framework-tool-ran.txt"));
    const QString packageManifestPath =
        QDir(entry.packageManifest.packageRootPath).filePath(QStringLiteral("ipcraft.json"));

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("framework_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("framework_0"),
                                       QStringLiteral("framework_tile"))),
            "framework tool package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = projectPath;
    request.catalogEntries = {entry};
    request.instances = {
        instanceRecord(ipcoreId, QStringLiteral("framework_0"), QStringLiteral("framework-marker"))
    };

    ProjectGenerationRunner runner(QStringList{toolsDir.absolutePath()});
    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1, "framework_tool package instance should be generated");
    const ProjectGenerationInstanceResult instance = result.instances.first();
    const QString proofPath = QDir(instance.outputDirectory).filePath(QStringLiteral("framework-tool-ran.txt"));
    require(QFileInfo::exists(proofPath), "framework tool output artifact should prove it ran");
    const QString proof = readText(proofPath);
    require(proof.contains(packageManifestPath),
            "framework tool should receive substituted package manifest path");
    require(proof.contains(instance.inputPath),
            "framework tool should receive substituted emitted inputs manifest path");
    require(proof.contains(instance.outputDirectory),
            "framework tool should receive substituted output path");

    const QJsonObject generationManifest = readJsonObject(instance.manifestPath);
    const QJsonObject process = generationManifest.value(QStringLiteral("process")).toObject();
    require(process.value(QStringLiteral("flow")).toString() == QStringLiteral("generate"),
            "generation manifest should record the package flow id");
    require(process.value(QStringLiteral("stderr")).toString().isEmpty(),
            "framework tool should not emit stderr on success");
    require(QFileInfo(toolPath).isExecutable(),
            "test framework tool should be executable from injected search path");
}

void testGenerateEmitsInputsForEveryPackageInstance() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
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
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
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
    const QJsonObject alphaInput = readJsonObject(emittedInputsManifestPath(alphaRoot));
    const QJsonObject betaInput = readJsonObject(emittedInputsManifestPath(betaRoot));
    require(alphaInput.value(QStringLiteral("schema")).toString()
                == ipcraft::schemaids::emittedInputsV1,
            "first package instance should receive an emitted inputs manifest");
    require(betaInput.value(QStringLiteral("schema")).toString()
                == ipcraft::schemaids::emittedInputsV1,
            "second package instance should receive an emitted inputs manifest");
    require(alphaInput.value(QStringLiteral("instance")).toString() == QStringLiteral("alpha_0"),
            "first emitted inputs manifest should identify the first project instance");
    require(betaInput.value(QStringLiteral("instance")).toString() == QStringLiteral("beta_0"),
            "second emitted inputs manifest should identify the second project instance");

    const QJsonObject alphaGraphConfig =
        readJsonObject(QDir(alphaRoot).filePath(QStringLiteral("inputs/graph_config.json")));
    const QJsonObject betaGraphConfig =
        readJsonObject(QDir(betaRoot).filePath(QStringLiteral("inputs/graph_config.json")));
    const QJsonArray alphaObjects = alphaGraphConfig.value(QStringLiteral("objects")).toArray();
    const QJsonArray betaObjects = betaGraphConfig.value(QStringLiteral("objects")).toArray();
    require(alphaObjects.size() == 1 &&
                alphaObjects.first().toObject().value(QStringLiteral("id")).toString()
                    == QStringLiteral("alpha_runtime"),
            "first graph_config input should include only the first instance modules");
    require(betaObjects.size() == 1 &&
                betaObjects.first().toObject().value(QStringLiteral("id")).toString()
                    == QStringLiteral("beta_runtime"),
            "second graph_config input should include only the second instance modules");

    const QJsonObject alphaManifest =
        readJsonObject(QDir(alphaRoot).filePath(QStringLiteral("generation-manifest.json")));
    const QJsonObject betaManifest =
        readJsonObject(QDir(betaRoot).filePath(QStringLiteral("generation-manifest.json")));
    require(alphaManifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString()
                    == ipcraft::schemaids::emittedInputsV1,
            "first generation manifest should record the emitted inputs schema");
    require(betaManifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString()
                    == ipcraft::schemaids::emittedInputsV1,
            "second generation manifest should record the emitted inputs schema");
}

void testGenerateRunsBuiltInValidationBeforeCommand() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
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
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
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
        testDefaultFrameworkToolSearchPathsIgnoreCurrentWorkingDirectory();
        testDefaultFrameworkToolSearchPathsFindRepositoryToolFromApplicationDir();
        testGeneratesEveryProjectInstanceIntoSeparateOutputDirectories();
        testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys();
        testGenerationFailureAndTimeoutFailWholeResult();
        testGenerationManifestExcludesStaleArtifacts();
        testGenerationRequiresSavedProjectPath();
        testGenerateEmitsPackageInputsForFlow();
        testPackageFlowInputPathUsesEmittedInputsManifest();
        testGenerateRunsFrameworkToolFromInjectedSearchPath();
        testGenerateEmitsInputsForEveryPackageInstance();
        testGenerateRunsBuiltInValidationBeforeCommand();
    } catch (const std::exception& error) {
        std::cerr << "projectgenerationrunner_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectgenerationrunner_test passed\n";
    return 0;
}
