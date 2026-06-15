// ProjectService tests for durable project source-of-truth behavior.
#include "ipcraft/core/project_patch.h"
#include "ipcraft/schemaids.h"
#include "project/projectservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasDiagnosticRule(const ProjectServiceResult& result, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : result.diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project JSON fixture should open");
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    require(json.isObject(), "project JSON fixture should be an object");
    return json.object();
}

ipcraft::core::ProjectDesign patchableDesign() {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("project_0");
    design.name = QStringLiteral("Patchable");
    design.packages.append(ipcraft::core::PackageRef{QStringLiteral("pkg"), QStringLiteral("1.0")});
    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("cpu0");
    component.type = QStringLiteral("core");
    component.packageRef = QStringLiteral("pkg@1.0");
    design.components.append(component);
    return design;
}

void testCreateSaveLoadRoundtrip() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("chip.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Chip Top"));
    require(createResult.success, "createNew should succeed");
    require(service.hasDocument(), "service should have a document after createNew");
    require(service.document().schema == ipcraft::schemaids::projectV1,
            "created document should use project v1 schema");
    require(service.document().projectName == QStringLiteral("Chip Top"),
            "created document should store project name");

    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed");
    require(QFileInfo::exists(path), "project file should be written");
    require(service.currentPath() == QFileInfo(path).absoluteFilePath(),
            "saveFile should store absolute current path");

    ProjectService loaded;
    ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "loadFile should succeed");
    require(loaded.hasDocument(), "loaded service should have a document");
    require(loaded.document().projectName == QStringLiteral("Chip Top"),
            "loaded document should preserve project name");
    require(loaded.currentPath() == QFileInfo(path).absoluteFilePath(),
            "loadFile should store absolute current path");
}

void testReplaceDocumentFromProjection() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("projection.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Original"));
    require(createResult.success, "createNew should succeed before projection replace");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before projection replace");
    const QString savedPath = service.currentPath();

    ProjectDocument document;
    document.projectName = QStringLiteral("Projection");
    document.projectId = QStringLiteral("projection_0");

    const ProjectServiceResult result = service.replaceDocumentFromProjection(document);
    require(result.success, "replaceDocumentFromProjection should accept valid document");
    require(service.hasDocument(), "service should have a document after projection replace");
    require(service.document().projectName == QStringLiteral("Projection"),
            "projection replacement should update durable document");
    require(service.currentPath() == savedPath,
            "projection replacement should preserve the current path");
}

void testReplaceDocumentFromLoadedFileStoresAbsolutePath() {
    ProjectService service;
    ProjectDocument document;
    document.projectName = QStringLiteral("Loaded");
    document.projectId = QStringLiteral("loaded_0");
    const QString path = QStringLiteral("relative-loaded.fpproj");

    const ProjectServiceResult result =
        service.replaceDocumentFromLoadedFile(document, path);

    require(result.success, "replaceDocumentFromLoadedFile should accept a loaded document");
    require(service.hasDocument(), "loaded-file replacement should establish a document");
    require(service.document().projectName == QStringLiteral("Loaded"),
            "loaded-file replacement should preserve the document");
    require(service.currentPath() == QFileInfo(path).absoluteFilePath(),
            "loaded-file replacement should store the absolute current path");
}

void testCreateNewClearsSavedPath() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("saved.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Saved"));
    require(createResult.success, "createNew should succeed before save");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before creating another document");
    require(!service.currentPath().isEmpty(), "saveFile should establish a current path");

    ProjectServiceResult newResult = service.createNew(QStringLiteral("Other"));
    require(newResult.success, "createNew should succeed after save");
    require(service.document().projectName == QStringLiteral("Other"),
            "createNew should replace the durable document");
    require(service.currentPath().isEmpty(),
            "createNew should clear the previous saved path");
}

