// EditorProjectionService bridges durable ProjectService documents and the live Graph projection.
#pragma once

#include "project/projectdocument.h"

#include <QString>

class Graph;
class ProjectIpService;
class ProjectService;
class ProjectStateService;

struct EditorProjectionResult {
    bool success = false;
    QString error;
};

class EditorProjectionService {
public:
    EditorProjectionService(Graph* graph,
                            ProjectStateService* projectStateService,
                            ProjectIpService* projectIpService,
                            ProjectService* projectService);

    EditorProjectionResult rebuildProjectionFromDocument(const ProjectDocument& document,
                                                         const QString& loadedPath);
    EditorProjectionResult syncProjectFromProjection(const QString& projectName);
    void clearProjection();

private:
    Graph* m_graph = nullptr;
    ProjectStateService* m_projectStateService = nullptr;
    ProjectIpService* m_projectIpService = nullptr;
    ProjectService* m_projectService = nullptr;
};
