// Plugin system tests for manifest discovery and command metadata.
#include "graph/graph.h"
#include "plugins/generatorrunner.h"
#include "plugins/pluginprojectadapter.h"
#include "plugins/pluginregistry.h"
#include "plugins/startupdiagnostics.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QString repositoryPluginPath(const QString& relativePluginPath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePluginPath));
            if (info.isDir()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePluginPath)).absoluteFilePath();
}

void testIpCoreManifestLoadsRuntimeAndSourcePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("generated/finepaper.demo/graphics")), "failed to create runtime dirs");
    require(root.mkpath(QStringLiteral("ipcores/demo")), "failed to create source dirs");
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/modules.xml")),
              QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/plugin.json")), QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "source_root": "../../ipcores/demo",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {
        "command": "ruby",
        "input_format": "generic_graph_v1",
        "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]
      },
      "drc": {
        "command": "ruby",
        "input_format": "generic_graph_v1",
        "args": ["generator/bin/drc", "-i", "{input}"]
      },
      "topology_presets": [
        {
          "id": "mesh",
          "label": "Mesh",
          "kind": "mesh",
          "router_module": "XP",
          "id_pattern": "xp_{row}_{col}",
          "ports": {"east": "east", "west": "west", "north": "north", "south": "south"},
          "parameters": {
            "rows": {"label": "Rows", "default": 2, "min": 1, "max": 16},
            "cols": {"label": "Columns", "default": 2, "min": 1, "max": 16}
          }
        },
        {
          "id": "ring",
          "label": "Ring",
          "kind": "ring",
          "router_module": "XP",
          "id_pattern": "xp_{index}",
          "ports": {"east": "east", "west": "west"},
          "parameters": {
            "nodes": {"label": "Nodes", "default": 4, "min": 2, "max": 64}
          }
        }
      ],
      "native": {"enabled": true, "library": "libdemo.so"}
    })json"));

    const QList<PluginDescriptor> plugins =
        PluginRegistry::discover({root.filePath(QStringLiteral("generated"))});
    const QString runtimeRoot =
        QFileInfo(root.filePath(QStringLiteral("generated/finepaper.demo"))).absoluteFilePath();
    const QString sourceRoot =
        QFileInfo(root.filePath(QStringLiteral("ipcores/demo"))).absoluteFilePath();

    require(plugins.size() == 1, "expected one plugin");
    require(plugins.first().id == QStringLiteral("finepaper.demo"), "plugin id should load");
    require(plugins.first().runtimeRootPath == runtimeRoot, "runtime root should be manifest directory");
    require(plugins.first().sourceRootPath == sourceRoot, "source root should resolve from source_root");
    require(plugins.first().rootPath == sourceRoot, "legacy root path should alias source root");
    require(QFileInfo(plugins.first().modulesPath).isAbsolute(), "modules path should be absolute");
    require(QFileInfo(plugins.first().graphicsPath).isAbsolute(), "graphics path should be absolute");
    require(plugins.first().modulesPath == QFileInfo(root.filePath(QStringLiteral("generated/finepaper.demo/modules.xml"))).absoluteFilePath(),
            "modules path should resolve against runtime root");
    require(plugins.first().graphicsPath == QFileInfo(root.filePath(QStringLiteral("generated/finepaper.demo/graphics"))).absoluteFilePath(),
            "graphics path should resolve against runtime root");
    require(plugins.first().native.enabled, "native metadata should be retained");
    require(plugins.first().generator.hasGenerator(), "generator should be retained");
    require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
            "generator input format should load");
    require(plugins.first().drc.hasCommand(), "DRC command should be retained");
    require(plugins.first().drc.inputFormat == QStringLiteral("generic_graph_v1"),
            "DRC input format should load");
    require(plugins.first().drc.args.first() == QStringLiteral("generator/bin/drc"),
            "DRC args should load");
    require(plugins.first().topologyPresets.size() == 2,
            "topology presets should load from manifest");
    require(plugins.first().topologyPresets.first().id == QStringLiteral("mesh"),
            "first topology preset id should load");
    require(plugins.first().topologyPresets.first().kind == QStringLiteral("mesh"),
            "first topology preset kind should load");
    require(plugins.first().topologyPresets.first().routerModule == QStringLiteral("XP"),
            "topology router module should load");
    require(plugins.first().topologyPresets.first().parameters.value(QStringLiteral("rows")).defaultValue == 2,
            "topology rows default should load");

    Graph graph;
    ModuleType type;
    type.name = QStringLiteral("DemoIp");
    type.pluginId = QStringLiteral("finepaper.demo");
    ModuleRegistry::instance().registerType(type);
    require(graph.addModule(std::make_unique<Module>(QStringLiteral("demo_ip"), QStringLiteral("DemoIp"))),
            "failed to add demo module");

    const GeneratorCommand generatorCommand =
        GeneratorRunner::resolveForGraph(&graph, plugins, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(generatorCommand.valid, "generator command should resolve for discovered plugin");
    require(generatorCommand.workingDirectory == sourceRoot,
            "generator working directory should use source root");

    const GeneratorCommand drcCommand =
        GeneratorRunner::resolveDrcForGraph(&graph, plugins, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));
    require(drcCommand.valid, "DRC command should resolve for discovered plugin");
    require(drcCommand.workingDirectory == sourceRoot,
            "DRC working directory should use source root");
}

