// ConnectionRuleService tests for v1 editor-time connection decisions.
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include "ipcraft/ipcraftconnectionvalidator.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QSet>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<Module> makeProducer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Producer"));
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("Out"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeConsumer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Consumer"));
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("In"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeRouter(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Router"));
    module->setIpcoreId(QStringLiteral("finepaper.test"));
    module->addPort(Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("North"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("north")));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("South"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("south")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    return module;
}

std::unique_ptr<Module> makeOwnedModule(const QString& id,
                                        const QString& type,
                                        const QString& ipcoreId) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    return module;
}

ModuleInterfaceMetadata interfaceMetadata(const QString& id,
                                          const QString& bus,
                                          const QString& role,
                                          const QString& connectsTo,
                                          const QString& group = {},
                                          const QString& topologyRule = {}) {
    ModuleInterfaceMetadata metadata;
    metadata.id = id;
    metadata.bus = bus;
    metadata.role = role;
    metadata.compatibleRoles = {connectsTo};
    metadata.cardinality = QStringLiteral("one");
    metadata.autocompleteGroup = group;
    metadata.topologyRule = topologyRule;
    return metadata;
}

ModuleType endpointTypeWithProtocol(const QString& typeName,
                                    const QString& ipcoreId,
                                    const QString& role,
                                    const QString& protocol) {
    ModuleType type;
    type.name = typeName;
    type.ipcoreId = ipcoreId;
    type.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("NoC"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("endpoint_link"),
                                     QStringLiteral("noc")));

    ModuleInterfaceMetadata metadata =
        interfaceMetadata(QStringLiteral("noc"),
                          QStringLiteral("endpoint_link"),
                          role,
                          role == QStringLiteral("initiator") ? QStringLiteral("target")
                                                              : QStringLiteral("initiator"),
                          QStringLiteral("endpoint_attachment"));
    metadata.matchFields = {QStringLiteral("protocol")};
    metadata.acceptedValues.insert(QStringLiteral("protocol"), QStringList{protocol});
    type.interfaceMetadata.insert(metadata.id, metadata);
    return type;
}

void registerRouterType() {
    ModuleType router;
    router.name = QStringLiteral("Router");
    router.ipcoreId = QStringLiteral("finepaper.test");
    router.graphGroup = QStringLiteral("routers");
    router.defaultPorts = {
        Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("North"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("north")),
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("East"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("east")),
        Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("South"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("south")),
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("West"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("west"))
    };

    ModuleInterfaceMetadata north;
    north.id = QStringLiteral("north");
    north.bus = QStringLiteral("router_link");
    north.role = QStringLiteral("target");
    north.compatibleRoles = {QStringLiteral("initiator")};
    north.cardinality = QStringLiteral("one");
    north.autocompleteGroup = QStringLiteral("router_side");
    north.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(north.id, north);

    ModuleInterfaceMetadata east;
    east.id = QStringLiteral("east");
    east.bus = QStringLiteral("router_link");
    east.role = QStringLiteral("initiator");
    east.compatibleRoles = {QStringLiteral("target")};
    east.cardinality = QStringLiteral("one");
    east.autocompleteGroup = QStringLiteral("router_side");
    east.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(east.id, east);

    ModuleInterfaceMetadata south;
    south.id = QStringLiteral("south");
    south.bus = QStringLiteral("router_link");
    south.role = QStringLiteral("initiator");
    south.compatibleRoles = {QStringLiteral("target")};
    south.cardinality = QStringLiteral("one");
    south.autocompleteGroup = QStringLiteral("router_side");
    south.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(south.id, south);

    ModuleInterfaceMetadata west;
    west.id = QStringLiteral("west");
    west.bus = QStringLiteral("router_link");
    west.role = QStringLiteral("target");
    west.compatibleRoles = {QStringLiteral("initiator")};
    west.cardinality = QStringLiteral("one");
    west.autocompleteGroup = QStringLiteral("router_side");
    west.topologyRule = QStringLiteral("opposite_side");
    router.interfaceMetadata.insert(west.id, west);

    ModuleRegistry::instance().registerType(router);
}

IpcraftInterfaceAcceptRule acceptRule(const QString& connectionClassId,
                                      const QString& role) {
    IpcraftInterfaceAcceptRule rule;
    rule.connectionClassId = connectionClassId;
    rule.role = role;
    return rule;
}

IpcraftInterfaceDescriptor interfaceDescriptor(
    const QString& id,
    QVector<IpcraftInterfaceAcceptRule> accepts,
    bool multiConnection = false) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.accepts = std::move(accepts);
    descriptor.multiConnection = multiConnection;
    return descriptor;
}

IpcraftModuleDescriptor moduleDescriptor(const QString& id,
                                         QVector<IpcraftInterfaceDescriptor> interfaces) {
    IpcraftModuleDescriptor descriptor;
    descriptor.id = id;
    descriptor.interfaces = std::move(interfaces);
    return descriptor;
}

IpcraftConnectionClass connectionClass(const QString& id,
                                       QStringList roles,
                                       bool symmetric = false) {
    IpcraftConnectionClass descriptor;
    descriptor.id = id;
    descriptor.roles = std::move(roles);
    descriptor.symmetric = symmetric;
    return descriptor;
}

