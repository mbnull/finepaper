// ToolPipelineService is the application boundary for validation/generation tools.
#pragma once

#include "app/generationflowprovider.h"
#include "app/projectgenerationrunner.h"

#include <QStringList>
#include <memory>

class ToolPipelineService {
public:
    ToolPipelineService();
    explicit ToolPipelineService(QStringList frameworkToolSearchPaths);

    QStringList frameworkToolSearchPaths() const;
    void setFrameworkToolSearchPaths(QStringList searchPaths);
    void addGenerationFlowProvider(std::unique_ptr<GenerationFlowProvider> provider);

    ProjectGenerationResult generateProject(const ProjectGenerationRequest& request) const;

private:
    ProjectGenerationRunner m_generationRunner;
};
