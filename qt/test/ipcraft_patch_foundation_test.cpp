// Ipcraft ProjectPatch foundation contract tests.
#include "ipcraft/core/project_patch.h"
#include "ipcraft/patchops.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonObject>
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

void requireIssueCode(const QVector<ipcraft::core::ValidationIssue>& issues,
                      const QString& code,
                      const char* message) {
    require(hasCode(issues, code), message);
}

ipcraft::core::ProjectDesign minimalProject() {
    ipcraft::core::ProjectDesign project;
    project.schema = ipcraft::schemaids::projectV1;
    project.id = QStringLiteral("proj_uart_min");
    project.name = QStringLiteral("Minimal UART");

    project.packages.append({QStringLiteral("vendor.uart16550"), QStringLiteral("1.0.0")});

    ipcraft::core::ComponentInstance uart;
    uart.id = QStringLiteral("uart0");
    uart.type = QStringLiteral("uart16550");
    uart.packageRef = QStringLiteral("vendor.uart16550@1.0.0");
    uart.config.insert(QStringLiteral("baud"), 115200);
    project.components.append(uart);

    return project;
}

QJsonObject setBaudOperationJson() {
    return QJsonObject{
        {QStringLiteral("op"), ipcraft::patchops::componentConfigSet},
        {QStringLiteral("target"), QStringLiteral("component:uart0")},
        {QStringLiteral("path"), QStringLiteral("/baud")},
        {QStringLiteral("value"), 921600}
    };
}

ipcraft::core::PatchOperation setBaudOperation() {
    ipcraft::core::PatchOperation operation;
    operation.op = ipcraft::patchops::componentConfigSet;
    operation.target = QStringLiteral("component:uart0");
    operation.path = QStringLiteral("/baud");
    operation.value = 921600;
    operation.payload = setBaudOperationJson();
    return operation;
}

QJsonObject setBaudPatchJson() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::patchV1},
        {QStringLiteral("id"), QStringLiteral("patch_set_uart_baud")},
        {QStringLiteral("description"), QStringLiteral("Set UART baud rate")},
        {QStringLiteral("ops"), QJsonArray{
            setBaudOperationJson()
        }}
    };
}

QJsonObject patchJsonWithOps(const QJsonArray& ops) {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::patchV1},
        {QStringLiteral("ops"), ops}
    };
}

QJsonObject componentPayload(const QString& componentId) {
    return QJsonObject{
        {QStringLiteral("id"), componentId},
        {QStringLiteral("type"), QStringLiteral("uart16550")},
        {QStringLiteral("packageRef"), QStringLiteral("vendor.uart16550")},
        {QStringLiteral("config"), QJsonObject{
            {QStringLiteral("frequency"), 100}
        }},
        {QStringLiteral("identity"), QJsonObject{
            {QStringLiteral("label"), componentId}
        }},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("role"), QStringLiteral("test")}
        }},
        {QStringLiteral("extensionData"), QJsonObject{
            {QStringLiteral("vendor"), true}
        }}
    };
}

QJsonObject componentAddOperationJson(const QString& componentId) {
    return QJsonObject{
        {QStringLiteral("op"), ipcraft::patchops::componentAdd},
        {QStringLiteral("target"), QStringLiteral("component")},
        {QStringLiteral("path"), QStringLiteral("/components/-")},
        {QStringLiteral("payload"), componentPayload(componentId)}
    };
}

ipcraft::core::ProjectDesign twoComponentProject() {
    ipcraft::core::ProjectDesign project = minimalProject();

    ipcraft::core::ComponentInstance dma;
    dma.id = QStringLiteral("dma0");
    dma.type = QStringLiteral("uart16550");
    dma.packageRef = QStringLiteral("vendor.uart16550@1.0.0");
    project.components.append(dma);

    return project;
}

ipcraft::core::ProjectDesign projectWithCanvasView() {
    ipcraft::core::ProjectDesign project = minimalProject();

    ipcraft::core::ViewDocument view;
    view.id = QStringLiteral("canvas");
    view.schema = ipcraft::schemaids::viewV1;
    view.kind = QStringLiteral("canvas");
    view.targetRef = QStringLiteral("project");
    view.providerRef = QStringLiteral("finepaper.editor");
    project.views.append(view);

    return project;
}