IpcraftPackageManifest validatorManifest(bool ambiguous = false) {
    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("finepaper.test");
    manifest.connectionClasses.push_back(
        connectionClass(QStringLiteral("chi_node_interface"),
                        {QStringLiteral("node"), QStringLiteral("interconnect")}));
    if (ambiguous) {
        manifest.connectionClasses.push_back(
            connectionClass(QStringLiteral("monitor_tap"),
                            {QStringLiteral("node"), QStringLiteral("interconnect")}));
    }
    manifest.connectionClasses.push_back(
        connectionClass(QStringLiteral("chi_peer_link"),
                        {QStringLiteral("peer")},
                        true));

    QVector<IpcraftInterfaceAcceptRule> endpointAccepts{
        acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("node"))
    };
    QVector<IpcraftInterfaceAcceptRule> interconnectAccepts{
        acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("interconnect"))
    };
    if (ambiguous) {
        endpointAccepts.push_back(acceptRule(QStringLiteral("monitor_tap"), QStringLiteral("node")));
        interconnectAccepts.push_back(
            acceptRule(QStringLiteral("monitor_tap"), QStringLiteral("interconnect")));
    }

    manifest.modules.push_back(moduleDescriptor(
        QStringLiteral("Endpoint"),
        {interfaceDescriptor(QStringLiteral("noc"), std::move(endpointAccepts))}));
    manifest.modules.push_back(moduleDescriptor(
        QStringLiteral("XP"),
        {interfaceDescriptor(QStringLiteral("local0"), std::move(interconnectAccepts))}));
    manifest.modules.push_back(moduleDescriptor(
        QStringLiteral("Peer"),
        {interfaceDescriptor(QStringLiteral("link"),
                             {acceptRule(QStringLiteral("chi_peer_link"), QStringLiteral("peer"))})}));
    return manifest;
}

IpcraftInterfaceDescriptor displayInterfaceDescriptor(const QString& id, const QString& label) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.label = label;
    return descriptor;
}

QJsonObject stringParameterDescriptor(const QString& defaultValue = {}) {
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("default"), defaultValue}
    };
}

IpcraftPackageManifest displayNamedManifest(const QString& packageId,
                                            bool includeShortLabel = false) {
    IpcraftPackageManifest manifest;
    manifest.id = packageId;

    IpcraftModuleDescriptor endpoint;
    endpoint.id = QStringLiteral("Endpoint");
    endpoint.name = QStringLiteral("Endpoint");
    endpoint.displayLabelParameter = QStringLiteral("label");
    if (includeShortLabel) {
        endpoint.shortLabelParameter = QStringLiteral("slot");
        endpoint.parameters.insert(QStringLiteral("slot"), stringParameterDescriptor());
    }
    endpoint.parameters.insert(QStringLiteral("label"), stringParameterDescriptor());
    endpoint.interfaces.push_back(
        displayInterfaceDescriptor(QStringLiteral("noc"), QStringLiteral("NoC Fabric")));

    IpcraftModuleDescriptor routerTile;
    routerTile.id = QStringLiteral("RouterTile");
    routerTile.name = QStringLiteral("Router Tile");
    routerTile.displayLabelParameter = QStringLiteral("label");
    if (includeShortLabel) {
        routerTile.shortLabelParameter = QStringLiteral("slot");
        routerTile.parameters.insert(QStringLiteral("slot"), stringParameterDescriptor());
    }
    routerTile.parameters.insert(QStringLiteral("label"), stringParameterDescriptor());
    routerTile.interfaces.push_back(
        displayInterfaceDescriptor(QStringLiteral("local"), QStringLiteral("Local Link")));

    manifest.modules.push_back(endpoint);
    manifest.modules.push_back(routerTile);
    return manifest;
}

void loadDisplayNamedManifest(const IpcraftPackageManifest& manifest) {
    require(ModuleRegistry::instance().loadIpcraftPackages({manifest}),
            "display-named module types should load");
}

std::unique_ptr<Module> makeDisplayNamedModule(const QString& packageId,
                                               const QString& id,
                                               const QString& moduleId,
                                               const QString& label,
                                               const QString& slot = {}) {
    auto module = std::make_unique<Module>(id, moduleId);
    module->setIpcoreId(packageId);
    module->setParameter(QStringLiteral("label"), label);
    if (!slot.isEmpty()) {
        module->setParameter(QStringLiteral("slot"), slot);
    }

    const ModuleType* type = ModuleRegistry::instance().getType(packageId, moduleId);
    require(type != nullptr, "display-named type should be registered");
    for (const Port& port : type->defaultPorts) {
        module->addPort(port);
    }
    return module;
}

std::unique_ptr<Graph> graphWithTwoDisplayIdenticalEndpoints(const QString& packageId) {
    auto graph = std::make_unique<Graph>();
    auto first = makeDisplayNamedModule(packageId,
                                        QStringLiteral("uuid_ep0"),
                                        QStringLiteral("Endpoint"),
                                        QStringLiteral("DMA"),
                                        QStringLiteral("slot 0"));
    auto second = makeDisplayNamedModule(packageId,
                                         QStringLiteral("uuid_ep1"),
                                         QStringLiteral("Endpoint"),
                                         QStringLiteral("DMA"),
                                         QStringLiteral("slot 1"));
    require(graph->addModule(std::move(first)), "first display endpoint should add");
    require(graph->addModule(std::move(second)), "second display endpoint should add");
    return graph;
}

