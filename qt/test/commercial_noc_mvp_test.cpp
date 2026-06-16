// Commercial NoC MVP workflow gate across the three bundled package IPs.
#include "app/projectgenerationrunner.h"
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "ipcraft/schemaids.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "project/graphprojectserializer.h"
#include "project/projectdesignserializer.h"
#include "project/projectstateservice.h"
#include "topology/topologypresetbuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireMessage(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toLocal8Bit().constData());
    }
}

QStringList requiredPackageIds() {
    return {
        QStringLiteral("finepaper.noc"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("finepaper.opennoc")
    };
}

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePath));
            if (info.exists()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePath)).absoluteFilePath();
}

QString packageDirectoryName(const QString& packageId) {
    if (packageId == QStringLiteral("finepaper.noc")) {
        return QStringLiteral("finepaper-noc");
    }
    if (packageId == QStringLiteral("finepaper.ravenoc")) {
        return QStringLiteral("ravenoc");
    }
    if (packageId == QStringLiteral("finepaper.opennoc")) {
        return QStringLiteral("opennoc");
    }
    return {};
}

void writeFile(const QString& path, const QByteArray& content) {
    const QFileInfo info(path);
    require(QDir().mkpath(info.absolutePath()), "test output directory should be created");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "test file should open");
    }
    require(file.write(content) == content.size(), "test file should write");
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        requireMessage(false, QStringLiteral("Text file should open: %1").arg(path));
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        requireMessage(false, QStringLiteral("JSON file should open: %1").arg(path));
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    requireMessage(document.isObject(),
                   QStringLiteral("JSON file should contain object: %1").arg(path));
    return document.object();
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
            "test executable permissions should be set");
}

void copyDirectoryRecursively(const QString& sourceRoot, const QString& destinationRoot) {
    const QFileInfo sourceInfo(sourceRoot);
    require(sourceInfo.isDir(), "source package directory should exist");
    require(QDir().mkpath(destinationRoot), "destination package directory should be created");

    QDirIterator iterator(sourceRoot,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    const QDir sourceDir(sourceRoot);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QString relativePath = sourceDir.relativeFilePath(sourcePath);
        const QString destinationPath = QDir(destinationRoot).filePath(relativePath);
        const QFileInfo itemInfo(sourcePath);
        if (itemInfo.isDir()) {
            require(QDir().mkpath(destinationPath), "copied directory should be created");
            continue;
        }
        require(QDir().mkpath(QFileInfo(destinationPath).absolutePath()),
                "copied file directory should be created");
        if (QFileInfo::exists(destinationPath)) {
            require(QFile::remove(destinationPath), "existing copied file should be removed");
        }
        require(QFile::copy(sourcePath, destinationPath), "package file should copy");
        QFile destination(destinationPath);
        destination.setPermissions(QFile(sourcePath).permissions());
    }
}

QString copyPackageToWorkspace(const QDir& workspace, const QString& packageId) {
    const QString source = repositoryPath(QStringLiteral("ipcores/%1").arg(packageDirectoryName(packageId)));
    const QString destination = workspace.filePath(QStringLiteral("ipcores/%1").arg(packageDirectoryName(packageId)));
    copyDirectoryRecursively(source, destination);
    return destination;
}

void createRaveNoCVendorFixture(const QString& packageRoot) {
    const QString vendorRoot = QDir(packageRoot).filePath(QStringLiteral("vendor/ravenoc"));
    const QStringList paths{
        QStringLiteral("bus_arch_sv_pkg/amba_axi_pkg.sv"),
        QStringLiteral("src/include/ravenoc_axi_fnc.svh"),
        QStringLiteral("src/include/ravenoc_defines.svh"),
        QStringLiteral("src/include/ravenoc_structs.svh"),
        QStringLiteral("src/include/ravenoc_pkg.sv"),
        QStringLiteral("src/ni/axi_csr.sv"),
        QStringLiteral("src/ni/axi_slave_if.sv"),
        QStringLiteral("src/ni/router_wrapper.sv"),
        QStringLiteral("src/ni/async_gp_fifo.sv"),
        QStringLiteral("src/ni/cdc_pkt.sv"),
        QStringLiteral("src/ni/pkt_proc.sv"),
        QStringLiteral("src/router/fifo.sv"),
        QStringLiteral("src/router/output_module.sv"),
        QStringLiteral("src/router/router_if.sv"),
        QStringLiteral("src/router/router_ravenoc.sv"),
        QStringLiteral("src/router/rr_arbiter.sv"),
        QStringLiteral("src/router/vc_buffer.sv"),
        QStringLiteral("src/router/input_router.sv"),
        QStringLiteral("src/router/input_module.sv"),
        QStringLiteral("src/router/input_datapath.sv"),
        QStringLiteral("src/ravenoc.sv")
    };
    for (const QString& path : paths) {
        writeFile(QDir(vendorRoot).filePath(path),
                  QStringLiteral("// fake RaveNoC vendor fixture: %1\n").arg(path).toUtf8());
    }
}

