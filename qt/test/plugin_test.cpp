// Plugin system tests for manifest discovery and command metadata.
#include "graph/graph.h"
#include "plugins/generatorrunner.h"
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
    const QStringList candidates = {
        QDir::current().filePath(relativePluginPath),
        QDir::current().filePath(QStringLiteral("../") + relativePluginPath),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../../../../") + relativePluginPath)
    };

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isDir()) {
            return info.absoluteFilePath();
        }
    }

    return QFileInfo(candidates.first()).absoluteFilePath();
}

void testPluginManifestLoadsRelativePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("demo/graphics")), "failed to create plugin dirs");
    writeFile(root.filePath(QStringLiteral("demo/modules.xml")), QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("demo/plugin.json")), QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
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

    const QList<PluginDescriptor> plugins = PluginRegistry::discover({temp.path()});

    require(plugins.size() == 1, "expected one plugin");
    require(plugins.first().id == QStringLiteral("finepaper.demo"), "plugin id should load");
    require(QFileInfo(plugins.first().modulesPath).isAbsolute(), "modules path should be absolute");
    require(QFileInfo(plugins.first().graphicsPath).isAbsolute(), "graphics path should be absolute");
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
              QByteArrayLiteral(R"json({"id":"first","name":"First","version":"1","modules":"modules.xml"})json"));
    writeFile(root.filePath(QStringLiteral("second/plugin.json")),
              QByteArrayLiteral(R"json({"id":"second","name":"Second","version":"1","modules":"modules.xml"})json"));

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(PluginRegistry::discover({temp.path()}));

    const ModuleType* type = registry.getType(QStringLiteral("Shared"));
    require(type != nullptr, "Shared type should load");
    require(type->pluginId == QStringLiteral("first"), "duplicate type should keep first plugin owner");
    require(registry.availableTypes().size() == 1, "duplicate type name should be skipped");
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
    plugin.rootPath = QStringLiteral("/tmp/finepaper-format-plugin");
    plugin.generator.command = QStringLiteral("ruby");
    plugin.generator.inputFormat = QStringLiteral("generic_graph_v1");
    plugin.generator.args = {QStringLiteral("generator/bin/generate")};

    const GeneratorCommand command =
        GeneratorRunner::resolveForGraph(&graph, {plugin}, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "generator command should resolve");
    require(command.inputFormat == QStringLiteral("generic_graph_v1"),
            "resolved command should carry input format");
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
    plugin.rootPath = QStringLiteral("/tmp/finepaper-drc-plugin");
    plugin.drc.command = QStringLiteral("ruby");
    plugin.drc.inputFormat = QStringLiteral("generic_graph_v1");
    plugin.drc.args = {QStringLiteral("generator/bin/drc"), QStringLiteral("-i"), QStringLiteral("{input}")};

    const GeneratorCommand command =
        GeneratorRunner::resolveDrcForGraph(&graph, {plugin}, QStringLiteral("/tmp/in.json"), QStringLiteral("/tmp/out"));

    require(command.valid, "DRC command should resolve");
    require(command.inputFormat == QStringLiteral("generic_graph_v1"),
            "resolved DRC command should carry input format");
    require(command.arguments.contains(QStringLiteral("/tmp/in.json")),
            "resolved DRC command should substitute input");
}

void testRepositoryRaveNoCPluginMetadataLoads() {
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("plugins/ravenoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});

    require(plugins.size() == 1, "RaveNoC plugin should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC plugin id should load");
    require(plugins.first().kind == QStringLiteral("noc"),
            "RaveNoC plugin kind should load");
    require(plugins.first().instanceParameters.contains(QStringLiteral("flit_data_width")),
            "RaveNoC instance flit width should load");
    require(std::get<int>(plugins.first().instanceParameters.value(QStringLiteral("flit_data_width")).defaultValue) == 32,
            "RaveNoC flit width default should load");
    require(plugins.first().instanceParameters.value(QStringLiteral("routing_algorithm")).choices.size() == 2,
            "RaveNoC routing algorithm choices should load as instance metadata");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "RaveNoC plugin should use Ruby generator");
    require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC plugin should request generic graph input");
    require(plugins.first().drc.command == QStringLiteral("ruby"),
            "RaveNoC plugin should use Ruby DRC");
    require(plugins.first().drc.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC DRC should request generic graph input");
    require(plugins.first().topologyPresets.size() == 1,
            "RaveNoC plugin should expose topology presets");
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
            "RaveTile module should keep plugin ownership");
    require(tileType->graphGroup == QStringLiteral("xps"),
            "RaveTile should participate as the RaveNoC router graph group");
    require(!tileType->defaultParameters.contains(QStringLiteral("routing_algorithm")),
            "RaveTile should not own fabric-wide routing algorithm parameter");

    const ModuleType* endpointType = registry.getType(QStringLiteral("RaveEndpoint"));
    require(endpointType != nullptr, "RaveEndpoint module type should load");
    require(endpointType->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveEndpoint module should keep plugin ownership");
    require(endpointType->graphGroup == QStringLiteral("endpoints"),
            "RaveEndpoint should participate as an editable endpoint graph group");
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

void testStartupDiagnosticsMarksJsonModuleBundlesDeprecated() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.legacy");
    plugin.name = QStringLiteral("Legacy");
    plugin.version = QStringLiteral("0.1");
    plugin.modulesPath = QStringLiteral("/tmp/modules.json");

    const QStringList lines = StartupDiagnostics::pluginLogLines({plugin});

    require(lines.join('\n').contains(QStringLiteral("bundle=json(deprecated)")),
            "JSON module bundle format should be marked deprecated");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testPluginManifestLoadsRelativePaths();
        testModuleTypesKeepPluginOwnershipAndSkipDuplicates();
        testModuleRegistryListsTypesByPlugin();
        testGeneratorArgumentsSubstituteInputAndOutput();
        testGeneratorRunnerPropagatesInputFormat();
        testGeneratorRunnerResolvesDrcCommand();
        testRepositoryRaveNoCPluginMetadataLoads();
        testStartupDiagnosticsListLoadedPluginsAndIpTypes();
        testStartupDiagnosticsMarksJsonModuleBundlesDeprecated();
    } catch (const std::exception& error) {
        std::cerr << "plugin_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "plugin_test passed\n";
    return 0;
}