void registerAnchorLabeledType(const QString& packageId) {
    ModuleType type;
    type.name = ModuleRegistry::scopedTypeName(packageId, QStringLiteral("AnchorNode"));
    type.packageId = packageId;
    type.ipcoreId = packageId;
    type.moduleId = QStringLiteral("AnchorNode");
    type.displayLabelParameter = QStringLiteral("label");
    type.defaultPorts.push_back(Port(QStringLiteral("noc"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Raw Port Label"),
                                     {},
                                     {},
                                     QStringLiteral("anchor_link"),
                                     QStringLiteral("noc")));

    ModuleInterfaceMetadata metadata;
    metadata.id = QStringLiteral("noc");
    metadata.label = QStringLiteral("Metadata Interface Label");
    metadata.bus = QStringLiteral("anchor_link");
    metadata.cardinality = QStringLiteral("one");
    type.interfaceMetadata.insert(metadata.id, metadata);

    ModuleInterfaceAnchor anchor;
    anchor.interfaceId = QStringLiteral("noc");
    anchor.label = QStringLiteral("Anchor Interface Label");
    type.interfaceAnchors.insert(anchor.interfaceId, anchor);

    require(ModuleRegistry::instance().registerType(type), "anchor-labeled type should register");
}

ConnectionRequest nodeToPortRequest() {
    ConnectionRequest request;
    request.kind = ConnectionRequestKind::NodeToPort;
    request.start.moduleId = QStringLiteral("uuid_ep0");
    request.start.fromNodeBody = true;
    request.start.hiddenPortsAllowed = true;
    request.end.moduleId = QStringLiteral("uuid_ep1");
    request.end.portId = QStringLiteral("noc");
    return request;
}

IpcraftConnectionParticipant participant(const QString& moduleId,
                                         const QString& instanceId,
                                         const QString& interfaceId) {
    IpcraftConnectionParticipant participant;
    participant.packageId = QStringLiteral("finepaper.test");
    participant.moduleId = moduleId;
    participant.interfaceRef = ProjectConnectionInterfaceRef{instanceId, interfaceId};
    return participant;
}

void testAcceptsMatchingClassAndRoles() {
    IpcraftConnectionValidator validator({validatorManifest()});
    const IpcraftConnectionDecision decision = validator.validate(
        {participant(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), QStringLiteral("noc")),
         participant(QStringLiteral("XP"), QStringLiteral("xp"), QStringLiteral("local0"))},
        QStringLiteral("chi_node_interface"));

    require(decision.status == IpcraftConnectionStatus::Valid,
            "matching connection class roles should be valid");
    require(decision.selectedClassId == QStringLiteral("chi_node_interface"),
            "validator should preserve selected class id");
    require(decision.normalizedInterfaces.size() == 2,
            "valid decision should include normalized interface participants");
    require(decision.normalizedInterfaces.at(0).instanceId == QStringLiteral("endpoint"),
            "non-symmetric class should preserve drag order");
}

void testRejectsMissingParticipantPackageMetadata() {
    IpcraftConnectionParticipant endpoint =
        participant(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), QStringLiteral("noc"));
    endpoint.packageId.clear();

    IpcraftConnectionValidator validator({validatorManifest()});
    const IpcraftConnectionDecision decision = validator.validate(
        {endpoint,
         participant(QStringLiteral("XP"), QStringLiteral("xp"), QStringLiteral("local0"))},
        QStringLiteral("chi_node_interface"));

    require(decision.status == IpcraftConnectionStatus::Invalid,
            "validator should reject a participant without package metadata");
    require(decision.message.contains(QStringLiteral("package metadata")),
            "missing package metadata rejection should be explicit");
}

void testRejectsRoleMismatch() {
    IpcraftConnectionValidator validator({validatorManifest()});
    const IpcraftConnectionDecision decision = validator.validate(
        {participant(QStringLiteral("XP"), QStringLiteral("xp"), QStringLiteral("local0")),
         participant(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), QStringLiteral("noc"))},
        QStringLiteral("chi_node_interface"));

    require(decision.status == IpcraftConnectionStatus::Invalid,
            "reverse drag for non-symmetric class should reject role mismatch");
    require(decision.message.contains(QStringLiteral("role")),
            "role mismatch rejection should explain role incompatibility");
}

void testRejectsUsedSingleConnectionInterface() {
    ProjectConnectionRecord existing;
    existing.id = QStringLiteral("conn_existing");
    existing.connectionClassId = QStringLiteral("chi_node_interface");
    existing.interfaces = {
        ProjectConnectionInterfaceRef{QStringLiteral("endpoint"), QStringLiteral("noc")},
        ProjectConnectionInterfaceRef{QStringLiteral("xp"), QStringLiteral("local0")}
    };

    IpcraftConnectionValidator validator({validatorManifest()}, {existing});
    const IpcraftConnectionDecision decision = validator.validate(
        {participant(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), QStringLiteral("noc")),
         participant(QStringLiteral("XP"), QStringLiteral("xp_2"), QStringLiteral("local0"))},
        QStringLiteral("chi_node_interface"));

    require(decision.status == IpcraftConnectionStatus::Invalid,
            "single-connection interface already in use should reject");
    require(decision.message.contains(QStringLiteral("already used")),
            "occupied interface rejection should mention that the interface is already used");
}