void createOpenNoCVendorFixture(const QString& packageRoot) {
    const QString vendorRoot = QDir(packageRoot).filePath(QStringLiteral("vendor/OpenNoC"));
    const QStringList paths{
        QStringLiteral("LICENSE"),
        QStringLiteral("tools/mesh_generator/template/mesh_wrapper.j2"),
        QStringLiteral("tools/mesh_generator/chi_xp_node.sv"),
        QStringLiteral("rtl/misc/chi_xp_channel.v"),
        QStringLiteral("rtl/misc/sync_fifo.v"),
        QStringLiteral("rtl/include/chie_defines.v"),
        QStringLiteral("rtl/include/rni_param.v"),
        QStringLiteral("rtl/include/hnf_param.v"),
        QStringLiteral("rtl/include/hni_param.v"),
        QStringLiteral("rtl/include/snf_param.v"),
        QStringLiteral("rtl/src/rni/rni.v"),
        QStringLiteral("rtl/src/hnf/hnf.v"),
        QStringLiteral("rtl/src/hni/hni.v"),
        QStringLiteral("rtl/src/snf/snf.v")
    };
    for (const QString& path : paths) {
        writeFile(QDir(vendorRoot).filePath(path),
                  QStringLiteral("// fake OpenNoC vendor fixture: %1\n").arg(path).toUtf8());
    }

    const QString meshGen = QDir(vendorRoot).filePath(QStringLiteral("tools/mesh_generator/mesh_gen.py"));
    writeFile(meshGen,
              QByteArrayLiteral("#!/usr/bin/env python3\n"
                                "from pathlib import Path\n"
                                "import sys\n"
                                "out = Path('.')\n"
                                "args = sys.argv[1:]\n"
                                "if '-o' in args:\n"
                                "    out = Path(args[args.index('-o') + 1])\n"
                                "out.mkdir(parents=True, exist_ok=True)\n"
                                "(out / 'mesh_wrapper_2x2.sv').write_text('// fake mesh wrapper\\n')\n"));
    makeExecutable(meshGen);
}

QVector<IpcraftPackageManifest> loadSinglePackageManifest(const QString& packageRoot,
                                                          const QString& packageId) {
    const QVector<IpcraftPackageManifest> packages =
        loadIpcraftPackageManifests({packageRoot});
    QVector<IpcraftPackageManifest> matches;
    for (const IpcraftPackageManifest& package : packages) {
        if (package.id == packageId) {
            matches.push_back(package);
        }
    }
    requireMessage(matches.size() == 1,
                   QStringLiteral("Expected one manifest for %1, got %2")
                       .arg(packageId)
                       .arg(matches.size()));
    return matches;
}

const IpCatalogEntry& onlyCatalogEntry(const IpCatalogService& catalog) {
    require(catalog.entries().size() == 1, "catalog should contain exactly one package");
    return catalog.entries().first();
}

TopologyPresetDescriptor meshPreset(const IpCatalogEntry& entry) {
    const auto it = std::find_if(entry.topologyPresets.cbegin(),
                                 entry.topologyPresets.cend(),
                                 [](const TopologyPresetDescriptor& preset) {
                                     return preset.id == QStringLiteral("mesh");
                                 });
    require(it != entry.topologyPresets.cend(), "package should expose mesh topology preset");
    return *it;
}

