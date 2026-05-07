#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "plugins/plugindescriptor.h"
#include "plugins/pluginregistry.h"
#include "topology/topologypresetbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
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

int intParameter(const Module* module, const QString& name) {
    require(module != nullptr, "module should exist");
    const auto it = module->parameters().find(name);
    require(it != module->parameters().end(), "int parameter should exist");
    const Parameter::Value parameterValue = it.value().value();
    const auto* value = std::get_if<int>(&parameterValue);
    require(value != nullptr, "parameter should be an int");
    return *value;
}

bool boolParameter(const Module* module, const QString& name) {
    require(module != nullptr, "module should exist");
    const auto it = module->parameters().find(name);
    require(it != module->parameters().end(), "bool parameter should exist");
    const Parameter::Value parameterValue = it.value().value();
    const auto* value = std::get_if<bool>(&parameterValue);
    require(value != nullptr, "parameter should be a bool");
    return *value;
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

ModuleType routerType(const QString& name, const QString& pluginId) {
    ModuleType type;
    type.name = name;
    type.pluginId = pluginId;
    type.graphGroup = QStringLiteral("xps");
    type.supportsCollapse = true;
    type.meshSpacingX = 220;
    type.meshSpacingY = 168;
    type.defaultPorts = {
        Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("North"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("north")),
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("east")),
        Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("South"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("south")),
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("West"), {}, QStringLiteral("router"), QStringLiteral("router_link"), QStringLiteral("west"))
    };
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("display_name"), Parameter(QStringLiteral("display_name"), QString{}));
    type.defaultParameters.insert(QStringLiteral("external_id"), Parameter(QStringLiteral("external_id"), QString{}));
    type.defaultParameters.insert(QStringLiteral("collapsed"), Parameter(QStringLiteral("collapsed"), true));

    ModuleInterfaceMetadata north;
    north.id = QStringLiteral("north");
    north.bus = QStringLiteral("router_link");
    north.role = QStringLiteral("target");
    north.compatibleRoles = {QStringLiteral("initiator")};
    type.interfaceMetadata.insert(north.id, north);

    ModuleInterfaceMetadata east;
    east.id = QStringLiteral("east");
    east.bus = QStringLiteral("router_link");
    east.role = QStringLiteral("initiator");
    east.compatibleRoles = {QStringLiteral("target")};
    type.interfaceMetadata.insert(east.id, east);

    ModuleInterfaceMetadata south = east;
    south.id = QStringLiteral("south");
    type.interfaceMetadata.insert(south.id, south);

    ModuleInterfaceMetadata west = north;
    west.id = QStringLiteral("west");
    type.interfaceMetadata.insert(west.id, west);

    return type;
}

TopologyPresetDescriptor meshPreset() {
    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("mesh");
    preset.label = QStringLiteral("Mesh");
    preset.kind = QStringLiteral("mesh");
    preset.routerModule = QStringLiteral("XP");
    preset.idPattern = QStringLiteral("xp_{row}_{col}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("east"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("west"));
    preset.ports.insert(QStringLiteral("north"), QStringLiteral("north"));
    preset.ports.insert(QStringLiteral("south"), QStringLiteral("south"));
    return preset;
}

TopologyPresetDescriptor ringPreset() {
    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("ring");
    preset.label = QStringLiteral("Ring");
    preset.kind = QStringLiteral("ring");
    preset.routerModule = QStringLiteral("XP");
    preset.idPattern = QStringLiteral("xp_{index}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("east"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("west"));
    return preset;
}

void testMeshPresetCreatesEditableGraph() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = QStringLiteral("finepaper.noc");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 3);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 6, "2x3 mesh should create six routers");
    require(graph.connections().size() == 7, "2x3 mesh should create seven links");
    require(graph.getModule(QStringLiteral("xp_0_0")) != nullptr, "mesh should create deterministic node ids");
    require(intParameter(graph.getModule(QStringLiteral("xp_0_1")), QStringLiteral("x")) == 220,
            "mesh preset should apply router horizontal spacing");
    require(intParameter(graph.getModule(QStringLiteral("xp_1_0")), QStringLiteral("y")) == 168,
            "mesh preset should apply router vertical spacing");
    require(!boolParameter(graph.getModule(QStringLiteral("xp_0_0")), QStringLiteral("collapsed")),
            "mesh preset should create routers expanded by default");
    require(graph.isValidConnection(PortRef{QStringLiteral("xp_0_0"), QStringLiteral("east")},
                                    PortRef{QStringLiteral("xp_0_1"), QStringLiteral("west")}) == false,
            "created east/west ports should be occupied by normal graph connections");
}

void testRingPresetCreatesClosedLoop() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = QStringLiteral("finepaper.noc");
    request.preset = ringPreset();
    request.parameters.insert(QStringLiteral("nodes"), 4);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "ring should create four routers");
    require(graph.connections().size() == 4, "ring should close the loop");
    require(graph.getModule(QStringLiteral("xp_3")) != nullptr, "ring should create deterministic ids");
    require(intParameter(graph.getModule(QStringLiteral("xp_3")), QStringLiteral("x")) == 660,
            "ring preset should apply router horizontal spacing");
}

void testRepositoryRaveNoCMeshPresetCreatesInternalTiles() {
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("plugins/ravenoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});
    require(plugins.size() == 1, "RaveNoC plugin should be discovered");
    require(!plugins.first().topologyPresets.isEmpty(), "RaveNoC should expose topology presets");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const auto meshIt = std::find_if(plugins.first().topologyPresets.cbegin(),
                                     plugins.first().topologyPresets.cend(),
                                     [](const TopologyPresetDescriptor& preset) {
                                         return preset.id == QStringLiteral("mesh");
                                     });
    require(meshIt != plugins.first().topologyPresets.cend(), "RaveNoC mesh preset should exist");

    Graph graph;
    TopologyPresetRequest request;
    request.pluginId = plugins.first().id;
    request.preset = *meshIt;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "RaveNoC 2x2 mesh should create four editable tiles");
    require(graph.connections().size() == 4, "RaveNoC 2x2 mesh should create four router links");
    require(graph.getModule(QStringLiteral("rave_0_0")) != nullptr,
            "RaveNoC mesh node id should be deterministic");
    require(intParameter(graph.getModule(QStringLiteral("rave_0_1")), QStringLiteral("x")) == 220,
            "RaveNoC mesh preset should apply RaveTile horizontal spacing");
    require(intParameter(graph.getModule(QStringLiteral("rave_1_0")), QStringLiteral("y")) == 168,
            "RaveNoC mesh preset should apply RaveTile vertical spacing");
    require(!boolParameter(graph.getModule(QStringLiteral("rave_0_0")), QStringLiteral("collapsed")),
            "RaveNoC mesh preset should create tiles expanded by default");
    require(intParameter(graph.getModule(QStringLiteral("rave_0_1")), QStringLiteral("mesh_col")) == 1,
            "RaveNoC mesh preset should preserve logical mesh columns");
    require(intParameter(graph.getModule(QStringLiteral("rave_1_0")), QStringLiteral("mesh_row")) == 1,
            "RaveNoC mesh preset should preserve logical mesh rows");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMeshPresetCreatesEditableGraph();
        testRingPresetCreatesClosedLoop();
        testRepositoryRaveNoCMeshPresetCreatesInternalTiles();
    } catch (const std::exception& error) {
        std::cerr << "topology_preset_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "topology_preset_test passed\n";
    return 0;
}
