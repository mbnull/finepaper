// Ipcraft V1 CompositionModel / LayoutModel / GraphConfig contract tests.
#include "ipcraft/compositionmodel.h"
#include "ipcraft/layoutmodel.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId &&
            diagnostic.source == QStringLiteral("core") &&
            diagnostic.severity == QStringLiteral("error")) {
            return true;
        }
    }
    return false;
}

int ruleCount(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    int count = 0;
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId &&
            diagnostic.source == QStringLiteral("core") &&
            diagnostic.severity == QStringLiteral("error")) {
            ++count;
        }
    }
    return count;
}

bool hasRuleAt(const ipcraft::DiagnosticStore& diagnostics,
               const QString& ruleId,
               const QString& path) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId != ruleId ||
            diagnostic.source != QStringLiteral("core") ||
            diagnostic.severity != QStringLiteral("error")) {
            continue;
        }
        for (const ipcraft::DiagnosticLocation& location : diagnostic.locations) {
            if (location.kind == QStringLiteral("document_path") &&
                location.path == path) {
                return true;
            }
        }
    }
    return false;
}

ipcraft::PackageInterfaceSpec iface(const QString& id,
                                    const QString& kind,
                                    const QString& protocol,
                                    const QString& role,
                                    bool required = false) {
    ipcraft::PackageInterfaceSpec spec;
    spec.id = id;
    spec.kind = kind;
    spec.protocol = protocol;
    spec.role = role;
    spec.required = required;
    return spec;
}

ipcraft::PackageCompatibilityRule rule(const QString& connectionType,
                                       const QString& fromKind,
                                       const QString& fromProtocol,
                                       const QString& fromRole,
                                       const QString& toKind,
                                       const QString& toProtocol,
                                       const QString& toRole,
                                       const QString& arity) {
    ipcraft::PackageCompatibilityRule compatibility;
    compatibility.connectionType = connectionType;
    compatibility.from.kind = fromKind;
    compatibility.from.protocol = fromProtocol;
    compatibility.from.role = fromRole;
    compatibility.to.kind = toKind;
    compatibility.to.protocol = toProtocol;
    compatibility.to.role = toRole;
    compatibility.arity = arity;
    return compatibility;
}

ipcraft::PackageSpec packageWith(QString id,
                                 QVector<ipcraft::PackageInterfaceSpec> interfaces,
                                 QVector<ipcraft::PackageCompatibilityRule> rules = {}) {
    ipcraft::PackageSpec package;
    package.id = std::move(id);
    package.version = QStringLiteral("1.0.0");
    package.interfaces = std::move(interfaces);
    package.connectionRules.compatibility = std::move(rules);
    package.connectionRules.protocolAliases.insert(QStringLiteral("AMBA_AXI4"), QStringLiteral("axi4"));
    package.connectionRules.protocolAliases.insert(QStringLiteral("AXI4"), QStringLiteral("axi4"));
    return package;
}

ipcraft::CompositionInstance instance(QString id, ipcraft::PackageSpec package) {
    ipcraft::CompositionInstance context;
    context.instanceId = std::move(id);
    context.package = std::move(package);
    return context;
}

ipcraft::CompositionEndpointRef endpoint(const QString& instanceId,
                                         const QString& interfaceId,
                                         const QString& role = {}) {
    ipcraft::CompositionEndpointRef ref;
    ref.instanceId = instanceId;
    ref.interfaceId = interfaceId;
    ref.role = role;
    return ref;
}

ipcraft::SystemConnection connection(const QString& id,
                                     const QString& type,
                                     QVector<ipcraft::CompositionEndpointRef> endpoints) {
    ipcraft::SystemConnection record;
    record.id = id;
    record.type = type;
    record.endpoints = std::move(endpoints);
    record.source = QStringLiteral("user");
    return record;
}

QVector<ipcraft::PackageCompatibilityRule> busRules() {
    return {rule(QStringLiteral("interface"),
                 QStringLiteral("bus"),
                 QStringLiteral("axi4"),
                 QStringLiteral("master"),
                 QStringLiteral("bus"),
                 QStringLiteral("axi4"),
                 QStringLiteral("slave"),
                 QStringLiteral("binary"))};
}

QVector<ipcraft::PackageCompatibilityRule> clockRules() {
    return {rule(QStringLiteral("clock"),
                 QStringLiteral("clock"),
                 QString{},
                 QStringLiteral("source"),
                 QStringLiteral("clock"),
                 QString{},
                 QStringLiteral("sink"),
                 QStringLiteral("fanout"))};
}

