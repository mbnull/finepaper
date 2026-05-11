#include "commands/commandmanager.h"
#include "commands/topologypresetcommand.h"
#include "graph/graph.h"
#include "ipcore/ipcoreruntimedescriptor.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "modules/moduleregistry.h"
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

QString repositoryRuntimePath(const QString& relativeRuntimePath) {
    const QStringList startPaths = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath()
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

QString stringParameter(const Module* module, const QString& name) {
    require(module != nullptr, "module should exist");
    const auto it = module->parameters().find(name);
    require(it != module->parameters().end(), "string parameter should exist");
    const Parameter::Value parameterValue = it.value().value();
    const auto* value = std::get_if<QString>(&parameterValue);
    require(value != nullptr, "parameter should be a string");
    return *value;
}

QString scopedPresetModuleId(const QString& instanceId, const QString& logicalId) {
    return instanceId + QLatin1Char('_') + logicalId;
}

QString scopedPresetConnectionId(const QString& instanceId,
                                 const QString& logicalSourceId,
                                 const QString& suffix) {
    return scopedPresetModuleId(instanceId, logicalSourceId) + QLatin1Char('_') + suffix;
}

ModuleType routerType(const QString& name, const QString& ipcoreId) {
    ModuleType type;
    type.name = name;
    type.ipcoreId = ipcoreId;
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
    north.topologyRule = QStringLiteral("opposite_side");
    type.interfaceMetadata.insert(north.id, north);

    ModuleInterfaceMetadata east;
    east.id = QStringLiteral("east");
    east.bus = QStringLiteral("router_link");
    east.role = QStringLiteral("initiator");
    east.compatibleRoles = {QStringLiteral("target")};
    east.topologyRule = QStringLiteral("opposite_side");
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
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 3);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString originId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"));
    const QString rightId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_1"));
    const QString belowId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_1_0"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 6, "2x3 mesh should create six routers");
    require(graph.connections().size() == 7, "2x3 mesh should create seven links");
    require(graph.getModule(originId) != nullptr, "mesh should create instance-scoped node ids");
    require(graph.getModule(originId)->instanceId() == QStringLiteral("noc_0"),
            "mesh preset should stamp module instance ownership");
    require(stringParameter(graph.getModule(originId), QStringLiteral("external_id")) ==
                QStringLiteral("xp_0_0"),
            "mesh preset should keep logical external_id values");
    require(stringParameter(graph.getModule(originId), QStringLiteral("display_name")) ==
                QStringLiteral("xp_0_0"),
            "mesh preset should keep logical display names");
    require(intParameter(graph.getModule(rightId), QStringLiteral("x")) == 220,
            "mesh preset should apply router horizontal spacing");
    require(intParameter(graph.getModule(belowId), QStringLiteral("y")) == 168,
            "mesh preset should apply router vertical spacing");
    require(!boolParameter(graph.getModule(originId), QStringLiteral("collapsed")),
            "mesh preset should create routers expanded by default");
    require(graph.isValidConnection(PortRef{originId, QStringLiteral("east")},
                                    PortRef{rightId, QStringLiteral("west")}) == false,
            "created east/west ports should be occupied by normal graph connections");
}

void testRingPresetCreatesClosedLoop() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = ringPreset();
    request.parameters.insert(QStringLiteral("nodes"), 4);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString ringId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_3"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "ring should create four routers");
    require(graph.connections().size() == 4, "ring should close the loop");
    require(graph.getModule(ringId) != nullptr, "ring should create instance-scoped ids");
    require(stringParameter(graph.getModule(ringId), QStringLiteral("external_id")) ==
                QStringLiteral("xp_3"),
            "ring preset should keep logical external ids");
    require(intParameter(graph.getModule(ringId), QStringLiteral("x")) == 660,
            "ring preset should apply router horizontal spacing");
}

void testPresetRejectsConnectionsThatFailRuleService() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.ports.insert(QStringLiteral("west"), QStringLiteral("east"));
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "preset should reject same-side links through connection rules");
    require(result.error.contains(QStringLiteral("Generated invalid connection")),
            "preset failure should identify generated invalid connection");
    require(graph.connections().empty(), "invalid preset link should not be inserted");
}

void testPresetFailureRollsBackPartialGraph() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString typeName = QStringLiteral("XPAtomicRollback");
    ModuleType type = routerType(typeName, QStringLiteral("finepaper.noc"));
    ModuleInterfaceMetadata south = type.interfaceMetadata.value(QStringLiteral("south"));
    south.role = QStringLiteral("target");
    south.compatibleRoles = {QStringLiteral("initiator")};
    type.interfaceMetadata.insert(south.id, south);
    require(registry.registerType(type), "router type should register");
    ModuleRegistry::instance().registerType(type);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.routerModule = typeName;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "preset should fail when a later generated link violates rules");
    require(graph.modules().empty(), "failed preset should roll back created modules");
    require(graph.connections().empty(), "failed preset should roll back earlier valid links");
}

