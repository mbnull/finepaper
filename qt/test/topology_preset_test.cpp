#include "commands/commandmanager.h"
#include "commands/topologypresetcommand.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "topology/topologypresetbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonObject>
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

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePath));
            if (info.isDir()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePath)).absoluteFilePath();
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

QString scopedTypeName(const QString& packageId, const QString& moduleId) {
    return packageId + QStringLiteral("::") + moduleId;
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

IpcraftInterfaceAcceptRule topologyAcceptRule(const QString& connectionClassId,
                                              const QString& role) {
    IpcraftInterfaceAcceptRule rule;
    rule.connectionClassId = connectionClassId;
    rule.role = role;
    return rule;
}

IpcraftInterfaceDescriptor topologyInterfaceDescriptor(const QString& id,
                                                       const QString& role) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.accepts.push_back(topologyAcceptRule(QStringLiteral("router_link"), role));
    return descriptor;
}

ModuleType packageBackedRouterType(const QString& name, const QString& packageId) {
    ModuleType type = routerType(name, packageId);
    type.packageId = packageId;
    type.moduleId = name;

    type.interfaceMetadata[QStringLiteral("east")].acceptRules.push_back(
        topologyAcceptRule(QStringLiteral("router_link"), QStringLiteral("initiator")));
    type.interfaceMetadata[QStringLiteral("south")].acceptRules.push_back(
        topologyAcceptRule(QStringLiteral("router_link"), QStringLiteral("initiator")));
    type.interfaceMetadata[QStringLiteral("west")].acceptRules.push_back(
        topologyAcceptRule(QStringLiteral("router_link"), QStringLiteral("target")));
    type.interfaceMetadata[QStringLiteral("north")].acceptRules.push_back(
        topologyAcceptRule(QStringLiteral("router_link"), QStringLiteral("target")));
    return type;
}

const Port* defaultPortById(const ModuleType& type, const QString& portId) {
    const auto it = std::find_if(type.defaultPorts.cbegin(),
                                 type.defaultPorts.cend(),
                                 [&](const Port& port) {
                                     return port.id() == portId;
                                 });
    return it == type.defaultPorts.cend() ? nullptr : &(*it);
}

IpcraftPackageManifest packageBackedRouterManifest(const QString& packageId,
                                                   const QString& moduleId) {
    IpcraftPackageManifest manifest;
    manifest.id = packageId;
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("router_link"),
        QStringList{QStringLiteral("initiator"), QStringLiteral("target")},
        false
    });
    IpcraftModuleDescriptor module;
    module.id = moduleId;
    module.name = QStringLiteral("Package-backed XP");
    module.interfaces = {
        topologyInterfaceDescriptor(QStringLiteral("east"), QStringLiteral("initiator")),
        topologyInterfaceDescriptor(QStringLiteral("south"), QStringLiteral("initiator")),
        topologyInterfaceDescriptor(QStringLiteral("west"), QStringLiteral("target")),
        topologyInterfaceDescriptor(QStringLiteral("north"), QStringLiteral("target"))
    };
    manifest.modules.push_back(module);
    return manifest;
}

IpcraftInterfaceDescriptor manifestMeshInterface(const QString& id,
                                                 const QString& role) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.accepts.push_back(topologyAcceptRule(QStringLiteral("manifest_mesh_link"), role));
    return descriptor;
}

IpcraftInterfaceDescriptor manifestMetadataMeshInterface(const QString& id,
                                                         const QString& role,
                                                         const QString& side,
                                                         const QString& oppositeInterfaceId) {
    IpcraftInterfaceDescriptor descriptor = manifestMeshInterface(id, role);
    descriptor.topology.side = side;
    descriptor.topology.oppositeInterfaceId = oppositeInterfaceId;
    descriptor.topology.role = role;
    return descriptor;
}