std::unique_ptr<Module> instantiateModule(const ModuleType& type,
                                          const QString& id,
                                          const QString& packageId,
                                          const QString& instanceId,
                                          const QString& externalId,
                                          int x,
                                          int y) {
    auto module = std::make_unique<Module>(id, type.name);
    module->setIpcoreId(packageId);
    module->setInstanceId(instanceId);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    if (!externalId.isEmpty()) {
        module->setParameter(QStringLiteral("external_id"), externalId);
    }
    module->setParameter(QStringLiteral("display_name"), externalId.isEmpty() ? id : externalId);
    module->setParameter(QStringLiteral("x"), x);
    module->setParameter(QStringLiteral("y"), y);
    return module;
}

void addModuleInstance(Graph& graph,
                       const ModuleRegistry& registry,
                       const QString& packageId,
                       const QString& instanceId,
                       const QString& manifestModuleId,
                       const QString& moduleId,
                       const QString& externalId,
                       int x,
                       int y) {
    const ModuleType* type = registry.getType(packageId, manifestModuleId);
    requireMessage(type != nullptr,
                   QStringLiteral("Module type %1::%2 should load")
                       .arg(packageId, manifestModuleId));
    require(graph.addModule(instantiateModule(*type,
                                              moduleId,
                                              packageId,
                                              instanceId,
                                              externalId,
                                              x,
                                              y)),
            "module instance should add");
}

QVector<ConnectionInterfaceRef> graphInterfaces(
    const QVector<ProjectConnectionInterfaceRef>& projectInterfaces) {
    QVector<ConnectionInterfaceRef> interfaces;
    interfaces.reserve(projectInterfaces.size());
    for (const ProjectConnectionInterfaceRef& interfaceRef : projectInterfaces) {
        interfaces.push_back(ConnectionInterfaceRef{interfaceRef.instanceId,
                                                    interfaceRef.interfaceId});
    }
    return interfaces;
}

void addResolvedConnection(Graph& graph,
                           const QVector<IpcraftPackageManifest>& manifests,
                           const QString& id,
                           const PortRef& source,
                           const PortRef& target) {
    ConnectionRuleService rules(&graph, manifests);
    const ConnectionCheckResult result =
        rules.check(ConnectionRequest::portToPort(source,
                                                  target,
                                                  ConnectionRequestKind::Programmatic));
    requireMessage(result.hasSingleOption(),
                   QStringLiteral("Connection %1 should resolve: %2")
                       .arg(id, result.message));
    const ConnectionResolvedOption option = result.options.first();
    graph.addConnection(std::make_unique<Connection>(id,
                                                     option.source,
                                                     option.target,
                                                     option.connectionClassId,
                                                     graphInterfaces(option.normalizedInterfaces),
                                                     option.connectionStatus,
                                                     option.alternatives));
}

ProjectIpInstanceRecord instanceRecord(const IpCatalogEntry& entry,
                                       const QString& instanceId,
                                       QJsonObject parameters = {}) {
    ProjectIpInstanceRecord record;
    record.id = instanceId;
    record.displayName = instanceId;
    record.package = ProjectPackageRef{entry.id, entry.version};
    if (!parameters.isEmpty()) {
        record.config.insert(QStringLiteral("parameters"), parameters);
    }
    record.ipcoreId = entry.id;
    record.instanceId = instanceId;
    record.schema = entry.id + QStringLiteral(".instance-state.v1");
    record.state = record.config;
    return record;
}

struct WorkflowContext {
    WorkflowContext() : registry(ModuleRegistry::LoadMode::Empty) {}

    QString packageRoot;
    QVector<IpcraftPackageManifest> manifests;
    ModuleRegistry registry;
    IpCatalogService catalog;
    IpCatalogEntry entry;
};

WorkflowContext loadWorkflowContext(const QString& packageRoot, const QString& packageId) {
    WorkflowContext context;
    context.packageRoot = packageRoot;
    context.manifests = loadSinglePackageManifest(packageRoot, packageId);
    require(context.registry.loadIpcraftPackages(context.manifests),
            "package module types should load");
    context.catalog = IpCatalogService(context.manifests, &context.registry);
    context.entry = onlyCatalogEntry(context.catalog);
    require(context.entry.id == packageId, "catalog entry should match package id");
    require(context.entry.isSelectable(), "commercial package should be selectable");
    return context;
}

