// EditorProjectionService bridges durable ProjectService documents and the live Graph projection.
#pragma once

#include "ipcraft/diagnostics.h"
#include "project/projectdocument.h"

#include <QString>

class Graph;
class ProjectIpService;
class ProjectService;
class ProjectStateService;

struct EditorProjectionResult {
    bool success = false;
    QString error;
    ipcraft::DiagnosticStore diagnostics;
};

class EditorProjectionService {
public:
    EditorProjectionService(Graph* graph,
                            ProjectStateService* projectStateService,
                            ProjectIpService* projectIpService,
                            ProjectService* projectService);

    EditorProjectionResult rebuildProjectionFromDocument(const ProjectDocument& document,
                                                         const QString& loadedPath);
    EditorProjectionResult rebuildProjectionViewOnly(const ProjectDocument& document);
    void clearProjection();
    bool projectionStale() const;
    const ipcraft::DiagnosticStore& projectionDiagnostics() const;

private:
    EditorProjectionResult recordProjectionFailure(const QString& error);
    EditorProjectionResult recordProjectionSuccess();

    Graph* m_graph = nullptr;
    ProjectStateService* m_projectStateService = nullptr;
    ProjectIpService* m_projectIpService = nullptr;
    ProjectService* m_projectService = nullptr;
    bool m_projectionStale = false;
    ipcraft::DiagnosticStore m_projectionDiagnostics;
};