qsizetype componentIndexById(const ipcraft::core::ProjectDesign& project,
                             const QString& componentId) {
    for (qsizetype index = 0; index < project.components.size(); ++index) {
        if (project.components.at(index).id == componentId) {
            return index;
        }
    }
    return -1;
}

qsizetype connectionIndexById(const ipcraft::core::ProjectDesign& project,
                              const QString& connectionId) {
    for (qsizetype index = 0; index < project.connections.size(); ++index) {
        if (project.connections.at(index).id == connectionId) {
            return index;
        }
    }
    return -1;
}

qsizetype viewIndexById(const ipcraft::core::ProjectDesign& project,
                        const QString& viewId) {
    for (qsizetype index = 0; index < project.views.size(); ++index) {
        if (project.views.at(index).id == viewId) {
            return index;
        }
    }
    return -1;
}

qsizetype topologyIndexById(const ipcraft::core::ProjectDesign& project,
                            const QString& topologyId) {
    for (qsizetype index = 0; index < project.topologies.size(); ++index) {
        if (project.topologies.at(index).id == topologyId) {
            return index;
        }
    }
    return -1;
}

ipcraft::core::ProjectPatch readPatchOrThrow(const QJsonObject& patchJson,
                                             const char* message) {
    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(result.success, message);
    return result.patch;
}

void expectReadFailsWith(const QJsonObject& patchJson,
                         const QString& code,
                         const char* message) {
    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(!result.success, message);
    requireIssueCode(result.issues, code, message);
}

void expectApplyFailsWith(const QJsonObject& patchJson,
                          const QString& code,
                          const char* message) {
    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "patch should parse before apply rejection");
    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(minimalProject(), patch);
    require(!result.success, message);
    requireIssueCode(result.issues, code, message);
}

void testPatchOpConstantsExposeCurrentNames() {
    require(ipcraft::patchops::componentAdd == QStringLiteral("component.add"),
            "component add op name should be stable");
    require(ipcraft::patchops::componentRemove == QStringLiteral("component.remove"),
            "component remove op name should be stable");
    require(ipcraft::patchops::componentConfigSet == QStringLiteral("component.config.set"),
            "component config set op name should be stable");
    require(ipcraft::patchops::componentConfigUnset == QStringLiteral("component.config.unset"),
            "component config unset op name should be stable");
    require(ipcraft::patchops::connectionAdd == QStringLiteral("connection.add"),
            "connection add op name should be stable");
    require(ipcraft::patchops::connectionRemove == QStringLiteral("connection.remove"),
            "connection remove op name should be stable");
    require(ipcraft::patchops::connectionConfigSet == QStringLiteral("connection.config.set"),
            "connection config set op name should be stable");
    require(ipcraft::patchops::connectionMetadataSet == QStringLiteral("connection.metadata.set"),
            "connection metadata set op name should be stable");
    require(ipcraft::patchops::connectionClassSet == QStringLiteral("connection.class.set"),
            "connection class set op name should be stable");
    require(ipcraft::patchops::viewLayoutSet == QStringLiteral("view.layout.set"),
            "view layout set op name should be stable");
    require(ipcraft::patchops::viewNodePositionSet == QStringLiteral("view.node_position.set"),
            "view node position set op name should be stable");
    require(ipcraft::patchops::topologyAddOrUpdate == QStringLiteral("topology.add_or_update"),
            "topology add-or-update op name should be stable");
    require(ipcraft::patchops::topologyRemove == QStringLiteral("topology.remove"),
            "topology remove op name should be stable");
}

void testPatchParsesAndSerializes() {
    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(setBaudPatchJson());
    require(result.success, "set-config patch should parse");
    const ipcraft::core::ProjectPatch patch = result.patch;
    require(patch.ops.size() == 1, "patch should contain one operation");
    require(patch.ops.first().op == ipcraft::patchops::componentConfigSet,
            "patch operation name should parse");

    const QJsonObject written = ipcraft::core::ProjectPatchApi::writeObject(patch);
    require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::patchV1,
            "writer should emit patch schema");
    require(written.value(QStringLiteral("ops")).toArray().size() == 1,
            "writer should emit one operation");
}

