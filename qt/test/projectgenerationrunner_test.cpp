// ProjectGenerationRunner tests project-level generation across all IP instances.
#include "app/generationflowprovider.h"
#include "app/projectgenerationrunner.h"

#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcraft/core/project_design.h"
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <optional>
#include <chrono>
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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "failed to open test file");
    }
    require(file.write(content) == content.size(), "failed to write test file");
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        require(false, "failed to read test file");
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        require(false, "failed to read JSON file");
    }
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

QJsonObject graphConfigForObject(const QString& objectId,
                                 const QString& objectType = QString()) {
    QJsonObject object{
        {QStringLiteral("id"), objectId},
        {QStringLiteral("type"), objectType.trimmed().isEmpty()
             ? QStringLiteral("component")
             : objectType},
        {QStringLiteral("properties"), QJsonObject{}}
    };
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{object}},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };
}

ProjectIpInstanceRecord instanceRecord(const QString& ipcoreId,
                                       const QString& instanceId,
                                       const QString& marker,
                                       const QString& graphObjectId = QString(),
                                       const QString& graphObjectType = QString()) {
    ProjectIpInstanceRecord record;
    record.id = instanceId;
    record.package = ProjectPackageRef{ipcoreId, QStringLiteral("1.0.0")};
    record.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("marker"), marker}}}
    };
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.hasGraphConfig = true;
    record.graphConfig = graphConfigForObject(
        graphObjectId.trimmed().isEmpty() ? instanceId : graphObjectId,
        graphObjectType);
    return record;
}

QString packageRefKey(const ProjectPackageRef& package) {
    return package.version.trimmed().isEmpty()
        ? package.id
        : package.id + QLatin1Char('@') + package.version;
}

ipcraft::core::ProjectDesign projectDesignFor(const QString& designName,
                                              const QVector<ProjectIpInstanceRecord>& instances) {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = designName + QStringLiteral("_id");
    design.name = designName;

    QStringList packageKeys;
    for (const ProjectIpInstanceRecord& instance : instances) {
        const QString packageId = instance.package.id.trimmed().isEmpty()
            ? instance.ipcoreId
            : instance.package.id;
        const QString packageVersion = instance.package.version;
        const QString packageKey = packageVersion.trimmed().isEmpty()
            ? packageId
            : packageId + QLatin1Char('@') + packageVersion;
        if (!packageId.trimmed().isEmpty() && !packageKeys.contains(packageKey)) {
            packageKeys.append(packageKey);
            design.packages.append(ipcraft::core::PackageRef{packageId, packageVersion});
        }

        ipcraft::core::ComponentInstance component;
        component.id = instance.id.trimmed().isEmpty() ? instance.instanceId : instance.id;
        component.packageRef = packageKey;
        component.config = instance.config;
        component.type = instance.native.value(QStringLiteral("componentType")).toString();
        if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
            component.extensionData.insert(QStringLiteral("graph_config"), instance.graphConfig);
        }
        design.components.append(component);
    }

    return design;
}

void appendOwnedGraphTopology(ipcraft::core::ProjectDesign& design,
                              const QString& ownerComponentId,
                              const QString& objectId,
                              const QString& objectType) {
    ipcraft::core::TopologyGraph topology;
    topology.id = ownerComponentId + QStringLiteral("_graph");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.ownerComponentId = ownerComponentId;
    topology.kind = QStringLiteral("explicit_graph");
    topology.nodes.append(QJsonObject{
        {QStringLiteral("id"), objectId},
        {QStringLiteral("type"), objectType},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("source"), QStringLiteral("project-design")}}}
    });
    design.topologies.append(topology);
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
                        std::chrono::milliseconds deadline = std::chrono::minutes(5)) {
    return QJsonObject{
        {QStringLiteral("executable"), executable},
        {QStringLiteral("args"), jsonStringArray(args)},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("timeout_ms"), static_cast<qint64>(deadline.count())},
        {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
        {QStringLiteral("capture"), flowCapture()}
    };
}