void testSymmetricClassNormalizesReverseDrag() {
    IpcraftConnectionValidator validator({validatorManifest()});
    const IpcraftConnectionDecision decision = validator.validate(
        {participant(QStringLiteral("Peer"), QStringLiteral("b"), QStringLiteral("link")),
         participant(QStringLiteral("Peer"), QStringLiteral("a"), QStringLiteral("link"))},
        QStringLiteral("chi_peer_link"));

    require(decision.status == IpcraftConnectionStatus::Valid,
            "symmetric class reverse drag should be valid");
    require(decision.normalizedInterfaces.size() == 2,
            "symmetric class should return two normalized participants");
    require(decision.normalizedInterfaces.at(0).instanceId == QStringLiteral("a"),
            "symmetric class should normalize reverse drag by instance id");
    require(decision.normalizedInterfaces.at(1).instanceId == QStringLiteral("b"),
            "symmetric class should normalize second participant by instance id");
}

void testAmbiguousClassCreatesWarningResult() {
    IpcraftConnectionValidator validator({validatorManifest(true)});
    const IpcraftConnectionDecision decision = validator.validate(
        {participant(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), QStringLiteral("noc")),
         participant(QStringLiteral("XP"), QStringLiteral("xp"), QStringLiteral("local0"))});

    require(decision.status == IpcraftConnectionStatus::Ambiguous,
            "multiple matching classes should create an ambiguous decision");
    require(decision.selectedClassId == QStringLiteral("chi_node_interface"),
            "ambiguous decision should choose deterministic first class");
    require(decision.alternatives == QStringList({QStringLiteral("chi_node_interface"),
                                                  QStringLiteral("monitor_tap")}),
            "ambiguous decision should expose every valid class alternative");
}

void testConnectionOptionsPreferDisplayNamesAndInterfaceLabels() {
    const QString packageId = QStringLiteral("finepaper.display.labels");
    const IpcraftPackageManifest manifest = displayNamedManifest(packageId);
    loadDisplayNamedManifest(manifest);

    Graph graph;
    auto xp = makeDisplayNamedModule(packageId,
                                     QStringLiteral("uuid_xp"),
                                     QStringLiteral("RouterTile"),
                                     QStringLiteral("XP A"));
    auto ep = makeDisplayNamedModule(packageId,
                                     QStringLiteral("uuid_ep"),
                                     QStringLiteral("Endpoint"),
                                     QStringLiteral("DMA 0"));
    require(graph.addModule(std::move(xp)), "display router should add");
    require(graph.addModule(std::move(ep)), "display endpoint should add");

    ConnectionRuleService service(&graph, {}, {manifest});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("uuid_ep"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("uuid_xp"), QStringLiteral("local")},
                                      ConnectionRequestKind::PortToPort));

    require(result.hasSingleOption(), "display-name test should have one option");
    const QString label = result.options.first().label;
    require(label.contains(QStringLiteral("DMA 0")),
            "option should show endpoint display name");
    require(label.contains(QStringLiteral("XP A")),
            "option should show router display name");
    require(label.contains(QStringLiteral("NoC Fabric")),
            "option should show endpoint interface label");
    require(label.contains(QStringLiteral("Local Link")),
            "option should show router interface label");
    require(!label.contains(QStringLiteral("uuid_ep")),
            "option main label should not expose endpoint runtime ID");
    require(!label.contains(QStringLiteral("uuid_xp")),
            "option main label should not expose router runtime ID");
    require(!label.contains(QStringLiteral(".noc")),
            "option should not show raw endpoint port id when an interface label exists");
    require(!label.contains(QStringLiteral(".local")),
            "option should not show raw router port id when an interface label exists");
}

void testDuplicateConnectionOptionLabelsUseShortLabelBeforeIds() {
    const QString packageId = QStringLiteral("finepaper.display.short_labels");
    const IpcraftPackageManifest manifest = displayNamedManifest(packageId, true);
    loadDisplayNamedManifest(manifest);

    std::unique_ptr<Graph> graph = graphWithTwoDisplayIdenticalEndpoints(packageId);
    ConnectionRuleService service(graph.get(), {}, {manifest});
    const ConnectionCheckResult result = service.check(nodeToPortRequest());

    require(result.status == ConnectionCheckStatus::NeedsSelection,
            "duplicate visible options should require selection");
    require(result.options.size() == 2,
            "two duplicate visible options should be exposed");

    QSet<QString> finalLabels;
    bool hasSlot0 = false;
    bool hasSlot1 = false;
    bool exposesRuntimeId = false;
    for (const ConnectionResolvedOption& option : result.options) {
        finalLabels.insert(option.label);
        hasSlot0 = hasSlot0 || option.label.contains(QStringLiteral("slot 0"));
        hasSlot1 = hasSlot1 || option.label.contains(QStringLiteral("slot 1"));
        exposesRuntimeId = exposesRuntimeId || option.label.contains(QStringLiteral("uuid_ep"));
    }

    require(finalLabels.size() == result.options.size(),
            "duplicate option labels should be unique after disambiguation");
    require(hasSlot0 && hasSlot1,
            "both short labels should disambiguate duplicate display names");
    require(!exposesRuntimeId,
            "short labels should be used before runtime IDs");
}