void testPatchReadRejectsInvalidFoundationJson() {
    QJsonObject unsupportedSchemaPatch = setBaudPatchJson();
    unsupportedSchemaPatch.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.patch.v0"));
    expectReadFailsWith(unsupportedSchemaPatch,
                        QStringLiteral("patch.unsupported_schema"),
                        "unsupported patch schema should be rejected");

    QJsonObject emptyOpsPatch = setBaudPatchJson();
    emptyOpsPatch.insert(QStringLiteral("ops"), QJsonArray{});
    expectReadFailsWith(emptyOpsPatch,
                        QStringLiteral("patch.empty_ops"),
                        "empty ops should be rejected");

    QJsonObject malformedOpPatch = setBaudPatchJson();
    malformedOpPatch.insert(QStringLiteral("ops"),
                            QJsonArray{QStringLiteral("not_an_object")});
    expectReadFailsWith(malformedOpPatch,
                        QStringLiteral("patch.invalid_op"),
                        "non-object ops should be rejected");

    QJsonObject malformedMetadataPatch = setBaudPatchJson();
    malformedMetadataPatch.insert(QStringLiteral("metadata"), QStringLiteral("not_object"));
    expectReadFailsWith(malformedMetadataPatch,
                        QStringLiteral("patch.invalid_metadata_shape"),
                        "non-object metadata should be rejected");
}

void testPatchReadRejectsUnknownTopLevelField() {
    QJsonObject patchJson = setBaudPatchJson();
    patchJson.insert(QStringLiteral("legacyCommand"), QJsonObject{});

    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(!result.success, "unknown top-level patch field should be rejected");
    require(hasIssue(result.issues,
                     QStringLiteral("patch.unknown_field"),
                     QStringLiteral("/legacyCommand")),
            "unknown top-level patch field should report stable issue code and path");
}

void testPatchApplyRejectsDirectUnsupportedSchemaWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::ProjectPatch patch;
    patch.schema = QStringLiteral("ipcraft.patch.v0");
    patch.ops.append(setBaudOperation());

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "direct unsupported schema patch should be rejected");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.unsupported_schema"),
                     "direct unsupported schema patch should report stable issue code");
    require(project.components.first().config[QStringLiteral("baud")] == 115200,
            "direct unsupported schema rejection should not mutate original project");
    require(result.project.components.first().config == project.components.first().config,
            "direct unsupported schema rejection should return the original project");
}

void testPatchApplyRejectsDirectEmptyOpsWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "direct empty ops patch should be rejected");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.empty_ops"),
                     "direct empty ops patch should report stable issue code");
    require(project.components.first().config[QStringLiteral("baud")] == 115200,
            "direct empty ops rejection should not mutate original project");
    require(result.project.components.first().config == project.components.first().config,
            "direct empty ops rejection should return the original project");
}

void testPatchApplyRejectsDirectSetConfigMissingValueWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;

    ipcraft::core::PatchOperation operation;
    operation.op = ipcraft::patchops::componentConfigSet;
    operation.target = QStringLiteral("component:uart0");
    operation.path = QStringLiteral("/baud");
    patch.ops.append(operation);

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "direct component.config.set without value should be rejected");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.invalid_op"),
                     "direct component.config.set without value should report stable issue code");
    require(project.components.first().config[QStringLiteral("baud")] == 115200,
            "direct component.config.set missing-value rejection should not mutate original project");
    require(result.project.components.first().config == project.components.first().config,
            "direct component.config.set missing-value rejection should return the original project");
}