void testPresetModuleCreationFailureRollsBackPartialGraph() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.idPattern = QStringLiteral("xp");
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "duplicate generated module id should fail preset");
    require(graph.modules().empty(), "module creation failure should roll back earlier created modules");
    require(graph.connections().empty(), "module creation failure should not leave connections");
}

void testPresetConnectionIdCollisionDoesNotRemoveExistingConnection() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString typeName = QStringLiteral("XPCollision");
    ModuleType type = routerType(typeName, QStringLiteral("finepaper.noc"));
    require(registry.registerType(type), "router type should register");
    ModuleRegistry::instance().registerType(type);

    Graph graph;
    require(graph.addModule(std::make_unique<Module>(QStringLiteral("existing_source"), typeName)),
            "existing source should add");
    graph.getModule(QStringLiteral("existing_source"))->addPort(
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("East"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("east")));
    require(graph.addModule(std::make_unique<Module>(QStringLiteral("existing_target"), typeName)),
            "existing target should add");
    graph.getModule(QStringLiteral("existing_target"))->addPort(
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("West"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("west")));
    graph.addConnection(std::make_unique<Connection>(
        scopedPresetConnectionId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"), QStringLiteral("east")),
        PortRef{QStringLiteral("existing_source"), QStringLiteral("east")},
        PortRef{QStringLiteral("existing_target"), QStringLiteral("west")}));
    require(graph.connections().size() == 1, "pre-existing connection should be present");

    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.routerModule = typeName;
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "generated connection id collision should fail preset");
    require(graph.connections().size() == 1, "rollback should preserve pre-existing connection");
    require(graph.connections().front()->id() ==
                scopedPresetConnectionId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"), QStringLiteral("east")),
            "existing connection id should remain");
    require(graph.getModule(QStringLiteral("existing_source")) != nullptr,
            "rollback should preserve pre-existing source module");
    require(graph.getModule(scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"))) == nullptr,
            "rollback should remove generated modules");
}

void testMeshPresetCanRepeatAcrossSameIpcoreInstances() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;

    TopologyPresetRequest firstRequest;
    firstRequest.ipcoreId = QStringLiteral("finepaper.noc");
    firstRequest.instanceId = QStringLiteral("noc_0");
    firstRequest.preset = meshPreset();
    firstRequest.parameters.insert(QStringLiteral("rows"), 1);
    firstRequest.parameters.insert(QStringLiteral("cols"), 2);

    TopologyPresetRequest secondRequest = firstRequest;
    secondRequest.instanceId = QStringLiteral("noc_1");

    const TopologyPresetResult firstResult = TopologyPresetBuilder::apply(&graph, registry, firstRequest);
    const TopologyPresetResult secondResult = TopologyPresetBuilder::apply(&graph, registry, secondRequest);

    require(firstResult.success, firstResult.error.toLocal8Bit().constData());
    require(secondResult.success, secondResult.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "repeating the preset for another instance should add another module set");
    require(graph.connections().size() == 2,
            "repeating the preset for another instance should add another connection set");

    const QString noc0Left = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"));
    const QString noc0Right = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_1"));
    const QString noc1Left = scopedPresetModuleId(QStringLiteral("noc_1"), QStringLiteral("xp_0_0"));
    const QString noc1Right = scopedPresetModuleId(QStringLiteral("noc_1"), QStringLiteral("xp_0_1"));
    require(graph.getModule(noc0Left) != nullptr, "first instance should keep its scoped left module");
    require(graph.getModule(noc0Right) != nullptr, "first instance should keep its scoped right module");
    require(graph.getModule(noc1Left) != nullptr, "second instance should create its own scoped left module");
    require(graph.getModule(noc1Right) != nullptr, "second instance should create its own scoped right module");
    require(graph.getModule(noc0Left)->instanceId() == QStringLiteral("noc_0"),
            "first instance modules should keep noc_0 ownership");
    require(graph.getModule(noc1Left)->instanceId() == QStringLiteral("noc_1"),
            "second instance modules should keep noc_1 ownership");
    require(stringParameter(graph.getModule(noc0Left), QStringLiteral("external_id")) ==
                QStringLiteral("xp_0_0"),
            "first instance external_id should stay logical");
    require(stringParameter(graph.getModule(noc1Left), QStringLiteral("external_id")) ==
                QStringLiteral("xp_0_0"),
            "second instance external_id should stay logical");
    require(std::find(firstResult.moduleIds.cbegin(), firstResult.moduleIds.cend(), noc0Left) !=
                firstResult.moduleIds.cend(),
            "first preset result should report scoped module ids");
    require(std::find(secondResult.moduleIds.cbegin(), secondResult.moduleIds.cend(), noc1Left) !=
                secondResult.moduleIds.cend(),
            "second preset result should report scoped module ids");
}

