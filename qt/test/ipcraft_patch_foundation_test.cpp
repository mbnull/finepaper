// Ipcraft ProjectPatch foundation contract tests.
#include "ipcraft/core/project_patch.h"
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
        {QStringLiteral("op"), QStringLiteral("set_config")},
        {QStringLiteral("target"), QStringLiteral("component:uart0")},
        {QStringLiteral("path"), QStringLiteral("/baud")},
        {QStringLiteral("value"), 921600}
    };
}

ipcraft::core::PatchOperation setBaudOperation() {
    ipcraft::core::PatchOperation operation;
    operation.op = QStringLiteral("set_config");
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

void testPatchParsesAndSerializes() {
    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(setBaudPatchJson());
    require(result.success, "set-config patch should parse");
    const ipcraft::core::ProjectPatch patch = result.patch;
    require(patch.ops.size() == 1, "patch should contain one operation");
    require(patch.ops.first().op == QStringLiteral("set_config"),
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
    const QJsonObject written = ipcraft::core::ProjectPatchApi::writeObject(patch);
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
}

} // namespace

int main() {
    testPatchParsesAndSerializes();
    testPatchReadRejectsInvalidFoundationJson();
    testPatchApplyRejectsDirectUnsupportedSchemaWithoutMutation();
    testPatchApplyRejectsDirectEmptyOpsWithoutMutation();
    testPatchApplyRejectsUnsupportedOperationForms();
    testPatchSerializationPreservesMetadataAndUnknownPayload();
    testPatchSerializationDropsStaleKnownPayloadFields();
    testPatchAppliesSetConfigTransactionally();
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
