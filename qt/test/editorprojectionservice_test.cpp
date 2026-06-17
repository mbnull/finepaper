// EditorProjectionService tests.
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/parameter.h"
#include "ipcraft/diagnosticids.h"
#include "modules/moduleregistry.h"
#include "project/editorprojectionservice.h"
#include "project/projectipservice.h"
#include "project/projectservice.h"
#include "project/projectstateservice.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void resetRegistry() {
    ModuleRegistry::instance() = ModuleRegistry(ModuleRegistry::LoadMode::Empty);
}

ModuleType editorTileType() {
    ModuleType type;
    type.name = ModuleRegistry::scopedTypeName(QStringLiteral("finepaper.editor"),
                                               QStringLiteral("Tile"));
    type.packageId = QStringLiteral("finepaper.editor");
    type.moduleId = QStringLiteral("Tile");
    type.ipcoreId = QStringLiteral("finepaper.editor");
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultParameters.insert(QStringLiteral("label"),
                                  Parameter(QStringLiteral("label"), QStringLiteral("Tile")));
    return type;
}

void registerEditorTileType() {
    resetRegistry();
    require(ModuleRegistry::instance().registerType(editorTileType()),
            "editor tile type should register");
}

bool hasDiagnosticRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

ProjectIpInstanceRecord editorInstanceRecord() {
    ProjectIpInstanceRecord record;
    record.id = QStringLiteral("editor_0");
    record.package = ProjectPackageRef{QStringLiteral("finepaper.editor"), QStringLiteral("1.0")};
    record.ipcoreId = QStringLiteral("finepaper.editor");
    record.instanceId = QStringLiteral("editor_0");
    record.schema = QStringLiteral("finepaper.editor-state-v1");
    record.state = QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}};
    return record;
}

ProjectDocument editorDocument() {
    ProjectDocument document;
    document.projectName = QStringLiteral("Editor Projection");
    document.name = document.projectName;
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.editor"),
                                                   QStringLiteral("1.0")});
    document.ipcoreState.push_back(editorInstanceRecord());

    ProjectModuleRecord tile;
    tile.id = QStringLiteral("tile_0");
    tile.ipcoreId = QStringLiteral("finepaper.editor");
    tile.instanceId = QStringLiteral("editor_0");
    tile.type = QStringLiteral("Tile");
    tile.parameters.insert(QStringLiteral("x"), 24);
    tile.parameters.insert(QStringLiteral("y"), 48);
    tile.parameters.insert(QStringLiteral("label"), QStringLiteral("Loaded Tile"));
    document.modules.push_back(tile);
    return document;
}

std::unique_ptr<Module> editorTileModule(const QString& id) {
    const ModuleType type = editorTileType();
    auto module = std::make_unique<Module>(id, type.name);
    module->setIpcoreId(type.ipcoreId);
    module->setInstanceId(QStringLiteral("editor_0"));
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    return module;
}

void testRebuildProjectionFromDocumentUpdatesGraphAndServices() {
    registerEditorTileType();
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService ipService(&stateService);
    ProjectService projectService;
    EditorProjectionService projectionService(&graph, &stateService, &ipService, &projectService);
    const ProjectDocument document = editorDocument();

    const EditorProjectionResult result =
        projectionService.rebuildProjectionFromDocument(document, QStringLiteral("/tmp/editor.fpproj"));

    require(result.success, "document rebuild should succeed");
    require(graph.modules().size() == 1, "graph projection should contain loaded module");
    require(graph.getModule(QStringLiteral("tile_0")) != nullptr,
            "loaded module should be available in graph");
    require(stateService.ipInstanceRecords().size() == 1,
            "project IP state should load from document");
    require(ipService.selectedIpInstance().has_value(),
            "project IP service should select the loaded instance");
    require(projectService.hasDocument(), "project service should adopt loaded document");
    require(projectService.document().projectName == QStringLiteral("Editor Projection"),
            "project service should keep loaded project name");
    require(projectService.currentPath() == QFileInfo(QStringLiteral("/tmp/editor.fpproj")).absoluteFilePath(),
            "project service should keep loaded path");
}