void testPatchApplyRejectsUnsupportedOperationForms() {
    QJsonObject unsupportedOpPatch = setBaudPatchJson();
    QJsonArray ops = unsupportedOpPatch.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("op"), QStringLiteral("remove_config"));
    ops.replace(0, op);
    unsupportedOpPatch.insert(QStringLiteral("ops"), ops);
    expectApplyFailsWith(unsupportedOpPatch,
                         QStringLiteral("patch.unsupported_op"),
                         "unsupported operation should be rejected");

    QJsonObject moduleTargetPatch = setBaudPatchJson();
    ops = moduleTargetPatch.value(QStringLiteral("ops")).toArray();
    op = ops.first().toObject();
    op.insert(QStringLiteral("target"), QStringLiteral("module:uart0"));
    ops.replace(0, op);
    moduleTargetPatch.insert(QStringLiteral("ops"), ops);
    expectApplyFailsWith(moduleTargetPatch,
                         QStringLiteral("patch.invalid_target"),
                         "non-component target grammar should be rejected");

    QJsonObject emptyComponentTargetPatch = setBaudPatchJson();
    ops = emptyComponentTargetPatch.value(QStringLiteral("ops")).toArray();
    op = ops.first().toObject();
    op.insert(QStringLiteral("target"), QStringLiteral("component:"));
    ops.replace(0, op);
    emptyComponentTargetPatch.insert(QStringLiteral("ops"), ops);
    expectApplyFailsWith(emptyComponentTargetPatch,
                         QStringLiteral("patch.invalid_target"),
                         "empty component target id should be rejected");

    QJsonObject multiLevelPathPatch = setBaudPatchJson();
    ops = multiLevelPathPatch.value(QStringLiteral("ops")).toArray();
    op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/nested/baud"));
    ops.replace(0, op);
    multiLevelPathPatch.insert(QStringLiteral("ops"), ops);
    expectApplyFailsWith(multiLevelPathPatch,
                         QStringLiteral("patch.unsupported_path"),
                         "multi-level config paths should be rejected");

    QJsonObject missingValuePatch = setBaudPatchJson();
    ops = missingValuePatch.value(QStringLiteral("ops")).toArray();
    op = ops.first().toObject();
    op.remove(QStringLiteral("value"));
    ops.replace(0, op);
    missingValuePatch.insert(QStringLiteral("ops"), ops);
    expectApplyFailsWith(missingValuePatch,
                         QStringLiteral("patch.invalid_op"),
                         "set_config without value should be rejected");
}

void testPatchSerializationPreservesMetadataAndUnknownPayload() {
    QJsonObject patchJson = setBaudPatchJson();
    patchJson.insert(QStringLiteral("author"), QStringLiteral("foundation_author"));

    const QJsonObject metadata{
        {QStringLiteral("author"), QStringLiteral("foundation_test")},
        {QStringLiteral("ticket"), QStringLiteral("TASK-4")}
    };
    patchJson.insert(QStringLiteral("metadata"), metadata);

    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("trace"), QStringLiteral("review-feedback"));
    op.insert(QStringLiteral("flags"), QJsonArray{QStringLiteral("preserve")});
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "metadata patch should parse");
    require(patch.author == QStringLiteral("foundation_author"),
            "top-level patch author should parse");

    const QJsonObject written = ipcraft::core::ProjectPatchApi::writeObject(patch);
    require(written.value(QStringLiteral("author")).toString() ==
                QStringLiteral("foundation_author"),
            "top-level patch author should round-trip through read/write");
    require(written.value(QStringLiteral("metadata")).toObject() == metadata,
            "patch metadata should round-trip through read/write");

    const QJsonObject writtenOp =
        written.value(QStringLiteral("ops")).toArray().first().toObject();
    require(writtenOp.value(QStringLiteral("trace")).toString() ==
                QStringLiteral("review-feedback"),
            "unknown operation string fields should round-trip");
    require(writtenOp.value(QStringLiteral("flags")).toArray().first().toString() ==
                QStringLiteral("preserve"),
            "unknown operation array fields should round-trip");
}

void testPatchSerializationDropsStaleKnownPayloadFields() {
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;

    ipcraft::core::PatchOperation operation;
    operation.value = QJsonValue(QJsonValue::Undefined);
    operation.payload = QJsonObject{
        {QStringLiteral("op"), QStringLiteral("stale_op")},
        {QStringLiteral("target"), QStringLiteral("component:stale")},
        {QStringLiteral("path"), QStringLiteral("/stale")},
        {QStringLiteral("value"), QStringLiteral("stale")},
        {QStringLiteral("unknown"), QStringLiteral("preserved")}
    };
    patch.ops.append(operation);

    const QJsonObject written = ipcraft::core::ProjectPatchApi::writeObject(patch);
    const QJsonObject writtenOp =
        written.value(QStringLiteral("ops")).toArray().first().toObject();
    require(!writtenOp.contains(QStringLiteral("op")),
            "writer should not emit stale payload op when structured op is empty");
    require(!writtenOp.contains(QStringLiteral("target")),
            "writer should not emit stale payload target when structured target is empty");
    require(!writtenOp.contains(QStringLiteral("path")),
            "writer should not emit stale payload path when structured path is empty");
    require(!writtenOp.contains(QStringLiteral("value")),
            "writer should not emit stale payload value when structured value is undefined");
    require(writtenOp.value(QStringLiteral("unknown")).toString() ==
                QStringLiteral("preserved"),
            "writer should preserve unknown payload fields");
}