void testModuleTypesKeepPluginOwnershipAndSkipDuplicates() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("first")), "failed to create first plugin");
    require(root.mkpath(QStringLiteral("second")), "failed to create second plugin");
    const QByteArray moduleXml = QByteArrayLiteral(R"xml(<module-bundle>
      <module name="Shared" palette_label="Shared" graph_group="demo">
        <ports><port id="in" direction="input" type="bus" name="IN"/></ports>
        <parameters><parameter name="width" type="int" default="32"/></parameters>
      </module>
    </module-bundle>)xml");
    writeFile(root.filePath(QStringLiteral("first/modules.xml")), moduleXml);
    writeFile(root.filePath(QStringLiteral("second/modules.xml")), moduleXml);
    writeFile(root.filePath(QStringLiteral("first/plugin.json")),
              QByteArrayLiteral(R"json({"id":"first","name":"First","version":"1","source_root":".","modules":"modules.xml"})json"));
    writeFile(root.filePath(QStringLiteral("second/plugin.json")),
              QByteArrayLiteral(R"json({"id":"second","name":"Second","version":"1","source_root":".","modules":"modules.xml"})json"));

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(PluginRegistry::discover({temp.path()}));

    const ModuleType* type = registry.getType(QStringLiteral("Shared"));
    require(type != nullptr, "Shared type should load");
    require(type->pluginId == QStringLiteral("first"), "duplicate type should keep first plugin owner");
    require(registry.availableTypes().size() == 1, "duplicate type name should be skipped");
}

void testJsonModuleBundlesAreIgnored() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("json_bundle")), "failed to create JSON bundle plugin");
    writeFile(root.filePath(QStringLiteral("json_bundle/modules.json")),
              QByteArrayLiteral(R"json([{"name":"JsonBundle","ports":[],"parameters":[]}])json"));
    writeFile(root.filePath(QStringLiteral("json_bundle/plugin.json")),
              QByteArrayLiteral(R"json({"id":"json_bundle","name":"JsonBundle","version":"1","source_root":".","modules":"modules.json"})json"));

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const bool loaded = registry.loadPlugins(PluginRegistry::discover({temp.path()}));

    require(!loaded, "JSON module bundles should no longer load");
    require(registry.availableTypes().isEmpty(), "JSON module type should be ignored");
}

void testManifestWithoutSourceRootIsSkipped() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("legacy")), "failed to create legacy plugin");
    writeFile(root.filePath(QStringLiteral("legacy/plugin.json")),
              QByteArrayLiteral(R"json({"id":"legacy","name":"Legacy","version":"1","modules":"modules.xml"})json"));

    const QList<PluginDescriptor> plugins = PluginRegistry::discover({temp.path()});

    require(plugins.empty(), "manifest without source_root should be skipped");
}

void testModuleRegistryListsTypesByPlugin() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);

    ModuleType nocType;
    nocType.name = QStringLiteral("XP");
    nocType.pluginId = QStringLiteral("finepaper.noc");
    nocType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(nocType), "noc type should register");

    ModuleType ravenType;
    ravenType.name = QStringLiteral("RaveTile");
    ravenType.pluginId = QStringLiteral("finepaper.ravenoc");
    ravenType.graphGroup = QStringLiteral("xps");
    require(registry.registerType(ravenType), "ravenoc type should register");

    const QStringList nocTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.noc"));
    const QStringList ravenTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.ravenoc"));

    require(nocTypes == QStringList{QStringLiteral("XP")},
            "NoC active IP should only list NoC module types");
    require(ravenTypes == QStringList{QStringLiteral("RaveTile")},
            "RaveNoC active IP should only list RaveNoC module types");

    const ModuleType* ravenRouter =
        registry.getTypeForGraphGroup(QStringLiteral("finepaper.ravenoc"), QStringLiteral("xps"));
    require(ravenRouter && ravenRouter->name == QStringLiteral("RaveTile"),
            "graph group lookup should be scoped by plugin id");
}

