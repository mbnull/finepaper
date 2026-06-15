// DesignEditingService runtime ownership contract tests.
#include "ipcraft/schemaids.h"
#include "project/designeditingservice.h"
#include "project/projectservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasIssueCode(const QVector<ipcraft::core::ValidationIssue>& issues, const QString& code) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

QString readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "scan fixture should open");
    return QString::fromUtf8(file.readAll());
}

QString sourceFilePath(const QString& repoRelativePath, const QString& qtRelativePath) {
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        const QString repoCandidate = dir.filePath(repoRelativePath);
        if (QFileInfo::exists(repoCandidate)) {
            return repoCandidate;
        }

        const QString qtCandidate = dir.filePath(qtRelativePath);
        if (QFileInfo::exists(qtCandidate)) {
            return qtCandidate;
        }

        if (!dir.cdUp()) {
            return repoCandidate;
        }
    }
}

ipcraft::core::ProjectDesign emptyMeshProject() {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("project_0");
    design.name = QStringLiteral("Mesh Project");
    design.packages.append({QStringLiteral("vendor.meshnoc"), QStringLiteral("1.0.0")});
    return design;
}

ipcraft::core::ProjectPatch addComponentPatch(const QString& componentId = QStringLiteral("mesh0")) {
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    patch.id = QStringLiteral("add-component");

    ipcraft::core::PatchOperation op;
    op.op = QStringLiteral("add");
    op.target = QStringLiteral("component");
    op.path = QStringLiteral("/components/-");
    op.payload = QJsonObject{
        {QStringLiteral("id"), componentId},
        {QStringLiteral("type"), QStringLiteral("VendorSwitch")},
        {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc")},
        {QStringLiteral("config"), QJsonObject{{QStringLiteral("width"), 4}}},
        {QStringLiteral("identity"), QJsonObject{{QStringLiteral("label"), QStringLiteral("Mesh 0")}}},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("role"), QStringLiteral("router")}}},
        {QStringLiteral("extensionData"), QJsonObject{{QStringLiteral("vendor"), true}}}
    };
    patch.ops.append(op);
    return patch;
}

ipcraft::core::ProjectPatch invalidAddComponentPatch() {
    ipcraft::core::ProjectPatch patch = addComponentPatch(QStringLiteral("broken"));
    patch.ops.first().payload.remove(QStringLiteral("type"));
    return patch;
}

ProjectDocument projectDocumentFixture() {
    ProjectDocument document;
    document.projectId = QStringLiteral("project_doc_0");
    document.projectName = QStringLiteral("Document Project");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.meshnoc"),
                                                QStringLiteral("1.0.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("mesh0");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.meshnoc"),
                                         QStringLiteral("1.0.0")};
    instance.config = QJsonObject{{QStringLiteral("width"), 4},
                                  {QStringLiteral("frequency"), 800}};
    document.instances.append(instance);
    return document;
}

void testApplyPatchMutatesDesignAndSupportsUndoRedo() {
    DesignEditingService service;
    int changeCount = 0;
    QObject::connect(&service, &DesignEditingService::designChanged, [&changeCount]() {
        ++changeCount;
    });

    service.replaceDesign(emptyMeshProject());
    require(changeCount == 1, "replaceDesign should emit designChanged");

    const DesignEditResult apply = service.applyPatch(addComponentPatch());
    require(apply.success, "patch should apply");
    require(service.design().components.size() == 1, "component should be added");
    require(service.design().components.first().id == QStringLiteral("mesh0"),
            "component id should come from patch payload");
    require(service.design().components.first().packageRef == QStringLiteral("vendor.meshnoc@1.0.0"),
            "bare payload package id should resolve to declared package ref");
    require(service.design().components.first().config.value(QStringLiteral("width")).toInt() == 4,
            "component config should be preserved");
    require(service.design().components.first().identity.value(QStringLiteral("label")).toString() ==
                QStringLiteral("Mesh 0"),
            "component identity should be preserved");
    require(service.design().components.first().metadata.value(QStringLiteral("role")).toString() ==
                QStringLiteral("router"),
            "component metadata should be preserved");
    require(service.design().components.first().extensionData.value(QStringLiteral("vendor")).toBool(),
            "component extension data should be preserved");
    require(service.canUndo(), "successful edit should be undoable");

    const DesignEditResult undo = service.undo();
    require(undo.success, "undo should succeed");
    require(service.design().components.isEmpty(), "undo should remove component");
    require(service.canRedo(), "undo should enable redo");

    const DesignEditResult redo = service.redo();
    require(redo.success, "redo should succeed");
    require(service.design().components.size() == 1, "redo should restore component");
    require(changeCount == 4, "replace, apply, undo, and redo should emit changes");
}