QJsonObject frameworkToolCommand(const QStringList& args,
                                 std::chrono::milliseconds deadline = std::chrono::minutes(5)) {
    return QJsonObject{
        {QStringLiteral("framework_tool"), QStringLiteral("ipcraft-generate")},
        {QStringLiteral("args"), jsonStringArray(args)},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("timeout_ms"), static_cast<qint64>(deadline.count())},
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
            {QStringLiteral("scope"), QStringLiteral("instance")},
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

struct CapturedGenerationFlow {
    int runCount = 0;
    QString flowId;
    QString packageId;
    QString instanceId;
    QString runRoot;
    QString outputRoot;
    QString marker;
    QStringList frameworkToolSearchPaths;
    bool hasGraphConfig = false;
    QString graphConfigObjectId;
    QStringList graphConfigObjectIds;
};

class CapturingGenerationFlowProvider final : public GenerationFlowProvider {
public:
    explicit CapturingGenerationFlowProvider(CapturedGenerationFlow* capture)
        : m_capture(capture) {}

    bool canRun(const GenerationFlowRequest& request) const override {
        return request.flowRequest.flowId == QStringLiteral("generate");
    }

    ipcraft::FlowRunResult run(const GenerationFlowRequest& request) const override {
        if (m_capture) {
            m_capture->runCount += 1;
            m_capture->flowId = request.flowRequest.flowId;
            m_capture->packageId = request.flowRequest.package.id;
            m_capture->instanceId = request.flowRequest.instanceId;
            m_capture->runRoot = request.flowRequest.runRoot;
            m_capture->outputRoot = request.flowRequest.outputRoot;
            m_capture->marker =
                request.flowRequest.config.parameters.value(QStringLiteral("marker")).toString();
            m_capture->frameworkToolSearchPaths = request.flowRequest.frameworkToolSearchPaths;
            m_capture->hasGraphConfig = request.flowRequest.graphConfig.has_value();
            if (request.flowRequest.graphConfig.has_value() &&
                !request.flowRequest.graphConfig->objects.isEmpty()) {
                m_capture->graphConfigObjectId =
                    request.flowRequest.graphConfig->objects.first().id;
                for (const ipcraft::GraphConfigObject& object :
                     request.flowRequest.graphConfig->objects) {
                    m_capture->graphConfigObjectIds.append(object.id);
                }
            }
        }

        require(QDir().mkpath(QDir(request.outputDirectory).filePath(QStringLiteral("inputs"))),
                "capturing provider should create inputs directory");
        writeFile(QDir(request.outputDirectory).filePath(QStringLiteral("inputs/manifest.json")),
                  QJsonDocument(QJsonObject{{QStringLiteral("schema"), ipcraft::schemaids::emittedInputsV1}})
                      .toJson(QJsonDocument::Indented));
        writeFile(QDir(request.outputDirectory).filePath(QStringLiteral("artifact.sv")),
                  QByteArrayLiteral("// generated by provider\n"));

        ipcraft::FlowRunResult result;
        result.ok = true;
        result.flowId = request.flowRequest.flowId;
        result.runId = request.flowRequest.runId;
        result.runRoot = request.flowRequest.runRoot;
        return result;
    }

private:
    CapturedGenerationFlow* m_capture = nullptr;
};

class MalformedInputsGenerationFlowProvider final : public GenerationFlowProvider {
public:
    bool canRun(const GenerationFlowRequest& request) const override {
        return request.flowRequest.flowId == QStringLiteral("generate");
    }

    ipcraft::FlowRunResult run(const GenerationFlowRequest& request) const override {
        require(QDir().mkpath(QDir(request.outputDirectory).filePath(QStringLiteral("inputs"))),
                "malformed provider should create inputs directory");
        writeFile(QDir(request.outputDirectory).filePath(QStringLiteral("inputs/manifest.json")),
                  QByteArrayLiteral("{not-json"));

        ipcraft::FlowRunResult result;
        result.ok = true;
        result.flowId = request.flowRequest.flowId;
        result.runId = request.flowRequest.runId;
        result.runRoot = request.flowRequest.runRoot;
        return result;
    }
};

class EscapingRunRootGenerationFlowProvider final : public GenerationFlowProvider {
public:
    explicit EscapingRunRootGenerationFlowProvider(QString runRoot)
        : m_runRoot(std::move(runRoot)) {}

    bool canRun(const GenerationFlowRequest& request) const override {
        return request.flowRequest.flowId == QStringLiteral("generate");
    }

    ipcraft::FlowRunResult run(const GenerationFlowRequest& request) const override {
        require(QDir().mkpath(QDir(request.outputDirectory).filePath(QStringLiteral("inputs"))),
                "escaping provider should create public inputs directory");
        writeFile(QDir(request.outputDirectory).filePath(QStringLiteral("inputs/manifest.json")),
                  QJsonDocument(QJsonObject{{QStringLiteral("schema"),
                                             ipcraft::schemaids::emittedInputsV1}})
                      .toJson(QJsonDocument::Indented));

        ipcraft::FlowRunResult result;
        result.ok = true;
        result.flowId = request.flowRequest.flowId;
        result.runId = request.flowRequest.runId;
        result.runRoot = m_runRoot;
        return result;
    }

private:
    QString m_runRoot;
};

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
                          "input_dir=$(dirname \"$input\")\n"
                          "test -f \"$input\"\n"
                          "test -f \"$input_dir/graph_config.json\"\n"
                          "test -f \"$input_dir/parameters.json\"\n"
                          "mkdir -p \"$output\"\n"
                          "{\n"
                          "  printf 'input=%s\\n' \"$input\"\n"
                          "  cat \"$input\"\n"
                          "  cat \"$input_dir/graph_config.json\"\n"
                          "  cat \"$input_dir/parameters.json\"\n"
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
                                     std::chrono::milliseconds deadline) {
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
                                 deadline));
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
                       QStringLiteral("alpha-marker"),
                       QStringLiteral("alpha_runtime"),
                       QStringLiteral("AlphaTile")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("beta_0"),
                       QStringLiteral("beta-marker"),
                       QStringLiteral("beta_runtime"),
                       QStringLiteral("BetaTile"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("demo_design"), instances);

    const QString projectPath = root.filePath(QStringLiteral("project/demo_design.fpproj"));
    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = projectPath;
    request.designName = QStringLiteral("demo_design");
    request.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
    };

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

