// EditorProjectionService implementation.
#include "project/editorprojectionservice.h"

#include "graph/graph.h"
#include "ipcraft/diagnosticids.h"
#include "project/graphprojectserializer.h"
#include "project/projectipservice.h"
#include "project/projectservice.h"
#include "project/projectstateservice.h"

#include <stdexcept>
#include <utility>

namespace {

ipcraft::Diagnostic projectionFailureDiagnostic(const QString& error) {
    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("editor");
    diagnostic.ruleId = ipcraft::diagnosticids::editorProjectionFailed();
    diagnostic.category = QStringLiteral("projection");
    diagnostic.message = error.trimmed().isEmpty()
        ? QStringLiteral("Editor projection refresh failed.")
        : error;
    return diagnostic;
}

} // namespace

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
    const EditorProjectionResult projectionResult = rebuildProjectionViewOnly(document);
    if (!projectionResult.success) {
        return projectionResult;
    }

    const ProjectServiceResult replaceResult =
        m_projectService->replaceDocumentFromLoadedFile(document, loadedPath);
    if (!replaceResult.success) {
        return {false, replaceResult.error, replaceResult.diagnostics};
    }

    return projectionResult;
}

EditorProjectionResult EditorProjectionService::rebuildProjectionViewOnly(
    const ProjectDocument& document) {
    const GraphProjectLoadResult graphLoadResult =
        GraphProjectSerializer::loadProject(document, *m_graph);
    if (!graphLoadResult.success) {
        return recordProjectionFailure(graphLoadResult.error);
    }

    m_projectIpService->loadFromDocument(document);
    return recordProjectionSuccess();
}

void EditorProjectionService::clearProjection() {
    m_graph->clear();
    m_projectIpService->clear();
    m_projectService->clear();
    m_projectionStale = false;
    m_projectionDiagnostics = ipcraft::DiagnosticStore{};
}

bool EditorProjectionService::projectionStale() const {
    return m_projectionStale;
}

const ipcraft::DiagnosticStore& EditorProjectionService::projectionDiagnostics() const {
    return m_projectionDiagnostics;
}

EditorProjectionResult EditorProjectionService::recordProjectionFailure(const QString& error) {
    m_projectionStale = true;
    m_projectionDiagnostics = ipcraft::DiagnosticStore{};
    m_projectionDiagnostics.records.append(projectionFailureDiagnostic(error));
    return {false, error, m_projectionDiagnostics};
}

EditorProjectionResult EditorProjectionService::recordProjectionSuccess() {
    m_projectionStale = false;
    m_projectionDiagnostics = ipcraft::DiagnosticStore{};
    return {true, {}, m_projectionDiagnostics};
}
