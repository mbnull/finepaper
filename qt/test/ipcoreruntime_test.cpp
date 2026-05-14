// IP core runtime tests for manifest discovery and command metadata.
#include "app/appsettings.h"
#include "graph/graph.h"
#include "ipcore/ipcorecommandrunner.h"
#include "project/ipinstanceparameteradapter.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "ipcore/ipcoreruntimediagnostics.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <variant>

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

QString repositoryRuntimePath(const QString& relativeRuntimePath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativeRuntimePath));
            if (info.isDir()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativeRuntimePath)).absoluteFilePath();
}

IpCatalogEntry entryFromRuntime(const IpCoreRuntimeDescriptor& runtime) {
    IpCatalogEntry entry;
    entry.id = runtime.id;
    entry.name = runtime.name;
    entry.version = runtime.version;
    entry.kind = runtime.kind;
    entry.runtimeRootPath = runtime.runtimeRootPath;
    entry.sourceRootPath = runtime.sourceRootPath;
    entry.modulesPath = runtime.modulesPath;
    entry.graphicsPath = runtime.graphicsPath;
    entry.generator = runtime.generator;
    entry.drc = runtime.drc;
    entry.topologyPresets = runtime.topologyPresets;
    return entry;
}

void testIpCoreRuntimeManifestLoadsRuntimeAndSourcePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("generated/finepaper.demo/graphics")),
            "failed to create runtime dirs");
    require(root.mkpath(QStringLiteral("ipcores/demo")),
            "failed to create source dirs");
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/modules.xml")),
              QByteArrayLiteral("<module-bundle/>"));
    const QString staleRuntimeManifestName = QStringLiteral("plugin") + QStringLiteral(".json");
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/") + staleRuntimeManifestName),
              QByteArrayLiteral(R"json({"id":"stale.plugin","source_root":"."})json"));
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "kind": "noc",
      "source_root": "../../ipcores/demo",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {
        "command": "ruby",
        "input_format": "ipcore_graph_v1",
        "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]
      },
      "drc": {
        "command": "ruby",
        "input_format": "ipcore_graph_v1",
        "args": ["generator/bin/drc", "-i", "{input}"]
      },
      "topology_presets": []
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes =
        IpCoreRuntimeRegistry::discover({root.filePath(QStringLiteral("generated"))});

    require(runtimes.size() == 1, "expected one IP core runtime");
    require(runtimes.first().id == QStringLiteral("finepaper.demo"),
            "runtime id should come from ipcore-runtime.json");
    require(runtimes.first().runtimeRootPath.endsWith(QStringLiteral("generated/finepaper.demo")),
            "runtime root should be manifest directory");
    require(QFileInfo(runtimes.first().modulesPath).isAbsolute(),
            "modules path should be absolute");
    require(runtimes.first().generator.hasCommand(),
            "generator command should be retained");
    require(runtimes.first().drc.hasCommand(),
            "DRC command should be retained");

    const QString sourceRoot =
        QFileInfo(root.filePath(QStringLiteral("ipcores/demo"))).absoluteFilePath();
    const IpCatalogEntry entry = entryFromRuntime(runtimes.first());

    const IpCoreResolvedCommand generatorCommand =
        IpCoreCommandRunner::resolveGenerator(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(generatorCommand.valid, "generator command should resolve for discovered IP core");
    require(generatorCommand.ipcoreId == QStringLiteral("finepaper.demo"),
            "generator command should carry IP-core id");
    require(generatorCommand.workingDirectory == sourceRoot,
            "generator working directory should use source root");

    const IpCoreResolvedCommand drcCommand =
        IpCoreCommandRunner::resolveDrc(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(drcCommand.valid, "DRC command should resolve for discovered IP core");
    require(drcCommand.ipcoreId == QStringLiteral("finepaper.demo"),
            "DRC command should carry IP-core id");
    require(drcCommand.workingDirectory == sourceRoot,
            "DRC working directory should use source root");
}

void testModuleTypesKeepRuntimeOwnershipAndSkipDuplicates() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("first")), "failed to create first runtime");
    require(root.mkpath(QStringLiteral("second")), "failed to create second runtime");
    const QByteArray moduleXml = QByteArrayLiteral(R"xml(<module-bundle>
      <module name="Shared" palette_label="Shared" graph_group="demo">
        <ports><port id="in" direction="input" type="bus" name="IN"/></ports>
        <parameters><parameter name="width" type="int" default="32"/></parameters>
      </module>
    </module-bundle>)xml");
    writeFile(root.filePath(QStringLiteral("first/modules.xml")), moduleXml);
    writeFile(root.filePath(QStringLiteral("second/modules.xml")), moduleXml);
    writeFile(root.filePath(QStringLiteral("first/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({"id":"first","name":"First","version":"1","source_root":".","modules":"modules.xml"})json"));
    writeFile(root.filePath(QStringLiteral("second/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({"id":"second","name":"Second","version":"1","source_root":".","modules":"modules.xml"})json"));

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(IpCoreRuntimeRegistry::discover({temp.path()}));

    const ModuleType* type = registry.getType(QStringLiteral("Shared"));
    require(type != nullptr, "Shared type should load");
    require(type->ipcoreId == QStringLiteral("first"), "duplicate type should keep first IP-core owner");
    require(registry.availableTypes().size() == 1, "duplicate type name should be skipped");
}

void testDuplicateRuntimeIdsKeepFirstDiscoveredRuntime() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("first")), "failed to create first runtime");
    require(root.mkpath(QStringLiteral("second")), "failed to create second runtime");
    require(root.mkpath(QStringLiteral("sources/first")), "failed to create first source root");
    require(root.mkpath(QStringLiteral("sources/second")), "failed to create second source root");
    writeFile(root.filePath(QStringLiteral("first/modules.xml")), QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("second/modules.xml")), QByteArrayLiteral("<module-bundle/>"));

    writeFile(root.filePath(QStringLiteral("first/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id":"finepaper.duplicate",
      "name":"First Runtime",
      "version":"1",
      "source_root":"../sources/first",
      "modules":"modules.xml"
    })json"));
    writeFile(root.filePath(QStringLiteral("second/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id":"finepaper.duplicate",
      "name":"Second Runtime",
      "version":"2",
      "source_root":"../sources/second",
      "modules":"modules.xml"
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({temp.path()});

    require(runtimes.size() == 1, "duplicate runtime ids should be skipped after the first runtime");
    require(runtimes.first().id == QStringLiteral("finepaper.duplicate"),
            "retained runtime should keep the duplicate id");
    require(runtimes.first().name == QStringLiteral("First Runtime"),
            "first discovered runtime should win duplicate id conflicts");
    require(runtimes.first().runtimeRootPath == QFileInfo(root.filePath(QStringLiteral("first"))).absoluteFilePath(),
            "retained duplicate runtime should keep the first runtime root");
    require(runtimes.first().sourceRootPath == QFileInfo(root.filePath(QStringLiteral("sources/first"))).absoluteFilePath(),
            "retained duplicate runtime should keep the first source root");
}

void testJsonModuleBundlesAreIgnored() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("json_bundle")), "failed to create JSON bundle runtime");
    writeFile(root.filePath(QStringLiteral("json_bundle/modules.json")),
              QByteArrayLiteral(R"json([{"name":"JsonBundle","ports":[],"parameters":[]}])json"));
    writeFile(root.filePath(QStringLiteral("json_bundle/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({"id":"json_bundle","name":"JsonBundle","version":"1","source_root":".","modules":"modules.json"})json"));

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const bool loaded = registry.loadIpCoreRuntimes(IpCoreRuntimeRegistry::discover({temp.path()}));

    require(!loaded, "JSON module bundles should no longer load");
    require(registry.availableTypes().isEmpty(), "JSON module type should be ignored");
}

void testRuntimeManifestWithoutSourceRootIsSkipped() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("legacy")), "failed to create legacy runtime");
    writeFile(root.filePath(QStringLiteral("legacy/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({"id":"legacy","name":"Legacy","version":"1","modules":"modules.xml"})json"));

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({temp.path()});

    require(runtimes.empty(), "manifest without source_root should be skipped");
}