void testGenerationRequiresProjectDesignSource() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    ProjectGenerationRequest request;
    request.projectPath = root.filePath(QStringLiteral("project/requires_design.fpproj"));
    request.designName = QStringLiteral("requires_design");
    request.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.requires_design"), QStringLiteral("Tile"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(!result.success, "missing ProjectDesign should fail generation");
    require(result.error.contains(QStringLiteral("project design"), Qt::CaseInsensitive) ||
                result.error.contains(QStringLiteral("source design"), Qt::CaseInsensitive),
            "missing ProjectDesign error should name the project/source design");
    require(!QFileInfo::exists(root.filePath(QStringLiteral("project/generated/project-snapshot.fpproj"))),
            "generation should not synthesize a snapshot when ProjectDesign is missing");
}

void testGenerateDerivesInstancesFromProjectDesignWithoutRequestInstances() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.design_source");
    const QVector<ProjectIpInstanceRecord> designInstances{
        instanceRecord(ipcoreId,
                       QStringLiteral("design_source_0"),
                       QStringLiteral("design-marker"))
    };
    ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("design_source_design"), designInstances);
    appendOwnedGraphTopology(design,
                             QStringLiteral("design_source_0"),
                             QStringLiteral("design_graph_object"),
                             QStringLiteral("Tile"));

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/design_source_design.fpproj"));
    request.designName = QStringLiteral("design_source_design");
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};

    CapturedGenerationFlow capture;
    ProjectGenerationRunner runner;
    runner.addGenerationFlowProvider(std::make_unique<CapturingGenerationFlowProvider>(&capture));

    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(capture.runCount == 1,
            "ProjectDesign-only generation should run one package flow");
    require(result.instances.size() == 1,
            "ProjectDesign-only generation should report one instance result");
    require(capture.instanceId == QStringLiteral("design_source_0"),
            "ProjectDesign component id should become the generation instance id");
    require(capture.marker == QStringLiteral("design-marker"),
            "ProjectDesign component config should become the flow config bundle");
    require(capture.hasGraphConfig,
            "ProjectDesign topology should become the flow graph config");
    require(capture.graphConfigObjectIds.contains(QStringLiteral("design_graph_object")),
            "ProjectDesign topology nodes should feed the flow graph config");
}