void applyMeshPreset(Graph& graph,
                     const WorkflowContext& context,
                     const QString& instanceId,
                     int rows,
                     int cols) {
    TopologyPresetRequest request;
    request.ipcoreId = context.entry.id;
    request.instanceId = instanceId;
    request.preset = meshPreset(context.entry);
    request.parameters.insert(QStringLiteral("rows"), rows);
    request.parameters.insert(QStringLiteral("cols"), cols);
    const TopologyPresetResult result =
        TopologyPresetBuilder::apply(&graph, context.registry, request);
    requireMessage(result.success, result.error);
}

ProjectGenerationResult runGeneration(const QDir& root,
                                      const WorkflowContext& context,
                                      const Graph& graph,
                                      QVector<ProjectIpInstanceRecord> instances) {
    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("commercial_mvp"));
    ProjectStateService stateService;
    for (const ProjectIpInstanceRecord& instance : instances) {
        stateService.ensureIpInstanceRecord(instance);
    }
    stateService.writeToDocument(document);
    const ipcraft::core::ProjectDesign design = ProjectDesignSerializer::fromDocument(document);

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/design.fpproj"));
    request.designName = QStringLiteral("commercial_mvp");
    request.catalogEntries = context.catalog.entries();
    ProjectGenerationRunner runner({repositoryPath(QStringLiteral("ipcraft_generator/bin"))});
    return runner.generate(request);
}

void requireArtifact(const QString& outputDirectory, const QString& relativePath) {
    const QFileInfo artifact(QDir(outputDirectory).filePath(relativePath));
    requireMessage(artifact.isFile() && artifact.size() > 0,
                   QStringLiteral("Expected artifact %1 under %2")
                       .arg(relativePath, outputDirectory));
}

void requireGeneratedInstance(const ProjectGenerationInstanceResult& instance,
                              const QString& packageId,
                              const QString& instanceId) {
    require(instance.success, instance.error.toLocal8Bit().constData());
    require(instance.ipcoreId == packageId, "generated instance should keep package id");
    require(instance.instanceId == instanceId, "generated instance should keep instance id");
    const QJsonObject inputs = readJsonObject(instance.inputPath);
    require(inputs.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::emittedInputsV1,
            "generated flow should receive emitted inputs manifest");
    require(inputs.value(QStringLiteral("package")).toObject()
                .value(QStringLiteral("id")).toString() == packageId,
            "emitted inputs should identify package");
    require(inputs.value(QStringLiteral("instance")).toString() == instanceId,
            "emitted inputs should identify instance");
    const QJsonObject generationManifest = readJsonObject(instance.manifestPath);
    require(generationManifest.value(QStringLiteral("schema")).toString()
                == QStringLiteral("ipcraft.generation.manifest.v1"),
            "generation manifest should use public schema");
    require(generationManifest.value(QStringLiteral("input")).toObject()
                .value(QStringLiteral("schema")).toString() == ipcraft::schemaids::emittedInputsV1,
            "generation manifest should record emitted inputs schema");
}

void requireProjectSuccess(const ProjectGenerationResult& result, int instanceCount) {
    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == instanceCount, "expected generated instance count");
    require(QFileInfo::exists(result.snapshotPath), "project snapshot should be written");
}

void testRequiredPackageIdsAreExplicit() {
    const QStringList ids = requiredPackageIds();
    require(ids.contains(QStringLiteral("finepaper.noc")), "finepaper.noc should be required");
    require(ids.contains(QStringLiteral("finepaper.ravenoc")), "finepaper.ravenoc should be required");
    require(ids.contains(QStringLiteral("finepaper.opennoc")), "finepaper.opennoc should be required");
}