IpcraftPackageManifest manifestNamedMeshPackage(const QString& packageId,
                                                const QString& moduleId) {
    IpcraftPackageManifest manifest;
    manifest.id = packageId;
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("manifest_mesh_link"),
        QStringList{QStringLiteral("source"), QStringLiteral("sink")},
        false
    });

    IpcraftModuleDescriptor module;
    module.id = moduleId;
    module.name = QStringLiteral("Manifest Named Mesh Tile");
    module.graphRole = QStringLiteral("host");
    module.parameters = QJsonObject{
        {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("int")},
                                          {QStringLiteral("default"), 0}}},
        {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("int")},
                                          {QStringLiteral("default"), 0}}},
        {QStringLiteral("display_name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("default"), QString()}}},
        {QStringLiteral("external_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                    {QStringLiteral("default"), QString()}}}
    };
    module.interfaces = {
        manifestMeshInterface(QStringLiteral("link_out"), QStringLiteral("source")),
        manifestMeshInterface(QStringLiteral("link_in"), QStringLiteral("sink")),
        manifestMeshInterface(QStringLiteral("down_out"), QStringLiteral("source")),
        manifestMeshInterface(QStringLiteral("up_in"), QStringLiteral("sink"))
    };
    manifest.modules.push_back(module);

    manifest.topologies.push_back(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("mesh")},
        {QStringLiteral("label"), QStringLiteral("Mesh")},
        {QStringLiteral("kind"), QStringLiteral("mesh")},
        {QStringLiteral("module"), moduleId},
        {QStringLiteral("id_pattern"), QStringLiteral("tile_{row}_{col}")},
        {QStringLiteral("ports"), QJsonObject{
            {QStringLiteral("east"), QStringLiteral("link_out")},
            {QStringLiteral("west"), QStringLiteral("link_in")},
            {QStringLiteral("south"), QStringLiteral("down_out")},
            {QStringLiteral("north"), QStringLiteral("up_in")}
        }}
    });

    return manifest;
}