void testGenerateDoesNotDropUnknownProjectDesignComponentType() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.unknown_type");

    ProjectIpInstanceRecord designInstance =
        instanceRecord(ipcoreId,
                       QStringLiteral("unknown_type_0"),
                       QStringLiteral("unknown-type-marker"));
    designInstance.native.insert(QStringLiteral("componentType"),
                                 QStringLiteral("UnknownTile"));
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("unknown_type_design"), {designInstance});

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/unknown_type_design.fpproj"));
    request.designName = QStringLiteral("unknown_type_design");
    request.catalogEntries = {createFailingFlowPackage(root,
                                                       ipcoreId,
                                                       QStringLiteral("SupportedTile"))};

    CapturedGenerationFlow capture;
    ProjectGenerationRunner runner;
    runner.addGenerationFlowProvider(std::make_unique<CapturingGenerationFlowProvider>(&capture));

    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(capture.runCount == 1,
            "unknown ProjectDesign component type should still reach generation");
    require(result.instances.size() == 1,
            "unknown ProjectDesign component type should not be omitted from results");
    require(result.instances.first().instance.native.value(QStringLiteral("componentType")).toString() ==
                QStringLiteral("UnknownTile"),
            "projector should preserve the unsupported component type for downstream diagnostics");
}

void testGenerateUsesProjectDesignAsOnlyInstanceSource() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.only_design_source");
    const QVector<ProjectIpInstanceRecord> designInstances{
        instanceRecord(ipcoreId,
                       QStringLiteral("design_authoritative_0"),
                       QStringLiteral("design-authoritative-marker"))
    };
    ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("only_design_source_design"), designInstances);
    appendOwnedGraphTopology(design,
                             QStringLiteral("design_authoritative_0"),
                             QStringLiteral("design_authoritative_graph_object"),
                             QStringLiteral("Tile"));

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/only_design_source_design.fpproj"));
    request.designName = QStringLiteral("only_design_source_design");
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};

    CapturedGenerationFlow capture;
    ProjectGenerationRunner runner;
    runner.addGenerationFlowProvider(std::make_unique<CapturingGenerationFlowProvider>(&capture));

    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(capture.runCount == 1,
            "ProjectDesign should provide the generation flow inputs");
    require(result.instances.size() == 1,
            "ProjectDesign should provide the generation instance result");
    require(capture.instanceId == QStringLiteral("design_authoritative_0"),
            "ProjectDesign component should become the generated instance");
    require(capture.marker == QStringLiteral("design-authoritative-marker"),
            "ProjectDesign config should feed the generated instance");
    require(capture.graphConfigObjectIds.contains(QStringLiteral("design_authoritative_graph_object")),
            "ProjectDesign graph config should feed the generated instance");
}

void testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    Graph graph;

    ProjectGenerationRequest unsafeRequest;
    unsafeRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    unsafeRequest.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
    };
    const QVector<ProjectIpInstanceRecord> unsafeInstances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("../escape"),
                       QStringLiteral("unsafe-marker"))
    };
    const ipcraft::core::ProjectDesign unsafeDesign =
        projectDesignFor(QStringLiteral("unsafe_design"), unsafeInstances);
    unsafeRequest.projectDesign = &unsafeDesign;

    const ProjectGenerationResult unsafeResult = ProjectGenerationRunner().generate(unsafeRequest);
    require(!unsafeResult.success, "unsafe instance id should fail generation");
    require(unsafeResult.error.contains(QStringLiteral("Unsafe IP instance id")),
            "unsafe instance id error should be explicit");
    require(!QFileInfo::exists(root.filePath(QStringLiteral("escape"))),
            "unsafe instance id should not create output outside generated root");

    ProjectGenerationRequest duplicateRequest;
    duplicateRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    duplicateRequest.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
    };
    const QVector<ProjectIpInstanceRecord> duplicateInstances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("shared_0"),
                       QStringLiteral("alpha-marker")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("shared_0"),
                       QStringLiteral("beta-marker"))
    };
    const ipcraft::core::ProjectDesign duplicateDesign =
        projectDesignFor(QStringLiteral("duplicate_design"), duplicateInstances);
    duplicateRequest.projectDesign = &duplicateDesign;

    const ProjectGenerationResult duplicateResult = ProjectGenerationRunner().generate(duplicateRequest);
    require(!duplicateResult.success, "duplicate instance output keys should fail generation");
    require(duplicateResult.error.contains(QStringLiteral("Duplicate IP instance output id")),
            "duplicate instance output key error should be explicit");

    ProjectGenerationRequest caseFoldedDuplicateRequest;
    caseFoldedDuplicateRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    caseFoldedDuplicateRequest.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile")),
        createCopyingFlowPackage(root, QStringLiteral("finepaper.beta"), QStringLiteral("BetaTile"))
    };
    const QVector<ProjectIpInstanceRecord> caseFoldedDuplicateInstances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("shared_1"),
                       QStringLiteral("alpha-marker")),
        instanceRecord(QStringLiteral("finepaper.beta"),
                       QStringLiteral("SHARED_1"),
                       QStringLiteral("beta-marker"))
    };
    const ipcraft::core::ProjectDesign caseFoldedDuplicateDesign =
        projectDesignFor(QStringLiteral("case_folded_duplicate_design"),
                         caseFoldedDuplicateInstances);
    caseFoldedDuplicateRequest.projectDesign = &caseFoldedDuplicateDesign;

    const ProjectGenerationResult caseFoldedDuplicateResult =
        ProjectGenerationRunner().generate(caseFoldedDuplicateRequest);
    require(!caseFoldedDuplicateResult.success,
            "case-folded duplicate instance output keys should fail generation");
    require(caseFoldedDuplicateResult.error.contains(QStringLiteral("Duplicate IP instance output id")),
            "case-folded duplicate output key error should be explicit");

    ProjectGenerationRequest reservedSnapshotRequest;
    reservedSnapshotRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    reservedSnapshotRequest.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
    };
    const QVector<ProjectIpInstanceRecord> reservedSnapshotInstances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("project-snapshot.fpproj"),
                       QStringLiteral("snapshot-marker"))
    };
    const ipcraft::core::ProjectDesign reservedSnapshotDesign =
        projectDesignFor(QStringLiteral("reserved_snapshot_design"),
                         reservedSnapshotInstances);
    reservedSnapshotRequest.projectDesign = &reservedSnapshotDesign;

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
    failingRequest.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    failingRequest.catalogEntries = {
        createFailingFlowPackage(root, QStringLiteral("finepaper.fail"), QStringLiteral("FailTile"))
    };
    const QVector<ProjectIpInstanceRecord> failingInstances{
        instanceRecord(QStringLiteral("finepaper.fail"),
                       QStringLiteral("fail_0"),
                       QStringLiteral("fail-marker"))
    };
    const ipcraft::core::ProjectDesign failingDesign =
        projectDesignFor(QStringLiteral("failing_design"), failingInstances);
    failingRequest.projectDesign = &failingDesign;

    const ProjectGenerationResult failingResult = ProjectGenerationRunner().generate(failingRequest);
    require(!failingResult.success, "nonzero generator should fail the whole generation result");
    require(failingResult.error.contains(QStringLiteral("exit code 7")),
            "nonzero generator failure should include exit code");

    Graph timeoutGraph;
    ProjectGenerationRequest timeoutRequest;
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
                              std::chrono::milliseconds(50))
    };
    const QVector<ProjectIpInstanceRecord> timeoutInstances{
        instanceRecord(QStringLiteral("finepaper.slow"),
                       QStringLiteral("slow_0"),
                       QStringLiteral("slow-marker"))
    };
    const ipcraft::core::ProjectDesign timeoutDesign =
        projectDesignFor(QStringLiteral("timeout_design"), timeoutInstances);
    timeoutRequest.projectDesign = &timeoutDesign;

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
    request.projectPath = projectPath;
    request.catalogEntries = {
        createCopyingFlowPackage(root, QStringLiteral("finepaper.alpha"), QStringLiteral("AlphaTile"))
    };
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(QStringLiteral("finepaper.alpha"),
                       QStringLiteral("alpha_0"),
                       QStringLiteral("alpha-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("stale_artifacts_design"), instances);
    request.projectDesign = &design;

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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("unsaved_design"), {});
    ProjectGenerationRequest request;
    request.projectDesign = &design;
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
    request.projectPath = projectPath;
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("package_inputs_design"), instances);
    request.projectDesign = &design;

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
    request.projectPath = projectPath;
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId, QStringLiteral("path_0"), QStringLiteral("path-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("flow_input_path_design"), instances);
    request.projectDesign = &design;

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
    request.projectPath = projectPath;
    request.catalogEntries = {entry};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId, QStringLiteral("framework_0"), QStringLiteral("framework-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("framework_tool_design"), instances);
    request.projectDesign = &design;

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
    require(proof.contains(QStringLiteral("inputs/manifest.json")),
            "framework tool should receive substituted emitted inputs manifest path");
    require(proof.contains(instance.outputDirectory),
            "framework tool should receive substituted output path");
    require(QFileInfo::exists(instance.inputPath),
            "runner should materialize emitted inputs manifest under the public output directory");

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
    request.projectPath = projectPath;
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("alpha_0"),
                       QStringLiteral("alpha-marker"),
                       QStringLiteral("alpha_runtime")),
        instanceRecord(ipcoreId,
                       QStringLiteral("beta_0"),
                       QStringLiteral("beta-marker"),
                       QStringLiteral("beta_runtime"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("multiple_inputs_design"), instances);
    request.projectDesign = &design;

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

void testGenerateUsesProjectDesignWithoutGraphForInstanceGraphConfig() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.graph_free");
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("graph_free_0"),
                       QStringLiteral("graph-free-marker"),
                       QStringLiteral("owned_graph_config_object"),
                       QStringLiteral("Tile"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("graph_free_design"), instances);

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/graph_free_design.fpproj"));
    request.designName = QStringLiteral("graph_free_design");
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 1,
            "graph-free generation should run the package flow");
    const QJsonObject graphConfig = readJsonObject(
        QDir(result.instances.first().outputDirectory).filePath(QStringLiteral("inputs/graph_config.json")));
    const QJsonArray objects = graphConfig.value(QStringLiteral("objects")).toArray();
    require(objects.size() == 1,
            "graph-free emitted inputs should include instance-owned graph_config");
    require(objects.first().toObject().value(QStringLiteral("id")).toString()
                == QStringLiteral("owned_graph_config_object"),
            "graph-free generation should use ProjectDesign-owned graph_config");
}

