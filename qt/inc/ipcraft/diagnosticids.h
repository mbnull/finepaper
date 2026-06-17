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

} // namespace ipcraft::diagnosticids