void testCompositionRejectsUnknownInstance() {
    const ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("master"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("conn0"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("missing"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model, {instance(QStringLiteral("ip0"), master)});
    require(!result.ok, "unknown endpoint instance should fail composition validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("composition.unknown_instance"),
                      QStringLiteral("$.connections[0].endpoints[1].instance")),
            "unknown instance should produce stable diagnostic path");
}

void testCompositionRejectsUnknownInterface() {
    const ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("master"))},
                    busRules());
    const ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("cfg"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("conn0"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), master),
                                           instance(QStringLiteral("ip1"), slave)});
    require(!result.ok, "unknown endpoint interface should fail composition validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("composition.unknown_interface"),
                      QStringLiteral("$.connections[0].endpoints[1].interface")),
            "unknown interface should produce stable diagnostic path");
}

void testCompositionRejectsRequiredInterfaceUnconnected() {
    const ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"), true)},
                    busRules());

    ipcraft::CompositionModel model;
    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model, {instance(QStringLiteral("ip0"), slave)});
    require(!result.ok, "required package interface should require a connection");
    require(hasRule(result.diagnostics,
                    QStringLiteral("composition.required_interface_unconnected")),
            "required unconnected interface should be diagnosed");
}

void testCompositionAllowsClockFanoutWithOneSource() {
    const ipcraft::PackageSpec source =
        packageWith(QStringLiteral("vendor.example.clock_source"),
                    {iface(QStringLiteral("clk_out"), QStringLiteral("clock"), QString{}, QStringLiteral("source"))},
                    clockRules());
    const ipcraft::PackageSpec sink =
        packageWith(QStringLiteral("vendor.example.clock_sink"),
                    {iface(QStringLiteral("aclk"), QStringLiteral("clock"), QString{}, QStringLiteral("sink"), true)},
                    clockRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("clk0"),
        QStringLiteral("clock"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("clk_out"), QStringLiteral("source")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("aclk"), QStringLiteral("sink")),
         endpoint(QStringLiteral("ip2"), QStringLiteral("aclk"), QStringLiteral("sink"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), source),
                                           instance(QStringLiteral("ip1"), sink),
                                           instance(QStringLiteral("ip2"), sink)});
    require(result.ok, "clock fanout with exactly one source should pass");
}

void testCompositionAllowsClockFanoutWithOneSink() {
    const ipcraft::PackageSpec source =
        packageWith(QStringLiteral("vendor.example.clock_source"),
                    {iface(QStringLiteral("clk_out"), QStringLiteral("clock"), QString{}, QStringLiteral("source"))},
                    clockRules());
    const ipcraft::PackageSpec sink =
        packageWith(QStringLiteral("vendor.example.clock_sink"),
                    {iface(QStringLiteral("aclk"), QStringLiteral("clock"), QString{}, QStringLiteral("sink"), true)},
                    clockRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("clk0"),
        QStringLiteral("clock"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("clk_out"), QStringLiteral("source")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("aclk"), QStringLiteral("sink"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), source),
                                           instance(QStringLiteral("ip1"), sink)});
    require(result.ok, "clock fanout rule should allow one source and one sink");
}

void testCompositionRejectsClockFanoutWithTwoSources() {
    const ipcraft::PackageSpec source =
        packageWith(QStringLiteral("vendor.example.clock_source"),
                    {iface(QStringLiteral("clk_out"), QStringLiteral("clock"), QString{}, QStringLiteral("source"))},
                    clockRules());
    const ipcraft::PackageSpec sink =
        packageWith(QStringLiteral("vendor.example.clock_sink"),
                    {iface(QStringLiteral("aclk"), QStringLiteral("clock"), QString{}, QStringLiteral("sink"))},
                    clockRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("clk0"),
        QStringLiteral("clock"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("clk_out"), QStringLiteral("source")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("clk_out"), QStringLiteral("source")),
         endpoint(QStringLiteral("ip2"), QStringLiteral("aclk"), QStringLiteral("sink"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), source),
                                           instance(QStringLiteral("ip1"), source),
                                           instance(QStringLiteral("ip2"), sink)});
    require(!result.ok, "clock fanout with two sources should fail");
    require(hasRule(result.diagnostics,
                    QStringLiteral("composition.clock_reset_source_count")),
            "clock/reset source count should be diagnosed");
    require(!hasRule(result.diagnostics,
                     QStringLiteral("composition.multiply_driven_input")),
            "clock/reset source count should not also emit multiply-driven input");
}