void testGenerateWritesProjectDesignSnapshotWithoutGraph() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.snapshot_graph_free");
    QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("snapshot_0"),
                       QStringLiteral("snapshot-marker"),
                       QStringLiteral("snapshot_object"),
                       QStringLiteral("Tile"))
    };
    ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("snapshot_graph_free_design"), instances);
    design.components.append(ipcraft::core::ComponentInstance{
        QStringLiteral("design_only_component"),
        QStringLiteral("Tile"),
        packageRefKey(instances.first().package),
        QJsonObject{{QStringLiteral("width"), 64}},
        {},
        {},
        QJsonObject{{QStringLiteral("graph_config"),
                     graphConfigForObject(QStringLiteral("design_only_object"),
                                          QStringLiteral("Tile"))}}
    });

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/snapshot_graph_free_design.fpproj"));
    request.designName = QStringLiteral("snapshot_graph_free_design");
    request.catalogEntries = {createCopyingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    const ProjectReadResult snapshot = ProjectReader::readFile(result.snapshotPath);
    require(snapshot.success, snapshot.error.toLocal8Bit().constData());
    require(snapshot.document.projectId == QStringLiteral("snapshot_graph_free_design_id"),
            "graph-free snapshot should preserve the ProjectDesign project id");
    require(snapshot.document.instances.size() == 2,
            "graph-free snapshot should preserve design-only and generated instances");
    bool sawGenerated = false;
    bool sawDesignOnly = false;
    for (const ProjectIpInstanceRecord& instance : snapshot.document.instances) {
        sawGenerated = sawGenerated || instance.id == QStringLiteral("snapshot_0");
        sawDesignOnly = sawDesignOnly || instance.id == QStringLiteral("design_only_component");
    }
    require(sawGenerated, "graph-free snapshot should include the generated instance");
    require(sawDesignOnly, "graph-free snapshot should preserve design-only components");
}