IpcraftPackageManifest manifestTopologyMetadataMeshPackage(const QString& packageId,
                                                           const QString& moduleId) {
    IpcraftPackageManifest manifest;
    manifest.id = packageId;
    manifest.connectionClasses.push_back(IpcraftConnectionClass{
        QStringLiteral("manifest_mesh_link"),
        QStringList{QStringLiteral("source"), QStringLiteral("sink")},
        false
    });

    IpcraftModuleDescriptor module;
    module.id = moduleId;
    module.name = QStringLiteral("Manifest Metadata Mesh Tile");
    module.graphRole = QStringLiteral("host");
    module.parameters = QJsonObject{
        {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("int")},
                                          {QStringLiteral("default"), 0}}},
        {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("int")},
                                          {QStringLiteral("default"), 0}}},
        {QStringLiteral("display_name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("default"), QString()}}},
        {QStringLiteral("external_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                    {QStringLiteral("default"), QString()}}}
    };
    module.interfaces = {
        manifestMetadataMeshInterface(QStringLiteral("mesh_out"),
                                      QStringLiteral("source"),
                                      QStringLiteral("east"),
                                      QStringLiteral("mesh_in")),
        manifestMetadataMeshInterface(QStringLiteral("mesh_in"),
                                      QStringLiteral("sink"),
                                      QStringLiteral("west"),
                                      QStringLiteral("mesh_out"))
    };
    manifest.modules.push_back(module);
    return manifest;
}

IpcraftPackageManifest manifestMixedLegacyAndMetadataMeshPackage(const QString& packageId,
                                                                 const QString& moduleId) {
    IpcraftPackageManifest manifest = manifestTopologyMetadataMeshPackage(packageId, moduleId);
    require(!manifest.modules.isEmpty(), "mixed metadata manifest should contain a module");
    manifest.modules.front().interfaces.prepend(
        manifestMeshInterface(QStringLiteral("east"), QStringLiteral("source")));
    return manifest;
}

IpcraftPackageManifest manifestDuplicateMetadataSideMeshPackage(const QString& packageId,
                                                                const QString& moduleId) {
    IpcraftPackageManifest manifest = manifestTopologyMetadataMeshPackage(packageId, moduleId);
    require(!manifest.modules.isEmpty(), "duplicate metadata manifest should contain a module");
    manifest.modules.front().interfaces.prepend(
        manifestMetadataMeshInterface(QStringLiteral("mesh_out_alt"),
                                      QStringLiteral("source"),
                                      QStringLiteral("east"),
                                      QStringLiteral("mesh_in")));
    return manifest;
}

IpcraftPackageManifest manifestMissingOppositeMetadataMeshPackage(const QString& packageId,
                                                                  const QString& moduleId) {
    IpcraftPackageManifest manifest = manifestTopologyMetadataMeshPackage(packageId, moduleId);
    require(!manifest.modules.isEmpty(), "missing-opposite manifest should contain a module");
    require(!manifest.modules.front().interfaces.isEmpty(),
            "missing-opposite manifest should contain interfaces");
    manifest.modules.front().interfaces.front().topology.oppositeInterfaceId =
        QStringLiteral("missing_mesh_in");
    return manifest;
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

void testPackageBackedPresetConnectionsPreserveResolvedMetadata() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.topology_metadata");
    const QString typeName = QStringLiteral("PackageBackedXP");
    const ModuleType type = packageBackedRouterType(typeName, packageId);
    require(registry.registerType(type), "package-backed router type should register");
    registry.loadIpcraftPackages({packageBackedRouterManifest(packageId, typeName)});
    ModuleRegistry::instance().registerType(type);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.routerModule = typeName;
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString leftId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_0"));
    const QString rightId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("xp_0_1"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "1x2 package-backed mesh should create one link");
    const Connection* connection = graph.connections().front().get();
    require(connection->connectionClassId() == QStringLiteral("router_link"),
            "generated package-backed link should store the resolved connection class");
    require(connection->status() == QStringLiteral("valid"),
            "generated package-backed link should store resolved connection status");
    require(connection->interfaces().size() == 2,
            "generated package-backed link should store normalized interfaces");
    require(connection->interfaces().at(0).instanceId == leftId &&
                connection->interfaces().at(0).interfaceId == QStringLiteral("east"),
            "generated package-backed link should store source interface metadata");
    require(connection->interfaces().at(1).instanceId == rightId &&
                connection->interfaces().at(1).interfaceId == QStringLiteral("west"),
            "generated package-backed link should store target interface metadata");
}

void testMeshPresetUsesManifestConnectionClassesNotEastWestNames() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.manifest_named_mesh");
    const QString typeName = QStringLiteral("ManifestNamedMeshTile");
    const IpcraftPackageManifest manifest = manifestNamedMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}), "manifest-named mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr, "manifest-named mesh type should register");
    ModuleRegistry::instance().registerType(*loadedType);

    const ModuleInterfaceMetadata linkOutMetadata =
        loadedType->interfaceMetadata.value(QStringLiteral("link_out"));
    require(linkOutMetadata.autocompleteGroup == QStringLiteral("router_side"),
            "topology preset ports should be marked as router-side metadata without east/west ids");
    require(linkOutMetadata.topologyRule == QStringLiteral("opposite_side"),
            "topology preset ports should carry topology rules from the manifest mapping");
    const Port* linkOutPort = defaultPortById(*loadedType, QStringLiteral("link_out"));
    require(linkOutPort != nullptr && linkOutPort->role() == QStringLiteral("router"),
            "topology preset ports should use router port role without hardcoded interface names");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.preset.ports.insert(QStringLiteral("east"), QStringLiteral("link_out"));
    request.preset.ports.insert(QStringLiteral("west"), QStringLiteral("link_in"));
    request.preset.ports.insert(QStringLiteral("south"), QStringLiteral("down_out"));
    request.preset.ports.insert(QStringLiteral("north"), QStringLiteral("up_in"));
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString leftId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("tile_0_0"));
    const QString rightId = scopedPresetModuleId(QStringLiteral("noc_0"), QStringLiteral("tile_0_1"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "1x2 mesh with manifest-named interfaces should create one link");
    require(result.connectionIds.first().endsWith(QStringLiteral("_link_out")),
            "generated connection ids should use the mapped interface id instead of an east literal");
    const Connection* connection = graph.connections().front().get();
    require(connection->connectionClassId() == QStringLiteral("manifest_mesh_link"),
            "generated link should resolve the manifest connection class");
    require(connection->interfaces().size() == 2,
            "generated link should store normalized manifest interfaces");
    require(connection->interfaces().at(0).instanceId == leftId &&
                connection->interfaces().at(0).interfaceId == QStringLiteral("link_out"),
            "generated link should store the mapped source interface id");
    require(connection->interfaces().at(1).instanceId == rightId &&
                connection->interfaces().at(1).interfaceId == QStringLiteral("link_in"),
            "generated link should store the mapped target interface id");
}