void testCompositionRejectsMultiplyDrivenInput() {
    const ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("master"))},
                    busRules());
    const ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("conn0"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip2"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));
    model.connections.append(connection(
        QStringLiteral("conn1"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip1"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip2"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), master),
                                           instance(QStringLiteral("ip1"), master),
                                           instance(QStringLiteral("ip2"), slave)});
    require(!result.ok, "input/sink endpoint should not be multiply driven");
    require(hasRule(result.diagnostics,
                    QStringLiteral("composition.multiply_driven_input")),
            "multiply driven input should be diagnosed");
    require(ruleCount(result.diagnostics,
                      QStringLiteral("composition.multiply_driven_input")) == 1,
            "multiply driven input should be diagnosed once");
}

void testCompositionRejectsSameConnectionMultiplyDrivenInputOnce() {
    const ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("master"))},
                    busRules());
    const ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("conn0"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip2"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), master),
                                           instance(QStringLiteral("ip1"), master),
                                           instance(QStringLiteral("ip2"), slave)});
    require(!result.ok, "same connection with two sources should fail");
    require(ruleCount(result.diagnostics,
                      QStringLiteral("composition.multiply_driven_input")) == 1,
            "same-connection multiply driven input should be diagnosed once");
}

void testExternalPortsRejectUnknownEndpointReferences() {
    const ipcraft::PackageSpec package =
        packageWith(QStringLiteral("vendor.example.ip"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::ExternalPort unknownInstance;
    unknownInstance.id = QStringLiteral("ext0");
    unknownInstance.hasInterface = true;
    unknownInstance.interfaceRef =
        endpoint(QStringLiteral("missing"), QStringLiteral("s_axi"), QStringLiteral("slave"));

    ipcraft::ExternalPort unknownInterface;
    unknownInterface.id = QStringLiteral("ext1");
    unknownInterface.hasInterface = true;
    unknownInterface.interfaceRef =
        endpoint(QStringLiteral("ip0"), QStringLiteral("missing"), QStringLiteral("slave"));

    ipcraft::CompositionModel model;
    model.externalPorts = {unknownInstance, unknownInterface};

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model, {instance(QStringLiteral("ip0"), package)});
    require(!result.ok, "external port endpoint references should be validated");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("composition.unknown_instance"),
                      QStringLiteral("$.external_ports[0].interface.instance")),
            "external port unknown instance should be diagnosed at stable path");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("composition.unknown_interface"),
                      QStringLiteral("$.external_ports[1].interface.interface")),
            "external port unknown interface should be diagnosed at stable path");
}

void testProtocolAliasesNormalizeBeforeCompatibility() {
    ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("AMBA_AXI4"), QStringLiteral("master"))},
                    busRules());
    ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("AXI4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("conn0"),
        QStringLiteral("interface"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), master),
                                           instance(QStringLiteral("ip1"), slave)});
    require(result.ok, "protocol aliases should normalize before compatibility checks");
}

