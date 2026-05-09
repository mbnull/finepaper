// Graph integration-style tests for topology and structural behavior.
#include "commands/addconnectioncommand.h"
#include "commands/commandmanager.h"
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "common/portlayout.h"

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

void testGraphConnectionValidationRejectsOnlyExactDuplicates() {
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
    require(graph.isValidConnection(source, targetB),
            "structural graph guard should leave port reuse to connection rules");
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

void testAddConnectionCommandRedoBuildsFreshRuleService() {
    Graph graph;

    require(graph.addModule(makeModule(
        "source",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add source module");
    require(graph.addModule(makeModule(
        "target",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add target module");

    int providerCalls = 0;
    auto ipInstanceRecordsProvider = [&providerCalls]() {
        ++providerCalls;
        return QVector<ProjectIpInstanceRecord>{};
    };

    CommandManager commandManager;
    auto command = std::make_unique<AddConnectionCommand>(
        &graph,
        ipInstanceRecordsProvider,
        std::make_unique<Connection>("command_link",
                                     PortRef{"source", "out"},
                                     PortRef{"target", "in"}));

    commandManager.executeCommand(std::move(command));
    require(providerCalls == 1, "initial execute should build a rule service from provider state");
    require(graph.connections().size() == 1, "initial execute should add the connection");

    commandManager.undo();
    require(graph.connections().empty(), "undo should remove the connection");

    commandManager.redo();
    require(providerCalls == 2, "redo should build a fresh rule service instead of reusing a stale pointer");
    require(graph.connections().size() == 1, "redo should restore the connection");
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
    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult sourceReuse = service.check(
        ConnectionRequest::portToPort(middle, target, ConnectionRequestKind::Programmatic));
    const ConnectionCheckResult targetReuse = service.check(
        ConnectionRequest::portToPort(target, middle, ConnectionRequestKind::Programmatic));
    require(sourceReuse.status == ConnectionCheckStatus::Rejected,
            "connection service should reject occupied inout source reuse");
    require(sourceReuse.reasonCode == QStringLiteral("port_occupied"),
            "source reuse should report port_occupied");
    require(targetReuse.status == ConnectionCheckStatus::Rejected,
            "connection service should reject occupied inout target reuse");
    require(targetReuse.reasonCode == QStringLiteral("port_occupied"),
            "target reuse should report port_occupied");
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

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{"endpoint", "noc"},
                                      PortRef{"xp", "local0"},
                                      ConnectionRequestKind::Programmatic));
    require(result.status == ConnectionCheckStatus::Rejected,
            "connection service should reject mismatched data_width");
    require(result.reasonCode == QStringLiteral("interface_field_mismatch"),
            "data_width mismatch should report interface_field_mismatch");
}

void testGraphStructuralValidationIgnoresSemanticMetadata() {
    ModuleType sourceType;
    sourceType.name = QStringLiteral("StructuralOnlySource");
    sourceType.pluginId = QStringLiteral("finepaper.structural");
    ModuleInterfaceMetadata sourceInterface =
        makeInterfaceMetadata(QStringLiteral("out"),
                              QStringLiteral("semantic_a"),
                              QStringLiteral("initiator"),
                              {QStringLiteral("target")},
                              {});
    sourceType.interfaceMetadata.insert(sourceInterface.id, sourceInterface);
    ModuleRegistry::instance().registerType(sourceType);

    ModuleType targetType;
    targetType.name = QStringLiteral("StructuralOnlyTarget");
    targetType.pluginId = QStringLiteral("finepaper.structural");
    ModuleInterfaceMetadata targetInterface =
        makeInterfaceMetadata(QStringLiteral("in"),
                              QStringLiteral("semantic_b"),
                              QStringLiteral("target"),
                              {QStringLiteral("initiator")},
                              {});
    targetType.interfaceMetadata.insert(targetInterface.id, targetInterface);
    ModuleRegistry::instance().registerType(targetType);

    Graph graph;
    require(graph.addModule(makeModule(
        QStringLiteral("source"),
        sourceType.name,
        {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
              QStringLiteral("Out"), {}, QStringLiteral("source"),
              QStringLiteral("semantic_a"), QStringLiteral("out"))})),
        "failed to add structural source");
    require(graph.addModule(makeModule(
        QStringLiteral("target"),
        targetType.name,
        {Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
              QStringLiteral("In"), {}, QStringLiteral("target"),
              QStringLiteral("semantic_b"), QStringLiteral("in"))})),
        "failed to add structural target");

    require(graph.isValidConnection(PortRef{QStringLiteral("source"), QStringLiteral("out")},
                                    PortRef{QStringLiteral("target"), QStringLiteral("in")}),
            "Graph structural validation should not reject semantic bus/role mismatches");
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

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{"endpoint", "noc"},
                                      PortRef{"xp", "local0"},
                                      ConnectionRequestKind::Programmatic));
    require(result.status == ConnectionCheckStatus::Allowed,
            "connection service should accept matching protocol and data_width");
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

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult opposite = service.check(
        ConnectionRequest::portToPort(PortRef{"router_a", "east"},
                                      PortRef{"router_b", "west"},
                                      ConnectionRequestKind::Programmatic));
    const ConnectionCheckResult wrongSide = service.check(
        ConnectionRequest::portToPort(PortRef{"router_a", "east"},
                                      PortRef{"router_b", "north"},
                                      ConnectionRequestKind::Programmatic));
    require(opposite.status == ConnectionCheckStatus::Allowed,
            "east router interface should connect to west");
    require(wrongSide.status == ConnectionCheckStatus::Rejected,
            "east router interface should not connect to north");
    require(wrongSide.reasonCode == QStringLiteral("topology_rule_mismatch"),
            "wrong router side should report topology_rule_mismatch");
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

void testEndpointTypeStillClassifiesAsEndpointPort() {
    const Port endpointTypedPort("noc", Port::Direction::Input, "endpoint", "NoC");
    require(PortLayout::isEndpointPort(endpointTypedPort),
            "endpoint type should classify as endpoint port");
    require(PortLayout::isEndpointPortId("local3"),
            "localN interface ids should classify as endpoint slots");
    require(PortLayout::endpointPortSlot("local3") == 3,
            "localN interface ids should preserve their endpoint slot index");
}

void testBundleMetadataLoadsFromXml() {
    const ModuleType* xpType = ModuleRegistry::instance().getType("XP");
    require(xpType != nullptr, "XP type should be registered");
    require(xpType->pluginId == "finepaper.noc", "XP type should come from bundled NoC IP core");
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
    require(!moduleTypeHasPort(xpType, "east_in"), "XP should not expose split east_in");
    require(!moduleTypeHasPort(xpType, "east_out"), "XP should not expose split east_out");

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
    require(endpointType->pluginId == "finepaper.noc", "Endpoint type should come from bundled NoC IP core");
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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testGraphConnectionValidationRejectsOnlyExactDuplicates();
        testInoutBusConnectionsAreValid();
        testAddConnectionCommandRedoBuildsFreshRuleService();
        testInoutPortsCannotBeReusedAcrossConnectionSides();
        testInterfaceCompatibilityRejectsMismatchedConfiguredFields();
        testGraphStructuralValidationIgnoresSemanticMetadata();
        testInterfaceCompatibilityAcceptsMatchingConfiguredFields();
        testRouterLinksRequireOppositeSides();
        testRemovingModuleAlsoRemovesAttachedConnections();
        testClearRemovesAllModulesAndConnections();
        testGraphForwardsModuleParameterChanges();
        testEndpointTypeStillClassifiesAsEndpointPort();
        testBundleMetadataLoadsFromXml();
        testXmlBundleWithoutGraphicsFallsBackToSimpleNode();
        testXmlBundleLoadsExtendedParameterMetadataWhenPresent();
    } catch (const std::exception& error) {
        std::cerr << "graph_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "graph_test passed\n";
    return 0;
}