void testPatchAppliesSetConfigTransactionally() {
    ipcraft::core::ProjectDesign project = minimalProject();
    const ipcraft::core::ProjectPatchReadResult patchResult =
        ipcraft::core::ProjectPatchApi::readObject(setBaudPatchJson());
    require(patchResult.success, "set-config patch should parse before apply");
    const ipcraft::core::ProjectPatch patch = patchResult.patch;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "set-config patch should apply");
    require(result.project.components.first().config[QStringLiteral("baud")] == 921600,
            "set-config patch should update component config");
}

void testPatchAppliesNamedComponentOperations() {
    ipcraft::core::ProjectDesign project = minimalProject();

    const QJsonObject setConfigOp{
        {QStringLiteral("op"), ipcraft::patchops::componentConfigSet},
        {QStringLiteral("target"), QStringLiteral("component:timer0")},
        {QStringLiteral("path"), QStringLiteral("/depth")},
        {QStringLiteral("value"), 8}
    };
    const QJsonObject unsetConfigOp{
        {QStringLiteral("op"), ipcraft::patchops::componentConfigUnset},
        {QStringLiteral("target"), QStringLiteral("component:timer0")},
        {QStringLiteral("path"), QStringLiteral("/frequency")}
    };

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{componentAddOperationJson(QStringLiteral("timer0")),
                                    setConfigOp,
                                    unsetConfigOp}),
        "named component operations patch should parse");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "named component operations should apply");

    const qsizetype timerIndex = componentIndexById(result.project, QStringLiteral("timer0"));
    require(timerIndex >= 0, "component.add should append the named component");
    const ipcraft::core::ComponentInstance& timer = result.project.components.at(timerIndex);
    require(timer.packageRef == QStringLiteral("vendor.uart16550@1.0.0"),
            "component.add should resolve bare package refs");
    require(timer.config.value(QStringLiteral("depth")).toInt() == 8,
            "component.config.set should write the decoded config field");
    require(!timer.config.contains(QStringLiteral("frequency")),
            "component.config.unset should remove the decoded config field");

    const ipcraft::core::ProjectPatch removePatch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{QJsonObject{
            {QStringLiteral("op"), ipcraft::patchops::componentRemove},
            {QStringLiteral("target"), QStringLiteral("component:timer0")}
        }}),
        "named component remove patch should parse");

    const ipcraft::core::PatchApplyResult removeResult =
        ipcraft::core::applyPatch(result.project, removePatch);
    require(removeResult.success, "component.remove should apply");
    require(componentIndexById(removeResult.project, QStringLiteral("timer0")) < 0,
            "component.remove should remove the targeted component");
}

