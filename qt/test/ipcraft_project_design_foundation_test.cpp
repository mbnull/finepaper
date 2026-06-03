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
    testLayoutFieldsRejectedFromComponentConfig();
    testAttachmentConnectionKindRejected();
    testExtensionBlockEnvelopeIsPreserved();

    std::cout << "ipcraft_project_design_foundation_test passed\n";
    return 0;
}
