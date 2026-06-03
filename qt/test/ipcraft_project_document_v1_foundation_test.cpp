#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/schemaids.h"

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

QJsonObject minimalUartProject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::projectV1},
        {QStringLiteral("id"), QStringLiteral("proj_uart_min")},
        {QStringLiteral("name"), QStringLiteral("Minimal UART")},
        {QStringLiteral("packages"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.uart16550")},
                        {QStringLiteral("version"), QStringLiteral("1.0.0")}}
        }},
        {QStringLiteral("components"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("uart0")},
                {QStringLiteral("type"), QStringLiteral("uart16550")},
                {QStringLiteral("packageRef"), QStringLiteral("vendor.uart16550@1.0.0")},
                {QStringLiteral("identity"), QJsonObject{{QStringLiteral("label"), QStringLiteral("UART 0")}}},
                {QStringLiteral("config"), QJsonObject{{QStringLiteral("baud"), 115200}}}
            }
        }},
        {QStringLiteral("connections"), QJsonArray{}},
        {QStringLiteral("topologies"), QJsonArray{}},
        {QStringLiteral("views"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("block.main")},
                {QStringLiteral("schema"), ipcraft::schemaids::viewV1},
                {QStringLiteral("kind"), QStringLiteral("block_diagram")},
                {QStringLiteral("targetRef"), QStringLiteral("project:proj_uart_min")},
                {QStringLiteral("providerRef"), QStringLiteral("ipcraft.ui.block_diagram")},
                {QStringLiteral("layout"), QJsonObject{
                    {QStringLiteral("nodes"), QJsonObject{
                        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("x"), 96}, {QStringLiteral("y"), 128}}}
                    }},
                    {QStringLiteral("edges"), QJsonObject{}}
                }}
            }
        }},
        {QStringLiteral("extensions"), QJsonArray{}}
    };
}

QJsonObject cpuNicNocProject() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("id"), QStringLiteral("proj_cpu_nic_noc"));
    project.insert(QStringLiteral("name"), QStringLiteral("CPU NIC NoC Demo"));
    project.insert(QStringLiteral("packages"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.riscv")},
                    {QStringLiteral("version"), QStringLiteral("2.1.0")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.meshnoc")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")}}
    });
    project.insert(QStringLiteral("components"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cpu0")},
                    {QStringLiteral("type"), QStringLiteral("core")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.riscv@2.1.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("xlen"), 64}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("nic_cpu0")},
                    {QStringLiteral("type"), QStringLiteral("axi_nic")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc@1.0.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("axi_data_width"), 64}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("noc0")},
                    {QStringLiteral("type"), QStringLiteral("meshnoc")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc@1.0.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("name"), QStringLiteral("noc0")}}}}
    });
    project.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("c_cpu_to_nic")},
                    {QStringLiteral("from"), QJsonObject{{QStringLiteral("component"), QStringLiteral("cpu0")},
                                                         {QStringLiteral("interface"), QStringLiteral("axi_m")}}},
                    {QStringLiteral("to"), QJsonObject{{QStringLiteral("component"), QStringLiteral("nic_cpu0")},
                                                       {QStringLiteral("interface"), QStringLiteral("axi_s")}}},
                    {QStringLiteral("kind"), QStringLiteral("interface")}}
    });
    project.insert(QStringLiteral("topologies"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("noc0.fabric")},
            {QStringLiteral("schema"), ipcraft::schemaids::topologyParametricV1},
            {QStringLiteral("ownerComponentId"), QStringLiteral("noc0")},
            {QStringLiteral("kind"), QStringLiteral("parametric")},
            {QStringLiteral("family"), QStringLiteral("mesh")},
            {QStringLiteral("providerRef"), QStringLiteral("ipcraft.capability.noc.topology.mesh")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("dimensions"), QJsonArray{2, 2}},
                                                       {QStringLiteral("routing"), QStringLiteral("xy")}}},
            {QStringLiteral("constraints"), QJsonObject{{QStringLiteral("max_hops"), 4}}},
            {QStringLiteral("attachments"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("a_cpu")},
                            {QStringLiteral("attachmentPoint"), QJsonObject{{QStringLiteral("tile"), QJsonArray{0, 0}},
                                                                             {QStringLiteral("slot"), QStringLiteral("local0")}}},
                            {QStringLiteral("componentRef"), QStringLiteral("nic_cpu0")},
                            {QStringLiteral("interfaceRef"), QStringLiteral("noc_ep")}}
            }}
        }
    });
    project.insert(QStringLiteral("extensions"), QJsonArray{
        QJsonObject{{QStringLiteral("ownerPackageId"), QStringLiteral("ipcraft.capability.noc")},
                    {QStringLiteral("schemaId"), ipcraft::schemaids::nocExtensionV1},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("data"), QJsonObject{{QStringLiteral("schema"), ipcraft::schemaids::nocExtensionV1}}}},
        QJsonObject{{QStringLiteral("ownerPackageId"), QStringLiteral("vendor.meshnoc")},
                    {QStringLiteral("schemaId"), QStringLiteral("vendor.meshnoc.project.v1")},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("data"), QJsonObject{{QStringLiteral("schema"), QStringLiteral("vendor.meshnoc.project.v1")},
                                                         {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}}}}
    });
    return project;
}

