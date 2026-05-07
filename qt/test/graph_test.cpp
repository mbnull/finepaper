// Graph integration-style tests for JSON import/export and topology behavior.
#include "graph/graph.h"
#include "modules/modulelabels.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "common/portlayout.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   std::initializer_list<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    for (const auto& port : ports) {
        module->addPort(port);
    }
    return module;
}

bool moduleTypeHasPort(const ModuleType* type, const QString& portId) {
    return type && std::any_of(type->defaultPorts.begin(), type->defaultPorts.end(),
        [&](const Port& port) { return port.id() == portId; });
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testConnectionValidationPreventsPortReuse() {
    Graph graph;

    require(graph.addModule(makeModule(
        "source",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add source module");
    require(graph.addModule(makeModule(
        "target_a",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add first target module");
    require(graph.addModule(makeModule(
        "target_b",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add second target module");

    const PortRef source{"source", "out"};
    const PortRef targetA{"target_a", "in"};
    const PortRef targetB{"target_b", "in"};

    require(graph.isValidConnection(source, targetA), "expected first connection to be valid");

    graph.addConnection(std::make_unique<Connection>("c1", source, targetA));

    require(graph.connections().size() == 1, "expected connection to be stored");
    require(!graph.isValidConnection(source, targetB), "source port should not be reusable");
    require(!graph.isValidConnection(source, targetA), "duplicate connection should be rejected");
}

void testInoutBusConnectionsAreValid() {
    Graph graph;

    require(graph.addModule(makeModule(
        "router_a",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add first inout bus module");
    require(graph.addModule(makeModule(
        "router_b",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add second inout bus module");

    const PortRef source{"router_a", "bus"};
    const PortRef target{"router_b", "bus"};

    require(graph.isValidConnection(source, target), "matching inout bus ports should connect");

    graph.addConnection(std::make_unique<Connection>("bus_link", source, target));
    require(graph.connections().size() == 1, "expected inout bus connection to be stored");
}

void testInoutPortsCannotBeReusedAcrossConnectionSides() {
    Graph graph;

    require(graph.addModule(makeModule(
        "router_a",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add first inout module");
    require(graph.addModule(makeModule(
        "router_b",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add second inout module");
    require(graph.addModule(makeModule(
        "router_c",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add third inout module");

    const PortRef source{"router_a", "bus"};
    const PortRef middle{"router_b", "bus"};
    const PortRef target{"router_c", "bus"};

    require(graph.isValidConnection(source, middle), "expected first inout connection to be valid");
    graph.addConnection(std::make_unique<Connection>("bus_link_ab", source, middle));

    require(graph.connections().size() == 1, "expected initial inout connection to be stored");
    require(!graph.isValidConnection(middle, target),
            "inout port already used as a target should not be reusable as a source");
    require(!graph.isValidConnection(target, middle),
            "inout port already used as a target should not be reusable as a target again");
}

ModuleInterfaceMetadata makeInterfaceMetadata(const QString& id,
                                              const QString& bus,
                                              const QString& role,
                                              const QStringList& compatibleRoles,
                                              const QStringList& matchFields) {
    ModuleInterfaceMetadata metadata;
    metadata.id = id;
    metadata.bus = bus;
    metadata.role = role;
    metadata.compatibleRoles = compatibleRoles;
    metadata.matchFields = matchFields;
    return metadata;
}

void testInterfaceCompatibilityRejectsMismatchedConfiguredFields() {
    ModuleType endpointType;
    endpointType.name = "IfaceEndpoint";
    ModuleInterfaceMetadata endpointInterface =
        makeInterfaceMetadata("noc", "ni_link", "initiator", {"target"}, {"protocol", "data_width"});
    endpointInterface.parameterBindings.insert("protocol", "protocol");
    endpointInterface.parameterBindings.insert("data_width", "data_width");
    endpointType.interfaceMetadata.insert("noc", endpointInterface);
    ModuleRegistry::instance().registerType(endpointType);

    ModuleType xpType;
    xpType.name = "IfaceXp";
    ModuleInterfaceMetadata xpInterface =
        makeInterfaceMetadata("local0", "ni_link", "target", {"initiator"}, {"protocol", "data_width"});
    xpInterface.acceptedValues.insert("protocol", {"axi4"});
    xpInterface.acceptedValues.insert("data_width", {"128"});
    xpType.interfaceMetadata.insert("local0", xpInterface);
    ModuleRegistry::instance().registerType(xpType);

    Graph graph;
    auto endpoint = makeModule(
        "endpoint",
        "IfaceEndpoint",
        {Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc")});
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    auto xp = makeModule(
        "xp",
        "IfaceXp",
        {Port("local0", Port::Direction::Input, "bus", "Local0", {}, "attachment", "ni_link", "local0")});

    require(graph.addModule(std::move(endpoint)), "failed to add interface endpoint");
    require(graph.addModule(std::move(xp)), "failed to add interface XP");

    require(!graph.isValidConnection(PortRef{"endpoint", "noc"}, PortRef{"xp", "local0"}),
            "interface connection should reject mismatched data_width");
}

void testInterfaceCompatibilityAcceptsMatchingConfiguredFields() {
    ModuleType endpointType;
    endpointType.name = "IfaceEndpointOk";
    ModuleInterfaceMetadata endpointInterface =
        makeInterfaceMetadata("noc", "ni_link", "initiator", {"target"}, {"protocol", "data_width"});
    endpointInterface.parameterBindings.insert("protocol", "protocol");
    endpointInterface.parameterBindings.insert("data_width", "data_width");
    endpointType.interfaceMetadata.insert("noc", endpointInterface);
    ModuleRegistry::instance().registerType(endpointType);

    ModuleType xpType;
    xpType.name = "IfaceXpOk";
    ModuleInterfaceMetadata xpInterface =
        makeInterfaceMetadata("local0", "ni_link", "target", {"initiator"}, {"protocol", "data_width"});
    xpInterface.acceptedValues.insert("protocol", {"axi4"});
    xpInterface.acceptedValues.insert("data_width", {"32", "64", "128"});
    xpType.interfaceMetadata.insert("local0", xpInterface);
    ModuleRegistry::instance().registerType(xpType);

    Graph graph;
    auto endpoint = makeModule(
        "endpoint",
        "IfaceEndpointOk",
        {Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc")});
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    auto xp = makeModule(
        "xp",
        "IfaceXpOk",
        {Port("local0", Port::Direction::Input, "bus", "Local0", {}, "attachment", "ni_link", "local0")});

    require(graph.addModule(std::move(endpoint)), "failed to add matching interface endpoint");
    require(graph.addModule(std::move(xp)), "failed to add matching interface XP");

    require(graph.isValidConnection(PortRef{"endpoint", "noc"}, PortRef{"xp", "local0"}),
            "interface connection should accept matching protocol and data_width");
}

void testRouterLinksRequireOppositeSides() {
    Graph graph;

    auto source = makeModule(
        "router_a",
        "XP",
        {
            Port("east", Port::Direction::Output, "bus", "East", {}, "router", "router_link", "east"),
            Port("south", Port::Direction::Output, "bus", "South", {}, "router", "router_link", "south")
        });
    auto target = makeModule(
        "router_b",
        "XP",
        {
            Port("west", Port::Direction::Input, "bus", "West", {}, "router", "router_link", "west"),
            Port("north", Port::Direction::Input, "bus", "North", {}, "router", "router_link", "north")
        });

    require(graph.addModule(std::move(source)), "failed to add source router");
    require(graph.addModule(std::move(target)), "failed to add target router");

    require(graph.isValidConnection(PortRef{"router_a", "east"}, PortRef{"router_b", "west"}),
            "east router interface should connect to west");
    require(!graph.isValidConnection(PortRef{"router_a", "east"}, PortRef{"router_b", "north"}),
            "east router interface should not connect to north");
}

void testLegacyAxiEndpointImportMigratesProtocolBeforeLinkValidation() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for legacy axi import");

    const QString jsonPath = QDir(tempDir.path()).filePath("legacy_axi.json");
    QFile file(jsonPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create legacy axi graph JSON");
    file.write(R"JSON({
  "name": "legacy_axi",
  "version": "1.0",
  "parameters": {},
  "xps": [
    {
      "id": "xp_0_0",
      "x": 0,
      "y": 0,
      "endpoints": ["ep_0"]
    }
  ],
  "endpoints": [
    {
      "id": "ep_0",
      "type": "master",
      "protocol": "axi",
      "data_width": 64
    }
  ],
  "connections": []
})JSON");
    file.close();

    Graph graph;
    require(graph.loadFromJson(jsonPath), "legacy axi graph should load");
    require(graph.connections().size() == 1,
            "legacy axi endpoint attachment should be restored");

    const Module* endpoint = nullptr;
    const Module* xp = nullptr;
    for (const auto& module : graph.modules()) {
        if (ModuleLabels::externalId(module.get()) == "ep_0") {
            endpoint = module.get();
        } else if (ModuleLabels::externalId(module.get()) == "xp_0_0") {
            xp = module.get();
        }
    }

    require(endpoint != nullptr, "legacy endpoint should be imported");
    require(xp != nullptr, "legacy XP should be imported");
    const auto protocolIt = endpoint->parameters().find("protocol");
    require(protocolIt != endpoint->parameters().end(),
            "legacy endpoint protocol should be present after import");
    const Parameter::Value protocolValue = protocolIt.value().value();
    const auto* protocol = std::get_if<QString>(&protocolValue);
    require(protocol && *protocol == "axi4",
            "legacy axi endpoint protocol should migrate to axi4");

    const Connection* connection = graph.connections().front().get();
    require(connection->source().moduleId == endpoint->id() &&
                connection->source().portId == "noc",
            "legacy endpoint attachment should use Endpoint.noc as the source interface");
    require(connection->target().moduleId == xp->id() &&
                connection->target().portId == "local0",
            "legacy endpoint attachment should use XP.local0 as the target interface");
}

void testRemovingModuleAlsoRemovesAttachedConnections() {
    Graph graph;

    require(graph.addModule(makeModule(
        "producer",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add producer module");
    require(graph.addModule(makeModule(
        "consumer",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add consumer module");

    graph.addConnection(std::make_unique<Connection>(
        "producer_consumer",
        PortRef{"producer", "out"},
        PortRef{"consumer", "in"}));

    require(graph.connections().size() == 1, "expected connection before module removal");

    graph.removeModule("producer");

    require(graph.getModule("producer") == nullptr, "removed module should no longer exist");
    require(graph.connections().empty(), "attached connections should be removed with the module");
}

void testClearRemovesAllModulesAndConnections() {
    Graph graph;

    require(graph.addModule(makeModule(
        "producer",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add producer module");
    require(graph.addModule(makeModule(
        "consumer",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add consumer module");

    graph.addConnection(std::make_unique<Connection>(
        "producer_consumer",
        PortRef{"producer", "out"},
        PortRef{"consumer", "in"}));

    require(graph.connections().size() == 1, "expected connection before clear");

    graph.clear();

    require(graph.modules().empty(), "clear should remove every module");
    require(graph.connections().empty(), "clear should remove every connection");
}

void testGraphForwardsModuleParameterChanges() {
    Graph graph;
    QString changedModuleId;
    QString changedParameterName;
    int signalCount = 0;

    QObject::connect(&graph, &Graph::parameterChanged, &graph,
        [&](const QString& moduleId, const QString& paramName) {
            ++signalCount;
            changedModuleId = moduleId;
            changedParameterName = paramName;
        });

    auto module = makeModule(
        "configurable",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")});
    Module* modulePtr = module.get();

    require(graph.addModule(std::move(module)), "failed to add configurable module");

    modulePtr->setParameter("buffer_depth", 8);

    require(signalCount == 1, "expected exactly one forwarded parameterChanged signal");
    require(changedModuleId == "configurable", "forwarded signal should include module id");
    require(changedParameterName == "buffer_depth", "forwarded signal should include parameter name");
}

void testLegacyEndpointTypeStillClassifiesAsEndpointPort() {
    const Port legacyEndpointPort("noc", Port::Direction::Input, "endpoint", "NoC");
    require(PortLayout::isEndpointPort(legacyEndpointPort),
            "legacy endpoint type should classify as endpoint port");
    require(PortLayout::isEndpointPortId("local3"),
            "localN interface ids should classify as endpoint slots");
    require(PortLayout::endpointPortSlot("local3") == 3,
            "localN interface ids should preserve their endpoint slot index");
}

void testBundleMetadataLoadsFromXml() {
    const ModuleType* xpType = ModuleRegistry::instance().getType("XP");
    require(xpType != nullptr, "XP type should be registered");
    require(xpType->pluginId == "finepaper.noc", "XP type should come from bundled NoC plugin");
    require(xpType->description.contains("Mesh router"), "XP description should come from bundle XML");
    require(xpType->nodeColor == "#7cb9e8", "XP node color should come from bundle XML");
    require(xpType->editorLayout == "mesh_router", "XP layout should come from bundle XML");
    require(xpType->supportsCollapse, "XP collapse capability should come from bundle XML");
    require(xpType->expandedNodeHeight == 116, "XP expanded height should come from bundle XML");
    require(xpType->configFields.size() == 5, "XP config zone should be generated from configurable parameters");
    require(xpType->configFields.first().description.contains("canvas"),
            "XP parameter descriptions should be preserved in config fields");
    require(moduleTypeHasPort(xpType, "east"), "XP should expose east as one visible router interface");
    require(moduleTypeHasPort(xpType, "west"), "XP should expose west as one visible router interface");
    require(moduleTypeHasPort(xpType, "local0"), "XP should expose local0 as one visible endpoint interface");
    require(!moduleTypeHasPort(xpType, "east_in"), "XP should not expose legacy east_in");
    require(!moduleTypeHasPort(xpType, "east_out"), "XP should not expose legacy east_out");

    const auto eastInterface = xpType->interfaceMetadata.find("east");
    require(eastInterface != xpType->interfaceMetadata.end(), "XP east interface metadata should load");
    require(eastInterface->label == "East", "XP east interface label should load");
    require(eastInterface->role == "initiator", "XP east interface should use IP-XACT-compatible initiator role");

    const auto eastAnchor = xpType->interfaceAnchors.find("east");
    require(eastAnchor != xpType->interfaceAnchors.end(), "XP east anchor should load from view XML");
    require(eastAnchor->x == 136.0 && eastAnchor->y == 58.0,
            "XP east anchor should preserve pixel coordinates from view XML");
    require(eastAnchor->normalX.value_or(0.0) == 1.0 &&
                eastAnchor->normalY.value_or(0.0) == 0.0,
            "XP east anchor should preserve connection normal");
    require(eastAnchor->label == "East", "XP east anchor label should load");
    require(eastAnchor->labelX.value_or(0.0) == 112.0 &&
                eastAnchor->labelY.value_or(0.0) == 58.0,
            "XP east anchor label position should preserve pixel coordinates");

    const ModuleType* endpointType = ModuleRegistry::instance().getType("Endpoint");
    require(endpointType != nullptr, "Endpoint type should be registered");
    require(endpointType->pluginId == "finepaper.noc", "Endpoint type should come from bundled NoC plugin");
    require(endpointType->description.contains("Endpoint interface"),
            "Endpoint description should come from bundle XML");
    require(endpointType->nodeColor == "#d6f4b6", "Endpoint node color should come from bundle XML");
    require(endpointType->configFields.size() == 7,
            "Endpoint config zone should be generated from configurable parameters");
    require(moduleTypeHasPort(endpointType, "noc"), "Endpoint should expose noc as the visible interface");
    require(endpointType->defaultPorts.front().direction() == Port::Direction::InOut,
            "Endpoint noc interface should be a bidirectional interface anchor");
    const auto nocAnchor = endpointType->interfaceAnchors.find("noc");
    require(nocAnchor != endpointType->interfaceAnchors.end(), "Endpoint noc anchor should load from view XML");
    require(nocAnchor->x == 104.0 && nocAnchor->y == 27.0,
            "Endpoint noc anchor should preserve pixel coordinates");

    const auto local0It = std::find_if(xpType->defaultPorts.begin(), xpType->defaultPorts.end(),
        [](const Port& port) { return port.id() == QStringLiteral("local0"); });
    require(local0It != xpType->defaultPorts.end(), "XP local0 port should exist");
    require(local0It->direction() == Port::Direction::InOut,
            "XP local endpoint interface should be a bidirectional interface anchor");
}

void testLegacyDirectionalImportUsesInterfaceIds() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for legacy dir import");

    const QString jsonPath = QDir(tempDir.path()).filePath("legacy_dir.json");
    QFile file(jsonPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create legacy dir graph JSON");
    file.write(R"JSON({
  "name": "legacy_dir",
  "version": "1.0",
  "parameters": {},
  "xps": [
    { "id": "xp_0_0", "x": 0, "y": 0, "endpoints": [] },
    { "id": "xp_1_0", "x": 1, "y": 0, "endpoints": [] }
  ],
  "endpoints": [],
  "connections": [
    { "from": "xp_0_0", "to": "xp_1_0", "dir": "east" }
  ]
})JSON");
    file.close();

    Graph graph;
    require(graph.loadFromJson(jsonPath), "legacy directional graph should load");
    require(graph.connections().size() == 1,
            "legacy directional router link should be restored");

    const Connection* connection = graph.connections().front().get();
    const Module* sourceModule = graph.getModule(connection->source().moduleId);
    const Module* targetModule = graph.getModule(connection->target().moduleId);
    require(ModuleLabels::externalId(sourceModule) == "xp_0_0",
            "legacy directional source router should be preserved");
    require(ModuleLabels::externalId(targetModule) == "xp_1_0",
            "legacy directional target router should be preserved");
    require(connection->source().portId == "east",
            "legacy east output should normalize to the east interface id");
    require(connection->target().portId == "west",
            "legacy east target should normalize to the west interface id");
}

void testXmlBundleWithoutGraphicsFallsBackToSimpleNode() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for xml fallback bundle test");

    const QString bundlePath = QDir(tempDir.path()).filePath("modules.xml");
    QFile bundleFile(bundlePath);
    require(bundleFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create XML fallback bundle");
    bundleFile.write(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <module name="DMA" palette_label="DMA" description="Generic DMA block.">
    <ports>
      <port id="cfg" direction="input" type="config" name="CFG" description="left configuration port" />
      <port id="irq" direction="output" type="interrupt" name="IRQ" description="right interrupt output" />
      <port id="trace" direction="output" type="debug" name="TRACE" description="top trace port" />
    </ports>
    <parameters>
      <parameter name="x" type="int" default="0" configurable="false" />
      <parameter name="mode" type="string" default="linear" label="Mode" description="DMA transfer mode." />
    </parameters>
  </module>
</module-bundle>)XML");
    bundleFile.close();

    XmlModuleTypeSource source(bundlePath);
    const QHash<QString, ModuleType> types = source.loadModuleTypes();
    auto dmaIt = types.find("DMA");
    require(dmaIt != types.end(), "DMA type should load from XML bundle");
    require(dmaIt->editorLayout == "fallback",
            "modules without explicit graphics should use fallback layout");
    require(dmaIt->configFields.size() == 1,
            "fallback XML bundle should auto-generate config fields from configurable parameters");
    require(dmaIt->defaultPorts.size() == 3, "port descriptions should load from XML bundle");
    require(dmaIt->defaultPorts[2].description().contains("top"),
            "port description should be preserved for fallback layout parsing");
}

void testXmlBundleLoadsExtendedParameterMetadataWhenPresent() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for parameter metadata bundle test");

    const QString bundlePath = QDir(tempDir.path()).filePath("modules.xml");
    QFile bundleFile(bundlePath);
    require(bundleFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create XML metadata bundle");
    bundleFile.write(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <module name="Router" palette_label="Router" graph_group="xps" description="Router test bundle.">
    <parameters>
      <parameter name="routing_algorithm" type="string" default="xy" label="Routing algorithm">
        <choices>
          <choice value="xy" label="XY" />
          <choice value="odd_even" label="Odd-Even" />
        </choices>
      </parameter>
      <parameter name="vc_count" type="int" default="2" min="1" max="8" unit="VCs"
                 label="VC count" description="Virtual channel count." />
      <parameter name="external_id" type="string" default="" read_only="true"
                 label="External ID" description="Framework-facing ID." />
    </parameters>
  </module>
</module-bundle>)XML");
    bundleFile.close();

    XmlModuleTypeSource source(bundlePath);
    const QHash<QString, ModuleType> types = source.loadModuleTypes();
    auto routerIt = types.find("Router");
    require(routerIt != types.end(), "Router type should load from XML bundle");
    require(routerIt->parameterMetadata.contains("routing_algorithm"),
            "routing_algorithm metadata should be available");
    require(routerIt->parameterMetadata.value("routing_algorithm").choices.size() == 2,
            "routing_algorithm should expose two editor choices");
    require(routerIt->parameterMetadata.value("vc_count").minimumValue.value_or(0.0) == 1.0,
            "vc_count minimum should load from XML metadata");
    require(routerIt->parameterMetadata.value("vc_count").maximumValue.value_or(0.0) == 8.0,
            "vc_count maximum should load from XML metadata");
    require(routerIt->parameterMetadata.value("vc_count").unit == "VCs",
            "vc_count unit should load from XML metadata");
    require(routerIt->parameterMetadata.value("external_id").readOnly,
            "external_id read_only flag should load from XML metadata");
}

void testFrameworkExportOmitsEditorOnlyCollapsedField() {
    Graph graph;

    auto xp = makeModule(
        "xp_internal",
        "XP",
        {Port("local0", Port::Direction::Input, "bus", "Local 0", {}, "attachment", "ni_link", "local0")});
    xp->setParameter("external_id", QString("xp_0_0"));
    xp->setParameter("x", 0);
    xp->setParameter("y", 0);
    xp->setParameter("collapsed", true);
    xp->setParameter("routing_algorithm", QString("xy"));

    auto endpoint = makeModule(
        "ep_internal",
        "Endpoint",
        {Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc")});
    endpoint->setParameter("external_id", QString("ep_0"));
    endpoint->setParameter("type", QString("master"));
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    require(graph.addModule(std::move(xp)), "failed to add XP module");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module");

    graph.addConnection(std::make_unique<Connection>(
        "xp_ep",
        PortRef{"ep_internal", "noc"},
        PortRef{"xp_internal", "local0"}));

    require(graph.connections().size() == 1, "expected endpoint connection to be stored");

    const QJsonObject frameworkRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Framework).object();
    const QJsonObject editorRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Editor).object();

    const QJsonObject frameworkConfig =
        frameworkRoot["xps"].toArray().first().toObject()["config"].toObject();
    const QJsonObject editorConfig =
        editorRoot["xps"].toArray().first().toObject()["config"].toObject();

    require(!frameworkConfig.contains("collapsed"),
            "framework export should omit editor-only collapsed state");
    require(editorConfig.contains("collapsed"),
            "editor export should preserve collapsed state");
    require(frameworkRoot["connections"].toArray().isEmpty(),
            "framework export should not emit endpoint attachments as generic edges");
    require(frameworkRoot["xps"].toArray().first().toObject()["endpoints"].toArray().first().toString() == "ep_0",
            "framework export should keep endpoint attachments in the XP endpoint list");
}

void testGenericPluginExportKeepsNonNocModules() {
    ModuleType accelType;
    accelType.name = QStringLiteral("GenericAccel");
    accelType.pluginId = QStringLiteral("finepaper.generic");
    ModuleRegistry::instance().registerType(accelType);

    Graph graph;
    auto module = makeModule(
        "accel_internal",
        "GenericAccel",
        {
            Port("cfg", Port::Direction::Input, "axi_lite", "CFG", {}, "control", "axi_lite"),
            Port("irq", Port::Direction::Output, "interrupt", "IRQ", {}, "status", "interrupt")
        });
    module->setParameter("display_name", QString("Generic Accel"));
    module->setParameter("x", 15);
    module->setParameter("y", 25);
    module->setParameter("width", 64);

    require(graph.addModule(std::move(module)), "failed to add generic module");

    const QJsonObject root =
        graph.toJsonDocument("generic_design", GraphJsonFlavor::Plugin).object();

    require(root["schema"].toString() == "finepaper-plugin-graph-v1",
            "generic plugin export should identify its schema");
    require(root["name"].toString() == "generic_design",
            "generic plugin export should include the design name");

    const QJsonArray modules = root["modules"].toArray();
    require(modules.size() == 1, "generic plugin export should include non-NoC module");
    const QJsonObject exportedModule = modules.first().toObject();
    require(exportedModule["id"].toString() == "generic_accel",
            "generic plugin export should prefer readable artifact ids over internal runtime ids");
    require(exportedModule["plugin"].toString() == "finepaper.generic",
            "generic plugin export should include plugin owner");
    require(exportedModule["type"].toString() == "GenericAccel",
            "generic plugin export should include module type");
    require(exportedModule["parameters"].toObject()["width"].toInt() == 64,
            "generic plugin export should include module parameters");

    const QJsonArray ports = exportedModule["ports"].toArray();
    require(ports.size() == 2, "generic plugin export should include ports");
    require(ports.first().toObject()["id"].toString() == "cfg",
            "generic plugin export should include port ids");
}

void testPluginExportUsesArtifactIdsInsteadOfRuntimeIds() {
    ModuleType sourceType;
    sourceType.name = QStringLiteral("PluginSource");
    sourceType.pluginId = QStringLiteral("finepaper.pluginids");
    ModuleRegistry::instance().registerType(sourceType);

    ModuleType targetType;
    targetType.name = QStringLiteral("PluginTarget");
    targetType.pluginId = QStringLiteral("finepaper.pluginids");
    ModuleRegistry::instance().registerType(targetType);

    Graph graph;
    auto source = makeModule(
        "0bf35d18_a3d3_4ce3_b89d_36e120b847b4",
        "PluginSource",
        {Port("out", Port::Direction::Output, "bus", "Out", {}, "source", "demo_bus")});
    source->setParameter("external_id", QString("source_0"));
    source->setParameter("display_name", QString("Source 0"));

    auto target = makeModule(
        "9ed21db3_a343_4420_afcb_d6b19cb997fe",
        "PluginTarget",
        {Port("in", Port::Direction::Input, "bus", "In", {}, "target", "demo_bus")});
    target->setParameter("external_id", QString("target_0"));
    target->setParameter("display_name", QString("Target 0"));

    require(graph.addModule(std::move(source)), "failed to add plugin source");
    require(graph.addModule(std::move(target)), "failed to add plugin target");
    graph.addConnection(std::make_unique<Connection>(
        "3c357093_4961_4ac7_8302_cad7f44f909d",
        PortRef{"0bf35d18_a3d3_4ce3_b89d_36e120b847b4", "out"},
        PortRef{"9ed21db3_a343_4420_afcb_d6b19cb997fe", "in"}));

    const QJsonObject root =
        graph.toJsonDocument("plugin_ids", GraphJsonFlavor::Plugin).object();
    const QJsonArray modules = root["modules"].toArray();
    const QJsonArray connections = root["connections"].toArray();

    require(modules.at(0).toObject()["id"].toString() == "source_0",
            "plugin export should use source external_id");
    require(modules.at(1).toObject()["id"].toString() == "target_0",
            "plugin export should use target external_id");
    require(connections.first().toObject()["id"].toString() == "source_0_out_to_target_0_in",
            "plugin export should generate readable connection id");
    require(connections.first().toObject()["source"].toObject()["module"].toString() == "source_0",
            "plugin export connection source should use external_id");
    require(connections.first().toObject()["target"].toObject()["module"].toString() == "target_0",
            "plugin export connection target should use external_id");

    const QString exportedText = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    require(!exportedText.contains("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"),
            "plugin export should not leak source runtime UUID");
    require(!exportedText.contains("9ed21db3_a343_4420_afcb_d6b19cb997fe"),
            "plugin export should not leak target runtime UUID");
    require(!exportedText.contains("3c357093_4961_4ac7_8302_cad7f44f909d"),
            "plugin export should not leak connection runtime UUID");
}

void testXmlExportPreservesEditorGraphContent() {
    Graph graph;

    auto xp = makeModule(
        "xp_internal",
        "XP",
        {Port("local0", Port::Direction::Input, "bus", "Local 0", {}, "attachment", "ni_link", "local0")});
    xp->setParameter("external_id", QString("xp_0_0"));
    xp->setParameter("x", 12);
    xp->setParameter("y", 34);
    xp->setParameter("collapsed", true);

    auto endpoint = makeModule(
        "ep_internal",
        "Endpoint",
        {Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc")});
    endpoint->setParameter("external_id", QString("ep_0"));
    endpoint->setParameter("type", QString("master"));
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    require(graph.addModule(std::move(xp)), "failed to add XP module for XML export");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module for XML export");

    graph.addConnection(std::make_unique<Connection>(
        "xp_ep",
        PortRef{"ep_internal", "noc"},
        PortRef{"xp_internal", "local0"}));

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const QString xmlPath = QDir(tempDir.path()).filePath("design.xml");
    require(graph.saveToXml(xmlPath), "failed to save graph XML");

    QFile file(xmlPath);
    require(file.open(QIODevice::ReadOnly), "failed to reopen graph XML");
    const QString xml = QString::fromUtf8(file.readAll());

    require(xml.contains("<graph>"), "XML export should contain graph root");
    require(xml.contains("<name type=\"string\">design</name>"),
            "XML export should contain design name");
    require(xml.contains("<id type=\"string\">xp_0_0</id>"),
            "XML export should contain XP external id");
    require(xml.contains("<collapsed type=\"bool\">true</collapsed>"),
            "XML export should preserve editor-only collapsed flag");
    require(xml.contains("<id type=\"string\">ep_0</id>"),
            "XML export should contain endpoint external id");
}

void testPluginExportOmitsIpInstanceByDefault() {
    Graph graph;

    const QJsonObject root =
        graph.toJsonDocument(QStringLiteral("demo"), GraphJsonFlavor::Plugin).object();
    require(!root.contains(QStringLiteral("ip_instance")),
            "Graph plugin export should not contain IP instance state by default");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testConnectionValidationPreventsPortReuse();
        testInoutBusConnectionsAreValid();
        testInoutPortsCannotBeReusedAcrossConnectionSides();
        testInterfaceCompatibilityRejectsMismatchedConfiguredFields();
        testInterfaceCompatibilityAcceptsMatchingConfiguredFields();
        testRouterLinksRequireOppositeSides();
        testLegacyAxiEndpointImportMigratesProtocolBeforeLinkValidation();
        testRemovingModuleAlsoRemovesAttachedConnections();
        testClearRemovesAllModulesAndConnections();
        testGraphForwardsModuleParameterChanges();
        testLegacyEndpointTypeStillClassifiesAsEndpointPort();
        testBundleMetadataLoadsFromXml();
        testLegacyDirectionalImportUsesInterfaceIds();
        testXmlBundleWithoutGraphicsFallsBackToSimpleNode();
        testXmlBundleLoadsExtendedParameterMetadataWhenPresent();
        testFrameworkExportOmitsEditorOnlyCollapsedField();
        testGenericPluginExportKeepsNonNocModules();
        testPluginExportUsesArtifactIdsInsteadOfRuntimeIds();
        testXmlExportPreservesEditorGraphContent();
        testPluginExportOmitsIpInstanceByDefault();
    } catch (const std::exception& error) {
        std::cerr << "graph_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "graph_test passed\n";
    return 0;
}