void testConnectionOptionLabelsUseAnchorInterfaceLabels() {
    const QString packageId = QStringLiteral("finepaper.display.anchor_labels");
    registerAnchorLabeledType(packageId);

    Graph graph;
    auto source = makeDisplayNamedModule(packageId,
                                         QStringLiteral("anchor_source_uuid"),
                                         QStringLiteral("AnchorNode"),
                                         QStringLiteral("Source"));
    auto target = makeDisplayNamedModule(packageId,
                                         QStringLiteral("anchor_target_uuid"),
                                         QStringLiteral("AnchorNode"),
                                         QStringLiteral("Target"));
    require(graph.addModule(std::move(source)), "anchor source should add");
    require(graph.addModule(std::move(target)), "anchor target should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("anchor_source_uuid"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("anchor_target_uuid"), QStringLiteral("noc")},
                                      ConnectionRequestKind::PortToPort));

    require(result.hasSingleOption(), "anchor-label test should have one option");
    const QString label = result.options.first().label;
    require(label.contains(QStringLiteral("Source.Anchor Interface Label")),
            "source endpoint should use anchor interface label");
    require(label.contains(QStringLiteral("Target.Anchor Interface Label")),
            "target endpoint should use anchor interface label");
    require(!label.contains(QStringLiteral("Metadata Interface Label")),
            "anchor label should take precedence over metadata interface label");
    require(!label.contains(QStringLiteral("Raw Port Label")),
            "anchor label should take precedence over raw port label");
}

ModuleType classValidationType(const QString& typeName,
                               const QString& manifestModuleId,
                               const QString& portId,
                               const QString& legacyBus,
                               QVector<IpcraftInterfaceAcceptRule> acceptRules,
                               const QString& topologyRule = {}) {
    ModuleType type;
    type.name = typeName;
    type.packageId = QStringLiteral("finepaper.test");
    type.ipcoreId = QStringLiteral("finepaper.test");
    type.moduleId = manifestModuleId;
    type.defaultPorts.push_back(Port(portId,
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     portId,
                                     {},
                                     {},
                                     legacyBus,
                                     portId));

    ModuleInterfaceMetadata metadata;
    metadata.id = portId;
    metadata.bus = legacyBus;
    metadata.role = acceptRules.first().role;
    metadata.cardinality = QStringLiteral("one");
    metadata.acceptRules = std::move(acceptRules);
    metadata.topologyRule = topologyRule;
    type.interfaceMetadata.insert(metadata.id, metadata);
    return type;
}

void testConnectionRuleServiceUsesInterfaceClassesNotLegacyBusNames() {
    const ModuleType endpointType = classValidationType(
        QStringLiteral("ClassEndpoint"),
        QStringLiteral("Endpoint"),
        QStringLiteral("noc"),
        QStringLiteral("legacy_source_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("node")),
         acceptRule(QStringLiteral("monitor_tap"), QStringLiteral("node"))});
    const ModuleType xpType = classValidationType(
        QStringLiteral("ClassXp"),
        QStringLiteral("XP"),
        QStringLiteral("local0"),
        QStringLiteral("legacy_target_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("interconnect")),
         acceptRule(QStringLiteral("monitor_tap"), QStringLiteral("interconnect"))});
    ModuleRegistry::instance().registerType(endpointType);
    ModuleRegistry::instance().registerType(xpType);

    Graph graph;
    auto endpoint = makeOwnedModule(QStringLiteral("endpoint"),
                                    endpointType.name,
                                    QStringLiteral("finepaper.test"));
    endpoint->addPort(endpointType.defaultPorts.front());
    auto xp = makeOwnedModule(QStringLiteral("xp"),
                              xpType.name,
                              QStringLiteral("finepaper.test"));
    xp->addPort(xpType.defaultPorts.front());
    require(graph.addModule(std::move(endpoint)), "class endpoint should add");
    require(graph.addModule(std::move(xp)), "class XP should add");

    ConnectionRuleService service(&graph, {}, {validatorManifest(true)});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("endpoint"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("xp"), QStringLiteral("local0")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Allowed,
            result.message.toLocal8Bit().constData());
    require(result.options.size() == 1,
            "interface class validation should produce one port option");
    require(result.options.first().connectionStatus == QStringLiteral("ambiguous"),
            "multiple valid classes should create an ambiguous connection option");
    require(result.options.first().connectionClassId == QStringLiteral("chi_node_interface"),
            "ambiguous option should select deterministic first class");
    require(result.options.first().alternatives == QStringList({QStringLiteral("chi_node_interface"),
                                                                QStringLiteral("monitor_tap")}),
            "ambiguous option should expose class alternatives");
    require(result.options.first().normalizedInterfaces.size() == 2,
            "connection option should carry normalized interface participants");
}