void testMinimalProjectRoundTripsStructurally() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(minimalUartProject());
    require(result.success, "minimal UART project should parse");
    require(result.project.id == QStringLiteral("proj_uart_min"), "project id should parse");
    require(result.project.components.size() == 1, "components should parse");
    require(result.project.views.size() == 1, "views should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1,
            "writer should emit project schema");
    require(written.value(QStringLiteral("components")).toArray().first().toObject()
                .value(QStringLiteral("config")).toObject().value(QStringLiteral("baud")).toInt() == 115200,
            "writer should preserve authored config");
}

void testViewOptionalObjectsRoundTripWithoutDataLoss() {
    const QJsonObject templates{
        {QStringLiteral("node"), QJsonObject{{QStringLiteral("component"), QStringLiteral("compact")}}}
    };
    const QJsonObject portGrouping{
        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("by_direction")}}}
    };
    const QJsonObject labels{
        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("text"), QStringLiteral("UART Core")}}}
    };
    const QJsonObject badges{
        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("severity"), QStringLiteral("info")}}}
    };
    const QJsonObject propertyGroups{
        {QStringLiteral("timing"), QJsonObject{{QStringLiteral("collapsed"), false}}}
    };
    const QJsonObject layoutPreference{
        {QStringLiteral("engine"), QStringLiteral("elk")},
        {QStringLiteral("rankDirection"), QStringLiteral("LR")}
    };
    const QJsonObject interactionAffordances{
        {QStringLiteral("canDragNodes"), true},
        {QStringLiteral("canEditLabels"), false}
    };
    const QJsonObject diagnosticsOverlay{
        {QStringLiteral("visible"), true},
        {QStringLiteral("levels"), QJsonArray{QStringLiteral("warning"), QStringLiteral("error")}}
    };
    const QJsonObject icons{
        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("name"), QStringLiteral("serial-port")}}}
    };

    QJsonObject project = minimalUartProject();
    QJsonArray views = project.value(QStringLiteral("views")).toArray();
    QJsonObject view = views.at(0).toObject();
    view.insert(QStringLiteral("templates"), templates);
    view.insert(QStringLiteral("portGrouping"), portGrouping);
    view.insert(QStringLiteral("labels"), labels);
    view.insert(QStringLiteral("badges"), badges);
    view.insert(QStringLiteral("propertyGroups"), propertyGroups);
    view.insert(QStringLiteral("layoutPreference"), layoutPreference);
    view.insert(QStringLiteral("interactionAffordances"), interactionAffordances);
    view.insert(QStringLiteral("diagnosticsOverlay"), diagnosticsOverlay);
    view.insert(QStringLiteral("icons"), icons);
    views.replace(0, view);
    project.insert(QStringLiteral("views"), views);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(result.success, "project with optional view objects should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    const QJsonObject writtenView =
        written.value(QStringLiteral("views")).toArray().first().toObject();
    require(writtenView.value(QStringLiteral("templates")).toObject() == templates,
            "writer should preserve view templates");
    require(writtenView.value(QStringLiteral("portGrouping")).toObject() == portGrouping,
            "writer should preserve view port grouping");
    require(writtenView.value(QStringLiteral("labels")).toObject() == labels,
            "writer should preserve view labels");
    require(writtenView.value(QStringLiteral("badges")).toObject() == badges,
            "writer should preserve view badges");
    require(writtenView.value(QStringLiteral("propertyGroups")).toObject() == propertyGroups,
            "writer should preserve view property groups");
    require(writtenView.value(QStringLiteral("layoutPreference")).toObject() == layoutPreference,
            "writer should preserve view layout preference");
    require(writtenView.value(QStringLiteral("interactionAffordances")).toObject() ==
                interactionAffordances,
            "writer should preserve view interaction affordances");
    require(writtenView.value(QStringLiteral("diagnosticsOverlay")).toObject() ==
                diagnosticsOverlay,
            "writer should preserve view diagnostics overlay");
    require(writtenView.value(QStringLiteral("icons")).toObject() == icons,
            "writer should preserve view icons");
}