void testGenerateRunsBuiltInValidationBeforeCommand() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("finepaper.alpha");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.projectPath = projectPath;
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId, QStringLiteral("alpha_0"), QStringLiteral("alpha-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("built_in_validation_design"), instances);
    request.projectDesign = &design;

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(!result.success, "built-in validation error should stop generation");
    require(result.error.contains(QStringLiteral("Built-in validation failed")),
            "generation error should report built-in validation failure");
    require(result.error.contains(QStringLiteral("missing package/catalog entry")),
            "generation error should include the graph-free built-in validation diagnostic");
    require(!QFileInfo::exists(root.filePath(QStringLiteral("project/generated/alpha_0/artifact.sv"))),
            "generator should not run after a built-in validation error");
}

void testInjectedGenerationFlowProviderOverridesDefaultRunner() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.provider");

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("provider_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("provider_0"),
                                       QStringLiteral("provider_tile"))),
            "provider package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.projectPath = projectPath;
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId, QStringLiteral("provider_0"), QStringLiteral("provider-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("provider_design"), instances);
    request.projectDesign = &design;

    CapturedGenerationFlow capture;
    ProjectGenerationRunner runner(QStringList{QStringLiteral("/custom/tools")});
    runner.addGenerationFlowProvider(std::make_unique<CapturingGenerationFlowProvider>(&capture));

    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(capture.runCount == 1, "injected generation provider should run exactly once");
    require(result.instances.size() == 1, "one provider-backed instance should be generated");
    require(QFileInfo::exists(QDir(result.instances.first().outputDirectory)
                                  .filePath(QStringLiteral("artifact.sv"))),
            "provider should write the generated artifact");
    require(result.instances.first().artifactPaths.contains(QStringLiteral("artifact.sv")),
            "provider artifact should be collected");
}

void testInjectedGenerationFlowProviderMustProduceValidEmittedInputsManifest() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.provider.bad_inputs");

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("provider_bad_inputs_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("provider_bad_inputs_0"),
                                       QStringLiteral("provider_bad_inputs_tile"))),
            "provider malformed-input package module should add");

    ProjectGenerationRequest request;
    request.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("provider_bad_inputs_0"),
                       QStringLiteral("bad-inputs-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("provider_bad_inputs_design"), instances);
    request.projectDesign = &design;

    ProjectGenerationRunner runner;
    runner.addGenerationFlowProvider(std::make_unique<MalformedInputsGenerationFlowProvider>());

    const ProjectGenerationResult result = runner.generate(request);

    require(!result.success,
            "provider-backed generation should fail when emitted inputs manifest is malformed");
    require(result.instances.size() == 1,
            "malformed provider should still report one instance result");
    require(!result.instances.first().success,
            "malformed emitted inputs should fail the instance result");
    require(result.instances.first().error.contains(QStringLiteral("Emitted inputs manifest")),
            "malformed emitted inputs error should name the manifest contract");
}

void testInjectedGenerationFlowProviderCannotReturnUnexpectedRunRoot() {
    QTemporaryDir tempDir;
    QTemporaryDir outsideRoot;
    require(tempDir.isValid(), "temporary directory should be valid");
    require(outsideRoot.isValid(), "outside run root should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.provider.escaped_run_root");

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("provider_escaped_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("provider_escaped_0"),
                                       QStringLiteral("provider_escaped_tile"))),
            "provider escaped-run-root package module should add");

    ProjectGenerationRequest request;
    request.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("provider_escaped_0"),
                       QStringLiteral("escaped-root-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("provider_escaped_design"), instances);
    request.projectDesign = &design;

    ProjectGenerationRunner runner;
    runner.addGenerationFlowProvider(
        std::make_unique<EscapingRunRootGenerationFlowProvider>(outsideRoot.path()));

    const ProjectGenerationResult result = runner.generate(request);

    require(!result.success,
            "provider-backed generation should fail when run root escapes the requested root");
    require(result.instances.size() == 1,
            "escaped run root provider should still report one instance result");
    require(result.instances.first().error.contains(QStringLiteral("unexpected run root")),
            "unexpected run root error should name the trust boundary");
}