void testConnectionRuleServiceClassValidationIgnoresLegacyTopologySideNames() {
    const ModuleType endpointType = classValidationType(
        QStringLiteral("ClassEndpointTopologyBypass"),
        QStringLiteral("Endpoint"),
        QStringLiteral("noc"),
        QStringLiteral("legacy_source_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("node"))},
        QStringLiteral("opposite_side"));
    const ModuleType xpType = classValidationType(
        QStringLiteral("ClassXpTopologyBypass"),
        QStringLiteral("XP"),
        QStringLiteral("local0"),
        QStringLiteral("legacy_target_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("interconnect"))},
        QStringLiteral("opposite_side"));
    ModuleRegistry::instance().registerType(endpointType);
    ModuleRegistry::instance().registerType(xpType);

    Graph graph;
    auto endpoint = makeOwnedModule(QStringLiteral("endpoint_topology"),
                                    endpointType.name,
                                    QStringLiteral("finepaper.test"));
    endpoint->addPort(endpointType.defaultPorts.front());
    auto xp = makeOwnedModule(QStringLiteral("xp_topology"),
                              xpType.name,
                              QStringLiteral("finepaper.test"));
    xp->addPort(xpType.defaultPorts.front());
    require(graph.addModule(std::move(endpoint)), "class topology endpoint should add");
    require(graph.addModule(std::move(xp)), "class topology XP should add");

    ConnectionRuleService service(&graph, {}, {validatorManifest()});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("endpoint_topology"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("xp_topology"), QStringLiteral("local0")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Allowed,
            result.message.toLocal8Bit().constData());
    require(result.options.size() == 1,
            "manifest class validation should not depend on cardinal side port names");
    require(result.options.first().connectionClassId == QStringLiteral("chi_node_interface"),
            "manifest class validation should still select the valid class");
}

void testConnectionRuleServiceRejectsMissingPackageManifestMetadata() {
    const ModuleType endpointType = classValidationType(
        QStringLiteral("ClassEndpointMissingManifest"),
        QStringLiteral("Endpoint"),
        QStringLiteral("noc"),
        QStringLiteral("legacy_source_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("node"))});
    const ModuleType xpType = classValidationType(
        QStringLiteral("ClassXpMissingManifest"),
        QStringLiteral("XP"),
        QStringLiteral("local0"),
        QStringLiteral("legacy_target_bus"),
        {acceptRule(QStringLiteral("chi_node_interface"), QStringLiteral("interconnect"))});
    ModuleRegistry::instance().registerType(endpointType);
    ModuleRegistry::instance().registerType(xpType);

    Graph graph;
    auto endpoint = makeOwnedModule(QStringLiteral("endpoint_missing_manifest"),
                                    endpointType.name,
                                    QStringLiteral("finepaper.test"));
    endpoint->addPort(endpointType.defaultPorts.front());
    auto xp = makeOwnedModule(QStringLiteral("xp_missing_manifest"),
                              xpType.name,
                              QStringLiteral("finepaper.test"));
    xp->addPort(xpType.defaultPorts.front());
    require(graph.addModule(std::move(endpoint)), "missing manifest endpoint should add");
    require(graph.addModule(std::move(xp)), "missing manifest XP should add");

    ConnectionRuleService service(&graph, {}, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("endpoint_missing_manifest"),
                                              QStringLiteral("noc")},
                                      PortRef{QStringLiteral("xp_missing_manifest"),
                                              QStringLiteral("local0")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "class validation should reject when loaded package metadata is missing");
    require(result.reasonCode == QStringLiteral("interface_class_mismatch"),
            "missing package metadata should reject at interface-class validation");
    require(result.message.contains(QStringLiteral("missing package")),
            "missing package metadata rejection should explain the missing package");
}

void testAllowsSimplePortToPortConnection() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Allowed,
            "matching output/input bus ports should be allowed");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "allowed connection should pass through IP-core layer");
    require(result.options.size() == 1, "simple port-to-port should produce one option");
    require(result.options.first().source.moduleId == QStringLiteral("producer"),
            "source module should be producer");
    require(result.options.first().target.moduleId == QStringLiteral("consumer"),
            "target module should be consumer");
}

void testRejectsMissingPortWithReason() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("missing")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "missing port should reject");
    require(result.reasonCode == QStringLiteral("missing_port"),
            "missing port rejection should have reason code");
}

void testStructuralLayerRunsBeforeSemanticLayers() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("missing")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "missing port should reject");
    require(result.layer == ConnectionRuleLayer::Structural,
            "missing port should be rejected by structural layer");
    require(result.reasonCode == QStringLiteral("missing_port"),
            "structural rejection should report missing_port");
}

void testDuplicateConnectionIsStructuralRejection() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("existing"),
        PortRef{QStringLiteral("producer"), QStringLiteral("out")},
        PortRef{QStringLiteral("consumer"), QStringLiteral("in")}));

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "duplicate connection should reject");
    require(result.layer == ConnectionRuleLayer::Structural,
            "duplicate connection should be structural");
    require(result.reasonCode == QStringLiteral("duplicate_connection"),
            "duplicate connection should report duplicate_connection");
}

void testSelfLoopIsStructuralRejection() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("router"))), "failed to add router");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("router"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("router"), QStringLiteral("west")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "self-loop should reject");
    require(result.layer == ConnectionRuleLayer::Structural,
            "self-loop should be structural");
    require(result.reasonCode == QStringLiteral("self_loop"),
            "self-loop should report self_loop");
}

void testRejectsSameSideTopologyRule() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("left"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("right"), QStringLiteral("east")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "same-side router connection should reject");
    require(result.layer == ConnectionRuleLayer::EditorRule,
            "same-side topology should be rejected by editor-rule layer");
    require(result.reasonCode == QStringLiteral("topology_rule_mismatch"),
            "same-side rejection should report topology rule mismatch");
}

void testAllowsBidirectionalRouterLinkFromTargetRoleToInitiatorRole() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("top"))), "failed to add top router");
    require(graph.addModule(makeRouter(QStringLiteral("bottom"))), "failed to add bottom router");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("bottom"), QStringLiteral("north")},
                                      PortRef{QStringLiteral("top"), QStringLiteral("south")},
                                      ConnectionRequestKind::PortToPort));

    require(result.status == ConnectionCheckStatus::Allowed,
            "inout router peer links should allow target-role to initiator-role graph direction");
    require(result.options.size() == 1,
            "bidirectional router peer link should produce one option");
    require(result.options.first().source.moduleId == QStringLiteral("bottom") &&
                result.options.first().source.portId == QStringLiteral("north"),
            "bidirectional router peer link should preserve the requested source");
    require(result.options.first().target.moduleId == QStringLiteral("top") &&
                result.options.first().target.portId == QStringLiteral("south"),
            "bidirectional router peer link should preserve the requested target");
}