void testGeneratorArgumentsSubstituteInputAndOutput() {
    PluginGeneratorDescriptor generator;
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

void testGeneratorRunnerPropagatesInputFormat() {
    Graph graph;

    ModuleType type;
    type.name = QStringLiteral("FormatIp");
    type.pluginId = QStringLiteral("finepaper.format");
    ModuleRegistry::instance().registerType(type);

    auto module = std::make_unique<Module>(QStringLiteral("format_node"), QStringLiteral("FormatIp"));
    require(graph.addModule(std::move(module)), "failed to add format module");

    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.format");
    plugin.sourceRootPath = QStringLiteral("/tmp/finepaper-format-ipcore");
    plugin.rootPath = plugin.sourceRootPath;
    plugin.generator.command = QStringLiteral("ruby");
    plugin.generator.inputFormat = QStringLiteral("generic_graph_v1");
    plugin.generator.args = {QStringLiteral("generator/bin/generate")};

    const GeneratorCommand command =
        GeneratorRunner::resolveForGraph(&graph, {plugin}, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "generator command should resolve");
    require(command.inputFormat == QStringLiteral("generic_graph_v1"),
            "resolved command should carry input format");
    require(command.workingDirectory == plugin.sourceRootPath,
            "resolved command should use IP core source root");
}

void testGeneratorRunnerResolvesDrcCommand() {
    Graph graph;

    ModuleType type;
    type.name = QStringLiteral("DrcIp");
    type.pluginId = QStringLiteral("finepaper.drc");
    ModuleRegistry::instance().registerType(type);

    auto module = std::make_unique<Module>(QStringLiteral("drc_node"), QStringLiteral("DrcIp"));
    require(graph.addModule(std::move(module)), "failed to add DRC module");

    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.drc");
    plugin.sourceRootPath = QStringLiteral("/tmp/finepaper-drc-ipcore");
    plugin.rootPath = plugin.sourceRootPath;
    plugin.drc.command = QStringLiteral("ruby");
    plugin.drc.inputFormat = QStringLiteral("generic_graph_v1");
    plugin.drc.args = {QStringLiteral("generator/bin/drc"), QStringLiteral("-i"), QStringLiteral("{input}")};

    const GeneratorCommand command =
        GeneratorRunner::resolveDrcForGraph(&graph, {plugin}, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "DRC command should resolve");
    require(command.inputFormat == QStringLiteral("generic_graph_v1"),
            "resolved DRC command should carry input format");
    require(command.workingDirectory == plugin.sourceRootPath,
            "resolved DRC command should use IP core source root");
    require(command.arguments.contains(QStringLiteral("/tmp/in.json")),
            "resolved DRC command should substitute input");
}

void testDefaultDiscoveryIncludesGeneratedIpcores() {
    const QStringList roots = PluginRegistry::defaultPluginRoots();
    const QString generatedRoot = repositoryPluginPath(QStringLiteral("generated/ipcores"));

    require(roots.contains(generatedRoot), "default roots should include generated ipcores");

    const QList<PluginDescriptor> plugins = PluginRegistry::discover(roots);
    const auto nocIt = std::find_if(plugins.cbegin(), plugins.cend(), [](const PluginDescriptor& plugin) {
        return plugin.id == QStringLiteral("finepaper.noc");
    });
    const auto ravenIt = std::find_if(plugins.cbegin(), plugins.cend(), [](const PluginDescriptor& plugin) {
        return plugin.id == QStringLiteral("finepaper.ravenoc");
    });

    require(nocIt != plugins.cend(), "generated NoC bundle should be discovered by default roots");
    require(ravenIt != plugins.cend(), "generated RaveNoC bundle should be discovered by default roots");
    require(nocIt->runtimeRootPath == repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.noc")),
            "default NoC discovery should load the generated runtime bundle");
    require(ravenIt->runtimeRootPath == repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.ravenoc")),
            "default RaveNoC discovery should load the generated runtime bundle");
}

void testRepositoryFinepaperNoCIpCoreMetadataLoads() {
    const QString ipcoreRoot = repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.noc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({ipcoreRoot});

    require(plugins.size() == 1, "Finepaper NoC IP core should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.noc"),
            "Finepaper NoC IP core id should load");
    require(plugins.first().runtimeRootPath == ipcoreRoot,
            "Finepaper NoC runtime root should be generated bundle directory");
    require(plugins.first().sourceRootPath == repositoryPluginPath(QStringLiteral("ipcores/finepaper-noc")),
            "Finepaper NoC source root should resolve to concrete IP source package");
    require(plugins.first().modulesPath == QFileInfo(QDir(ipcoreRoot).filePath(QStringLiteral("modules.xml"))).absoluteFilePath(),
            "Finepaper NoC modules should resolve against generated runtime bundle");
    require(plugins.first().graphicsPath == QFileInfo(QDir(ipcoreRoot).filePath(QStringLiteral("graphics"))).absoluteFilePath(),
            "Finepaper NoC graphics should resolve against generated runtime bundle");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const QStringList nocTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.noc"));
    require(nocTypes == QStringList({QStringLiteral("Endpoint"), QStringLiteral("XP")}),
            "Finepaper NoC IP core should list its internal editable module types");

    const ModuleType* xpType = registry.getType(QStringLiteral("XP"));
    require(xpType != nullptr, "XP module type should load");
    require(xpType->pluginId == QStringLiteral("finepaper.noc"),
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
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.ravenoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});

    require(plugins.size() == 1, "RaveNoC IP core should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC IP core id should load");
    require(plugins.first().runtimeRootPath == pluginRoot,
            "RaveNoC runtime root should be generated bundle directory");
    require(plugins.first().sourceRootPath == repositoryPluginPath(QStringLiteral("ipcores/ravenoc")),
            "RaveNoC source root should resolve to concrete IP source package");
    require(plugins.first().modulesPath == QFileInfo(QDir(pluginRoot).filePath(QStringLiteral("modules.xml"))).absoluteFilePath(),
            "RaveNoC modules should resolve against generated runtime bundle");
    require(plugins.first().graphicsPath == QFileInfo(QDir(pluginRoot).filePath(QStringLiteral("graphics"))).absoluteFilePath(),
            "RaveNoC graphics should resolve against generated runtime bundle");
    require(plugins.first().kind == QStringLiteral("noc"),
            "RaveNoC IP core kind should load");
    require(plugins.first().instanceParameters.contains(QStringLiteral("flit_data_width")),
            "RaveNoC instance flit width should load");
    require(std::get<int>(plugins.first().instanceParameters.value(QStringLiteral("flit_data_width")).defaultValue) == 32,
            "RaveNoC flit width default should load");
    require(plugins.first().instanceParameters.value(QStringLiteral("routing_algorithm")).choices.size() == 2,
            "RaveNoC routing algorithm choices should load as instance metadata");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "RaveNoC IP core should use Ruby generator");
    require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC IP core should request generic graph input");
    require(plugins.first().drc.command == QStringLiteral("ruby"),
            "RaveNoC IP core should use Ruby DRC");
    require(plugins.first().drc.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC DRC should request generic graph input");
    require(plugins.first().topologyPresets.size() == 1,
            "RaveNoC IP core should expose topology presets");
    require(plugins.first().topologyPresets.first().routerModule == QStringLiteral("RaveTile"),
            "RaveNoC mesh preset should create RaveTile routers");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const QStringList ravenTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.ravenoc"));
    require(ravenTypes == QStringList({QStringLiteral("RaveEndpoint"), QStringLiteral("RaveTile")}),
            "RaveNoC active IP should list only its internal editable module types");

    const ModuleType* tileType = registry.getType(QStringLiteral("RaveTile"));
    require(tileType != nullptr, "RaveTile module type should load");
    require(tileType->pluginId == QStringLiteral("finepaper.ravenoc"),
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
    require(endpointType->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveEndpoint module should keep IP core ownership");
    require(endpointType->graphGroup == QStringLiteral("endpoints"),
            "RaveEndpoint should participate as an editable endpoint graph group");
}

void testManifestPluginAdapterExposesGlobalParameterSection() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.ravenoc");
    plugin.name = QStringLiteral("RaveNoC");

    PluginInstanceParameterDescriptor routing;
    routing.name = QStringLiteral("routing_algorithm");
    routing.type = QStringLiteral("string");
    routing.defaultValue = QStringLiteral("xy");
    routing.description = QStringLiteral("Routing algorithm");
    routing.choices = {
        PluginInstanceParameterChoice{QStringLiteral("west_first"), QStringLiteral("West-first")},
        PluginInstanceParameterChoice{QStringLiteral("xy"), QStringLiteral("XY")}
    };
    routing.configurable = false;
    plugin.instanceParameters.insert(routing.name, routing);

    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    width.configurable = true;
    plugin.instanceParameters.insert(width.name, width);

    ManifestPluginProjectAdapter adapter(plugin);
    const QVector<PluginParameterSection> sections = adapter.parameterSections();

    require(sections.size() == 1, "adapter should expose one global parameter section");
    require(sections.first().pluginId == QStringLiteral("finepaper.ravenoc"),
            "section should retain plugin id");
    require(sections.first().instanceId == QStringLiteral("ravenoc_0"),
            "section instance id should use stable plugin id suffix");
    require(sections.first().id == QStringLiteral("global_parameters"),
            "section id should identify global parameters");
    require(sections.first().label == QStringLiteral("RaveNoC"),
            "section label should use plugin name");
    require(sections.first().expandedByDefault,
            "section should be expanded by default");
    require(sections.first().fields.size() == 2,
            "section should expose manifest fields");

    const PluginParameterField& first = sections.first().fields.first();
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

    const PluginParameterField& second = sections.first().fields.last();
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

void testManifestPluginAdapterUsesPluginIdLabelAndSkipsEmptyParameters() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.empty");

    ManifestPluginProjectAdapter adapter(plugin);
    require(adapter.parameterSections().isEmpty(),
            "adapter should not expose sections without instance parameters");

    PluginInstanceParameterDescriptor mode;
    mode.name = QStringLiteral("mode");
    mode.type = QStringLiteral("string");
    mode.defaultValue = QStringLiteral("basic");
    plugin.instanceParameters.insert(mode.name, mode);

    ManifestPluginProjectAdapter namedAdapter(plugin);
    const QVector<PluginParameterSection> sections = namedAdapter.parameterSections();
    require(sections.size() == 1, "adapter should expose one section after parameter is added");
    require(sections.first().label == QStringLiteral("finepaper.empty"),
            "section label should fall back to plugin id");
}

void testStartupDiagnosticsListLoadedPluginsAndIpTypes() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("demo/graphics")), "failed to create plugin dirs");
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
    writeFile(root.filePath(QStringLiteral("demo/plugin.json")),
              QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "source_root": ".",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {"command": "ruby", "args": ["generator/bin/generate"]}
    })json"));

    const QList<PluginDescriptor> plugins = PluginRegistry::discover({temp.path()});
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const QStringList lines = StartupDiagnostics::logLines(plugins, registry);

    require(lines.join('\n').contains(QStringLiteral("[Startup] Plugin finepaper.demo")),
            "startup diagnostics should include loaded plugin id");
    require(lines.join('\n').contains(QStringLiteral("Demo v1.0")),
            "startup diagnostics should include plugin display name and version");
    require(lines.join('\n').contains(QStringLiteral("[Startup] IP Shared")),
            "startup diagnostics should include loaded IP type");
    require(lines.join('\n').contains(QStringLiteral("plugin=finepaper.demo")),
            "startup diagnostics should include IP plugin owner");
    require(lines.join('\n').contains(QStringLiteral("ports=1 parameters=1 interfaces=1")),
            "startup diagnostics should include IP metadata counts");
}

