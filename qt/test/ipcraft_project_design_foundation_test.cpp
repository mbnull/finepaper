// Ipcraft ProjectDesign foundation contract tests.
#include "ipcraft/core/project_design.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasCode(const QVector<ipcraft::core::ValidationIssue>& issues, const QString& code) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

bool hasIssue(const QVector<ipcraft::core::ValidationIssue>& issues,
              const QString& code,
              const QString& path) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code && issue.path == path) {
            return true;
        }
    }

    return false;
}

ipcraft::core::ProjectDesign minimalProject() {
    ipcraft::core::ProjectDesign project;
    project.id = QStringLiteral("proj_uart_min");
    project.name = QStringLiteral("Minimal UART");

    project.packages.append({QStringLiteral("vendor.uart16550"), QStringLiteral("1.0.0")});

    ipcraft::core::ComponentInstance uart;
    uart.id = QStringLiteral("uart0");
    uart.type = QStringLiteral("uart16550");
    uart.packageRef = QStringLiteral("vendor.uart16550@1.0.0");
    uart.identity.insert(QStringLiteral("label"), QStringLiteral("UART 0"));
    uart.config.insert(QStringLiteral("baud"), 115200);
    project.components.append(uart);

    return project;
}

void testSchemaConstantsCoverFoundationAndFollowupContracts() {
    require(ipcraft::schemaids::projectV1 == QStringLiteral("ipcraft.project.v1"),
            "project schema id should be canonical");
    require(ipcraft::schemaids::patchV1 == QStringLiteral("ipcraft.patch.v1"),
            "patch schema id should be canonical");
    require(ipcraft::schemaids::topologyGraphV1 == QStringLiteral("ipcraft.topology.graph.v1"),
            "topology graph schema id should be canonical");
    require(ipcraft::schemaids::toolInputV1 == QStringLiteral("ipcraft.tool.input.v1"),
            "tool input schema id should be canonical");
    require(ipcraft::schemaids::nocCapabilityV1 == QStringLiteral("ipcraft.capability.noc.v1"),
            "NoC capability schema id should be canonical");
}

void testMinimalProjectDesignValidates() {
    require(ipcraft::core::validateProjectDesign(minimalProject()).isEmpty(),
            "minimal ProjectDesign should validate");
}

void testDuplicateComponentIdsRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components.append(project.components.first());

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.duplicate_component_id")),
            "duplicate component ids should be rejected");
}

void testUnknownComponentPackageRefRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components.first().packageRef = QStringLiteral("vendor.timer@1.0.0");

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("component.unknown_package_ref"),
                     QStringLiteral("/components/0/packageRef")),
            "unknown component packageRef should be rejected");
}

void testMalformedComponentPackageRefRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components.first().packageRef = QStringLiteral("vendor.uart16550");

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("component.unknown_package_ref"),
                     QStringLiteral("/components/0/packageRef")),
            "malformed component packageRef should be rejected");
}

void testLayoutFieldsRejectedFromComponentConfig() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components.first().config.insert(QStringLiteral("x"), 24);
    project.components.first().config.insert(
        QStringLiteral("waypoints"),
        QJsonArray{QJsonObject{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20}}});

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.layout_in_component_config")),
            "layout fields should not be accepted in component config");
}

void testAttachmentConnectionKindRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("attach0");
    connection.from = {QStringLiteral("uart0"), QStringLiteral("serial")};
    connection.to = {QStringLiteral("uart0"), QStringLiteral("axi_s")};
    connection.kind = QStringLiteral("attachment");
    project.connections.append(connection);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.attachment_connection_forbidden")),
            "attachment connection kind should be rejected");
}

void testConnectionEndpointUnknownComponentRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("conn0");
    connection.from = {QStringLiteral("missing0"), QStringLiteral("serial")};
    connection.to = {QStringLiteral("uart0"), QStringLiteral("axi_s")};
    project.connections.append(connection);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("connection.unknown_component_ref"),
                     QStringLiteral("/connections/0/from/component")),
            "connection endpoint component refs should reference component ids");
}

void testMalformedInterfaceInstanceRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::InterfaceInstance interface;
    project.interfaces.append(interface);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_id"),
                     QStringLiteral("/interfaces/0/id")),
            "interface id should be required");
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_owner_component_id"),
                     QStringLiteral("/interfaces/0/ownerComponentId")),
            "interface owner component id should be required");
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_type"),
                     QStringLiteral("/interfaces/0/type")),
            "interface type should be required");
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_role"),
                     QStringLiteral("/interfaces/0/role")),
            "interface role should be required");
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_direction"),
                     QStringLiteral("/interfaces/0/direction")),
            "interface direction should be required");
    require(hasIssue(issues,
                     QStringLiteral("interface.missing_protocol"),
                     QStringLiteral("/interfaces/0/protocol")),
            "interface protocol should be required");
}

void testInterfaceOwnerComponentMustExist() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::InterfaceInstance interface;
    interface.id = QStringLiteral("axi_s");
    interface.ownerComponentId = QStringLiteral("missing0");
    interface.type = QStringLiteral("vendor.axi4lite");
    interface.role = QStringLiteral("slave");
    interface.direction = QStringLiteral("target");
    interface.protocol = QStringLiteral("axi4lite");
    project.interfaces.append(interface);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("interface.unknown_owner_component_id"),
                     QStringLiteral("/interfaces/0/ownerComponentId")),
            "interface owner component id should reference a declared component");
}

void testExplicitConnectionInterfaceRefsMustExist() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::InterfaceInstance interface;
    interface.id = QStringLiteral("serial");
    interface.ownerComponentId = QStringLiteral("uart0");
    interface.type = QStringLiteral("vendor.serial");
    interface.role = QStringLiteral("device");
    interface.direction = QStringLiteral("bidirectional");
    interface.protocol = QStringLiteral("serial");
    project.interfaces.append(interface);

    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("conn0");
    connection.from = {QStringLiteral("uart0"), QStringLiteral("missing")};
    connection.to = {QStringLiteral("uart0"), QStringLiteral("serial")};
    project.connections.append(connection);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("connection.unknown_interface_ref"),
                     QStringLiteral("/connections/0/from/interface")),
            "explicit connection endpoint interface refs should reference declared interfaces");
}

void testMalformedViewDocumentRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::ViewDocument view;
    project.views.append(view);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("view.missing_id"),
                     QStringLiteral("/views/0/id")),
            "view id should be required");
    require(hasIssue(issues,
                     QStringLiteral("view.missing_schema"),
                     QStringLiteral("/views/0/schema")),
            "view schema should be required");
    require(hasIssue(issues,
                     QStringLiteral("view.missing_kind"),
                     QStringLiteral("/views/0/kind")),
            "view kind should be required");
    require(hasIssue(issues,
                     QStringLiteral("view.missing_target_ref"),
                     QStringLiteral("/views/0/targetRef")),
            "view targetRef should be required");
    require(hasIssue(issues,
                     QStringLiteral("view.missing_provider_ref"),
                     QStringLiteral("/views/0/providerRef")),
            "view providerRef should be required");
}

void testUnsupportedViewSchemaRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::ViewDocument view;
    view.id = QStringLiteral("block.main");
    view.schema = QStringLiteral("vendor.view.v1");
    view.kind = QStringLiteral("block_diagram");
    view.targetRef = QStringLiteral("project:proj_uart_min");
    view.providerRef = QStringLiteral("ipcraft.ui.block_diagram");
    project.views.append(view);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("view.unsupported_schema"),
                     QStringLiteral("/views/0/schema")),
            "unsupported view schema should be rejected");
}

void testMalformedTopologyEnvelopeRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.schema = ipcraft::schemaids::topologyParametricV1;
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.missing_id"),
                     QStringLiteral("/topologies/0/id")),
            "topology id should be required");
    require(hasIssue(issues,
                     QStringLiteral("topology.missing_kind"),
                     QStringLiteral("/topologies/0/kind")),
            "topology kind should be required");
    require(hasIssue(issues,
                     QStringLiteral("topology.missing_family"),
                     QStringLiteral("/topologies/0/family")),
            "parametric topology family should be required");
}

void testInvalidTopologyKindRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("topo0");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("parametric");
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.invalid_kind"),
                     QStringLiteral("/topologies/0/kind")),
            "topology kind should match its schema");
}

void testMissingTopologyAttachmentIdRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("topo0");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("explicit_graph");
    topology.attachments.append(ipcraft::core::TopologyAttachment{});
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.missing_attachment_id"),
                     QStringLiteral("/topologies/0/attachments/0/id")),
            "topology attachment id should be required");
}

void testDuplicateTopologyNodeIdsRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("topo0");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("explicit_graph");
    topology.nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("node0")}});
    topology.nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("node0")}});
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.duplicate_node_id"),
                     QStringLiteral("/topologies/0/nodes/1/id")),
            "duplicate topology node ids should be rejected");
}

void testDuplicateTopologyLinkIdsRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("topo0");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("explicit_graph");
    topology.links.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("link0")}});
    topology.links.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("link0")}});
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.duplicate_link_id"),
                     QStringLiteral("/topologies/0/links/1/id")),
            "duplicate topology link ids should be rejected");
}

void testDuplicateTopologyAttachmentIdsStillRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("topo0");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("explicit_graph");
    topology.attachments.append({QStringLiteral("attach0")});
    topology.attachments.append({QStringLiteral("attach0")});
    project.topologies.append(topology);

    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    require(hasIssue(issues,
                     QStringLiteral("topology.duplicate_attachment_id"),
                     QStringLiteral("/topologies/0/attachments/1/id")),
            "duplicate topology attachment ids should remain rejected");
}

void testExtensionBlockEnvelopeIsPreserved() {
    ipcraft::core::ExtensionBlock extension;
    extension.ownerPackageId = QStringLiteral("vendor.meshnoc");
    extension.schemaId = QStringLiteral("vendor.meshnoc.project.v1");
    extension.version = 1;
    extension.data.insert(QStringLiteral("schema"), QStringLiteral("vendor.meshnoc.project.v1"));
    extension.data.insert(QStringLiteral("routing_algorithm"), QStringLiteral("xy"));

    const QJsonObject serialized = ipcraft::core::extensionBlockToJson(extension);
    const ipcraft::core::ExtensionBlock parsed =
        ipcraft::core::extensionBlockFromJson(serialized);

    require(parsed.ownerPackageId == QStringLiteral("vendor.meshnoc"),
            "extension owner package id should round-trip");
    require(parsed.schemaId == QStringLiteral("vendor.meshnoc.project.v1"),
            "extension schema id should round-trip");
    require(parsed.version == 1, "extension version should round-trip");
    require(parsed.data.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("vendor.meshnoc.project.v1"),
            "extension data schema should round-trip");
    require(parsed.data.value(QStringLiteral("routing_algorithm")).toString() ==
                QStringLiteral("xy"),
            "extension data should preserve routing algorithm");
}

} // namespace

int main() {
    testSchemaConstantsCoverFoundationAndFollowupContracts();
    testMinimalProjectDesignValidates();
    testDuplicateComponentIdsRejected();
    testUnknownComponentPackageRefRejected();
    testMalformedComponentPackageRefRejected();
    testLayoutFieldsRejectedFromComponentConfig();
    testAttachmentConnectionKindRejected();
    testConnectionEndpointUnknownComponentRejected();
    testMalformedInterfaceInstanceRejected();
    testInterfaceOwnerComponentMustExist();
    testExplicitConnectionInterfaceRefsMustExist();
    testMalformedViewDocumentRejected();
    testUnsupportedViewSchemaRejected();
    testMalformedTopologyEnvelopeRejected();
    testInvalidTopologyKindRejected();
    testMissingTopologyAttachmentIdRejected();
    testDuplicateTopologyNodeIdsRejected();
    testDuplicateTopologyLinkIdsRejected();
    testDuplicateTopologyAttachmentIdsStillRejected();
    testExtensionBlockEnvelopeIsPreserved();

    std::cout << "ipcraft_project_design_foundation_test passed\n";
    return 0;
}