void testCpuNicNocRoundTripsWithExtensionsAndAttachments() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(cpuNicNocProject());
    require(result.success, "CPU NIC NoC project should parse structurally");
    require(result.project.topologies.size() == 1, "topology should parse");
    require(result.project.topologies.first().attachments.size() == 1, "attachment should parse");
    require(result.project.extensions.size() == 2, "extension blocks should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    const QJsonObject writtenTopology =
        written.value(QStringLiteral("topologies")).toArray().first().toObject();
    require(writtenTopology.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::topologyParametricV1,
            "writer should preserve parametric topology schema");
    require(writtenTopology.value(QStringLiteral("family")).toString() == QStringLiteral("mesh"),
            "writer should preserve parametric topology family");
    require(writtenTopology.value(QStringLiteral("providerRef")).toString() ==
                QStringLiteral("ipcraft.capability.noc.topology.mesh"),
            "writer should preserve parametric topology provider ref");
    const QJsonObject writtenParameters =
        writtenTopology.value(QStringLiteral("parameters")).toObject();
    const QJsonArray writtenDimensions =
        writtenParameters.value(QStringLiteral("dimensions")).toArray();
    require(writtenDimensions.size() == 2,
            "writer should preserve parametric topology dimensions length");
    require(writtenDimensions.at(0).toInt() == 2 && writtenDimensions.at(1).toInt() == 2,
            "writer should preserve parametric topology dimensions values");
    require(writtenParameters.value(QStringLiteral("routing")).toString() == QStringLiteral("xy"),
            "writer should preserve parametric topology routing parameter");
    require(writtenTopology.value(QStringLiteral("constraints")).toObject()
                .value(QStringLiteral("max_hops")).toInt() == 4,
            "writer should preserve parametric topology constraints");
    require(!writtenTopology.contains(QStringLiteral("nodes")),
            "writer should not emit graph nodes for parametric topology requests");
    require(!writtenTopology.contains(QStringLiteral("links")),
            "writer should not emit graph links for parametric topology requests");
    require(writtenTopology.value(QStringLiteral("attachments")).toArray().size() == 1,
            "writer should preserve topology attachments");

    require(written.value(QStringLiteral("extensions")).toArray().size() == 2,
            "writer should preserve extension blocks");
    require(written.value(QStringLiteral("extensions")).toArray().at(1).toObject()
                .value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("routing_algorithm")).toString() == QStringLiteral("xy"),
            "writer should preserve vendor extension data");
}

void testInterfacesRoundTripAtTopLevel() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("interfaces"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("axi_s")},
                    {QStringLiteral("ownerComponentId"), QStringLiteral("uart0")},
                    {QStringLiteral("type"), QStringLiteral("vendor.axi4lite")},
                    {QStringLiteral("role"), QStringLiteral("slave")},
                    {QStringLiteral("direction"), QStringLiteral("target")},
                    {QStringLiteral("protocol"), QStringLiteral("axi4lite")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("data_width"), 32}}}}
    });

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(result.success, "project with top-level interface should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    const QJsonArray writtenInterfaces = written.value(QStringLiteral("interfaces")).toArray();
    require(writtenInterfaces.size() == 1, "writer should preserve top-level interfaces");
    const QJsonObject writtenInterface = writtenInterfaces.first().toObject();
    require(writtenInterface.value(QStringLiteral("id")).toString() == QStringLiteral("axi_s"),
            "writer should preserve interface id");
    require(writtenInterface.value(QStringLiteral("ownerComponentId")).toString() ==
                QStringLiteral("uart0"),
            "writer should preserve interface owner component");
    require(writtenInterface.value(QStringLiteral("type")).toString() ==
                QStringLiteral("vendor.axi4lite"),
            "writer should preserve interface type");
    require(writtenInterface.value(QStringLiteral("role")).toString() == QStringLiteral("slave"),
            "writer should preserve interface role");
    require(writtenInterface.value(QStringLiteral("direction")).toString() ==
                QStringLiteral("target"),
            "writer should preserve interface direction");
    require(writtenInterface.value(QStringLiteral("protocol")).toString() ==
                QStringLiteral("axi4lite"),
            "writer should preserve interface protocol");
    require(writtenInterface.value(QStringLiteral("config")).toObject()
                .value(QStringLiteral("data_width")).toInt() == 32,
            "writer should preserve interface config");
}