void testFinepaperNoCWorkflowGeneratesPublicArtifacts() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    const QString packageRoot = copyPackageToWorkspace(root, QStringLiteral("finepaper.noc"));
    const WorkflowContext context = loadWorkflowContext(packageRoot, QStringLiteral("finepaper.noc"));
    require(context.entry.moduleTypes.contains(QStringLiteral("finepaper.noc::XP")),
            "finepaper.noc XP module type should be scoped");
    require(context.entry.topologyPresets.size() >= 2,
            "finepaper.noc should expose mesh and ring presets");

    Graph graph;
    applyMeshPreset(graph, context, QStringLiteral("noc_0"), 2, 2);
    require(graph.modules().size() == 4, "finepaper.noc mesh should create four routers");
    require(graph.connections().size() == 4, "finepaper.noc mesh should create four router links");

    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(context.entry, QStringLiteral("noc_0"))
    };
    const ProjectGenerationResult result = runGeneration(root, context, graph, instances);
    requireProjectSuccess(result, 1);

    const ProjectGenerationInstanceResult generated = result.instances.first();
    requireGeneratedInstance(generated, QStringLiteral("finepaper.noc"), QStringLiteral("noc_0"));
    requireArtifact(generated.outputDirectory, QStringLiteral("rtl/top.v"));
    requireArtifact(generated.outputDirectory, QStringLiteral("filelist.f"));
    requireArtifact(generated.outputDirectory, QStringLiteral("manifest.json"));
    require(readText(QDir(generated.outputDirectory).filePath(QStringLiteral("rtl/top.v")))
                .contains(QStringLiteral("module top")),
            "finepaper.noc generated top should contain top module");
    require(readText(QDir(generated.outputDirectory).filePath(QStringLiteral("filelist.f")))
                .contains(QStringLiteral("rtl/top.v")),
            "finepaper.noc filelist should reference generated top");
    const QJsonObject manifest =
        readJsonObject(QDir(generated.outputDirectory).filePath(QStringLiteral("manifest.json")));
    require(manifest.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.noc"),
            "finepaper.noc output manifest should identify package");
    require(manifest.value(QStringLiteral("routers")).toInt() == 4,
            "finepaper.noc output manifest should count routers");
}

void testRaveNoCWorkflowGeneratesPublicArtifacts() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    const QString packageRoot = copyPackageToWorkspace(root, QStringLiteral("finepaper.ravenoc"));
    createRaveNoCVendorFixture(packageRoot);
    const WorkflowContext context = loadWorkflowContext(packageRoot, QStringLiteral("finepaper.ravenoc"));
    require(context.entry.instanceParameters.contains(QStringLiteral("flit_data_width")),
            "RaveNoC catalog should expose commercial parameters");
    require(context.entry.moduleTypes.contains(QStringLiteral("finepaper.ravenoc::RaveTile")),
            "RaveNoC tile module type should be scoped");

    Graph graph;
    applyMeshPreset(graph, context, QStringLiteral("ravenoc_0"), 2, 2);
    require(graph.modules().size() == 4, "RaveNoC mesh should create four tiles");
    require(graph.connections().size() == 4, "RaveNoC mesh should create four mesh links");

    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(context.entry, QStringLiteral("ravenoc_0"),
                       QJsonObject{{QStringLiteral("flit_data_width"), 32},
                                   {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}})
    };
    const ProjectGenerationResult result = runGeneration(root, context, graph, instances);
    requireProjectSuccess(result, 1);

    const ProjectGenerationInstanceResult generated = result.instances.first();
    requireGeneratedInstance(generated, QStringLiteral("finepaper.ravenoc"), QStringLiteral("ravenoc_0"));
    requireArtifact(generated.outputDirectory, QStringLiteral("ravenoc_config.svh"));
    requireArtifact(generated.outputDirectory, QStringLiteral("ravenoc_top.sv"));
    requireArtifact(generated.outputDirectory, QStringLiteral("ravenoc_filelist.f"));
    requireArtifact(generated.outputDirectory, QStringLiteral("manifest.json"));
    require(readText(QDir(generated.outputDirectory).filePath(QStringLiteral("ravenoc_config.svh")))
                .contains(QStringLiteral("`define NOC_CFG_SZ_ROWS 2")),
            "RaveNoC config should preserve mesh rows");
    require(readText(QDir(generated.outputDirectory).filePath(QStringLiteral("ravenoc_filelist.f")))
                .contains(QStringLiteral("ravenoc_top.sv")),
            "RaveNoC filelist should reference generated top");
    requireArtifact(generated.outputDirectory,
                    QStringLiteral("vendor/ravenoc/src/ravenoc.sv"));
    require(readText(QDir(generated.outputDirectory)
                         .filePath(QStringLiteral("vendor/ravenoc/src/ravenoc.sv")))
                .contains(QStringLiteral("fake RaveNoC vendor fixture: src/ravenoc.sv")),
            "RaveNoC generation should copy package vendor RTL");
    require(readText(QDir(generated.outputDirectory).filePath(QStringLiteral("ravenoc_filelist.f")))
                .contains(QStringLiteral("vendor/ravenoc/src/ravenoc.sv")),
            "RaveNoC filelist should reference copied vendor RTL");
    const QJsonObject manifest =
        readJsonObject(QDir(generated.outputDirectory).filePath(QStringLiteral("manifest.json")));
    require(manifest.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC output manifest should identify package");
    require(manifest.value(QStringLiteral("tiles")).toInt() == 4,
            "RaveNoC output manifest should count tiles");
}