void testPatchAppliesNamedConnectionOperations() {
    ipcraft::core::ProjectDesign project = twoComponentProject();
    const QJsonObject connectionPayload{
        {QStringLiteral("id"), QStringLiteral("uart_dma")},
        {QStringLiteral("from"), QJsonObject{
            {QStringLiteral("component"), QStringLiteral("uart0")},
            {QStringLiteral("interface"), QStringLiteral("tx")}
        }},
        {QStringLiteral("to"), QJsonObject{
            {QStringLiteral("component"), QStringLiteral("dma0")},
            {QStringLiteral("interface"), QStringLiteral("rx")}
        }},
        {QStringLiteral("kind"), QStringLiteral("interface")},
        {QStringLiteral("class"), QStringLiteral("stream")},
        {QStringLiteral("status"), QStringLiteral("valid")},
        {QStringLiteral("config"), QJsonObject{
            {QStringLiteral("latency"), 1}
        }},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("origin"), QStringLiteral("foundation")}
        }},
        {QStringLiteral("extensionData"), QJsonObject{
            {QStringLiteral("vendor"), true}
        }}
    };

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::connectionAdd},
                {QStringLiteral("target"), QStringLiteral("connection")},
                {QStringLiteral("path"), QStringLiteral("/connections/-")},
                {QStringLiteral("payload"), connectionPayload}
            },
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::connectionConfigSet},
                {QStringLiteral("target"), QStringLiteral("connection:uart_dma")},
                {QStringLiteral("path"), QStringLiteral("/bandwidth")},
                {QStringLiteral("value"), 64}
            },
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::connectionMetadataSet},
                {QStringLiteral("target"), QStringLiteral("connection:uart_dma")},
                {QStringLiteral("path"), QStringLiteral("/owner")},
                {QStringLiteral("value"), QStringLiteral("patch-test")}
            },
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::connectionClassSet},
                {QStringLiteral("target"), QStringLiteral("connection:uart_dma")},
                {QStringLiteral("path"), QStringLiteral("/class")},
                {QStringLiteral("value"), QStringLiteral("axi-stream")}
            }
        }),
        "named connection operations patch should parse");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "named connection operations should apply");
    require(result.project.connections.size() == 1, "connection.add should append one connection");
    const ipcraft::core::Connection& connection = result.project.connections.first();
    require(connection.from.component == QStringLiteral("uart0") &&
                connection.from.interface == QStringLiteral("tx"),
            "connection.add should copy the source endpoint");
    require(connection.to.component == QStringLiteral("dma0") &&
                connection.to.interface == QStringLiteral("rx"),
            "connection.add should copy the target endpoint");
    require(connection.config.value(QStringLiteral("latency")).toInt() == 1,
            "connection.add should copy config");
    require(connection.config.value(QStringLiteral("bandwidth")).toInt() == 64,
            "connection.config.set should write the decoded config field");
    require(connection.metadata.value(QStringLiteral("origin")).toString() ==
                QStringLiteral("foundation"),
            "connection.add should copy metadata");
    require(connection.metadata.value(QStringLiteral("owner")).toString() ==
                QStringLiteral("patch-test"),
            "connection.metadata.set should write the decoded metadata field");
    require(connection.metadata.value(QStringLiteral("class")).toString() ==
                QStringLiteral("axi-stream"),
            "connection.class.set should write the class metadata field");
    require(connection.metadata.value(QStringLiteral("status")).toString() ==
                QStringLiteral("valid"),
            "connection.add should preserve status metadata");
    require(connection.metadata.value(QStringLiteral("extensionData")).toObject()
                .value(QStringLiteral("vendor")).toBool(),
            "connection.add should preserve extensionData metadata");

    const ipcraft::core::ProjectPatch removePatch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{QJsonObject{
            {QStringLiteral("op"), ipcraft::patchops::connectionRemove},
            {QStringLiteral("target"), QStringLiteral("connection:uart_dma")}
        }}),
        "named connection remove patch should parse");

    const ipcraft::core::PatchApplyResult removeResult =
        ipcraft::core::applyPatch(result.project, removePatch);
    require(removeResult.success, "connection.remove should apply");
    require(connectionIndexById(removeResult.project, QStringLiteral("uart_dma")) < 0,
            "connection.remove should remove the targeted connection");
}

void testPatchAppliesNamedViewAndTopologyOperations() {
    ipcraft::core::ProjectDesign project = projectWithCanvasView();
    const QJsonObject topologyPayload{
        {QStringLiteral("id"), QStringLiteral("mesh_topo")},
        {QStringLiteral("schema"), ipcraft::schemaids::topologyGraphV1},
        {QStringLiteral("kind"), QStringLiteral("explicit_graph")},
        {QStringLiteral("providerRef"), QStringLiteral("finepaper.topology")},
        {QStringLiteral("nodes"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("n0")}
        }}},
        {QStringLiteral("links"), QJsonArray{}},
        {QStringLiteral("attachments"), QJsonArray{}},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("foundation")}
        }}
    };

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::viewLayoutSet},
                {QStringLiteral("target"), QStringLiteral("view:canvas")},
                {QStringLiteral("path"), QStringLiteral("/layout")},
                {QStringLiteral("value"), QJsonObject{
                    {QStringLiteral("canvas"), QJsonObject{{QStringLiteral("zoom"), 1.25}}}
                }}
            },
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::viewNodePositionSet},
                {QStringLiteral("target"), QStringLiteral("view:canvas")},
                {QStringLiteral("path"), QStringLiteral("/nodes/uart0")},
                {QStringLiteral("value"), QJsonObject{
                    {QStringLiteral("x"), 42},
                    {QStringLiteral("y"), 64}
                }}
            },
            QJsonObject{
                {QStringLiteral("op"), ipcraft::patchops::topologyAddOrUpdate},
                {QStringLiteral("target"), QStringLiteral("topology")},
                {QStringLiteral("payload"), topologyPayload}
            }
        }),
        "named view and topology patch should parse");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "named view and topology operations should apply");

    const qsizetype viewIndex = viewIndexById(result.project, QStringLiteral("canvas"));
    require(viewIndex >= 0, "view should still exist after layout updates");
    const QJsonObject viewLayout = result.project.views.at(viewIndex).layout;
    require(viewLayout.value(QStringLiteral("canvas")).toObject()
                .value(QStringLiteral("zoom")).toDouble() == 1.25,
            "view.layout.set should replace the view layout object");
    require(viewLayout.value(QStringLiteral("nodes")).toObject()
                .value(QStringLiteral("uart0")).toObject()
                .value(QStringLiteral("x")).toInt() == 42,
            "view.node_position.set should write x into view layout nodes");
    require(!result.project.components.first().config.contains(QStringLiteral("x")),
            "view.node_position.set should not write layout fields into component config");

    const qsizetype topologyIndex = topologyIndexById(result.project, QStringLiteral("mesh_topo"));
    require(topologyIndex >= 0, "topology.add_or_update should append a new topology");
    require(result.project.topologies.at(topologyIndex).metadata
                .value(QStringLiteral("source")).toString() == QStringLiteral("foundation"),
            "topology.add_or_update should copy topology metadata");

    const ipcraft::core::ProjectPatch removePatch = readPatchOrThrow(
        patchJsonWithOps(QJsonArray{QJsonObject{
            {QStringLiteral("op"), ipcraft::patchops::topologyRemove},
            {QStringLiteral("target"), QStringLiteral("topology:mesh_topo")}
        }}),
        "named topology remove patch should parse");

    const ipcraft::core::PatchApplyResult removeResult =
        ipcraft::core::applyPatch(result.project, removePatch);
    require(removeResult.success, "topology.remove should apply");
    require(topologyIndexById(removeResult.project, QStringLiteral("mesh_topo")) < 0,
            "topology.remove should remove the targeted topology");
}

