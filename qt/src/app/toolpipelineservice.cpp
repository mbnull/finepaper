// ToolPipelineService delegates project generation through registered flow providers.
#include "app/toolpipelineservice.h"

#include <utility>

ToolPipelineService::ToolPipelineService() = default;

ToolPipelineService::ToolPipelineService(QStringList frameworkToolSearchPaths)
    : m_generationRunner(std::move(frameworkToolSearchPaths)) {}

QStringList ToolPipelineService::frameworkToolSearchPaths() const {
    return m_generationRunner.frameworkToolSearchPaths();
}

void ToolPipelineService::setFrameworkToolSearchPaths(QStringList searchPaths) {
    m_generationRunner.setFrameworkToolSearchPaths(std::move(searchPaths));
}

void ToolPipelineService::addGenerationFlowProvider(
    std::unique_ptr<GenerationFlowProvider> provider) {
    m_generationRunner.addGenerationFlowProvider(std::move(provider));
}

ProjectGenerationResult ToolPipelineService::generateProject(
    const ProjectGenerationRequest& request) const {
    return m_generationRunner.generate(request);
}
