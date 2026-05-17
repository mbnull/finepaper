// IP core compatibility tests for package-driven runtime support.
#include "app/appsettings.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcorecommandrunner.h"
#include "ipcore/ipcoreruntimediagnostics.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "project/ipinstanceparameteradapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <variant>
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

void makeExecutable(const QString& path) {
    QFile file(path);
    require(file.setPermissions(QFile::ReadOwner |
                                QFile::WriteOwner |
                                QFile::ExeOwner |
                                QFile::ReadGroup |
                                QFile::ExeGroup |
                                QFile::ReadOther |
                                QFile::ExeOther),
            "failed to make test framework tool executable");
}

void testRuntimeRegistryDefaultRootsComeFromAppSettings() {
    QTemporaryDir settingsRoot;
    QTemporaryDir appSettingsRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");
    require(appSettingsRoot.isValid(), "temporary app settings root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcoreruntime_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcoreruntime_test_app"));
    AppSettings().setIpcorePaths(QStringList{appSettingsRoot.path()});

    const QStringList roots = IpCoreRuntimeRegistry::defaultRuntimeRoots();
    const QString appSettingsPath = QFileInfo(appSettingsRoot.path()).absoluteFilePath();

    require(roots == QStringList{appSettingsPath},
            "runtime compatibility roots should mirror AppSettings IP core roots");
}

void testDefaultIpcraftPackageRootsDoNotWalkLocalIpcoreDirectories() {
    QTemporaryDir settingsRoot;
    QTemporaryDir localRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");
    require(localRoot.isValid(), "temporary local root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("default_ipcraft_roots_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("default_ipcraft_roots_test_app"));
    AppSettings().setIpcorePaths({});

    QDir localDir(localRoot.path());
    require(localDir.mkpath(QStringLiteral("ipcores")),
            "local ipcores directory should be created");
    require(localDir.mkpath(QStringLiteral("nested/project")),
            "nested current directory should be created");

    const QString previousCurrentPath = QDir::currentPath();
    require(QDir::setCurrent(localDir.filePath(QStringLiteral("nested/project"))),
            "current directory should switch to nested test project");
    const QStringList roots = defaultIpcraftPackageRoots();
    QDir::setCurrent(previousCurrentPath);

    require(roots.isEmpty(),
            "default ipcraft package roots should come only from AppSettings, not local ipcores directories");
}

void testRuntimeRegistryDoesNotDiscoverDescriptorBundles() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({temp.path()});

    require(runtimes.isEmpty(),
            "runtime compatibility registry should not discover descriptor bundles");
}

void testModuleRegistryListsTypesByRuntimeDescriptor() {
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

    require(registry.availableTypesForIpcore(QStringLiteral("finepaper.noc")) ==
                QStringList{QStringLiteral("XP")},
            "NoC active IP should only list NoC module types");
    require(registry.availableTypesForIpcore(QStringLiteral("finepaper.ravenoc")) ==
                QStringList{QStringLiteral("RaveTile")},
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
        QStringLiteral("{output}")
    };

    const QStringList args = generator.arguments(QStringLiteral("/tmp/design.json"),
                                                 QStringLiteral("/tmp/out"));

    require(args.contains(QStringLiteral("/tmp/design.json")), "input placeholder should be substituted");
    require(args.contains(QStringLiteral("/tmp/out")), "output placeholder should be substituted");
    require(args.first() == QStringLiteral("generator/bin/generate"), "literal relative args should be preserved");
}

void testIpCoreCommandRunnerPropagatesGraphInputFormat() {
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
    require(command.inputSchema == QStringLiteral("finepaper-ipcore-graph-v1"),
            "resolved command should expose exported graph schema");
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

    require(!command.valid, "unsupported generator input format should be rejected");
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
    require(command.command == QStringLiteral("ruby"), "DRC command should retain executable");
    require(command.arguments.contains(QStringLiteral("/tmp/in.json")),
            "DRC input placeholder should be substituted");
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
            "unsupported schema error should name the package schema");
}