void testStartupDiagnosticsMarksJsonModuleBundlesUnsupported() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.jsonbundle");
    plugin.name = QStringLiteral("JsonBundle");
    plugin.version = QStringLiteral("0.1");
    plugin.modulesPath = QStringLiteral("/tmp/modules.json");

    const QStringList lines = StartupDiagnostics::pluginLogLines({plugin});

    require(lines.join('\n').contains(QStringLiteral("bundle=json(unsupported)")),
            "JSON module bundle format should be marked unsupported");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testIpCoreManifestLoadsRuntimeAndSourcePaths();
        testModuleTypesKeepPluginOwnershipAndSkipDuplicates();
        testJsonModuleBundlesAreIgnored();
        testManifestWithoutSourceRootIsSkipped();
        testModuleRegistryListsTypesByPlugin();
        testGeneratorArgumentsSubstituteInputAndOutput();
        testGeneratorRunnerPropagatesInputFormat();
        testGeneratorRunnerResolvesDrcCommand();
        testDefaultDiscoveryIncludesGeneratedIpcores();
        testRepositoryFinepaperNoCIpCoreMetadataLoads();
        testRepositoryRaveNoCIpCoreMetadataLoads();
        testManifestPluginAdapterExposesGlobalParameterSection();
        testManifestPluginAdapterUsesPluginIdLabelAndSkipsEmptyParameters();
        testStartupDiagnosticsListLoadedPluginsAndIpTypes();
        testStartupDiagnosticsMarksJsonModuleBundlesUnsupported();
    } catch (const std::exception& error) {
        std::cerr << "plugin_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "plugin_test passed\n";
    return 0;
}