void testReaderRejectsOldSchemaAndAttachmentConnections() {
    QJsonObject oldSchema = minimalUartProject();
    oldSchema.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.noc.project.v1"));
    const ipcraft::core::ProjectDocumentReadResult oldResult =
        ipcraft::core::ProjectDocumentV1::readObject(oldSchema);
    require(!oldResult.success, "old NoC schema should be rejected");
    require(hasCode(oldResult.issues, QStringLiteral("project.unsupported_schema")),
            "old schema should report project.unsupported_schema");

    QJsonObject badConnection = minimalUartProject();
    badConnection.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("c_bad")},
                    {QStringLiteral("from"), QJsonObject{{QStringLiteral("component"), QStringLiteral("uart0")},
                                                         {QStringLiteral("interface"), QStringLiteral("serial")}}},
                    {QStringLiteral("to"), QJsonObject{{QStringLiteral("component"), QStringLiteral("uart0")},
                                                       {QStringLiteral("interface"), QStringLiteral("axi_s")}}},
                    {QStringLiteral("kind"), QStringLiteral("attachment")}}
    });
    const ipcraft::core::ProjectDocumentReadResult badConnectionResult =
        ipcraft::core::ProjectDocumentV1::readObject(badConnection);
    require(!badConnectionResult.success, "attachment connection should be rejected");
    require(hasCode(badConnectionResult.issues, QStringLiteral("project.attachment_connection_forbidden")),
            "attachment connection should report stable issue code");
}

void testReaderRejectsMalformedPackageShape() {
    QJsonObject malformedPackage = minimalUartProject();
    malformedPackage.insert(QStringLiteral("packages"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.uart16550")}}
    });

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(malformedPackage);
    require(!result.success, "package missing version should be rejected");
    require(hasCode(result.issues, QStringLiteral("package.missing_version")),
            "package missing version should report stable issue code");
}

void testReaderRejectsMissingRequiredProjectArrays() {
    QJsonObject missingArrays = minimalUartProject();
    missingArrays.remove(QStringLiteral("packages"));
    missingArrays.remove(QStringLiteral("components"));

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(missingArrays);
    require(!result.success, "missing required project arrays should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.missing_packages"),
                     QStringLiteral("/packages")),
            "missing packages should report stable issue code and path");
    require(hasIssue(result.issues,
                     QStringLiteral("project.missing_components"),
                     QStringLiteral("/components")),
            "missing components should report stable issue code and path");
}

void testReaderRejectsExtensionMissingVersion() {
    QJsonObject missingExtensionVersion = cpuNicNocProject();
    QJsonArray extensions = missingExtensionVersion.value(QStringLiteral("extensions")).toArray();
    QJsonObject extension = extensions.at(0).toObject();
    extension.remove(QStringLiteral("version"));
    extensions.replace(0, extension);
    missingExtensionVersion.insert(QStringLiteral("extensions"), extensions);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(missingExtensionVersion);
    require(!result.success, "extension missing version should be rejected");
    require(hasCode(result.issues, QStringLiteral("extension.missing_version")),
            "extension missing version should report stable issue code");
}

void testReaderRejectsNonObjectPackageEntry() {
    QJsonObject malformedPackage = minimalUartProject();
    malformedPackage.insert(QStringLiteral("packages"), QJsonArray{QStringLiteral("not_object")});

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(malformedPackage);
    require(!result.success, "non-object package entry should be rejected");
    require(hasCode(result.issues, QStringLiteral("project.invalid_package_shape")),
            "non-object package entry should report stable issue code");
}

void testReaderRejectsNonObjectInterfaceEntry() {
    QJsonObject malformedInterface = minimalUartProject();
    malformedInterface.insert(QStringLiteral("interfaces"), QJsonArray{QStringLiteral("not_object")});

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(malformedInterface);
    require(!result.success, "non-object interface entry should be rejected");
    require(hasCode(result.issues, QStringLiteral("project.invalid_interface_shape")),
            "non-object interface entry should report stable issue code");
}

void testReaderRejectsUnknownTopLevelField() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("legacyGraph"), QJsonObject{});

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "unknown top-level field should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.unknown_field"),
                     QStringLiteral("/legacyGraph")),
            "unknown top-level field should report stable issue code and path");
}

void testReaderRejectsNonArrayComponentsField() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("components"), QStringLiteral("not_array"));

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-array components field should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_components_shape"),
                     QStringLiteral("/components")),
            "non-array components field should report stable issue code and path");
}