void attachOpenNoCAgent(Graph& graph,
                        const WorkflowContext& context,
                        const QVector<ProjectIpInstanceRecord>& instances,
                        const QString& agentModule,
                        const QString& agentId,
                        const QString& externalId,
                        const QString& xpLogicalId,
                        const QString& xpPort,
                        int x,
                        int y) {
    const QString instanceId = QStringLiteral("opennoc_0");
    addModuleInstance(graph,
                      context.registry,
                      context.entry.id,
                      instanceId,
                      agentModule,
                      agentId,
                      externalId,
                      x,
                      y);
    addResolvedConnection(graph,
                          context.manifests,
                          agentId + QStringLiteral("_to_") + xpLogicalId + QLatin1Char('_') + xpPort,
                          PortRef{agentId, QStringLiteral("chi")},
                          PortRef{QStringLiteral("opennoc_0_%1").arg(xpLogicalId), xpPort});
}

void testOpenNoCWorkflowGeneratesPublicArtifacts() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    QDir root(tempDir.path());

    const QString packageRoot = copyPackageToWorkspace(root, QStringLiteral("finepaper.opennoc"));
    createOpenNoCVendorFixture(packageRoot);
    const WorkflowContext context = loadWorkflowContext(packageRoot, QStringLiteral("finepaper.opennoc"));
    require(context.entry.instanceParameters.contains(QStringLiteral("req_flit_width")),
            "OpenNoC catalog should expose flit parameters");
    require(context.entry.moduleTypes.contains(QStringLiteral("finepaper.opennoc::OpenNoCXP")),
            "OpenNoC XP module type should be scoped");

    Graph graph;
    applyMeshPreset(graph, context, QStringLiteral("opennoc_0"), 2, 2);
    require(graph.modules().size() == 4, "OpenNoC mesh should create four XPs");
    require(graph.connections().size() == 4, "OpenNoC mesh should create four mesh links");

    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(context.entry, QStringLiteral("opennoc_0"),
                       QJsonObject{{QStringLiteral("req_flit_width"), 128},
                                   {QStringLiteral("rsp_flit_width"), 64},
                                   {QStringLiteral("dat_flit_width"), 256},
                                   {QStringLiteral("snp_flit_width"), 128}})
    };

    attachOpenNoCAgent(graph, context, instances,
                       QStringLiteral("OpenNoCRNF"),
                       QStringLiteral("opennoc_0_rnf_0"),
                       QStringLiteral("rnf_0"),
                       QStringLiteral("xp_0_0"),
                       QStringLiteral("p0"),
                       -180,
                       -60);
    attachOpenNoCAgent(graph, context, instances,
                       QStringLiteral("OpenNoCRNI"),
                       QStringLiteral("opennoc_0_rni_0"),
                       QStringLiteral("rni_0"),
                       QStringLiteral("xp_0_0"),
                       QStringLiteral("p1"),
                       -180,
                       60);
    attachOpenNoCAgent(graph, context, instances,
                       QStringLiteral("OpenNoCHNF"),
                       QStringLiteral("opennoc_0_hnf_0"),
                       QStringLiteral("hnf_0"),
                       QStringLiteral("xp_0_1"),
                       QStringLiteral("p0"),
                       580,
                       -60);
    attachOpenNoCAgent(graph, context, instances,
                       QStringLiteral("OpenNoCHNI"),
                       QStringLiteral("opennoc_0_hni_0"),
                       QStringLiteral("hni_0"),
                       QStringLiteral("xp_1_0"),
                       QStringLiteral("p0"),
                       -180,
                       260);
    attachOpenNoCAgent(graph, context, instances,
                       QStringLiteral("OpenNoCSNF"),
                       QStringLiteral("opennoc_0_snf_0"),
                       QStringLiteral("snf_0"),
                       QStringLiteral("xp_1_1"),
                       QStringLiteral("p0"),
                       580,
                       260);

    const ProjectGenerationResult result = runGeneration(root, context, graph, instances);
    requireProjectSuccess(result, 1);

    const ProjectGenerationInstanceResult generated = result.instances.first();
    requireGeneratedInstance(generated, QStringLiteral("finepaper.opennoc"), QStringLiteral("opennoc_0"));
    requireArtifact(generated.outputDirectory, QStringLiteral("opennoc_mesh.json"));
    requireArtifact(generated.outputDirectory, QStringLiteral("opennoc_filelist.f"));
    requireArtifact(generated.outputDirectory, QStringLiteral("rtl/mesh_wrapper_2x2.sv"));
    requireArtifact(generated.outputDirectory, QStringLiteral("vendor/OpenNoC/rtl/src/rni/rni.v"));
    requireArtifact(generated.outputDirectory, QStringLiteral("manifest.json"));
    require(readText(QDir(generated.outputDirectory)
                         .filePath(QStringLiteral("rtl/mesh_wrapper_2x2.sv")))
                .contains(QStringLiteral("fake mesh wrapper")),
            "OpenNoC generation should run package vendor mesh generator");
    require(readText(QDir(generated.outputDirectory)
                         .filePath(QStringLiteral("vendor/OpenNoC/rtl/src/rni/rni.v")))
                .contains(QStringLiteral("fake OpenNoC vendor fixture: rtl/src/rni/rni.v")),
            "OpenNoC generation should copy package vendor RTL");
    const QString opennocFilelist =
        readText(QDir(generated.outputDirectory).filePath(QStringLiteral("opennoc_filelist.f")));
    require(opennocFilelist.contains(QStringLiteral("rtl/mesh_wrapper_2x2.sv")),
            "OpenNoC filelist should reference generated mesh wrapper");
    require(opennocFilelist.contains(QStringLiteral("vendor/OpenNoC/rtl/src/rni/rni.v")),
            "OpenNoC filelist should reference copied vendor RTL");
    const QJsonObject mesh =
        readJsonObject(QDir(generated.outputDirectory).filePath(QStringLiteral("opennoc_mesh.json")));
    require(mesh.value(QStringLiteral("xp_0_0")).toObject()
                .value(QStringLiteral("P0")).toString() == QStringLiteral("RNF"),
            "OpenNoC mesh should map RNF to XP0_0 P0");
    require(mesh.value(QStringLiteral("xp_0_0")).toObject()
                .value(QStringLiteral("P1")).toString() == QStringLiteral("RNI"),
            "OpenNoC mesh should map RNI to XP0_0 P1");
    require(mesh.value(QStringLiteral("xp_1_1")).toObject()
                .value(QStringLiteral("P0")).toString() == QStringLiteral("SNF"),
            "OpenNoC mesh should map SNF to XP1_1 P0");
    const QJsonObject manifest =
        readJsonObject(QDir(generated.outputDirectory).filePath(QStringLiteral("manifest.json")));
    require(manifest.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.opennoc"),
            "OpenNoC output manifest should identify package");
    require(manifest.value(QStringLiteral("rows")).toInt() == 2 &&
                manifest.value(QStringLiteral("cols")).toInt() == 2,
            "OpenNoC output manifest should record mesh dimensions");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testRequiredPackageIdsAreExplicit();
        testFinepaperNoCWorkflowGeneratesPublicArtifacts();
        testRaveNoCWorkflowGeneratesPublicArtifacts();
        testOpenNoCWorkflowGeneratesPublicArtifacts();
        std::cout << "commercial_noc_mvp_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