void testCompositionRejectsUnknownConnectionClassBeforeEndpointCompatibility() {
    const ipcraft::PackageSpec master =
        packageWith(QStringLiteral("vendor.example.master"),
                    {iface(QStringLiteral("m_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("master"))},
                    busRules());
    const ipcraft::PackageSpec slave =
        packageWith(QStringLiteral("vendor.example.slave"),
                    {iface(QStringLiteral("s_axi"), QStringLiteral("bus"), QStringLiteral("axi4"), QStringLiteral("slave"))},
                    busRules());

    ipcraft::CompositionModel model;
    model.connections.append(connection(
        QStringLiteral("axi_link"),
        QStringLiteral("unlisted"),
        {endpoint(QStringLiteral("ip0"), QStringLiteral("m_axi"), QStringLiteral("master")),
         endpoint(QStringLiteral("ip1"), QStringLiteral("s_axi"), QStringLiteral("slave"))}));

    const ipcraft::CompositionValidationResult result =
        ipcraft::validateCompositionModel(model,
                                          {instance(QStringLiteral("ip0"), master),
                                           instance(QStringLiteral("ip1"), slave)});
    require(!result.ok, "unknown connection class should fail composition validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("composition.unknown_connection_class"),
                      QStringLiteral("$.connections[0].type")),
            "unknown connection class should be diagnosed at connection type");
    require(!hasRule(result.diagnostics, QStringLiteral("composition.incompatible_endpoint")),
            "unknown connection class should stop endpoint compatibility checks");
}

void testCompositionJsonAlwaysExposesPublicKeys() {
    const QJsonObject json = ipcraft::CompositionModel{}.toJson();
    require(json.contains(QStringLiteral("connections")), "composition JSON should expose connections");
    require(json.contains(QStringLiteral("external_ports")), "composition JSON should expose external_ports");
    require(json.contains(QStringLiteral("groups")), "composition JSON should expose groups");
    require(json.contains(QStringLiteral("properties")), "composition JSON should expose properties");
    require(json.value(QStringLiteral("groups")).isArray(), "groups should be an array");
    require(json.value(QStringLiteral("properties")).isObject(), "properties should be an object");
}

void testLayoutStoresCanvasCoordinatesOutsideConfig() {
    const QJsonObject layoutJson{
        {QStringLiteral("views"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("main")},
                {QStringLiteral("canvas"), QJsonObject{
                    {QStringLiteral("nodes"), QJsonObject{
                        {QStringLiteral("ip0"), QJsonObject{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20}}}
                    }},
                    {QStringLiteral("connections"), QJsonObject{}},
                    {QStringLiteral("zoom"), 1.5},
                    {QStringLiteral("pan"), QJsonObject{{QStringLiteral("x"), 3}, {QStringLiteral("y"), 4}}}
                }}
            }
        }}
    };

    const ipcraft::LayoutModel model = ipcraft::LayoutModel::fromJson(layoutJson);
    const QJsonObject roundTrip = model.toJson();
    const QJsonObject canvas =
        roundTrip.value(QStringLiteral("views")).toArray().first().toObject()
            .value(QStringLiteral("canvas")).toObject();
    require(canvas.value(QStringLiteral("nodes")).toObject()
                .value(QStringLiteral("ip0")).toObject()
                .value(QStringLiteral("x")).toInt() == 10,
            "layout node x coordinate should round-trip");
    require(!QJsonDocument(roundTrip).toJson(QJsonDocument::Compact).contains("parameters"),
            "layout must not store canvas coordinates as config parameters");
}

void testGraphConfigUsesNaryRelationshipsNotPortRefs() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj2")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}, {QStringLiteral("role"), QStringLiteral("target")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj2")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            }
        }},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(readResult.ok, "n-ary graph-config should parse");
    const ipcraft::DiagnosticStore diagnostics = ipcraft::validateGraphConfig(readResult.config);
    require(diagnostics.records.isEmpty(), "valid n-ary graph-config should validate");

    const QJsonObject roundTrip = readResult.config.toJson();
    const QJsonObject relationship =
        roundTrip.value(QStringLiteral("relationships")).toArray().first().toObject();
    require(relationship.value(QStringLiteral("endpoints")).toArray().size() == 3,
            "graph-config relationships should remain n-ary");
    require(!relationship.contains(QStringLiteral("source")) &&
                !relationship.contains(QStringLiteral("target")),
            "graph-config must not expose old source/target PortRef shape");
}

void testGraphConfigRejectsDuplicateAndUnknownEndpointObjects() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("missing")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            }
        }}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(readResult.ok, "graph-config shape should parse before semantic validation");
    const ipcraft::DiagnosticStore diagnostics = ipcraft::validateGraphConfig(readResult.config);
    require(hasRule(diagnostics, QStringLiteral("graph_config.duplicate_object")),
            "duplicate graph object should be diagnosed");
    require(hasRuleAt(diagnostics,
                      QStringLiteral("graph_config.unknown_endpoint_object"),
                      QStringLiteral("$.relationships[0].endpoints[1].object")),
            "unknown graph endpoint object should be diagnosed at stable path");
}

void testGraphConfigRejectsDuplicateRelationships() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            }
        }}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(readResult.ok, "duplicate relationship ids should parse before semantic validation");
    const ipcraft::DiagnosticStore diagnostics = ipcraft::validateGraphConfig(readResult.config);
    require(hasRuleAt(diagnostics,
                      QStringLiteral("graph_config.duplicate_relationship"),
                      QStringLiteral("$.relationships[1].id")),
            "duplicate graph relationship should emit graph_config.duplicate_relationship");
    require(hasRuleAt(diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.relationships[1].id")),
            "duplicate graph relationship should also emit generic graph_config.type_mismatch");
}