void testMeshPresetUsesManifestTopologyMetadataWithoutPortMappings() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.manifest_topology_metadata_mesh");
    const QString typeName = QStringLiteral("ManifestTopologyMetadataMeshTile");
    const IpcraftPackageManifest manifest =
        manifestTopologyMetadataMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}),
            "manifest topology metadata mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr, "manifest topology metadata mesh type should register");
    ModuleRegistry::instance().registerType(*loadedType);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "1x2 metadata mesh should create one link without explicit directional port mappings");
    const Connection* connection = graph.connections().front().get();
    require(connection->source().portId == QStringLiteral("mesh_out"),
            "metadata mesh source port should come from topology side metadata");
    require(connection->target().portId == QStringLiteral("mesh_in"),
            "metadata mesh target port should come from topology opposite metadata");
}

void testMeshPresetPrefersTopologyMetadataOverLegacyPortHints() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.mixed_legacy_metadata_mesh");
    const QString typeName = QStringLiteral("MixedLegacyMetadataMeshTile");
    const IpcraftPackageManifest manifest =
        manifestMixedLegacyAndMetadataMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}),
            "mixed legacy and metadata mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr, "mixed legacy and metadata mesh type should register");
    require(loadedType->defaultPorts.size() >= 3,
            "mixed mesh type should include legacy and metadata ports");
    require(loadedType->defaultPorts.front().id() == QStringLiteral("east"),
            "legacy east port should appear before metadata mesh_out");
    ModuleRegistry::instance().registerType(*loadedType);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "1x2 mixed metadata mesh should create one link without explicit port mappings");
    const Connection* connection = graph.connections().front().get();
    require(connection->source().portId == QStringLiteral("mesh_out"),
            "metadata topology side should win over an earlier legacy east port");
    require(connection->target().portId == QStringLiteral("mesh_in"),
            "metadata topology opposite should still select the paired target port");
}

void testMeshPresetRejectsDuplicateTopologySideMetadata() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.duplicate_topology_side_mesh");
    const QString typeName = QStringLiteral("DuplicateTopologySideMeshTile");
    const IpcraftPackageManifest manifest =
        manifestDuplicateMetadataSideMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}),
            "duplicate topology side mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr, "duplicate topology side mesh type should register");
    ModuleRegistry::instance().registerType(*loadedType);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "duplicate topology side metadata should reject mesh preset");
    require(result.error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive) ||
                result.error.contains(QStringLiteral("ambiguous"), Qt::CaseInsensitive),
            "duplicate topology side error should mention duplicate or ambiguous metadata");
    require(result.error.contains(QStringLiteral("east")),
            "duplicate topology side error should identify the ambiguous side");
    require(graph.modules().empty(), "duplicate topology side failure should roll back modules");
    require(graph.connections().empty(), "duplicate topology side failure should not create connections");
}

