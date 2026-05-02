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

void testRepositoryRaveNoCPluginMetadataLoads() {
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("plugins/ravenoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});

    require(plugins.size() == 1, "RaveNoC plugin should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC plugin id should load");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "RaveNoC plugin should use Ruby generator");
    require(plugins.first().generator.inputFormat == QStringLiteral("generic_graph_v1"),
            "RaveNoC plugin should request generic graph input");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const ModuleType* type = registry.getType(QStringLiteral("RaveNoC"));
    require(type != nullptr, "RaveNoC module type should load");
    require(type->pluginId == QStringLiteral("finepaper.ravenoc"),
            "RaveNoC module should keep plugin ownership");
    require(type->graphGroup != QStringLiteral("xps") &&
                type->graphGroup != QStringLiteral("endpoints"),
            "RaveNoC must not reuse XP/Endpoint graph groups");
    require(type->defaultParameters.contains(QStringLiteral("rows")),
            "RaveNoC rows parameter should load");
    require(type->defaultParameters.contains(QStringLiteral("routing_algorithm")),
            "RaveNoC routing algorithm parameter should load");
    require(type->parameterMetadata.value(QStringLiteral("routing_algorithm")).choices.size() == 2,
            "RaveNoC routing algorithm choices should load");
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
        testGeneratorArgumentsSubstituteInputAndOutput();
        testGeneratorRunnerPropagatesInputFormat();
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
