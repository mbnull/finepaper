// Final V1 architecture gate for the repository IP-core mainline flow.
#include "app/projectgenerationrunner.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcoregraphexporter.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "project/graphprojectserializer.h"
#include "project/projectipservice.h"
#include "project/projectreader.h"
#include "project/projectstateservice.h"
#include "project/projectwriter.h"
#include "topology/topologypresetbuilder.h"
#include "validation/projectvalidationrunner.h"
#include "workspace/activeworkspacecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QList>
#include <QProcess>
#include <QTemporaryDir>
#include <QStringList>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
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

QString repositoryRootPath() {
    QDir root(QFileInfo(repositoryPath(QStringLiteral("qt/test/v1architecturegate_test.cpp")))
                  .absoluteDir());
    require(root.cdUp() && root.cdUp(),
            "repository root should be reachable from architecture gate source");
    return root.absolutePath();
}

const TopologyPresetDescriptor* findPreset(const QVector<TopologyPresetDescriptor>& presets,
                                           const QString& id) {
    const auto it = std::find_if(presets.cbegin(), presets.cend(), [&](const TopologyPresetDescriptor& preset) {
        return preset.id == id;
    });
    return it == presets.cend() ? nullptr : &(*it);
}

QStringList presetIds(const QVector<TopologyPresetDescriptor>& presets) {
    QStringList ids;
    for (const TopologyPresetDescriptor& preset : presets) {
        ids.append(preset.id);
    }
    ids.sort();
    return ids;
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

QStringList legacyRuntimeVocabularyTokens() {
    return {
        QStringLiteral("Plugin") + QStringLiteral("Registry"),
        QStringLiteral("Plugin") + QStringLiteral("Descriptor"),
        QStringLiteral("Plugin") + QStringLiteral("CommandDescriptor"),
        QStringLiteral("Plugin") + QStringLiteral("InstanceParameterDescriptor"),
        QStringLiteral("FINEPAPER_") + QStringLiteral("PLUGIN_PATH"),
        QStringLiteral("plugin") + QStringLiteral(".json"),
        QStringLiteral("ConnectionRuleLayer::") + QStringLiteral("FeaturePlugin")
    };
}

QString generatedRuntimeRootToken() {
    return QStringLiteral("generated") + QLatin1Char('/') + QStringLiteral("ipcores");
}

QString legacyRuntimeManifestToken() {
    return QStringLiteral("ipcore-runtime") + QStringLiteral(".json");
}

QString legacyModuleBundleToken() {
    return QStringLiteral("modules") + QStringLiteral(".xml");
}

QString legacyRuntimeEnvironmentToken() {
    return QStringLiteral("FINEPAPER_") + QStringLiteral("IPCORE_PATH");
}

QStringList maintainedRuntimeScanPaths() {
    return {
        QStringLiteral("spec_generator/README.md"),
        QStringLiteral("spec_generator/lib"),
        QStringLiteral("spec_generator/bin"),
        QStringLiteral("qt/inc/ipcore"),
        QStringLiteral("qt/src/ipcore"),
        QStringLiteral("qt/inc/modules"),
        QStringLiteral("qt/src/modules/moduleprovider.cpp"),
        QStringLiteral("qt/src/modules/moduleregistry.cpp"),
        QStringLiteral("qt/doc"),
        QStringLiteral("qt/test/ipcoreruntime_test.cpp"),
        QStringLiteral("qt/test/ipcatalogservice_test.cpp"),
        QStringLiteral("qt/test/topology_preset_test.cpp"),
        QStringLiteral("qt/test/v1architecturegate_test.cpp")
    };
}

QVector<QFileInfo> scanFilesForPath(const QString& relativePath) {
    const QFileInfo info(repositoryPath(relativePath));
    require(info.exists(),
            QStringLiteral("maintained runtime scan path should exist: %1")
                .arg(relativePath)
                .toLocal8Bit()
                .constData());

    if (info.isFile()) {
        return {info};
    }

    QVector<QFileInfo> files;
    QDirIterator iterator(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        files.push_back(iterator.fileInfo());
    }
    return files;
}

void testMaintainedRuntimeSurfaceDoesNotRequireGeneratedBundles() {
    const QStringList forbiddenTokens{
        generatedRuntimeRootToken(),
        legacyRuntimeManifestToken(),
        legacyModuleBundleToken(),
        legacyRuntimeEnvironmentToken()
    };

    QStringList violations;
    for (const QString& scanPath : maintainedRuntimeScanPaths()) {
        for (const QFileInfo& fileInfo : scanFilesForPath(scanPath)) {
            QFile file(fileInfo.absoluteFilePath());
            require(file.open(QIODevice::ReadOnly),
                    QStringLiteral("maintained runtime scan file should be readable: %1")
                        .arg(fileInfo.absoluteFilePath())
                        .toLocal8Bit()
                        .constData());
            const QString source = QString::fromUtf8(file.readAll());
            for (const QString& token : forbiddenTokens) {
                if (source.contains(token)) {
                    violations.append(QStringLiteral("%1 contains %2")
                                          .arg(fileInfo.absoluteFilePath(), token));
                }
            }
        }
    }

    const QFileInfo generatedBundleInfo(QDir(repositoryRootPath()).filePath(generatedRuntimeRootToken()));
    if (generatedBundleInfo.exists()) {
        violations.append(QStringLiteral("%1 exists")
                              .arg(generatedBundleInfo.absoluteFilePath()));
    }

    if (!violations.isEmpty()) {
        throw std::runtime_error(QStringLiteral("generated runtime bundle dependency remains:\n%1")
                                     .arg(violations.join(QStringLiteral("\n")))
                                     .toLocal8Bit()
                                     .constData());
    }
}

bool sameIpInstanceRecord(const ProjectIpInstanceRecord& left,
                          const ProjectIpInstanceRecord& right) {
    return left.ipcoreId == right.ipcoreId &&
           left.instanceId == right.instanceId &&
           left.schema == right.schema &&
           left.state == right.state;
}

bool containsRtlArtifact(const QStringList& artifactPaths) {
    return std::any_of(artifactPaths.cbegin(), artifactPaths.cend(), [](const QString& path) {
        return path.endsWith(QStringLiteral(".sv")) || path.endsWith(QStringLiteral(".v"));
    });
}

void testRuntimeVocabularyHasNoQtPluginManifestPath() {
    const QString qtRootPath =
        QFileInfo(repositoryPath(QStringLiteral("qt/test/v1architecturegate_test.cpp")))
            .absoluteDir()
            .absolutePath() + QStringLiteral("/..");
    const QFileInfo qtRootInfo(qtRootPath);
    require(qtRootInfo.isDir(), "qt source directory should exist for vocabulary scan");

    const QStringList tokens = legacyRuntimeVocabularyTokens();

    for (const QString& token : tokens) {
        QProcess process;
        process.start(QStringLiteral("rg"), QStringList{QStringLiteral("-n"), token, qtRootInfo.absoluteFilePath()});
        require(process.waitForFinished(),
                "Qt vocabulary scan should finish");
        if (process.exitCode() == 0) {
            const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            throw std::runtime_error(QStringLiteral("forbidden runtime token remains: %1\n%2")
                                         .arg(token, output)
                                         .toLocal8Bit()
                                         .constData());
        }
        if (process.exitCode() != 1) {
            const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            throw std::runtime_error(QStringLiteral("Qt vocabulary scan failed for %1: %2")
                                         .arg(token, error)
                                         .toLocal8Bit()
                                         .constData());
        }
    }
}

void testMainWindowDefaultPathDoesNotUseRuntimeSingleton() {
    QFile sourceFile(repositoryPath(QStringLiteral("qt/src/app/mainwindow.cpp")));
    require(sourceFile.open(QIODevice::ReadOnly),
            "MainWindow source should be readable for default-path architecture scan");
    const QString source = QString::fromUtf8(sourceFile.readAll());

    require(!source.contains(QStringLiteral("IpCoreRuntimeRegistry::instance().runtimes()")),
            "MainWindow default path should consume ipcraft catalog/package entries, not runtime singleton runtimes");
}

QString writeStubIpcraftCommand(const QString& directory) {
    const QString path = QDir(directory).filePath(QStringLiteral("stub-ipcraft-command.rb"));
    writeFile(path, QByteArrayLiteral(R"ruby(#!/usr/bin/env ruby
require 'fileutils'
require 'json'
input = nil
output = nil
ARGV.each_with_index do |arg, index|
  input = ARGV[index + 1] if arg == '-i'
  output = ARGV[index + 1] if arg == '-o'
end
abort 'missing input' unless input
document = JSON.parse(File.read(input))
abort "expected schema ipcraft.noc.project.v1" unless document['schema'] == 'ipcraft.noc.project.v1'
if output
  FileUtils.mkdir_p(output)
  File.write(File.join(output, 'stub.sv'), "module stub; endmodule\n")
end
puts 'stub command passed'
)ruby"));
    QFile::setPermissions(path,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);
    return path;
}

void replaceCommandsWithStub(IpCatalogEntry& entry, const QString& commandPath) {
    IpcraftCommandDescriptor command;
    command.executablePath = commandPath;
    command.resolvedExecutablePath = commandPath;
    command.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    command.args = {
        QStringLiteral("-i"),
        QStringLiteral("{input}"),
        QStringLiteral("-o"),
        QStringLiteral("{output}")
    };
    entry.packageManifest.commands.insert(QStringLiteral("validate"), command);
    entry.packageManifest.commands.insert(QStringLiteral("generate"), command);
    entry.drc.command = commandPath;
    entry.drc.inputFormat = command.inputSchema;
    entry.drc.args = command.args;
    entry.generator = entry.drc;
}

void replaceEntry(QList<IpCatalogEntry>& entries, const IpCatalogEntry& replacement) {
    for (IpCatalogEntry& entry : entries) {
        if (entry.id == replacement.id) {
            entry = replacement;
            return;
        }
    }
}

std::unique_ptr<Module> makeManualModule(const ModuleType& type,
                                         const QString& moduleId,
                                         const QString& logicalId,
                                         const QString& ipcoreId,
                                         const QString& instanceId) {
    auto module = std::make_unique<Module>(moduleId, type.name);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    if (module->parameters().contains(QStringLiteral("x"))) {
        module->setParameter(QStringLiteral("x"), 0);
    }
    if (module->parameters().contains(QStringLiteral("y"))) {
        module->setParameter(QStringLiteral("y"), 0);
    }
    if (module->parameters().contains(QStringLiteral("mesh_col"))) {
        module->setParameter(QStringLiteral("mesh_col"), 0);
    }
    if (module->parameters().contains(QStringLiteral("mesh_row"))) {
        module->setParameter(QStringLiteral("mesh_row"), 0);
    }
    if (module->parameters().contains(QStringLiteral("display_name"))) {
        module->setParameter(QStringLiteral("display_name"), logicalId);
    }
    if (module->parameters().contains(QStringLiteral("external_id"))) {
        module->setParameter(QStringLiteral("external_id"), logicalId);
    }
    return module;
}

void testRepositoryNoCMainlineFlow() {
    const QVector<IpcraftPackageManifest> packages =
        loadIpcraftPackageManifests({repositoryPath(QStringLiteral("ipcores"))});

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages(packages), "repository IP modules should load");
    ModuleRegistry::instance().loadIpcraftPackages(packages);

    IpCatalogService catalog(packages, &registry);
    std::optional<IpCatalogEntry> nocEntry = catalog.entry(QStringLiteral("finepaper.noc"));
    require(nocEntry.has_value(), "catalog should expose Finepaper NoC");
    require(nocEntry->moduleTypes.contains(QStringLiteral("XP")),
            "active NoC module list should include XP");
    require(nocEntry->moduleTypes.contains(QStringLiteral("Endpoint")),
            "active NoC module list should include Endpoint");
    require(nocEntry->generator.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "NoC generator should consume package project input");
    require(nocEntry->drc.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "NoC DRC should consume package project input");

    QTemporaryDir commandDir;
    require(commandDir.isValid(), "temporary command directory should be created");
    replaceCommandsWithStub(*nocEntry, writeStubIpcraftCommand(commandDir.path()));
    QList<IpCatalogEntry> catalogEntries = catalog.entries();
    replaceEntry(catalogEntries, *nocEntry);

    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ActiveWorkspaceController workspace(&projectIpService, &catalog);
    const ProjectIpServiceResult firstCreated = projectIpService.createInstanceForIpcore(*nocEntry);
    require(firstCreated.success, firstCreated.error.toLocal8Bit().constData());
    require(workspace.state().hasActiveIp, "workspace should activate selected NoC instance");
    require(workspace.state().ipcoreId == QStringLiteral("finepaper.noc"),
            "workspace should expose selected NoC id");
    require(workspace.state().moduleTypes == QStringList({
                QStringLiteral("Endpoint"),
                QStringLiteral("XP")
            }),
            "workspace should expose only selected NoC modules");
    require(presetIds(workspace.state().topologyPresets) == QStringList({
                QStringLiteral("mesh"),
                QStringLiteral("ring")
            }),
            "workspace should expose only selected NoC topology presets");

    const TopologyPresetDescriptor* mesh =
        findPreset(workspace.state().topologyPresets, QStringLiteral("mesh"));
    require(mesh != nullptr, "NoC workspace should expose mesh preset");
    const ProjectIpServiceResult secondCreated = projectIpService.createInstanceForIpcore(*nocEntry);
    require(secondCreated.success, secondCreated.error.toLocal8Bit().constData());
    require(stateService.ipInstanceRecords().size() == 2,
            "gate project should contain at least two IP instances");
    require(workspace.state().instanceId == secondCreated.record.instanceId,
            "workspace should select the newest NoC instance");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = firstCreated.record.ipcoreId;
    request.instanceId = firstCreated.record.instanceId;
    request.preset = *mesh;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);
    const TopologyPresetResult topology = TopologyPresetBuilder::apply(&graph, registry, request);
    require(topology.success, topology.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "mesh topology should create four routers");
    require(graph.connections().size() == 4, "mesh topology should create four router links");

    const ModuleType* xpType =
        registry.getTypeForGraphGroup(secondCreated.record.ipcoreId, QStringLiteral("xps"));
    require(xpType != nullptr, "second NoC instance should expose a router module type");
    const QString manualModuleId =
        secondCreated.record.instanceId + QStringLiteral("_manual_xp");
    require(graph.addModule(makeManualModule(*xpType,
                                             manualModuleId,
                                             QStringLiteral("manual_xp"),
                                             secondCreated.record.ipcoreId,
                                             secondCreated.record.instanceId)),
            "manual module should add for second IP instance");
    require(graph.modules().size() == 5, "manual second instance module should join the graph");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary gate directory should be created");
    ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("v1_gate"));
    stateService.writeToDocument(document);
    const QString projectPath = QDir(tempDir.path()).filePath(QStringLiteral("v1_gate.fpproj"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    require(writeResult.success, writeResult.error.toLocal8Bit().constData());
    const ProjectReadResult readResult = ProjectReader::readFile(projectPath);
    require(readResult.success, readResult.error.toLocal8Bit().constData());

    Graph restored;
    const GraphProjectLoadResult loadResult =
        GraphProjectSerializer::loadProject(readResult.document, restored);
    require(loadResult.success, loadResult.error.toLocal8Bit().constData());
    require(restored.modules().size() == graph.modules().size(),
            "project load should restore topology and manual modules");
    require(restored.connections().size() == graph.connections().size(),
            "project load should restore topology connections");
    require(readResult.document.ipcoreState.size() == 2,
            "project load should preserve all IP-instance state");
    for (qsizetype index = 0; index < readResult.document.ipcoreState.size(); ++index) {
        require(sameIpInstanceRecord(readResult.document.ipcoreState.at(index),
                                     stateService.ipInstanceRecords().at(index)),
                "project load should restore IP-instance state payloads");
    }

    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &restored,
            *nocEntry,
            readResult.document.ipcoreState.first(),
            QStringLiteral("v1_gate"),
            nullptr
        });
    require(exportResult.success, exportResult.error.toLocal8Bit().constData());
    require(exportResult.document.object().value(QStringLiteral("schema")).toString() ==
                QStringLiteral("ipcraft.noc.project.v1"),
            "generator input should use package project schema");

    ProjectValidationRunner validationRunner;
    const QList<ValidationResult> validationResults =
        validationRunner.validate(&restored, catalogEntries, readResult.document.ipcoreState);
    for (const ValidationResult& result : validationResults) {
        require(result.severity() != ValidationSeverity::Error,
                result.message().toLocal8Bit().constData());
    }

    ProjectGenerationRequest generationRequest;
    generationRequest.graph = &restored;
    generationRequest.projectPath = projectPath;
    generationRequest.designName = QStringLiteral("v1_gate");
    generationRequest.outputRoot = QDir(tempDir.path()).filePath(QStringLiteral("project_generated"));
    generationRequest.catalogEntries = catalogEntries;
    generationRequest.instances = readResult.document.ipcoreState;

    const ProjectGenerationResult generationResult =
        ProjectGenerationRunner().generate(generationRequest);
    require(generationResult.success, generationResult.error.toLocal8Bit().constData());
    require(generationResult.instances.size() == 2,
            "project generation should run for both IP instances");
    for (const ProjectGenerationInstanceResult& instanceResult : generationResult.instances) {
        require(instanceResult.success, instanceResult.error.toLocal8Bit().constData());
        require(QFileInfo::exists(instanceResult.inputPath),
                "project generation should persist each IP-core graph input");
        require(QFileInfo::exists(instanceResult.manifestPath),
                "project generation should persist each generation manifest");
        require(!instanceResult.artifactPaths.isEmpty(),
                "project generation should report generated artifacts for each instance");
        require(containsRtlArtifact(instanceResult.artifactPaths),
                "project generation should report RTL artifacts for each instance");
    }
    require(QFileInfo::exists(generationResult.snapshotPath),
            "project generation should write a generated project snapshot");
    const ProjectReadResult snapshotRead = ProjectReader::readFile(generationResult.snapshotPath);
    require(snapshotRead.success, snapshotRead.error.toLocal8Bit().constData());
    require(snapshotRead.document.ipcoreState.size() == 2,
            "generated project snapshot should preserve every IP-instance state");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMaintainedRuntimeSurfaceDoesNotRequireGeneratedBundles();
        testRuntimeVocabularyHasNoQtPluginManifestPath();
        testMainWindowDefaultPathDoesNotUseRuntimeSingleton();
        testRepositoryNoCMainlineFlow();
    } catch (const std::exception& error) {
        std::cerr << "v1architecturegate_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "v1architecturegate_test passed\n";
    return 0;
}
