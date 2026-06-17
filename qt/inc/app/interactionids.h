#pragma once

#include <QString>

namespace app::interactionids {

inline QString packageCoverageInteractions() {
    return QStringLiteral("finepaper.package.coverage-interactions");
}

inline QString packageCoverageInteractionHandler() {
    return QStringLiteral("finepaper.package.coverage-interaction-handler");
}

inline QString packageCoverageInspector() {
    return QStringLiteral("finepaper.package.coverage-inspector");
}

inline QString nocTopologyProvider() {
    return QStringLiteral("finepaper.noc-plugin.topology-provider");
}

inline QString nocTopologyPresetHandler() {
    return QStringLiteral("finepaper.noc-plugin.topology-preset-handler");
}

inline QString workspaceInteraction() {
    return QStringLiteral("ui.workspaceInteraction");
}

inline QString inspectorSection() {
    return QStringLiteral("ui.inspectorSection");
}

inline QString topologyPreset() {
    return QStringLiteral("topology.preset");
}

inline QString packageFeature() {
    return QStringLiteral("package.feature");
}

inline QString editorTool() {
    return QStringLiteral("editor.tool");
}

inline QString connectionRuleProvider() {
    return QStringLiteral("connection.ruleProvider");
}

inline QString toolFlowInputProjector() {
    return QStringLiteral("tool.flowInputProjector");
}

inline QString artifactPresenter() {
    return QStringLiteral("artifact.presenter");
}

} // namespace app::interactionids