void testPatchApplyDecodesJsonPointerTildeEscapeInConfigKey() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/clock~0rate"));
    op.insert(QStringLiteral("value"), 100);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "escaped-tilde patch should parse before apply");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "escaped tilde config path should apply");
    require(result.project.components.first().config[QStringLiteral("clock~rate")] == 100,
            "escaped tilde should write decoded config key");
    require(!result.project.components.first().config.contains(QStringLiteral("clock~0rate")),
            "escaped tilde should not write the encoded path text");
}

void testPatchApplyDecodesJsonPointerSlashEscapeAsSingleSegment() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/lane~1id"));
    op.insert(QStringLiteral("value"), QStringLiteral("rx/0"));
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "escaped-slash patch should parse before apply");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "escaped slash config path should apply as one segment");
    require(result.project.components.first().config[QStringLiteral("lane/id")] ==
                QStringLiteral("rx/0"),
            "escaped slash should write decoded config key");
    require(!result.project.components.first().config.contains(QStringLiteral("lane~1id")),
            "escaped slash should not write the encoded path text");
}

void testPatchRejectsMalformedJsonPointerEscapeDigitWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/bad~2escape"));
    op.insert(QStringLiteral("value"), 100);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "malformed-escape patch should parse before apply");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "unsupported JSON Pointer escape should be rejected");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.unsupported_path"),
                     "unsupported JSON Pointer escape should report stable issue code");
    require(result.project.components.first().config == project.components.first().config,
            "malformed escape rejection should return the original project");
}

void testPatchRejectsTrailingJsonPointerEscapeWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/bad~"));
    op.insert(QStringLiteral("value"), 100);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "trailing-escape patch should parse before apply");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "trailing JSON Pointer escape should be rejected");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.unsupported_path"),
                     "trailing JSON Pointer escape should report stable issue code");
    require(result.project.components.first().config == project.components.first().config,
            "trailing escape rejection should return the original project");
}

void testPatchRejectsInvalidTargetWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("target"), QStringLiteral("component:missing"));
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatchReadResult patchResult =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(patchResult.success, "missing-target patch should parse before apply");
    const ipcraft::core::ProjectPatch patch = patchResult.patch;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "missing component target should be rejected");
    require(hasCode(result.issues, QStringLiteral("patch.target_not_found")),
            "missing target should report stable issue code");
    require(result.project.components.first().config[QStringLiteral("baud")] == 115200,
            "failed patch should not mutate original project");
}

