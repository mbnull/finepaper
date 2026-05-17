// ProjectGenerationRunner drives project-level generation for every IP instance.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class Graph;

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
    const Graph* graph = nullptr;
    QString projectPath;
    QString designName;
    QString outputRoot;
    int generatorTimeoutMs = 300000;
    QList<IpCatalogEntry> catalogEntries;
    QVector<ProjectIpInstanceRecord> instances;
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

    static QStringList defaultFrameworkToolSearchPaths();

    QStringList frameworkToolSearchPaths() const;
    void setFrameworkToolSearchPaths(QStringList searchPaths);

    ProjectGenerationResult generate(const ProjectGenerationRequest& request) const;

private:
    QStringList m_frameworkToolSearchPaths;
};