void testRejectsOccupiedCardinalityOnePort() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");
    require(graph.addModule(makeRouter(QStringLiteral("extra"))), "failed to add extra router");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("existing"),
        PortRef{QStringLiteral("left"), QStringLiteral("east")},
        PortRef{QStringLiteral("right"), QStringLiteral("west")}));

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("left"), QStringLiteral("east")},
                                      PortRef{QStringLiteral("extra"), QStringLiteral("west")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "occupied cardinality-one port should reject");
    require(result.layer == ConnectionRuleLayer::EditorRule,
            "cardinality should be rejected by editor-rule layer");
    require(result.reasonCode == QStringLiteral("port_occupied"),
            "occupied rejection should report port_occupied");
}

void testVisualSideOrientsInOutPortToNodeCompletion() {
    registerRouterType();
    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left"))), "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right"))), "failed to add right router");

    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.allowAutoComplete = true;
    request.allowAlternatives = true;
    request.start.moduleId = QStringLiteral("left");
    request.start.portId = QStringLiteral("west");
    request.start.visualSide = ConnectionVisualSide::Input;
    request.end.moduleId = QStringLiteral("right");
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;
    request.end.visualSide = ConnectionVisualSide::Output;

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(request);

    require(result.status == ConnectionCheckStatus::Allowed,
            "input-side inout drag to a node should resolve to one oriented option");
    require(result.options.size() == 1, "visual side should avoid symmetric duplicate options");
    require(result.options.first().source.moduleId == QStringLiteral("right") &&
                result.options.first().source.portId == QStringLiteral("east"),
            "body target output side should become the source through its east interface");
    require(result.options.first().target.moduleId == QStringLiteral("left") &&
                result.options.first().target.portId == QStringLiteral("west"),
            "input-side start port should become the target");
}