void testReplaceDocumentClearsSavedPath() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("saved.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Saved"));
    require(createResult.success, "createNew should succeed before replaceDocument path test");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before replaceDocument");
    require(!service.currentPath().isEmpty(), "saveFile should establish a current path");

    ProjectDocument replacement;
    replacement.projectName = QStringLiteral("Replacement");
    replacement.projectId = QStringLiteral("replacement_0");

    const ProjectServiceResult result = service.replaceDocument(replacement);
    require(result.success, "replaceDocument should accept a valid replacement");
    require(service.document().projectName == QStringLiteral("Replacement"),
            "replaceDocument should update the durable document");
    require(service.currentPath().isEmpty(),
            "replaceDocument should clear the previous saved path");
}

void testApplyDesignPatch() {
    ProjectService service;
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    patch.id = QStringLiteral("patch_0");
    patch.ops.append(ipcraft::core::PatchOperation{
        QStringLiteral("set_config"),
        QStringLiteral("component:cpu0"),
        QStringLiteral("/frequency_mhz"),
        QJsonValue(800),
        {}
    });

    const ipcraft::core::PatchApplyResult result =
        service.applyDesignPatch(patchableDesign(), patch);

    require(result.success, "applyDesignPatch should succeed");
    require(result.project.components.first().config.value(QStringLiteral("frequency_mhz")).toInt() == 800,
            "patch should update component config");
}

void testFlatDesignConfigSavesAsV1BundleAndReloadsAsRuntimeConfig() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("flat_config.fpproj"));

    ipcraft::core::ProjectDesign design = patchableDesign();
    design.components.first().config.insert(QStringLiteral("width"), 4);

    ProjectService service;
    service.replaceDesign(design);
    const ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should accept flat runtime design config");

    const QJsonObject root = readJsonObject(path);
    const QJsonObject savedConfig = root.value(QStringLiteral("instances"))
                                        .toArray()
                                        .first()
                                        .toObject()
                                        .value(QStringLiteral("config"))
                                        .toObject();
    require(savedConfig.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 4,
            "saved project config should use the V1 parameters bundle");
    require(!savedConfig.contains(QStringLiteral("width")),
            "saved project config should not write flat keys at bundle top level");

    ProjectService loaded;
    const ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "fresh ProjectService should load the saved V1 document");
    require(loaded.design().components.size() == 1,
            "loaded runtime design should preserve the component");
    require(loaded.design().components.first().config.value(QStringLiteral("width")).toInt() == 4,
            "loaded runtime design should restore flat component config");
}

void testRejectsUnsupportedDocumentKind() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("not-a-project.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "fixture should open");
    }
    file.write("{\"schema\":\"unknown\"}");
    file.close();

    ProjectService service;
    const ProjectServiceResult result = service.loadFile(path);
    require(!result.success, "loadFile should reject unsupported project kind");
    require(result.error.contains(QStringLiteral("Unsupported project schema")),
            "unsupported project schema error should come from ProjectReader");
    require(hasDiagnosticRule(result, QStringLiteral("project.unsupported_schema")),
            "unsupported project schema should expose structured diagnostics");
    require(!service.hasDocument(), "failed load should not replace current document");
}

void testLoadFileReportsInvalidJsonDiagnostics() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("broken.fpproj"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "invalid JSON fixture should open");
    }
    file.write("{not-json");
    file.close();

    ProjectService service;
    const ProjectServiceResult result = service.loadFile(path);

    require(!result.success, "loadFile should reject invalid JSON");
    require(result.error.contains(QStringLiteral("Invalid project JSON")),
            "invalid JSON error should come from ProjectReader");
    require(hasDiagnosticRule(result, QStringLiteral("project.invalid_json")),
            "invalid JSON should expose structured diagnostics");
    require(!service.hasDocument(), "failed invalid JSON load should not replace current document");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCreateSaveLoadRoundtrip();
        testReplaceDocumentFromProjection();
        testReplaceDocumentFromLoadedFileStoresAbsolutePath();
        testCreateNewClearsSavedPath();
        testReplaceDocumentClearsSavedPath();
        testApplyDesignPatch();
        testFlatDesignConfigSavesAsV1BundleAndReloadsAsRuntimeConfig();
        testRejectsUnsupportedDocumentKind();
        testLoadFileReportsInvalidJsonDiagnostics();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "projectservice_test passed\n";
    return 0;
}