void testRuntimeManifestWithMissingSourceRootDirectoryIsSkipped() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("missing_source")), "failed to create runtime directory");
    writeFile(root.filePath(QStringLiteral("missing_source/modules.xml")),
              QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("missing_source/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id":"missing.source",
      "name":"Missing Source",
      "version":"1",
      "source_root":"../does-not-exist",
      "modules":"modules.xml"
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({temp.path()});

    require(runtimes.empty(), "manifest with missing source_root directory should be skipped");
}

void testRuntimeManifestWithMissingModulesFileIsSkipped() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("missing_modules/source")), "failed to create source directory");
    require(root.mkpath(QStringLiteral("missing_modules/runtime")), "failed to create runtime directory");
    writeFile(root.filePath(QStringLiteral("missing_modules/runtime/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id":"missing.modules",
      "name":"Missing Modules",
      "version":"1",
      "source_root":"../source",
      "modules":"modules.xml"
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes =
        IpCoreRuntimeRegistry::discover({root.filePath(QStringLiteral("missing_modules/runtime"))});

    require(runtimes.empty(), "manifest with missing modules file should be skipped");
}

void testRuntimeManifestWithMissingDeclaredGraphicsDirectoryClearsGraphicsPath() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("missing_graphics/source")), "failed to create source directory");
    require(root.mkpath(QStringLiteral("missing_graphics/runtime")), "failed to create runtime directory");
    writeFile(root.filePath(QStringLiteral("missing_graphics/runtime/modules.xml")),
              QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("missing_graphics/runtime/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id":"missing.graphics",
      "name":"Missing Graphics",
      "version":"1",
      "source_root":"../source",
      "modules":"modules.xml",
      "graphics":"missing-graphics"
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes =
        IpCoreRuntimeRegistry::discover({root.filePath(QStringLiteral("missing_graphics/runtime"))});

    require(runtimes.size() == 1, "runtime with missing declared graphics directory should still load");
    require(runtimes.first().id == QStringLiteral("missing.graphics"),
            "loaded runtime should keep manifest id");
    require(runtimes.first().graphicsPath.isEmpty(),
            "missing declared graphics directory should clear graphics path");
}

void testModuleRegistryListsTypesByRuntime() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);

    ModuleType nocType;
    nocType.name = QStringLiteral("XP");
    nocType.ipcoreId = QStringLiteral("finepaper.noc");
    nocType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(nocType), "noc type should register");

    ModuleType ravenType;
    ravenType.name = QStringLiteral("RaveTile");
    ravenType.ipcoreId = QStringLiteral("finepaper.ravenoc");
    ravenType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(ravenType), "ravenoc type should register");

    const QStringList nocTypes = registry.availableTypesForIpcore(QStringLiteral("finepaper.noc"));
    const QStringList ravenTypes = registry.availableTypesForIpcore(QStringLiteral("finepaper.ravenoc"));

    require(nocTypes == QStringList{QStringLiteral("XP")},
            "NoC active IP should only list NoC module types");
    require(ravenTypes == QStringList{QStringLiteral("RaveTile")},
            "RaveNoC active IP should only list RaveNoC module types");

    const ModuleType* ravenRouter =
        registry.getTypeForGraphGroup(QStringLiteral("finepaper.ravenoc"), QStringLiteral("xps"));
    require(ravenRouter && ravenRouter->name == QStringLiteral("RaveTile"),
            "graph group lookup should be scoped by IP-core id");
}

