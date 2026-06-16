// ProjectGenerationRunner drives project-level generation for every IP instance.
#pragma once

#include "app/generationflowprovider.h"
#include "ipcraft/core/project_design.h"
#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

struct ProjectGenerationInstanceResult {
    bool success = false;
    ProjectIpInstanceRecord instance;
    QString ipcoreId;
    QString instanceId;
    QString outputDirectory;
    QString inputPath;
    QString inputSchema;
    QString manifestPath;
    QString standardOutput;
    QString standardError;
    int exitCode = -1;
    QString exitStatus;
    QString error;
    QStringList artifactPaths;
};

struct ProjectGenerationRequest {
    const ipcraft::core::ProjectDesign* projectDesign = nullptr;
    QString projectPath;
    QString designName;
    QString outputRoot;
    QList<IpCatalogEntry> catalogEntries;
};

struct ProjectGenerationResult {
    bool success = false;
    QString outputRoot;
    QString snapshotPath;
    QString error;
    QVector<ProjectGenerationInstanceResult> instances;
    QStringList errors;
};

class ProjectGenerationRunner {
public:
    ProjectGenerationRunner();
    explicit ProjectGenerationRunner(QStringList frameworkToolSearchPaths);
    ~ProjectGenerationRunner();

    ProjectGenerationRunner(const ProjectGenerationRunner&) = delete;
    ProjectGenerationRunner& operator=(const ProjectGenerationRunner&) = delete;

    static QStringList defaultFrameworkToolSearchPaths();

    QStringList frameworkToolSearchPaths() const;
    void setFrameworkToolSearchPaths(QStringList searchPaths);
    void addGenerationFlowProvider(std::unique_ptr<GenerationFlowProvider> provider);

    ProjectGenerationResult generate(const ProjectGenerationRequest& request) const;

private:
    QStringList m_frameworkToolSearchPaths;
    std::vector<std::unique_ptr<GenerationFlowProvider>> m_generationFlowProviders;
};
