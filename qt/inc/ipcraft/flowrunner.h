#pragma once

#include "ipcraft/artifactmodel.h"
#include "ipcraft/emitter.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace ipcraft {

struct FlowRunRequest {
    QString projectId;
    QString instanceId;
    QString flowId;
    QString runId;
    QString runRoot;
    QString outputRoot;
    QString packageRoot;
    PackageSpec package;
    ConfigBundle config;
    CompositionModel composition;
    std::optional<GraphConfig> graphConfig;
    QStringList frameworkToolSearchPaths;
    QJsonObject placeholders;
};

struct FlowRunResult {
    bool ok = false;
    QString flowId;
    QString runId;
    QString runRoot;
    EmittedInputsManifest inputsManifest;
    ArtifactIndex artifacts;
    DiagnosticStore diagnostics;
    QJsonObject state;
};

class FlowRunner {
public:
    static FlowRunResult runFlow(const FlowRunRequest& request);
};

} // namespace ipcraft