void testGeneratorArgumentsSubstituteInputAndOutput() {
    IpCoreCommandDescriptor generator;
    generator.command = QStringLiteral("ruby");
    generator.args = {
        QStringLiteral("generator/bin/generate"),
        QStringLiteral("-i"),
        QStringLiteral("{input}"),
        QStringLiteral("-o"),
        QStringLiteral("{output}"),
        QStringLiteral("-t"),
        QStringLiteral("generator/template")
    };

    const QStringList args = generator.arguments(QStringLiteral("/tmp/design.json"),
                                                 QStringLiteral("/tmp/out"));

    require(args.contains(QStringLiteral("/tmp/design.json")), "input placeholder should be substituted");
    require(args.contains(QStringLiteral("/tmp/out")), "output placeholder should be substituted");
    require(args.first() == QStringLiteral("generator/bin/generate"), "literal relative args should be preserved");
}

void testIpCoreCommandRunnerPropagatesInputFormat() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.format");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-format-ipcore");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.generator.args = {QStringLiteral("generator/bin/generate")};

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "generator command should resolve");
    require(command.ipcoreId == QStringLiteral("finepaper.format"),
            "resolved command should carry IP-core id");
    require(command.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "resolved command should carry IP-core graph input format");
    require(command.workingDirectory == entry.sourceRootPath,
            "resolved command should use IP core source root");
}

void testIpCoreCommandRunnerRejectsMissingCommands() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.missing");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-missing-ipcore");

    const IpCoreResolvedCommand generatorCommand =
        IpCoreCommandRunner::resolveGenerator(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(!generatorCommand.valid, "missing generator declaration should be rejected");
    require(generatorCommand.errorMessage ==
                QStringLiteral("IP core 'finepaper.missing' does not declare a generator."),
            "missing generator error should be explicit");

    const IpCoreResolvedCommand drcCommand =
        IpCoreCommandRunner::resolveDrc(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(!drcCommand.valid, "missing DRC declaration should be rejected");
    require(drcCommand.errorMessage ==
                QStringLiteral("IP core 'finepaper.missing' does not declare a DRC command."),
            "missing DRC error should be explicit");
}

void testIpCoreCommandRunnerRejectsUnsupportedGeneratorInputFormat() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.unsupported-generator");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-unsupported-generator");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("generic_graph_v1");
    entry.generator.args = {QStringLiteral("generator/bin/generate")};

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(!command.valid, "unsupported generator input_format should be rejected");
    require(command.errorMessage.contains(QStringLiteral("finepaper.unsupported-generator")),
            "unsupported generator error should mention IP core id");
    require(command.errorMessage.contains(QStringLiteral("generic_graph_v1")),
            "unsupported generator error should mention input format");
}

void testIpCoreCommandRunnerResolvesDrcCommand() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.drc");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-drc-ipcore");
    entry.drc.command = QStringLiteral("ruby");
    entry.drc.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.drc.args = {QStringLiteral("generator/bin/drc"), QStringLiteral("-i"), QStringLiteral("{input}")};

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveDrc(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "DRC command should resolve");
    require(command.ipcoreId == QStringLiteral("finepaper.drc"),
            "resolved DRC command should carry IP-core id");
    require(command.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "resolved DRC command should carry IP-core graph input format");
    require(command.workingDirectory == entry.sourceRootPath,
            "resolved DRC command should use IP core source root");
    require(command.arguments.contains(QStringLiteral("/tmp/in.json")),
            "resolved DRC command should substitute input");
}

void testIpCoreCommandRunnerRejectsUnsupportedDrcInputFormat() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.unsupported-drc");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-unsupported-drc");
    entry.drc.command = QStringLiteral("ruby");
    entry.drc.inputFormat = QStringLiteral("generic_graph_v1");
    entry.drc.args = {QStringLiteral("generator/bin/drc")};

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveDrc(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(!command.valid, "unsupported DRC input_format should be rejected");
    require(command.errorMessage.contains(QStringLiteral("finepaper.unsupported-drc")),
            "unsupported DRC error should mention IP core id");
    require(command.errorMessage.contains(QStringLiteral("generic_graph_v1")),
            "unsupported DRC error should mention input format");
}

void testIpCoreCommandRunnerRejectsIpcraftProjectSchemaWithoutPackageManifest() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.runtime-ipcraft-schema");
    entry.sourceRootPath = QStringLiteral("/tmp/finepaper-runtime-ipcraft-schema");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("ipcraft.noc.project.v1");
    entry.generator.args = {QStringLiteral("generator/bin/generate")};

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(!command.valid,
            "ipcraft project schema should only be bridged for package manifest commands");
    require(command.errorMessage.contains(QStringLiteral("ipcraft.noc.project.v1")),
            "unsupported package schema error should mention input format");
}