void testIpCoreCommandRunnerResolvesIpcraftPackageCommand() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    const QString inputPath = QDir(temp.path()).filePath(QStringLiteral("input.json"));
    writeFile(inputPath, QByteArrayLiteral(R"json({"schema":"ipcraft.noc.project.v1"})json"));

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.package");
    entry.sourceRootPath = temp.path();
    entry.packageManifest.id = entry.id;

    IpcraftCommandDescriptor commandDescriptor;
    commandDescriptor.executablePath = QStringLiteral("tools/generate");
    commandDescriptor.resolvedExecutablePath = QStringLiteral("/tmp/tools/generate");
    commandDescriptor.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    commandDescriptor.args = {QStringLiteral("--in"), QStringLiteral("{input}")};
    entry.packageManifest.commands.insert(QStringLiteral("generate"), commandDescriptor);

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry, inputPath, temp.path());

    require(command.valid, command.errorMessage.toLocal8Bit().constData());
    require(command.command == QStringLiteral("/tmp/tools/generate"),
            "package command should use resolved executable path");
    require(command.inputSchema == QStringLiteral("ipcraft.noc.project.v1"),
            "package command should expose package project schema");
    require(command.arguments.contains(inputPath),
            "package command should substitute input placeholder");
}

void testIpCoreCommandRunnerResolvesPackageFrameworkToolGenerateCommand() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("framework-tools")), "framework tool directory should be created");
    require(root.mkpath(QStringLiteral("package")), "package directory should be created");

    const QString toolsDir = root.filePath(QStringLiteral("framework-tools"));
    const QString toolPath = QDir(toolsDir).filePath(QStringLiteral("ipcraft-generate"));
    writeFile(toolPath, QByteArrayLiteral("#!/bin/sh\nexit 0\n"));
    makeExecutable(toolPath);

    const QString packageRoot = root.filePath(QStringLiteral("package"));
    const QString manifestPath = QDir(packageRoot).filePath(QStringLiteral("ipcraft.json"));
    writeFile(manifestPath, QByteArrayLiteral("{}\n"));
    const QString inputPath = root.filePath(QStringLiteral("input.json"));
    writeFile(inputPath, QByteArrayLiteral(R"json({"schema":"ipcraft.noc.project.v1"})json"));
    const QString outputPath = root.filePath(QStringLiteral("generated"));

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.framework-tool");
    entry.sourceRootPath = packageRoot;
    entry.packageManifest.id = entry.id;
    entry.packageManifest.packageRootPath = packageRoot;

    IpcraftCommandDescriptor commandDescriptor;
    commandDescriptor.frameworkTool = QStringLiteral("ipcraft-generate");
    commandDescriptor.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    commandDescriptor.args = {
        QStringLiteral("--manifest"),
        QStringLiteral("{manifest}"),
        QStringLiteral("--input"),
        QStringLiteral("{input}"),
        QStringLiteral("--output"),
        QStringLiteral("{output}")
    };
    entry.packageManifest.commands.insert(QStringLiteral("generate"), commandDescriptor);

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry, inputPath, outputPath, QStringList{toolsDir});

    require(command.valid, command.errorMessage.toLocal8Bit().constData());
    require(command.command == toolPath,
            "framework_tool command should resolve from injected search path");
    require(command.arguments.contains(manifestPath),
            "framework_tool command should substitute manifest placeholder");
    require(command.arguments.contains(inputPath),
            "framework_tool command should substitute input placeholder");
    require(command.arguments.contains(outputPath),
            "framework_tool command should substitute output placeholder");
}

void testIpCoreCommandRunnerReportsFrameworkToolSearchPaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("package")), "package directory should be created");

    const QString packageRoot = root.filePath(QStringLiteral("package"));
    const QString inputPath = root.filePath(QStringLiteral("input.json"));
    writeFile(inputPath, QByteArrayLiteral(R"json({"schema":"ipcraft.noc.project.v1"})json"));
    const QString firstSearchPath = root.filePath(QStringLiteral("framework-tools-a"));
    const QString secondSearchPath = root.filePath(QStringLiteral("framework-tools-b"));

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.missing-framework-tool");
    entry.sourceRootPath = packageRoot;
    entry.packageManifest.id = entry.id;
    entry.packageManifest.packageRootPath = packageRoot;

    IpcraftCommandDescriptor commandDescriptor;
    commandDescriptor.frameworkTool = QStringLiteral("ipcraft-generate");
    commandDescriptor.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    entry.packageManifest.commands.insert(QStringLiteral("generate"), commandDescriptor);

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveGenerator(entry,
                                              inputPath,
                                              root.filePath(QStringLiteral("generated")),
                                              QStringList{firstSearchPath, secondSearchPath});

    require(!command.valid, "missing framework tool should fail resolution");
    require(command.errorMessage.contains(QStringLiteral("Searched paths")),
            "missing framework tool diagnostic should mention searched paths");
    require(command.errorMessage.contains(QFileInfo(firstSearchPath).absoluteFilePath()) &&
                command.errorMessage.contains(QFileInfo(secondSearchPath).absoluteFilePath()),
            "missing framework tool diagnostic should list every searched path");
}

