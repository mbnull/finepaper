// Generation flow providers execute package-declared tool flows for project generation.
#pragma once

#include "ipcraft/flowrunner.h"

#include <QString>

struct GenerationFlowRequest {
    ipcraft::FlowRunRequest flowRequest;
    QString outputDirectory;
};

class GenerationFlowProvider {
public:
    virtual ~GenerationFlowProvider() = default;

    virtual bool canRun(const GenerationFlowRequest& request) const = 0;
    virtual ipcraft::FlowRunResult run(const GenerationFlowRequest& request) const = 0;
};

class PackageGenerationFlowProvider final : public GenerationFlowProvider {
public:
    bool canRun(const GenerationFlowRequest& request) const override;
    ipcraft::FlowRunResult run(const GenerationFlowRequest& request) const override;
};