void testIpcraftPackageLoadingRejectsMissingDeclaredViews() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary package root should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("broken")), "failed to create broken package");
    writeFile(root.filePath(QStringLiteral("broken/ipcraft.json")),
              QByteArrayLiteral(R"json({
      "schema": "ipcraft.manifest.v1",
      "id": "finepaper.brokenview",
      "name": "Broken View",
      "version": "1.0",
      "modules": [
        {"id": "Tile", "name": "Tile", "interfaces": []}
      ],
      "views": [
        {"module": "Tile", "file": "views/Missing.xml"}
      ]
    })json"));

    const QVector<IpcraftPackageManifest> manifests =
        loadIpcraftPackageManifests({temp.path()});

    require(manifests.isEmpty(),
            "default ipcraft package loading should reject packages with missing declared views");
}

void testIpcraftPackageLoadingRejectsInvalidDeclaredViewXml() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary package root should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("broken/views")), "failed to create broken package views");
    writeFile(root.filePath(QStringLiteral("broken/views/Tile.xml")),
              QByteArrayLiteral(R"xml(<module-view module="Tile">
      <anchors><anchor ref="missing_interface" x="0.5" y="0.5"/></anchors>
    </module-view>)xml"));
    writeFile(root.filePath(QStringLiteral("broken/ipcraft.json")),
              QByteArrayLiteral(R"json({
      "schema": "ipcraft.manifest.v1",
      "id": "finepaper.invalidview",
      "name": "Invalid View",
      "version": "1.0",
      "modules": [
        {"id": "Tile", "name": "Tile", "interfaces": [{"id": "valid"}]}
      ],
      "views": [
        {"module": "Tile", "file": "views/Tile.xml"}
      ]
    })json"));

    const QVector<IpcraftPackageManifest> manifests =
        loadIpcraftPackageManifests({temp.path()});

    require(manifests.isEmpty(),
            "default ipcraft package loading should reject packages with invalid declared view XML");
}

void testDefaultRuntimeRootDiscoveryKeepsEnvironmentRootsForCompatibility() {
    const QByteArray previous = qgetenv("FINEPAPER_IPCORE_PATH");
    QTemporaryDir extra;
    require(extra.isValid(), "temporary runtime root should be valid");
    qputenv("FINEPAPER_IPCORE_PATH", extra.path().toLocal8Bit());

    const QStringList roots = IpCoreRuntimeRegistry::defaultRuntimeRoots();

    require(roots.contains(QFileInfo(extra.path()).absoluteFilePath()),
            "FINEPAPER_IPCORE_PATH should be included");
    for (const QString& root : roots) {
        require(!root.endsWith(QStringLiteral("/plugins")),
                "runtime roots should not scan plugins directories");
    }

    if (previous.isEmpty()) {
        qunsetenv("FINEPAPER_IPCORE_PATH");
    } else {
        qputenv("FINEPAPER_IPCORE_PATH", previous);
    }
}

void testDefaultDiscoveryIncludesAppSettingsIpcorePaths() {
    const QByteArray previous = qgetenv("FINEPAPER_IPCORE_PATH");
    QTemporaryDir settingsRoot;
    QTemporaryDir envRoot;
    QTemporaryDir appSettingsRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");
    require(envRoot.isValid(), "temporary env runtime root should be valid");
    require(appSettingsRoot.isValid(), "temporary app settings runtime root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcoreruntime_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcoreruntime_test_app"));
    AppSettings().setIpcorePaths(QStringList{appSettingsRoot.path()});
    qputenv("FINEPAPER_IPCORE_PATH", envRoot.path().toLocal8Bit());

    const QStringList roots = IpCoreRuntimeRegistry::defaultRuntimeRoots();
    const QString envPath = QFileInfo(envRoot.path()).absoluteFilePath();
    const QString appSettingsPath = QFileInfo(appSettingsRoot.path()).absoluteFilePath();
    const qsizetype envIndex = roots.indexOf(envPath);
    const qsizetype appSettingsIndex = roots.indexOf(appSettingsPath);

    require(envIndex >= 0, "FINEPAPER_IPCORE_PATH root should be included");
    require(appSettingsIndex >= 0, "AppSettings IP core root should be included");
    require(envIndex < appSettingsIndex,
            "AppSettings IP core roots should come after environment roots");

    if (previous.isEmpty()) {
        qunsetenv("FINEPAPER_IPCORE_PATH");
    } else {
        qputenv("FINEPAPER_IPCORE_PATH", previous);
    }
}

void testDefaultRuntimeRootDiscoveryDoesNotScanRepositoryGeneratedIpcores() {
    const QByteArray previous = qgetenv("FINEPAPER_IPCORE_PATH");
    qunsetenv("FINEPAPER_IPCORE_PATH");

    QTemporaryDir settingsRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcoreruntime_test_org_no_generated"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcoreruntime_test_app_no_generated"));
    AppSettings().setIpcorePaths(QStringList{});

    const QString generatedRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores"));
    require(QFileInfo(generatedRoot).isDir(), "repository generated/ipcores test fixture should exist");

    const QStringList roots = IpCoreRuntimeRegistry::defaultRuntimeRoots();
    const QString absoluteGeneratedRoot = QFileInfo(generatedRoot).absoluteFilePath();
    require(!roots.contains(absoluteGeneratedRoot),
            "default runtime roots should not include repository generated/ipcores");
    for (const QString& root : roots) {
        require(!QDir::fromNativeSeparators(root).contains(QStringLiteral("/generated/ipcores")),
                "default runtime roots should not scan generated/ipcores descendants");
    }

    if (previous.isEmpty()) {
        qunsetenv("FINEPAPER_IPCORE_PATH");
    } else {
        qputenv("FINEPAPER_IPCORE_PATH", previous);
    }
}