void testPatchRollsBackWhenLaterOperationFails() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject firstOp = setBaudOperationJson();
    firstOp.insert(QStringLiteral("value"), 9600);

    QJsonObject failingOp = setBaudOperationJson();
    failingOp.insert(QStringLiteral("path"), QStringLiteral("/nested/baud"));
    failingOp.insert(QStringLiteral("value"), 4800);

    QJsonObject patchJson;
    patchJson.insert(QStringLiteral("schema"), ipcraft::schemaids::patchV1);
    patchJson.insert(QStringLiteral("ops"), QJsonArray{firstOp, failingOp});
    const ipcraft::core::ProjectPatch patch = readPatchOrThrow(
        patchJson,
        "multi-op rollback patch should parse");

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "failed later operation should reject whole patch");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.unsupported_path"),
                     "failed later operation should report its issue code");
    require(project.components.first().config[QStringLiteral("baud")] == 115200,
            "failed multi-op patch should not mutate the original project");
    require(result.project.components.first().config[QStringLiteral("baud")] == 115200,
            "failed multi-op patch should return the original project state");
}

void testPatchRejectsLayoutFieldInsideComponentConfig() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/x"));
    op.insert(QStringLiteral("value"), 100);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatchReadResult patchResult =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(patchResult.success, "layout-field patch should parse before apply");
    const ipcraft::core::ProjectPatch patch = patchResult.patch;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "layout fields should not be accepted in component config");
    require(hasCode(result.issues, QStringLiteral("patch.layout_in_component_config")),
            "layout field should report stable patch issue code");
    require(!project.components.first().config.contains(QStringLiteral("x")),
            "layout-field rejection should not mutate the original project");
    require(result.project.components.first().config == project.components.first().config,
            "layout-field rejection should return the original project");
}

void testPatchRejectsInsertedValueContainingLayoutField() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    QJsonObject insertedValue;
    insertedValue.insert(QStringLiteral("x"), 1);
    op.insert(QStringLiteral("path"), QStringLiteral("/display"));
    op.insert(QStringLiteral("value"), insertedValue);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatchReadResult patchResult =
        ipcraft::core::ProjectPatchApi::readObject(patchJson);
    require(patchResult.success, "nested-layout patch should parse before apply");
    const ipcraft::core::ProjectPatch patch = patchResult.patch;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "inserted layout fields should be rejected during patch apply");
    require(hasCode(result.issues, QStringLiteral("patch.layout_in_component_config")),
            "inserted layout field should report stable patch issue code");
    require(!hasCode(result.issues, QStringLiteral("project.layout_in_component_config")),
            "inserted layout field should not fall through to project validation");
    require(result.issues.first().path == QStringLiteral("/ops/0/value"),
            "inserted layout field should report the patch value path");
    require(!project.components.first().config.contains(QStringLiteral("display")),
            "inserted-layout rejection should not mutate the original project");
    require(result.project.components.first().config == project.components.first().config,
            "inserted-layout rejection should return the original project");
}

} // namespace

int main() {
    testPatchOpConstantsExposeCurrentNames();
    testPatchParsesAndSerializes();
    testPatchReadRejectsInvalidFoundationJson();
    testPatchReadRejectsUnknownTopLevelField();
    testPatchApplyRejectsDirectUnsupportedSchemaWithoutMutation();
    testPatchApplyRejectsDirectEmptyOpsWithoutMutation();
    testPatchApplyRejectsDirectSetConfigMissingValueWithoutMutation();
    testPatchApplyRejectsUnsupportedOperationForms();
    testPatchSerializationPreservesMetadataAndUnknownPayload();
    testPatchSerializationDropsStaleKnownPayloadFields();
    testPatchAppliesSetConfigTransactionally();
    testPatchAppliesNamedComponentOperations();
    testPatchAppliesNamedConnectionOperations();
    testPatchAppliesNamedViewAndTopologyOperations();
    testPatchApplyDecodesJsonPointerTildeEscapeInConfigKey();
    testPatchApplyDecodesJsonPointerSlashEscapeAsSingleSegment();
    testPatchRejectsMalformedJsonPointerEscapeDigitWithoutMutation();
    testPatchRejectsTrailingJsonPointerEscapeWithoutMutation();
    testPatchRejectsInvalidTargetWithoutMutation();
    testPatchRollsBackWhenLaterOperationFails();
    testPatchRejectsLayoutFieldInsideComponentConfig();
    testPatchRejectsInsertedValueContainingLayoutField();

    std::cout << "ipcraft_patch_foundation_test passed\n";
    return 0;
}
