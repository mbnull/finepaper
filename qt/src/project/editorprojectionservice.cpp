// EditorProjectionService implementation.
#include "project/editorprojectionservice.h"

#include "graph/graph.h"
#include "project/graphprojectserializer.h"
#include "project/projectipservice.h"
#include "project/projectservice.h"
#include "project/projectstateservice.h"

#include <stdexcept>
#include <utility>

EditorProjectionService::EditorProjectionService(Graph* graph,
                                                 ProjectStateService* projectStateService,
                                                 ProjectIpService* projectIpService,
                                                 ProjectService* projectService)
    : m_graph(graph),
      m_projectStateService(projectStateService),
      m_projectIpService(projectIpService),
      m_projectService(projectService) {
    if (!m_graph || !m_projectStateService || !m_projectIpService || !m_projectService) {
        throw std::invalid_argument("EditorProjectionService requires graph and project services.");
    }
}

EditorProjectionResult EditorProjectionService::rebuildProjectionFromDocument(
    const ProjectDocument& document,
    const QString& loadedPath) {
    const GraphProjectLoadResult graphLoadResult =
        GraphProjectSerializer::loadProject(document, *m_graph);
    if (!graphLoadResult.success) {
        return {false, graphLoadResult.error};
    }

    m_projectIpService->loadFromDocument(document);
    const ProjectServiceResult replaceResult =
        m_projectService->replaceDocumentFromLoadedFile(document, loadedPath);
    if (!replaceResult.success) {
        return {false, replaceResult.error};
    }

    return {true, {}};
}

EditorProjectionResult EditorProjectionService::syncProjectFromProjection(
    const QString& projectName) {
    ProjectDocument document = GraphProjectSerializer::toProject(*m_graph, projectName);
    m_projectStateService->writeToDocument(document);

    const ProjectServiceResult replaceResult =
        m_projectService->replaceDocumentFromProjection(std::move(document));
    if (!replaceResult.success) {
        return {false, replaceResult.error};
    }

    return {true, {}};
}

void EditorProjectionService::clearProjection() {
    m_graph->clear();
    m_projectIpService->clear();
    m_projectService->clear();
}