void testCompatibilityDiscoveryFindsExplicitGeneratedIpCoreRuntimes() {
    const QString generatedRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores"));

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({generatedRoot});
    const auto nocIt = std::find_if(runtimes.cbegin(), runtimes.cend(), [](const IpCoreRuntimeDescriptor& runtime) {
        return runtime.id == QStringLiteral("finepaper.noc");
    });
    const auto ravenIt = std::find_if(runtimes.cbegin(), runtimes.cend(), [](const IpCoreRuntimeDescriptor& runtime) {
        return runtime.id == QStringLiteral("finepaper.ravenoc");
    });

    require(nocIt != runtimes.cend(), "generated NoC bundle should be discoverable explicitly");
    require(ravenIt != runtimes.cend(), "generated RaveNoC bundle should be discoverable explicitly");
    require(nocIt->runtimeRootPath == repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.noc")),
            "explicit NoC compatibility discovery should load the generated runtime bundle");
    require(ravenIt->runtimeRootPath == repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.ravenoc")),
            "explicit RaveNoC compatibility discovery should load the generated runtime bundle");
}

void testRepositoryFinepaperNoCIpCoreMetadataLoads() {
    const QString ipcoreRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.noc"));
    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({ipcoreRoot});

    require(runtimes.size() == 1, "Finepaper NoC IP core should be discovered");
    require(runtimes.first().id == QStringLiteral("finepaper.noc"),
            "Finepaper NoC IP core id should load");
    require(runtimes.first().runtimeRootPath == ipcoreRoot,
            "Finepaper NoC runtime root should be generated bundle directory");
    require(runtimes.first().sourceRootPath == repositoryRuntimePath(QStringLiteral("ipcores/finepaper-noc")),
            "Finepaper NoC source root should resolve to concrete IP source package");
    require(runtimes.first().modulesPath == QFileInfo(QDir(ipcoreRoot).filePath(QStringLiteral("modules.xml"))).absoluteFilePath(),
            "Finepaper NoC modules should resolve against generated runtime bundle");
    require(runtimes.first().graphicsPath == QFileInfo(QDir(ipcoreRoot).filePath(QStringLiteral("graphics"))).absoluteFilePath(),
            "Finepaper NoC graphics should resolve against generated runtime bundle");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(runtimes);

    const QStringList nocTypes = registry.availableTypesForIpcore(QStringLiteral("finepaper.noc"));
    require(nocTypes == QStringList({QStringLiteral("Endpoint"), QStringLiteral("XP")}),
            "Finepaper NoC IP core should list its internal editable module types");

    const ModuleType* xpType = registry.getType(QStringLiteral("XP"));
    require(xpType != nullptr, "XP module type should load");
    require(xpType->ipcoreId == QStringLiteral("finepaper.noc"),
            "XP module should keep IP core ownership");
    require(xpType->graphGroup == QStringLiteral("xps"),
            "XP should participate as the NoC router graph group");
    const ModuleInterfaceMetadata localInterface =
        xpType->interfaceMetadata.value(QStringLiteral("local0"));
    require(localInterface.cardinality == QStringLiteral("one"),
            "XP local0 interface should declare one endpoint attachment");
    require(localInterface.autocompleteGroup == QStringLiteral("endpoint_attachment"),
            "XP local0 interface should declare endpoint attachment autocomplete group");
}

