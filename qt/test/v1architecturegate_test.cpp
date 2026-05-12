// Final V1 architecture gate for the repository IP-core mainline flow.
#include "app/generationartifacts.h"
#include "graph/graph.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcorecommandrunner.h"
#include "ipcore/ipcoregraphexporter.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "modules/moduleregistry.h"
#include "project/graphprojectserializer.h"
#include "project/projectipservice.h"
#include "project/projectreader.h"
#include "project/projectstateservice.h"
#include "project/projectwriter.h"
#include "topology/topologypresetbuilder.h"
#include "validation/drcrunner.h"
#include "workspace/activeworkspacecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QList>
#include <QProcess>
#include <QTemporaryDir>
#include <QStringList>
#include <algorithm>
#include <iostream>
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

const IpCoreRuntimeDescriptor* findRuntime(const QList<IpCoreRuntimeDescriptor>& runtimes, const QString& id) {
    const auto it = std::find_if(runtimes.cbegin(), runtimes.cend(), [&](const IpCoreRuntimeDescriptor& runtime) {
        return runtime.id == id;
    });
    return it == runtimes.cend() ? nullptr : &(*it);
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

void writeJsonFile(const QString& path, const QJsonDocument& document) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "gate JSON input should open for writing");
    const QByteArray bytes = document.toJson();
    require(file.write(bytes) == bytes.size(), "gate JSON input should be written completely");
}

QString runCommand(const IpCoreResolvedCommand& command) {
    QProcess process;
    process.setWorkingDirectory(command.workingDirectory);
    process.start(command.command, command.arguments);
    if (!process.waitForStarted()) {
        return QStringLiteral("process failed to start: ") + process.errorString();
    }
    if (!process.waitForFinished(-1)) {
        return QStringLiteral("process did not finish: ") + process.errorString();
    }
    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return QStringLiteral("process failed: stdout=%1 stderr=%2").arg(stdoutText, stderrText);
    }
    return {};
}

void testRepositoryNoCMainlineFlow() {
    const QList<IpCoreRuntimeDescriptor> runtimes =
        IpCoreRuntimeRegistry::discover({repositoryPath(QStringLiteral("generated/ipcores"))});
    const IpCoreRuntimeDescriptor* nocRuntime = findRuntime(runtimes, QStringLiteral("finepaper.noc"));
    require(nocRuntime != nullptr, "Finepaper NoC IP core should be discovered");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpCoreRuntimes(runtimes), "repository IP modules should load");
    ModuleRegistry::instance().loadIpCoreRuntimes(runtimes);

    IpCatalogService catalog(runtimes, &registry);
    const std::optional<IpCatalogEntry> nocEntry = catalog.entry(QStringLiteral("finepaper.noc"));
    require(nocEntry.has_value(), "catalog should expose Finepaper NoC");
    require(nocEntry->moduleTypes.contains(QStringLiteral("XP")),
            "active NoC module list should include XP");
    require(nocEntry->moduleTypes.contains(QStringLiteral("Endpoint")),
            "active NoC module list should include Endpoint");
    require(nocEntry->generator.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "NoC generator should consume IP-core graph input");
    require(nocEntry->drc.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "NoC DRC should consume IP-core graph input");

    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    ActiveWorkspaceController workspace(&projectIpService, &catalog);
    const ProjectIpServiceResult created = projectIpService.createInstanceForIpcore(*nocEntry);
    require(created.success, created.error.toLocal8Bit().constData());
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
    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = workspace.state().ipcoreId;
    request.instanceId = workspace.state().instanceId;
    request.preset = *mesh;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);
    const TopologyPresetResult topology = TopologyPresetBuilder::apply(&graph, registry, request);
    require(topology.success, topology.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "mesh topology should create four routers");
    require(graph.connections().size() == 4, "mesh topology should create four router links");

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
            "project load should restore topology modules");
    require(restored.connections().size() == graph.connections().size(),
            "project load should restore topology connections");
    require(readResult.document.ipcoreState.size() == 1,
            "project load should preserve selected IP-instance state");
    const ProjectIpInstanceRecord& originalInstance = stateService.ipInstanceRecords().first();
    const ProjectIpInstanceRecord& restoredInstance = readResult.document.ipcoreState.first();
    require(restoredInstance.ipcoreId == originalInstance.ipcoreId,
            "project load should restore selected IP-core owner");
    require(restoredInstance.instanceId == originalInstance.instanceId,
            "project load should restore selected instance id");
    require(restoredInstance.schema == originalInstance.schema,
            "project load should restore selected IP-instance schema");
    require(restoredInstance.state == originalInstance.state,
            "project load should restore selected IP-instance state payload");

    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &restored,
            *nocEntry,
            restoredInstance,
            QStringLiteral("v1_gate"),
            nullptr
        });
    require(exportResult.success, exportResult.error.toLocal8Bit().constData());
    require(exportResult.document.object().value(QStringLiteral("schema")).toString() ==
                QStringLiteral("finepaper-ipcore-graph-v1"),
            "generator input should use final IP-core schema");

    DRCRunner drcRunner;
    const QList<ValidationResult> validationResults =
        drcRunner.validate(&restored, *nocEntry, restoredInstance);
    for (const ValidationResult& result : validationResults) {
        require(result.severity() != ValidationSeverity::Error,
                result.message().toLocal8Bit().constData());
    }

    const QString inputPath = QDir(tempDir.path()).filePath(QStringLiteral("v1_gate.json"));
    writeJsonFile(inputPath, exportResult.document);
    const QString outputPath = QDir(tempDir.path()).filePath(QStringLiteral("generated"));
    require(QDir().mkpath(outputPath), "generated output directory should be created");
    const IpCoreResolvedCommand generator =
        IpCoreCommandRunner::resolveGenerator(*nocEntry, inputPath, outputPath);
    require(generator.valid, generator.errorMessage.toLocal8Bit().constData());
    const QString generatorError = runCommand(generator);
    require(generatorError.isEmpty(), generatorError.toLocal8Bit().constData());
    require(!QDir(outputPath).entryList(QStringList{
                QStringLiteral("*.sv"),
                QStringLiteral("*.v")
            }, QDir::Files).isEmpty(),
            "generator should write RTL files");

    const GeneratedProjectSnapshotResult snapshot =
        writeGeneratedProjectSnapshot(restored,
                                      outputPath,
                                      QStringLiteral("v1_gate"),
                                      QVector<ProjectIpInstanceRecord>{restoredInstance});
    require(snapshot.success, snapshot.error.toLocal8Bit().constData());
    require(QFileInfo::exists(snapshot.path), "generated project snapshot should exist");
    const ProjectReadResult snapshotRead = ProjectReader::readFile(snapshot.path);
    require(snapshotRead.success, snapshotRead.error.toLocal8Bit().constData());
    require(snapshotRead.document.ipcoreState.size() == 1,
            "generated project snapshot should preserve IP-instance state");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRuntimeVocabularyHasNoQtPluginManifestPath();
        testRepositoryNoCMainlineFlow();
    } catch (const std::exception& error) {
        std::cerr << "v1architecturegate_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "v1architecturegate_test passed\n";
    return 0;
}