void testMeshPresetExplicitPortMappingWinsWithDuplicateTopologySideMetadata() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.explicit_duplicate_topology_side_mesh");
    const QString typeName = QStringLiteral("ExplicitDuplicateTopologySideMeshTile");
    const IpcraftPackageManifest manifest =
        manifestDuplicateMetadataSideMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}),
            "explicit duplicate topology side mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr,
            "explicit duplicate topology side mesh type should register");
    ModuleRegistry::instance().registerType(*loadedType);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.preset.ports.insert(QStringLiteral("east"), QStringLiteral("mesh_out"));
    request.preset.ports.insert(QStringLiteral("west"), QStringLiteral("mesh_in"));
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.connections().size() == 1,
            "explicit port mappings should bypass duplicate metadata-side ambiguity");
    const Connection* connection = graph.connections().front().get();
    require(connection->source().portId == QStringLiteral("mesh_out"),
            "explicit source mapping should select mesh_out");
    require(connection->target().portId == QStringLiteral("mesh_in"),
            "explicit target mapping should select mesh_in");
}

void testMeshPresetRejectsMissingTopologyOppositeInterface() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const QString packageId = QStringLiteral("finepaper.missing_topology_opposite_mesh");
    const QString typeName = QStringLiteral("MissingTopologyOppositeMeshTile");
    const IpcraftPackageManifest manifest =
        manifestMissingOppositeMetadataMeshPackage(packageId, typeName);
    require(registry.loadIpcraftPackages({manifest}),
            "missing topology opposite mesh package should load");
    const ModuleType* loadedType = registry.getType(typeName);
    require(loadedType != nullptr, "missing topology opposite mesh type should register");
    ModuleRegistry::instance().registerType(*loadedType);

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageId;
    request.instanceId = QStringLiteral("noc_0");
    request.preset.id = QStringLiteral("mesh");
    request.preset.label = QStringLiteral("Mesh");
    request.preset.kind = QStringLiteral("mesh");
    request.preset.routerModule = typeName;
    request.preset.idPattern = QStringLiteral("tile_{row}_{col}");
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    require(!result.success, "missing topology opposite metadata should reject mesh preset");
    require(result.error.contains(QStringLiteral("opposite"), Qt::CaseInsensitive),
            "missing topology opposite error should identify opposite metadata");
    require(result.error.contains(QStringLiteral("missing_mesh_in")),
            "missing topology opposite error should name the unknown interface");
    require(graph.modules().empty(), "missing topology opposite failure should roll back modules");
    require(graph.connections().empty(), "missing topology opposite failure should not create connections");
}

