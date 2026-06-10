// Built-in generation flow provider backed by the package FlowRunner.
#include "app/generationflowprovider.h"

namespace {

bool isGenerateFlow(const GenerationFlowRequest& request) {
    return request.flowRequest.flowId == QStringLiteral("generate");
}

} // namespace

bool PackageGenerationFlowProvider::canRun(const GenerationFlowRequest& request) const {
    return isGenerateFlow(request) && !request.flowRequest.package.id.trimmed().isEmpty();
}

ipcraft::FlowRunResult PackageGenerationFlowProvider::run(
    const GenerationFlowRequest& request) const {
    return ipcraft::FlowRunner::runFlow(request.flowRequest);
}