void testRebuildProjectionViewOnlyDoesNotMutateProjectService() {
    registerEditorTileType();
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService ipService(&stateService);
    ProjectService projectService;
    EditorProjectionService projectionService(&graph, &stateService, &ipService, &projectService);
    const ProjectDocument document = editorDocument();

    const EditorProjectionResult result =
        projectionService.rebuildProjectionViewOnly(document);

    require(result.success, "view-only projection rebuild should succeed");
    require(graph.modules().size() == 1, "view-only rebuild should update the graph");
    require(graph.getModule(QStringLiteral("tile_0")) != nullptr,
            "view-only rebuild should load project modules into the graph");
    require(stateService.ipInstanceRecords().size() == 1,
            "view-only rebuild should update project IP state");
    require(ipService.selectedIpInstance().has_value(),
            "view-only rebuild should update project IP selection");
    require(!projectService.hasDocument(),
            "view-only rebuild must not adopt the document into ProjectService");
    require(!projectionService.projectionStale(),
            "successful view-only rebuild should clear stale projection state");
}

void testProjectionFailureReportsDiagnosticAndKeepsProjectServiceAuthoritative() {
    registerEditorTileType();
    Graph graph;
    require(graph.addModule(editorTileModule(QStringLiteral("existing_tile"))),
            "existing graph module should be added");
    ProjectStateService stateService;
    ProjectIpService ipService(&stateService);
    ProjectService projectService;
    require(projectService.replaceDocument(editorDocument()).success,
            "project service fixture should have an authoritative document");
    EditorProjectionService projectionService(&graph, &stateService, &ipService, &projectService);

    ProjectDocument brokenDocument = editorDocument();
    brokenDocument.modules.first().type = QStringLiteral("MissingType");

    const EditorProjectionResult result =
        projectionService.rebuildProjectionViewOnly(brokenDocument);

    require(!result.success, "invalid view-only projection rebuild should fail");
    require(hasDiagnosticRule(result.diagnostics, ipcraft::diagnosticids::editorProjectionFailed()),
            "projection failure should report editor.projection_failed");
    require(hasDiagnosticRule(projectionService.projectionDiagnostics(),
                              ipcraft::diagnosticids::editorProjectionFailed()),
            "projection service should remember the projection failure diagnostic");
    require(projectionService.projectionStale(),
            "failed view-only rebuild should mark the projection stale");
    require(projectService.hasDocument(),
            "projection failure should not clear the authoritative project service document");
    require(projectService.document().projectName == QStringLiteral("Editor Projection"),
            "projection failure must not replace ProjectService from the Graph projection");
    require(projectService.document().modules.size() == 1 &&
                projectService.document().modules.first().id == QStringLiteral("tile_0"),
            "projection failure should leave ProjectService document unchanged");
    require(graph.getModule(QStringLiteral("existing_tile")) != nullptr,
            "failed projection validation should leave the previous graph projection intact");
}

void testClearProjectionClearsGraphStateAndProjectService() {
    registerEditorTileType();
    Graph graph;
    require(graph.addModule(editorTileModule(QStringLiteral("tile_2"))),
            "test module should be added");
    ProjectStateService stateService;
    ProjectIpService ipService(&stateService);
    stateService.ensureIpInstanceRecord(editorInstanceRecord());
    ProjectService projectService;
    require(projectService.replaceDocument(editorDocument()).success,
            "project service fixture should have a document");
    EditorProjectionService projectionService(&graph, &stateService, &ipService, &projectService);

    projectionService.clearProjection();

    require(graph.modules().empty(), "clear should remove graph modules");
    require(stateService.ipInstanceRecords().isEmpty(), "clear should remove project IP state");
    require(!ipService.selectedIpInstance().has_value(), "clear should remove selected IP instance");
    require(!projectService.hasDocument(), "clear should clear durable project service");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRebuildProjectionFromDocumentUpdatesGraphAndServices();
        testRebuildProjectionViewOnlyDoesNotMutateProjectService();
        testProjectionFailureReportsDiagnosticAndKeepsProjectServiceAuthoritative();
        testClearProjectionClearsGraphStateAndProjectService();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "editorprojectionservice_test passed\n";
    return 0;
}