void testPackageScopedTopologyPresetUsesActivePackageRouter() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const IpcraftPackageManifest alpha =
        manifestNamedMeshPackage(QStringLiteral("finepaper.alpha_router"), QStringLiteral("Router"));
    const IpcraftPackageManifest beta =
        manifestNamedMeshPackage(QStringLiteral("finepaper.beta_router"), QStringLiteral("Router"));
    require(registry.loadIpcraftPackages({alpha, beta}),
            "duplicate Router modules should load from separate packages");

    const QString betaTypeName =
        scopedTypeName(QStringLiteral("finepaper.beta_router"), QStringLiteral("Router"));
    const ModuleType* betaRouter = registry.getType(betaTypeName);
    require(betaRouter != nullptr,
            "beta Router should be registered with a package-scoped type name");

    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("mesh");
    preset.label = QStringLiteral("Mesh");
    preset.kind = QStringLiteral("mesh");
    preset.routerModule = QStringLiteral("Router");
    preset.idPattern = QStringLiteral("tile_{row}_{col}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("link_out"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("link_in"));
    preset.ports.insert(QStringLiteral("south"), QStringLiteral("down_out"));
    preset.ports.insert(QStringLiteral("north"), QStringLiteral("up_in"));

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.beta_router");
    request.instanceId = QStringLiteral("beta_0");
    request.preset = preset;
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 2);

    const TopologyPresetResult result = TopologyPresetBuilder::apply(&graph, registry, request);

    const QString leftId = scopedPresetModuleId(QStringLiteral("beta_0"), QStringLiteral("tile_0_0"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(graph.getModule(leftId) != nullptr,
            "package-scoped topology should create the active package router");
    require(graph.getModule(leftId)->type() == betaTypeName,
            "topology-created modules should store the package-scoped module type");
    require(graph.getModule(leftId)->ipcoreId() == QStringLiteral("finepaper.beta_router"),
            "topology-created modules should belong to the active package");
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
    const QString typeName = QStringLiteral("XPRuleReject");
    const ModuleType type = routerType(typeName, QStringLiteral("finepaper.noc"));
    require(registry.registerType(type),
            "router type should register");
    require(ModuleRegistry::instance().registerType(type),
            "global router type should register for connection rule metadata");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.instanceId = QStringLiteral("noc_0");
    request.preset = meshPreset();
    request.preset.routerModule = typeName;
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
    const QVector<IpcraftPackageManifest> packages =
        loadIpcraftPackageManifests({repositoryPath(QStringLiteral("ipcores"))});
    const auto packageIt = std::find_if(packages.cbegin(),
                                        packages.cend(),
                                        [](const IpcraftPackageManifest& package) {
                                            return package.id == QStringLiteral("finepaper.ravenoc");
                                        });
    require(packageIt != packages.cend(), "RaveNoC package should be discovered");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages({*packageIt}),
            "RaveNoC package module types should load");

    QVector<TopologyPresetDescriptor> presets;
    for (const QJsonObject& topology : packageIt->topologies) {
        TopologyPresetDescriptor preset;
        preset.id = topology.value(QStringLiteral("id")).toString();
        preset.label = topology.value(QStringLiteral("label")).toString();
        preset.kind = topology.value(QStringLiteral("kind")).toString();
        preset.routerModule = topology.value(QStringLiteral("module")).toString();
        preset.idPattern = topology.value(QStringLiteral("id_pattern")).toString();
        const QJsonObject ports = topology.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            preset.ports.insert(it.key(), it.value().toString());
        }
        const QJsonObject parameters = topology.value(QStringLiteral("parameters")).toObject();
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            const QJsonObject parameterObject = it.value().toObject();
            preset.parameters.insert(it.key(),
                                     TopologyPresetParameterDescriptor{
                                         parameterObject.value(QStringLiteral("label")).toString(),
                                         parameterObject.value(QStringLiteral("default")).toInt(),
                                         parameterObject.value(QStringLiteral("min")).toInt(),
                                         parameterObject.value(QStringLiteral("max")).toInt()
                                     });
        }
        presets.push_back(preset);
    }

    const auto meshIt = std::find_if(presets.cbegin(),
                                     presets.cend(),
                                     [](const TopologyPresetDescriptor& preset) {
                                         return preset.id == QStringLiteral("mesh");
                                     });
    require(meshIt != presets.cend(), "RaveNoC mesh preset should exist");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = packageIt->id;
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
        testPackageBackedPresetConnectionsPreserveResolvedMetadata();
        testMeshPresetUsesManifestConnectionClassesNotEastWestNames();
        testMeshPresetUsesManifestTopologyMetadataWithoutPortMappings();
        testMeshPresetPrefersTopologyMetadataOverLegacyPortHints();
        testMeshPresetRejectsDuplicateTopologySideMetadata();
        testMeshPresetExplicitPortMappingWinsWithDuplicateTopologySideMetadata();
        testMeshPresetRejectsMissingTopologyOppositeInterface();
        testPackageScopedTopologyPresetUsesActivePackageRouter();
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