void testFailedPatchDoesNotPushUndoHistory() {
    DesignEditingService service;
    service.replaceDesign(emptyMeshProject());

    const DesignEditResult invalid = service.applyPatch(invalidAddComponentPatch());
    require(!invalid.success, "invalid patch should fail");
    require(hasIssueCode(invalid.issues, QStringLiteral("patch.component_missing_type")),
            "invalid add component should report stable missing type issue");
    require(!service.canUndo(), "failed patch should not create undo history");
    require(service.design().components.isEmpty(), "failed patch should not mutate design");

    const DesignEditResult valid = service.applyPatch(addComponentPatch());
    require(valid.success, "valid patch should apply after failed patch");
    const DesignEditResult duplicate = service.applyPatch(addComponentPatch());
    require(!duplicate.success, "duplicate component id should fail");
    require(hasIssueCode(duplicate.issues, QStringLiteral("patch.duplicate_component_id")),
            "duplicate component id should report stable issue");
    require(service.design().components.size() == 1, "duplicate patch should not mutate design");

    const DesignEditResult undo = service.undo();
    require(undo.success, "undo after duplicate failure should still undo the last successful edit");
    require(service.design().components.isEmpty(),
            "failed patch must not add an extra undo checkpoint");
}

void testProjectServiceOwnsAndReplacesRuntimeDesign() {
    ProjectService service;
    ipcraft::core::ProjectDesign design = emptyMeshProject();
    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("mesh1");
    component.type = QStringLiteral("VendorSwitch");
    component.packageRef = QStringLiteral("vendor.meshnoc@1.0.0");
    component.config.insert(QStringLiteral("width"), 8);
    design.components.append(component);

    service.replaceDesign(design);
    require(service.design().id == QStringLiteral("project_0"),
            "ProjectService should expose replaced design id");
    require(service.document().projectId == QStringLiteral("project_0"),
            "replaceDesign should update document project id");
    require(service.document().projectName == QStringLiteral("Mesh Project"),
            "replaceDesign should update document project name");
    require(service.document().ipcores.size() == 1,
            "replaceDesign should update document package refs");
    require(service.document().instances.size() == 1,
            "replaceDesign should update document instances");
    require(service.document().instances.first().id == QStringLiteral("mesh1"),
            "replaceDesign should update document component id");
    require(service.document().instances.first().package.id == QStringLiteral("vendor.meshnoc"),
            "replaceDesign should split component package id");
    require(service.document().instances.first().package.version == QStringLiteral("1.0.0"),
            "replaceDesign should split component package version");
    require(service.document().instances.first().config.value(QStringLiteral("width")).toInt() == 8,
            "replaceDesign should preserve component config");
}

void testProjectDocumentToProjectDesignConversionPreservesCoreFields() {
    ProjectService service;
    const ProjectServiceResult result = service.replaceDocument(projectDocumentFixture());
    require(result.success, "replaceDocument should accept fixture document");

    const ipcraft::core::ProjectDesign& design = service.design();
    require(design.id == QStringLiteral("project_doc_0"),
            "ProjectDocument project id should convert to ProjectDesign id");
    require(design.name == QStringLiteral("Document Project"),
            "ProjectDocument project name should convert to ProjectDesign name");
    require(design.packages.size() == 1,
            "ProjectDocument package refs should convert to ProjectDesign packages");
    require(design.packages.first().id == QStringLiteral("vendor.meshnoc"),
            "package id should be preserved");
    require(design.packages.first().version == QStringLiteral("1.0.0"),
            "package version should be preserved");
    require(design.components.size() == 1,
            "ProjectDocument instances should convert to ProjectDesign components");
    require(design.components.first().id == QStringLiteral("mesh0"),
            "component id should be preserved");
    require(design.components.first().packageRef == QStringLiteral("vendor.meshnoc@1.0.0"),
            "component package ref should be preserved");
    require(design.components.first().config.value(QStringLiteral("width")).toInt() == 4,
            "component config width should be preserved");
    require(design.components.first().config.value(QStringLiteral("frequency")).toInt() == 800,
            "component config frequency should be preserved");
}

void testMainWindowRegistersDesignEditingService() {
    const QString source = readFile(sourceFilePath(QStringLiteral("qt/src/app/mainwindow.cpp"),
                                                   QStringLiteral("src/app/mainwindow.cpp")));
    require(source.contains(QStringLiteral("finepaper.design-editing")),
            "MainWindow should register finepaper.design-editing");
    require(source.contains(QStringLiteral("registerService")),
            "MainWindow registration scan should cover ServiceRegistry registration");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testApplyPatchMutatesDesignAndSupportsUndoRedo();
        testFailedPatchDoesNotPushUndoHistory();
        testProjectServiceOwnsAndReplacesRuntimeDesign();
        testProjectDocumentToProjectDesignConversionPreservesCoreFields();
        testMainWindowRegistersDesignEditingService();
    } catch (const std::exception& error) {
        std::cerr << "designeditingservice_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "designeditingservice_test passed\n";
    return 0;
}