void testRepositoryRaveNoCMeshPresetCreatesInternalTiles() {
    const QString runtimeRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores/finepaper.ravenoc"));
    const QList<IpCoreRuntimeDescriptor> runtimes = IpCoreRuntimeRegistry::discover({runtimeRoot});
    require(runtimes.size() == 1, "RaveNoC IP core should be discovered");
    require(runtimes.first().runtimeRootPath == runtimeRoot,
            "RaveNoC runtime root should be generated bundle directory");
    require(runtimes.first().sourceRootPath == repositoryRuntimePath(QStringLiteral("ipcores/ravenoc")),
            "RaveNoC source root should resolve to concrete IP source package");
    require(!runtimes.first().topologyPresets.isEmpty(), "RaveNoC should expose topology presets");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadIpCoreRuntimes(runtimes);

    const auto meshIt = std::find_if(runtimes.first().topologyPresets.cbegin(),
                                     runtimes.first().topologyPresets.cend(),
                                     [](const TopologyPresetDescriptor& preset) {
                                         return preset.id == QStringLiteral("mesh");
                                     });
    require(meshIt != runtimes.first().topologyPresets.cend(), "RaveNoC mesh preset should exist");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = runtimes.first().id;
    request.instanceId = QStringLiteral("ravenoc_0");
    request.preset = *meshIt;
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString tile00 = scopedPresetModuleId(QStringLiteral("ravenoc_0"), QStringLiteral("rave_0_0"));
    const QString tile01 = scopedPresetModuleId(QStringLiteral("ravenoc_0"), QStringLiteral("rave_0_1"));
    const QString tile10 = scopedPresetModuleId(QStringLiteral("ravenoc_0"), QStringLiteral("rave_1_0"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.modules().size() == 4, "RaveNoC 2x2 mesh should create four editable tiles");
    require(graph.connections().size() == 4, "RaveNoC 2x2 mesh should create four router links");
    require(graph.getModule(tile00) != nullptr,
            "RaveNoC mesh node id should be instance-scoped");
    require(stringParameter(graph.getModule(tile00), QStringLiteral("external_id")) ==
                QStringLiteral("rave_0_0"),
            "RaveNoC mesh should keep logical external ids");
    require(intParameter(graph.getModule(tile01), QStringLiteral("x")) == 220,
            "RaveNoC mesh preset should apply RaveTile horizontal spacing");
    require(intParameter(graph.getModule(tile10), QStringLiteral("y")) == 168,
            "RaveNoC mesh preset should apply RaveTile vertical spacing");
    require(!boolParameter(graph.getModule(tile00), QStringLiteral("collapsed")),
            "RaveNoC mesh preset should create tiles expanded by default");
    require(intParameter(graph.getModule(tile01), QStringLiteral("mesh_col")) == 1,
            "RaveNoC mesh preset should preserve logical mesh columns");
    require(intParameter(graph.getModule(tile10), QStringLiteral("mesh_row")) == 1,
            "RaveNoC mesh preset should preserve logical mesh rows");
}

void testTopologyPresetCommandIsUndoableAndRedoable() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    CommandManager manager;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    manager.executeCommand(std::make_unique<TopologyPresetCommand>(&graph, &registry, request));

    require(graph.modules().size() == 4, "command should create mesh modules");
    require(graph.connections().size() == 4, "command should create mesh connections");
    require(manager.canUndo(), "topology command should be undoable");
    const int dirtyState = manager.currentStateId();
    require(dirtyState != 0, "topology command should advance command history state");

    manager.undo();

    require(graph.modules().empty(), "undo should remove topology modules");
    require(graph.connections().empty(), "undo should remove topology connections");
    require(manager.currentStateId() == 0, "undo should restore clean command state");
    require(manager.canRedo(), "topology command should be redoable");

    manager.redo();

    require(graph.modules().size() == 4, "redo should recreate topology modules");
    require(graph.connections().size() == 4, "redo should recreate topology connections");
    require(manager.currentStateId() == dirtyState, "redo should restore dirty command state");
}

void testTopologyPresetCommandStampsModuleOwnership() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 1);

    TopologyPresetCommand command(&graph, &registry, request);
    command.execute();

    require(command.wasExecuted(), "topology command should execute");
    require(graph.modules().size() == 1, "one-node mesh should create one module");
    require(graph.modules().front()->ipcoreId() == QStringLiteral("finepaper.noc"),
            "topology command should stamp module IP-core ownership");
    require(graph.modules().front()->instanceId() == QStringLiteral("noc_0"),
            "topology command should stamp module instance ownership");
}

void testPresetRejectsMissingInstanceId() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 1);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "topology preset should reject missing instance scope");
    require(result.error.contains(QStringLiteral("instance")),
            "missing instance error should mention instance scope");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMeshPresetCreatesEditableGraph();
        testRingPresetCreatesClosedLoop();
        testPresetRejectsConnectionsThatFailRuleService();
        testPresetFailureRollsBackPartialGraph();
        testPresetModuleCreationFailureRollsBackPartialGraph();
        testPresetConnectionIdCollisionDoesNotRemoveExistingConnection();
        testMeshPresetCanRepeatAcrossSameIpcoreInstances();
        testRepositoryRaveNoCMeshPresetCreatesInternalTiles();
        testTopologyPresetCommandIsUndoableAndRedoable();
        testTopologyPresetCommandStampsModuleOwnership();
        testPresetRejectsMissingInstanceId();
    } catch (const std::exception& error) {
        std::cerr << "topology_preset_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "topology_preset_test passed\n";
    return 0;
}