void testInjectedGenerationFlowProviderReceivesFlowContext() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());
    const QString ipcoreId = QStringLiteral("org.example.provider.context");

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("provider_context_runtime"),
                                       QStringLiteral("Tile"),
                                       ipcoreId,
                                       QStringLiteral("provider_context_0"),
                                       QStringLiteral("provider_context_tile"))),
            "provider context package module should add");

    const QString projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    ProjectGenerationRequest request;
    request.projectPath = projectPath;
    request.catalogEntries = {createFailingFlowPackage(root, ipcoreId, QStringLiteral("Tile"))};
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(ipcoreId,
                       QStringLiteral("provider_context_0"),
                       QStringLiteral("context-marker"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("provider_context_design"), instances);
    request.projectDesign = &design;

    CapturedGenerationFlow capture;
    ProjectGenerationRunner runner(QStringList{QStringLiteral("/context/tools")});
    runner.addGenerationFlowProvider(std::make_unique<CapturingGenerationFlowProvider>(&capture));

    const ProjectGenerationResult result = runner.generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(capture.flowId == QStringLiteral("generate"),
            "provider should receive the selected flow id");
    require(capture.packageId == ipcoreId,
            "provider should receive the package spec");
    require(capture.instanceId == QStringLiteral("provider_context_0"),
            "provider should receive the instance id");
    require(capture.runRoot != result.instances.first().outputDirectory,
            "provider run root should be isolated from the instance output directory");
    require(capture.runRoot.contains(QStringLiteral(".runs")),
            "provider run root should live under the generated flow run area");
    require(capture.outputRoot == result.instances.first().outputDirectory,
            "provider output root should match the instance output directory");
    require(capture.marker == QStringLiteral("context-marker"),
            "provider should receive the instance config bundle");
    require(capture.frameworkToolSearchPaths == QStringList{QStringLiteral("/context/tools")},
            "provider should receive framework tool search paths");
    require(capture.hasGraphConfig,
            "provider should receive projected graph config");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testDefaultFrameworkToolSearchPathsIgnoreCurrentWorkingDirectory();
        testDefaultFrameworkToolSearchPathsFindRepositoryToolFromApplicationDir();
        testGeneratesEveryProjectInstanceIntoSeparateOutputDirectories();
        testGenerationRequiresProjectDesignSource();
        testGenerateDerivesInstancesFromProjectDesignWithoutRequestInstances();
        testGenerateDoesNotDropUnknownProjectDesignComponentType();
        testGenerateUsesProjectDesignAsOnlyInstanceSource();
        testGenerationRejectsUnsafeAndDuplicateInstanceOutputKeys();
        testGenerationFailureAndTimeoutFailWholeResult();
        testGenerationManifestExcludesStaleArtifacts();
        testGenerationRequiresSavedProjectPath();
        testGenerateEmitsPackageInputsForFlow();
        testPackageFlowInputPathUsesEmittedInputsManifest();
        testGenerateRunsFrameworkToolFromInjectedSearchPath();
        testGenerateEmitsInputsForEveryPackageInstance();
        testGenerateUsesProjectDesignWithoutGraphForInstanceGraphConfig();
        testGenerateWritesProjectDesignSnapshotWithoutGraph();
        testGenerateRunsBuiltInValidationBeforeCommand();
        testInjectedGenerationFlowProviderOverridesDefaultRunner();
        testInjectedGenerationFlowProviderMustProduceValidEmittedInputsManifest();
        testInjectedGenerationFlowProviderCannotReturnUnexpectedRunRoot();
        testInjectedGenerationFlowProviderReceivesFlowContext();
    } catch (const std::exception& error) {
        std::cerr << "projectgenerationrunner_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectgenerationrunner_test passed\n";
    return 0;
}
