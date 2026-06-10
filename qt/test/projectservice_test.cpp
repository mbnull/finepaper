// ProjectService tests for durable project source-of-truth behavior.
#include "ipcraft/core/project_patch.h"
#include "ipcraft/schemaids.h"
#include "project/projectservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    ProjectService service;
    ProjectDocument document;
    document.projectName = QStringLiteral("Projection");
    document.projectId = QStringLiteral("projection_0");

    const ProjectServiceResult result = service.replaceDocumentFromProjection(document);
    require(result.success, "replaceDocumentFromProjection should accept valid document");
    require(service.hasDocument(), "service should have a document after projection replace");
    require(service.document().projectName == QStringLiteral("Projection"),
            "projection replacement should update durable document");
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

void testRejectsUnsupportedDocumentKind() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("not-a-project.json"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture should open");
    file.write("{\"schema\":\"unknown\"}");
    file.close();

    ProjectService service;
    const ProjectServiceResult result = service.loadFile(path);
    require(!result.success, "loadFile should reject unsupported project kind");
    require(!service.hasDocument(), "failed load should not replace current document");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCreateSaveLoadRoundtrip();
        testReplaceDocumentFromProjection();
        testApplyDesignPatch();
        testRejectsUnsupportedDocumentKind();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "projectservice_test passed\n";
    return 0;
}