void testReaderRejectsNonObjectComponentConfig() {
    QJsonObject project = minimalUartProject();
    QJsonArray components = project.value(QStringLiteral("components")).toArray();
    QJsonObject component = components.at(0).toObject();
    component.insert(QStringLiteral("config"), QStringLiteral("not_object"));
    components.replace(0, component);
    project.insert(QStringLiteral("components"), components);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-object component config should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_component_config_shape"),
                     QStringLiteral("/components/0/config")),
            "non-object component config should report stable issue code and path");
}

void testReaderRejectsNonObjectTopologyNodeEntry() {
    QJsonObject project = cpuNicNocProject();
    QJsonArray topologies = project.value(QStringLiteral("topologies")).toArray();
    QJsonObject topology = topologies.at(0).toObject();
    topology.insert(QStringLiteral("nodes"), QJsonArray{QStringLiteral("not_object")});
    topologies.replace(0, topology);
    project.insert(QStringLiteral("topologies"), topologies);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-object topology node entry should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_topology_node_shape"),
                     QStringLiteral("/topologies/0/nodes/0")),
            "non-object topology node entry should report stable issue code and path");
}

void testReaderRejectsNonObjectTopologyLinkEntry() {
    QJsonObject project = cpuNicNocProject();
    QJsonArray topologies = project.value(QStringLiteral("topologies")).toArray();
    QJsonObject topology = topologies.at(0).toObject();
    topology.insert(QStringLiteral("links"), QJsonArray{QStringLiteral("not_object")});
    topologies.replace(0, topology);
    project.insert(QStringLiteral("topologies"), topologies);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-object topology link entry should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_topology_link_shape"),
                     QStringLiteral("/topologies/0/links/0")),
            "non-object topology link entry should report stable issue code and path");
}

void testReaderRejectsNonObjectConnectionEndpoint() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("c_bad")},
                    {QStringLiteral("from"), QStringLiteral("not_object")},
                    {QStringLiteral("to"), QJsonObject{{QStringLiteral("component"), QStringLiteral("uart0")},
                                                       {QStringLiteral("interface"), QStringLiteral("serial")}}}}
    });

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-object connection endpoint should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_connection_endpoint_shape"),
                     QStringLiteral("/connections/0/from")),
            "non-object connection endpoint should report stable issue code and path");
}

void testReaderRejectsNonObjectViewTemplates() {
    QJsonObject project = minimalUartProject();
    QJsonArray views = project.value(QStringLiteral("views")).toArray();
    QJsonObject view = views.at(0).toObject();
    view.insert(QStringLiteral("templates"), QStringLiteral("bad"));
    views.replace(0, view);
    project.insert(QStringLiteral("views"), views);

    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(project);
    require(!result.success, "non-object view templates should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("project.invalid_view_templates_shape"),
                     QStringLiteral("/views/0/templates")),
            "non-object view templates should report stable issue code and path");
}

void testWriterIsDeterministic() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(cpuNicNocProject());
    require(result.success, "input should parse");

    const QByteArray first = QJsonDocument(ipcraft::core::ProjectDocumentV1::writeObject(result.project))
                                 .toJson(QJsonDocument::Indented);
    const QByteArray second = QJsonDocument(ipcraft::core::ProjectDocumentV1::writeObject(result.project))
                                  .toJson(QJsonDocument::Indented);
    require(first == second, "writer output should be deterministic");
}

} // namespace

int main() {
    testMinimalProjectRoundTripsStructurally();
    testViewOptionalObjectsRoundTripWithoutDataLoss();
    testCpuNicNocRoundTripsWithExtensionsAndAttachments();
    testInterfacesRoundTripAtTopLevel();
    testReaderRejectsOldSchemaAndAttachmentConnections();
    testReaderRejectsMalformedPackageShape();
    testReaderRejectsMissingRequiredProjectArrays();
    testReaderRejectsExtensionMissingVersion();
    testReaderRejectsNonObjectPackageEntry();
    testReaderRejectsNonObjectInterfaceEntry();
    testReaderRejectsUnknownTopLevelField();
    testReaderRejectsNonArrayComponentsField();
    testReaderRejectsNonObjectComponentConfig();
    testReaderRejectsNonObjectTopologyNodeEntry();
    testReaderRejectsNonObjectTopologyLinkEntry();
    testReaderRejectsNonObjectConnectionEndpoint();
    testReaderRejectsNonObjectViewTemplates();
    testWriterIsDeterministic();
    std::cout << "ipcraft_project_document_v1_foundation_test passed\n";
    return 0;
}