void testIpcraftPackageLoadingRejectsMissingDeclaredViews() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("broken")), "failed to create broken package");
    writeFile(root.filePath(QStringLiteral("broken/ipcraft.json")),
              QByteArrayLiteral(R"json({
      "schema": "ipcraft.manifest.v1",
      "id": "finepaper.missingview",
      "name": "Missing View",
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
            "default ipcraft package loading should reject packages with missing declared views");
}

void testIpcraftPackageLoadingRejectsInvalidDeclaredViewXml() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

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
    require(sections.first().fields.size() == 2,
            "section should expose manifest fields");
    require(sections.first().fields.first().name == QStringLiteral("flit_data_width"),
            "section fields should be sorted deterministically by name");
    require(std::get<int>(sections.first().fields.first().defaultValue) == 32,
            "field should retain manifest default value");
    require(!sections.first().fields.last().configurable,
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
    require(sections.first().label == QStringLiteral("RaveNoC"),
            "catalog adapter section should use package display name");
    require(sections.first().fields.first().name == QStringLiteral("routing_algorithm"),
            "catalog adapter field should retain package parameter name");
}

void testIpCoreRuntimeDiagnosticsListLoadedPackagesAndIpTypes() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    ModuleType type;
    type.name = QStringLiteral("Shared");
    type.ipcoreId = QStringLiteral("finepaper.demo");
    type.graphGroup = QStringLiteral("demo");
    type.defaultPorts = {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("OUT"))};
    type.defaultParameters.insert(QStringLiteral("width"), Parameter(QStringLiteral("width"), 32));
    ModuleInterfaceMetadata interfaceMetadata;
    interfaceMetadata.id = QStringLiteral("bus");
    type.interfaceMetadata.insert(interfaceMetadata.id, interfaceMetadata);
    require(registry.registerType(type), "test IP type should register");

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.demo");
    entry.name = QStringLiteral("Demo");
    entry.version = QStringLiteral("1.0");
    entry.runtimeRootPath = QStringLiteral("/tmp/ipcores/demo");
    entry.moduleTypes = {QStringLiteral("Shared")};
    entry.generator.command = QStringLiteral("ruby");

    const QStringList lines = IpCoreRuntimeDiagnostics::logLines(QList<IpCatalogEntry>{entry}, registry);

    require(lines.join('\n').contains(QStringLiteral("[Startup] IP core package finepaper.demo")),
            "startup diagnostics should include loaded IP-core package id");
    require(lines.join('\n').contains(QStringLiteral("Demo v1.0")),
            "startup diagnostics should include package display name and version");
    require(lines.join('\n').contains(QStringLiteral("[Startup] IP Shared")),
            "startup diagnostics should include loaded IP type");
    require(lines.join('\n').contains(QStringLiteral("ports=1 parameters=1 interfaces=1")),
            "startup diagnostics should include IP metadata counts");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testRuntimeRegistryDefaultRootsComeFromAppSettings();
        testDefaultIpcraftPackageRootsDoNotWalkLocalIpcoreDirectories();
        testRuntimeRegistryDoesNotDiscoverDescriptorBundles();
        testModuleRegistryListsTypesByRuntimeDescriptor();
        testGeneratorArgumentsSubstituteInputAndOutput();
        testIpCoreCommandRunnerPropagatesGraphInputFormat();
        testIpCoreCommandRunnerRejectsMissingCommands();
        testIpCoreCommandRunnerRejectsUnsupportedGeneratorInputFormat();
        testIpCoreCommandRunnerResolvesDrcCommand();
        testIpCoreCommandRunnerRejectsIpcraftProjectSchemaWithoutPackageManifest();
        testIpCoreCommandRunnerResolvesIpcraftPackageCommand();
        testIpCoreCommandRunnerResolvesPackageFrameworkToolGenerateCommand();
        testIpCoreCommandRunnerReportsFrameworkToolSearchPaths();
        testIpcraftPackageLoadingRejectsMissingDeclaredViews();
        testIpcraftPackageLoadingRejectsInvalidDeclaredViewXml();
        testRuntimeIpInstanceAdapterExposesGlobalParameterSection();
        testRuntimeIpInstanceAdapterUsesIpcoreIdLabelAndSkipsEmptyParameters();
        testCatalogIpInstanceAdapterExposesPackageParameterSection();
        testIpCoreRuntimeDiagnosticsListLoadedPackagesAndIpTypes();
    } catch (const std::exception& error) {
        std::cerr << "ipcoreruntime_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcoreruntime_test passed\n";
    return 0;
}