void testNodeBodyAutocompleteUsesMatchingGroup() {
    ModuleType host;
    host.name = QStringLiteral("AutocompleteHost");
    host.ipcoreId = QStringLiteral("finepaper.test");
    host.defaultPorts.push_back(Port(QStringLiteral("router"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Router"),
                                     {},
                                     QStringLiteral("router"),
                                     QStringLiteral("shared_bus"),
                                     QStringLiteral("router")));
    host.defaultPorts.push_back(Port(QStringLiteral("endpoint"),
                                     Port::Direction::InOut,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Endpoint"),
                                     {},
                                     QStringLiteral("attachment"),
                                     QStringLiteral("shared_bus"),
                                     QStringLiteral("endpoint")));
    host.interfaceMetadata.insert(QStringLiteral("router"),
        interfaceMetadata(QStringLiteral("router"),
                          QStringLiteral("shared_bus"),
                          QStringLiteral("target"),
                          QStringLiteral("initiator"),
                          QStringLiteral("router_side")));
    host.interfaceMetadata.insert(QStringLiteral("endpoint"),
        interfaceMetadata(QStringLiteral("endpoint"),
                          QStringLiteral("shared_bus"),
                          QStringLiteral("target"),
                          QStringLiteral("initiator"),
                          QStringLiteral("endpoint_attachment")));
    ModuleRegistry::instance().registerType(host);

    ModuleType endpoint = endpointTypeWithProtocol(QStringLiteral("AutocompleteEndpoint"),
                                                   QStringLiteral("finepaper.test"),
                                                   QStringLiteral("initiator"),
                                                   QStringLiteral("axi4"));
    endpoint.defaultPorts.front() = Port(QStringLiteral("noc"),
                                         Port::Direction::InOut,
                                         QStringLiteral("bus"),
                                         QStringLiteral("NoC"),
                                         {},
                                         QStringLiteral("attachment"),
                                         QStringLiteral("shared_bus"),
                                         QStringLiteral("noc"));
    endpoint.interfaceMetadata[QStringLiteral("noc")].bus = QStringLiteral("shared_bus");
    endpoint.interfaceMetadata[QStringLiteral("noc")].matchFields.clear();
    endpoint.interfaceMetadata[QStringLiteral("noc")].acceptedValues.clear();
    ModuleRegistry::instance().registerType(endpoint);

    Graph graph;
    auto source = makeOwnedModule(QStringLiteral("endpoint"), QStringLiteral("AutocompleteEndpoint"), QStringLiteral("finepaper.test"));
    source->addPort(endpoint.defaultPorts.front());
    auto target = makeOwnedModule(QStringLiteral("host"), QStringLiteral("AutocompleteHost"), QStringLiteral("finepaper.test"));
    target->addPort(host.defaultPorts.at(0));
    target->addPort(host.defaultPorts.at(1));
    require(graph.addModule(std::move(source)), "endpoint should add");
    require(graph.addModule(std::move(target)), "host should add");

    ConnectionRequest request;
    request.kind = ConnectionRequestKind::PortToNode;
    request.start.moduleId = QStringLiteral("endpoint");
    request.start.portId = QStringLiteral("noc");
    request.end.moduleId = QStringLiteral("host");
    request.end.fromNodeBody = true;
    request.end.hiddenPortsAllowed = true;

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(request);

    require(result.status == ConnectionCheckStatus::Allowed,
            result.reasonCode.toLocal8Bit().constData());
    require(result.options.size() == 1,
            "autocomplete group should suppress same-bus nonmatching hidden ports");
    require(result.options.first().target.portId == QStringLiteral("endpoint"),
            "node-body autocomplete should choose endpoint_attachment hidden port");
}

void testRejectsCrossIpcoreConnectionAtIpcoreLayer() {
    Graph graph;
    auto producer = makeProducer(QStringLiteral("producer"));
    producer->setIpcoreId(QStringLiteral("finepaper.left"));
    auto consumer = makeConsumer(QStringLiteral("consumer"));
    consumer->setIpcoreId(QStringLiteral("finepaper.right"));
    require(graph.addModule(std::move(producer)), "producer should add");
    require(graph.addModule(std::move(consumer)), "consumer should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "cross-IP-core connection should reject");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "cross-IP-core connection should be rejected by IP-core layer");
    require(result.reasonCode == QStringLiteral("ipcore_mismatch"),
            "cross-IP-core rejection should report ipcore_mismatch");
}

void testRejectsCrossInstanceConnectionAtIpcoreLayer() {
    Graph graph;
    auto producer = makeProducer(QStringLiteral("producer"));
    producer->setInstanceId(QStringLiteral("ravenoc_0"));
    auto consumer = makeConsumer(QStringLiteral("consumer"));
    consumer->setInstanceId(QStringLiteral("ravenoc_1"));
    require(graph.addModule(std::move(producer)), "producer should add");
    require(graph.addModule(std::move(consumer)), "consumer should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "cross-instance same-IP-core connection should reject");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "cross-instance rejection should happen at the IP-core layer");
    require(result.reasonCode == QStringLiteral("ip_instance_mismatch"),
            "cross-instance rejection should report ip_instance_mismatch");
    require(result.message.contains(QStringLiteral("different IP instances")),
            "cross-instance rejection should explain the instance mismatch");
}

void testRejectsInterfaceFieldMismatchAtIpcoreLayer() {
    ModuleRegistry::instance().registerType(endpointTypeWithProtocol(QStringLiteral("AxiEndpoint"),
                                                                     QStringLiteral("finepaper.test"),
                                                                     QStringLiteral("initiator"),
                                                                     QStringLiteral("axi4")));
    ModuleRegistry::instance().registerType(endpointTypeWithProtocol(QStringLiteral("ApbTarget"),
                                                                     QStringLiteral("finepaper.test"),
                                                                     QStringLiteral("target"),
                                                                     QStringLiteral("apb")));

    Graph graph;
    auto source = makeOwnedModule(QStringLiteral("source"), QStringLiteral("AxiEndpoint"), QStringLiteral("finepaper.test"));
    source->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("endpoint_link"), QStringLiteral("noc")));
    auto target = makeOwnedModule(QStringLiteral("target"), QStringLiteral("ApbTarget"), QStringLiteral("finepaper.test"));
    target->addPort(Port(QStringLiteral("noc"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("NoC"), {}, QStringLiteral("attachment"),
                         QStringLiteral("endpoint_link"), QStringLiteral("noc")));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("source"), QStringLiteral("noc")},
                                      PortRef{QStringLiteral("target"), QStringLiteral("noc")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "IP-core interface match mismatch should reject");
    require(result.layer == ConnectionRuleLayer::Ipcore,
            "interface field mismatch should be rejected by IP-core layer");
    require(result.reasonCode == QStringLiteral("interface_field_mismatch"),
            "IP-core constraint should report interface_field_mismatch");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testAllowsSimplePortToPortConnection();
        testRejectsMissingPortWithReason();
        testStructuralLayerRunsBeforeSemanticLayers();
        testDuplicateConnectionIsStructuralRejection();
        testSelfLoopIsStructuralRejection();
        testRejectsSameSideTopologyRule();
        testAllowsBidirectionalRouterLinkFromTargetRoleToInitiatorRole();
        testRejectsOccupiedCardinalityOnePort();
        testVisualSideOrientsInOutPortToNodeCompletion();
        testNodeBodyAutocompleteUsesMatchingGroup();
        testRejectsCrossIpcoreConnectionAtIpcoreLayer();
        testRejectsCrossInstanceConnectionAtIpcoreLayer();
        testRejectsInterfaceFieldMismatchAtIpcoreLayer();
        testAcceptsMatchingClassAndRoles();
        testRejectsMissingParticipantPackageMetadata();
        testRejectsRoleMismatch();
        testRejectsUsedSingleConnectionInterface();
        testSymmetricClassNormalizesReverseDrag();
        testAmbiguousClassCreatesWarningResult();
        testConnectionOptionsPreferDisplayNamesAndInterfaceLabels();
        testDuplicateConnectionOptionLabelsUseShortLabelBeforeIds();
        testConnectionOptionLabelsUseAnchorInterfaceLabels();
        testConnectionRuleServiceUsesInterfaceClassesNotLegacyBusNames();
        testConnectionRuleServiceClassValidationIgnoresLegacyTopologySideNames();
        testConnectionRuleServiceRejectsMissingPackageManifestMetadata();
    } catch (const std::exception& error) {
        std::cerr << "connectionruleservice_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "connectionruleservice_test passed\n";
    return 0;
}