void testRepositoryRaveNoCIpCoreMetadataLoads() {
    const QString runtimeRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.ravenoc"));
    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({runtimeRoot});

    require(runtimes.size() == 1, "RaveNoC IP core should be discovered");
    require(runtimes.first().id == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC IP core id should load");
    require(runtimes.first().runtimeRootPath == runtimeRoot,
            "RaveNoC runtime root should be generated bundle directory");
    require(runtimes.first().sourceRootPath == repositoryRuntimePath(QStringLiteral("ipcores/ravenoc")),
            "RaveNoC source root should resolve to concrete IP source package");
    require(runtimes.first().modulesPath == QFileInfo(QDir(runtimeRoot).filePath(QStringLiteral("modules.xml"))).absoluteFilePath(),
            "RaveNoC modules should resolve against generated runtime bundle");
    require(runtimes.first().graphicsPath == QFileInfo(QDir(runtimeRoot).filePath(QStringLiteral("graphics"))).absoluteFilePath(),
            "RaveNoC graphics should resolve against generated runtime bundle");
    require(runtimes.first().kind == QStringLiteral("noc"),
            "RaveNoC IP core kind should load");
    require(runtimes.first().instanceParameters.contains(QStringLiteral("flit_data_width")),
            "RaveNoC instance flit width should load");
    require(std::get<int>(runtimes.first().instanceParameters.value(QStringLiteral("flit_data_width")).defaultValue) == 32,
            "RaveNoC flit width default should load");
    require(runtimes.first().instanceParameters.value(QStringLiteral("routing_algorithm")).choices.size() == 2,
            "RaveNoC routing algorithm choices should load as instance metadata");
    require(runtimes.first().generator.command == QStringLiteral("ruby"),
            "RaveNoC IP core should use Ruby generator");
    require(runtimes.first().generator.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "RaveNoC IP core should request IP-core graph input");
    require(runtimes.first().drc.command == QStringLiteral("ruby"),
            "RaveNoC IP core should use Ruby DRC");
    require(runtimes.first().drc.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "RaveNoC DRC should request IP-core graph input");
    require(runtimes.first().topologyPresets.size() == 1,
            "RaveNoC IP core should expose topology presets");
    require(runtimes.first().topologyPresets.first().routerModule == QStringLiteral("RaveTile"),
            "RaveNoC mesh preset should create RaveTile routers");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(runtimes);

    const QStringList ravenTypes = registry.availableTypesForIpcore(QStringLiteral("finepaper.ravenoc"));
    require(ravenTypes == QStringList({QStringLiteral("RaveEndpoint"), QStringLiteral("RaveTile")}),
            "RaveNoC active IP should list only its internal editable module types");

    const ModuleType* tileType = registry.getType(QStringLiteral("RaveTile"));
    require(tileType != nullptr, "RaveTile module type should load");
    require(tileType->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "RaveTile module should keep IP core ownership");
    require(tileType->graphGroup == QStringLiteral("xps"),
            "RaveTile should participate as the RaveNoC router graph group");
    require(!tileType->defaultParameters.contains(QStringLiteral("routing_algorithm")),
            "RaveTile should not own fabric-wide routing algorithm parameter");
    const ModuleInterfaceMetadata eastInterface =
        tileType->interfaceMetadata.value(QStringLiteral("east"));
    require(eastInterface.cardinality == QStringLiteral("one"),
            "RaveTile east interface should declare one connection");
    require(eastInterface.autocompleteGroup == QStringLiteral("router_side"),
            "RaveTile east interface should declare router_side autocomplete group");
    require(eastInterface.topologyRule == QStringLiteral("opposite_side"),
            "RaveTile east interface should declare opposite_side topology rule");

    const ModuleInterfaceMetadata localInterface =
        tileType->interfaceMetadata.value(QStringLiteral("local"));
    require(localInterface.cardinality == QStringLiteral("one"),
            "RaveTile local interface should declare one endpoint attachment");
    require(localInterface.autocompleteGroup == QStringLiteral("endpoint_attachment"),
            "RaveTile local interface should declare endpoint attachment autocomplete group");

    const ModuleType* endpointType = registry.getType(QStringLiteral("RaveEndpoint"));
    require(endpointType != nullptr, "RaveEndpoint module type should load");
    require(endpointType->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "RaveEndpoint module should keep IP core ownership");
    require(endpointType->graphGroup == QStringLiteral("endpoints"),
            "RaveEndpoint should participate as an editable endpoint graph group");
}

void testRepositoryOpenNoCIpCoreMetadataLoads() {
    const QString runtimeRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.opennoc"));
    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({runtimeRoot});

    require(runtimes.size() == 1, "OpenNoC IP core should be discovered");
    require(runtimes.first().id == QStringLiteral("finepaper.opennoc"),
            "OpenNoC IP core id should load");
    require(runtimes.first().runtimeRootPath == runtimeRoot,
            "OpenNoC runtime root should be generated bundle directory");
    require(runtimes.first().sourceRootPath == repositoryRuntimePath(QStringLiteral("ipcores/opennoc")),
            "OpenNoC source root should resolve to concrete IP source package");
    require(runtimes.first().kind == QStringLiteral("noc"),
            "OpenNoC IP core kind should load");
    require(runtimes.first().instanceParameters.contains(QStringLiteral("req_flit_width")),
            "OpenNoC req flit width should load");
    require(std::get<int>(runtimes.first().instanceParameters.value(QStringLiteral("req_flit_width")).defaultValue) == 128,
            "OpenNoC req flit width default should load");
    require(runtimes.first().generator.command == QStringLiteral("ruby"),
            "OpenNoC IP core should use Ruby generator");
    require(runtimes.first().generator.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "OpenNoC generator should request IP-core graph input");
    require(runtimes.first().drc.command == QStringLiteral("ruby"),
            "OpenNoC IP core should use Ruby DRC");
    require(runtimes.first().drc.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "OpenNoC DRC should request IP-core graph input");
    require(runtimes.first().topologyPresets.size() == 1,
            "OpenNoC IP core should expose one topology preset");
    require(runtimes.first().topologyPresets.first().routerModule == QStringLiteral("OpenNoCXP"),
            "OpenNoC mesh preset should create OpenNoCXP routers");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(runtimes);

    const QStringList types = registry.availableTypesForIpcore(QStringLiteral("finepaper.opennoc"));
    require(types == QStringList({
                QStringLiteral("OpenNoCHNF"),
                QStringLiteral("OpenNoCHNI"),
                QStringLiteral("OpenNoCRNF"),
                QStringLiteral("OpenNoCRNI"),
                QStringLiteral("OpenNoCSNF"),
                QStringLiteral("OpenNoCXP")
            }),
            "OpenNoC active IP should list XP plus five agent module types");

    const ModuleType* xpType = registry.getType(QStringLiteral("OpenNoCXP"));
    require(xpType != nullptr, "OpenNoCXP module type should load");
    require(xpType->ipcoreId == QStringLiteral("finepaper.opennoc"),
            "OpenNoCXP should keep IP core ownership");
    require(xpType->graphGroup == QStringLiteral("xps"),
            "OpenNoCXP should participate as the router graph group");
    require(xpType->editorLayout == QStringLiteral("mesh_router"),
            "OpenNoCXP should use the mesh-router editor layout");
    require(xpType->supportsCollapse,
            "OpenNoCXP should support collapse/expand presentation");
    require(xpType->interfaceMetadata.value(QStringLiteral("east")).topologyRule == QStringLiteral("opposite_side"),
            "OpenNoCXP east should declare opposite_side topology rule");
    require(xpType->interfaceMetadata.value(QStringLiteral("p0")).cardinality == QStringLiteral("one"),
            "OpenNoCXP p0 should declare one attachment");

    const ModuleType* rniType = registry.getType(QStringLiteral("OpenNoCRNI"));
    require(rniType != nullptr, "OpenNoCRNI module type should load");
    require(rniType->graphGroup == QStringLiteral("endpoints"),
            "OpenNoC agents should use endpoint presentation grouping");
    require(rniType->editorLayout == QStringLiteral("endpoint"),
            "OpenNoC agents should use endpoint editor layout");
    require(rniType->interfaceMetadata.value(QStringLiteral("chi")).autocompleteGroup == QStringLiteral("endpoint_attachment"),
            "OpenNoCRNI CHI interface should use endpoint attachment autocomplete");
}

void testRuntimeIpInstanceAdapterExposesGlobalParameterSection() {
    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.ravenoc");
    runtime.name = QStringLiteral("RaveNoC");

    IpCoreInstanceParameterDescriptor routing;
    routing.name = QStringLiteral("routing_algorithm");
    routing.type = QStringLiteral("string");
    routing.defaultValue = QStringLiteral("xy");
    routing.description = QStringLiteral("Routing algorithm");
    routing.choices = {
        IpCoreInstanceParameterChoice{QStringLiteral("west_first"), QStringLiteral("West-first")},
        IpCoreInstanceParameterChoice{QStringLiteral("xy"), QStringLiteral("XY")}
    };
    routing.configurable = false;
    runtime.instanceParameters.insert(routing.name, routing);

    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    width.configurable = true;
    runtime.instanceParameters.insert(width.name, width);

    RuntimeIpInstanceParameterAdapter adapter(runtime);
    const QVector<IpInstanceParameterSection> sections = adapter.parameterSections();

    require(sections.size() == 1, "adapter should expose one global parameter section");
    require(sections.first().ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "section should retain IP-core id");
    require(sections.first().instanceId == QStringLiteral("ravenoc_0"),
            "section instance id should use stable IP-core id suffix");
    require(sections.first().id == QStringLiteral("global_parameters"),
            "section id should identify global parameters");
    require(sections.first().label == QStringLiteral("RaveNoC"),
            "section label should use IP-core name");
    require(sections.first().expandedByDefault,
            "section should be expanded by default");
    require(sections.first().fields.size() == 2,
            "section should expose manifest fields");

    const IpInstanceParameterField& first = sections.first().fields.first();
    require(first.name == QStringLiteral("flit_data_width"),
            "section fields should be sorted deterministically by name");
    require(first.label == QStringLiteral("Flit data width"),
            "field should retain manifest label");
    require(first.type == QStringLiteral("int"),
            "field should retain manifest type");
    require(std::get<int>(first.defaultValue) == 32,
            "field should retain manifest default value");
    require(first.configurable,
            "field should retain manifest configurable flag");

    const IpInstanceParameterField& second = sections.first().fields.last();
    require(second.name == QStringLiteral("routing_algorithm"),
            "second sorted field should match routing parameter");
    require(second.label == QStringLiteral("routing_algorithm"),
            "field label should fall back to parameter name");
    require(second.description == QStringLiteral("Routing algorithm"),
            "field should retain manifest description");
    require(second.choices.size() == 2,
            "field should retain manifest choices");
    require(!second.configurable,
            "field should retain false configurable flag");
}

void testRuntimeIpInstanceAdapterUsesIpcoreIdLabelAndSkipsEmptyParameters() {
    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.empty");

    RuntimeIpInstanceParameterAdapter adapter(runtime);
    require(adapter.parameterSections().isEmpty(),
            "adapter should not expose sections without instance parameters");

    IpCoreInstanceParameterDescriptor mode;
    mode.name = QStringLiteral("mode");
    mode.type = QStringLiteral("string");
    mode.defaultValue = QStringLiteral("basic");
    runtime.instanceParameters.insert(mode.name, mode);

    RuntimeIpInstanceParameterAdapter namedAdapter(runtime);
    const QVector<IpInstanceParameterSection> sections = namedAdapter.parameterSections();
    require(sections.size() == 1, "adapter should expose one section after parameter is added");
    require(sections.first().label == QStringLiteral("finepaper.empty"),
            "section label should fall back to IP-core id");
}

void testCatalogIpInstanceAdapterExposesPackageParameterSection() {
    QHash<QString, IpCoreInstanceParameterDescriptor> parameters;

    IpCoreInstanceParameterDescriptor routing;
    routing.name = QStringLiteral("routing_algorithm");
    routing.type = QStringLiteral("string");
    routing.defaultValue = QStringLiteral("xy");
    routing.label = QStringLiteral("Routing algorithm");
    parameters.insert(routing.name, routing);

    CatalogIpInstanceParameterAdapter adapter(QStringLiteral("finepaper.ravenoc"),
                                              QStringLiteral("RaveNoC"),
                                              parameters);
    const QVector<IpInstanceParameterSection> sections = adapter.parameterSections();

    require(sections.size() == 1, "catalog adapter should expose one global parameter section");
    require(sections.first().ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "catalog adapter section should retain package id");
    require(sections.first().instanceId == QStringLiteral("ravenoc_0"),
            "catalog adapter section should use stable package id suffix");
    require(sections.first().label == QStringLiteral("RaveNoC"),
            "catalog adapter section should use package display name");
    require(sections.first().fields.size() == 1,
            "catalog adapter should expose package parameters");
    require(sections.first().fields.first().name == QStringLiteral("routing_algorithm"),
            "catalog adapter field should retain package parameter name");
}

void testIpCoreRuntimeDiagnosticsListLoadedRuntimesAndIpTypes() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("demo/graphics")), "failed to create runtime dirs");
    writeFile(root.filePath(QStringLiteral("demo/modules.xml")),
              QByteArrayLiteral(R"xml(<module-bundle>
      <module name="Shared" palette_label="Shared" graph_group="demo">
        <interfaces>
          <interface id="bus" bus="demo_bus" role="initiator" connects_to="target" match="" />
        </interfaces>
        <ports><port id="out" direction="output" type="bus" name="OUT" interface="bus"/></ports>
        <parameters><parameter name="width" type="int" default="32"/></parameters>
      </module>
    </module-bundle>)xml"));
    writeFile(root.filePath(QStringLiteral("demo/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "source_root": ".",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {"command": "ruby", "args": ["generator/bin/generate"]}
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({temp.path()});
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(runtimes);

    const QStringList lines = IpCoreRuntimeDiagnostics::logLines(runtimes, registry);

    require(lines.join('\n').contains(QStringLiteral("[Startup] IP core runtime finepaper.demo")),
            "startup diagnostics should include loaded IP-core id");
    require(lines.join('\n').contains(QStringLiteral("Demo v1.0")),
            "startup diagnostics should include runtime display name and version");
    require(lines.join('\n').contains(QStringLiteral("[Startup] IP Shared")),
            "startup diagnostics should include loaded IP type");
    require(lines.join('\n').contains(QStringLiteral("ipcore=finepaper.demo")),
            "startup diagnostics should include IP-core owner");
    require(lines.join('\n').contains(QStringLiteral("ports=1 parameters=1 interfaces=1")),
            "startup diagnostics should include IP metadata counts");
}

void testIpCoreRuntimeDiagnosticsMarksJsonModuleBundlesUnsupported() {
    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.jsonbundle");
    runtime.name = QStringLiteral("JsonBundle");
    runtime.version = QStringLiteral("0.1");
    runtime.modulesPath = QStringLiteral("/tmp/modules.json");

    const QStringList lines = IpCoreRuntimeDiagnostics::runtimeLogLines({runtime});

    require(lines.join('\n').contains(QStringLiteral("bundle=json(unsupported)")),
            "JSON module bundle format should be marked unsupported");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testIpCoreRuntimeManifestLoadsRuntimeAndSourcePaths();
        testModuleTypesKeepRuntimeOwnershipAndSkipDuplicates();
        testDuplicateRuntimeIdsKeepFirstDiscoveredRuntime();
        testJsonModuleBundlesAreIgnored();
        testRuntimeManifestWithoutSourceRootIsSkipped();
        testRuntimeManifestWithMissingSourceRootDirectoryIsSkipped();
        testRuntimeManifestWithMissingModulesFileIsSkipped();
        testRuntimeManifestWithMissingDeclaredGraphicsDirectoryClearsGraphicsPath();
        testModuleRegistryListsTypesByRuntime();
        testGeneratorArgumentsSubstituteInputAndOutput();
        testIpCoreCommandRunnerPropagatesInputFormat();
        testIpCoreCommandRunnerRejectsMissingCommands();
        testIpCoreCommandRunnerRejectsUnsupportedGeneratorInputFormat();
        testIpCoreCommandRunnerResolvesDrcCommand();
        testIpCoreCommandRunnerRejectsUnsupportedDrcInputFormat();
        testIpCoreCommandRunnerRejectsIpcraftProjectSchemaWithoutPackageManifest();
        testIpcraftPackageLoadingRejectsMissingDeclaredViews();
        testIpcraftPackageLoadingRejectsInvalidDeclaredViewXml();
        testDefaultRuntimeRootDiscoveryKeepsEnvironmentRootsForCompatibility();
        testDefaultDiscoveryIncludesAppSettingsIpcorePaths();
        testDefaultRuntimeRootDiscoveryDoesNotScanRepositoryGeneratedIpcores();
        testCompatibilityDiscoveryFindsExplicitGeneratedIpCoreRuntimes();
        testRepositoryFinepaperNoCIpCoreMetadataLoads();
        testRepositoryRaveNoCIpCoreMetadataLoads();
        testRepositoryOpenNoCIpCoreMetadataLoads();
        testRuntimeIpInstanceAdapterExposesGlobalParameterSection();
        testRuntimeIpInstanceAdapterUsesIpcoreIdLabelAndSkipsEmptyParameters();
        testCatalogIpInstanceAdapterExposesPackageParameterSection();
        testIpCoreRuntimeDiagnosticsListLoadedRuntimesAndIpTypes();
        testIpCoreRuntimeDiagnosticsMarksJsonModuleBundlesUnsupported();
    } catch (const std::exception& error) {
        std::cerr << "ipcoreruntime_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcoreruntime_test passed\n";
    return 0;
}
