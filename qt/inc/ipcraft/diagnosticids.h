#pragma once

#include <QString>

namespace ipcraft::diagnosticids {

inline QString flowCommandPolicyViolation() {
    return QStringLiteral("flow.command_policy_violation");
}

inline QString flowExecutableMissing() { return QStringLiteral("flow.executable_missing"); }
inline QString flowExecFailed() { return QStringLiteral("flow.exec_failed"); }
inline QString flowTimeout() { return QStringLiteral("flow.timeout"); }
inline QString flowOutputTruncated() { return QStringLiteral("flow.output_truncated"); }
inline QString flowUnknownFlow() { return QStringLiteral("flow.unknown_flow"); }
inline QString flowInputsManifestMissing() { return QStringLiteral("flow.inputs_manifest_missing"); }
inline QString flowInputsManifestModified() { return QStringLiteral("flow.inputs_manifest_modified"); }
inline QString flowPluginUnavailable() { return QStringLiteral("flow.plugin_unavailable"); }
inline QString editorProjectionFailed() { return QStringLiteral("editor.projection_failed"); }
inline QString packageExtensionRequired() { return QStringLiteral("package.extension_required"); }
inline QString packageUnknownExtension() { return QStringLiteral("package.unknown_extension"); }
inline QString projectUnsupportedSchema() { return QStringLiteral("project.unsupported_schema"); }
inline QString projectInvalidJson() { return QStringLiteral("project.invalid_json"); }
inline QString projectMissingRequired() { return QStringLiteral("project.missing_required"); }
inline QString patchUnsupportedSchema() { return QStringLiteral("patch.unsupported_schema"); }
inline QString patchUnsupportedOp() { return QStringLiteral("patch.unsupported_op"); }
inline QString patchInvalidTarget() { return QStringLiteral("patch.invalid_target"); }
inline QString patchTargetNotFound() { return QStringLiteral("patch.target_not_found"); }

} // namespace ipcraft::diagnosticids