void testGraphConfigRejectsUnknownTopLevelFields() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{}},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("unexpected"), true}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(!readResult.ok, "graph-config should reject unknown top-level fields");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.unknown_top_level_field"),
                      QStringLiteral("$.unexpected")),
            "unknown graph-config root field should emit graph_config.unknown_top_level_field");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.unexpected")),
            "unknown graph-config root field should also emit generic graph_config.type_mismatch");
}

void testGraphConfigRejectsMalformedPropertiesAndNative() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")},
                        {QStringLiteral("type"), QStringLiteral("vendor.node")},
                        {QStringLiteral("properties"), true}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")},
                        {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")},
                                {QStringLiteral("role"), QStringLiteral("source")},
                                {QStringLiteral("properties"), QStringLiteral("bad")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")},
                                {QStringLiteral("role"), QStringLiteral("target")}}
                }},
                {QStringLiteral("properties"), 1}
            }
        }},
        {QStringLiteral("properties"), false},
        {QStringLiteral("native"), QJsonArray{}}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(!readResult.ok, "graph-config should reject malformed properties/native values");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.objects[0].properties")),
            "object properties should be object-valued");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.relationships[0].properties")),
            "relationship properties should be object-valued");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.relationships[0].endpoints[0].properties")),
            "endpoint properties should be object-valued");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.properties")),
            "top-level properties should be object-valued");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.native")),
            "top-level native should be object-valued");
}

void testGraphConfigRejectsMalformedObjects() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QStringLiteral("not-an-object"),
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            }
        }}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(!readResult.ok, "malformed graph-config objects should fail parsing");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.objects[0].id")),
            "missing graph object id should be diagnosed at stable path");
    require(hasRuleAt(readResult.diagnostics,
                      QStringLiteral("graph_config.type_mismatch"),
                      QStringLiteral("$.objects[1]")),
            "non-object graph object entry should be diagnosed at stable path");
    require(readResult.config.objects.isEmpty(),
            "malformed graph objects should not be inserted with empty ids");
}

void testGraphConfigRejectsOldSourceTargetShape() {
    const QJsonObject graphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("source"), QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}}},
                {QStringLiteral("target"), QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}}}
            }
        }}
    };

    const ipcraft::GraphConfigReadResult readResult = ipcraft::GraphConfig::fromJson(graphJson);
    require(!readResult.ok || !ipcraft::validateGraphConfig(readResult.config).records.isEmpty(),
            "old source/target graph-config shape should be rejected");

    const QJsonObject mixedGraphJson{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}, {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}, {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}, {QStringLiteral("role"), QStringLiteral("target")}}
                }},
                {QStringLiteral("source"), QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")}}},
                {QStringLiteral("target"), QJsonObject{{QStringLiteral("object"), QStringLiteral("obj1")}}}
            }
        }}
    };

    const ipcraft::GraphConfigReadResult mixedReadResult =
        ipcraft::GraphConfig::fromJson(mixedGraphJson);
    require(!mixedReadResult.ok,
            "graph-config relationships should reject extra legacy source/target fields");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCompositionRejectsUnknownInstance();
        testCompositionRejectsUnknownInterface();
        testCompositionRejectsRequiredInterfaceUnconnected();
        testCompositionAllowsClockFanoutWithOneSource();
        testCompositionAllowsClockFanoutWithOneSink();
        testCompositionRejectsClockFanoutWithTwoSources();
        testCompositionRejectsMultiplyDrivenInput();
        testCompositionRejectsSameConnectionMultiplyDrivenInputOnce();
        testExternalPortsRejectUnknownEndpointReferences();
        testProtocolAliasesNormalizeBeforeCompatibility();
        testCompositionRejectsUnknownConnectionClassBeforeEndpointCompatibility();
        testCompositionJsonAlwaysExposesPublicKeys();
        testLayoutStoresCanvasCoordinatesOutsideConfig();
        testGraphConfigUsesNaryRelationshipsNotPortRefs();
        testGraphConfigRejectsDuplicateAndUnknownEndpointObjects();
        testGraphConfigRejectsDuplicateRelationships();
        testGraphConfigRejectsUnknownTopLevelFields();
        testGraphConfigRejectsMalformedPropertiesAndNative();
        testGraphConfigRejectsMalformedObjects();
        testGraphConfigRejectsOldSourceTargetShape();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_composition_test passed\n";
    return 0;
}
