# Project Plugin V1 Source Of Truth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 2 of the plugin-extensible IP platform: a Project Plugin service that owns V1 project documents and durable project I/O, then route `MainWindow` create/open/save through it.

**Architecture:** `ProjectService` becomes the durable `ipcraft.project.v1` document boundary. Existing `GraphProjectSerializer` remains a temporary editor projection bridge only: load rebuilds the graph projection from the service document, and save refreshes the service document from the current projection before writing. `ProjectPlugin` is an internal plugin activation shim that requires `ProjectService` in `AppContext` without introducing a public ABI.

**Tech Stack:** C++23, Qt Core/Widgets, existing V1 `ProjectDocument`, `ProjectReader`, `ProjectWriter`, `ipcraft::core::ProjectPatch`, xmake Qt test targets.

---

## Execution Rules

- Continue in worktree `/home/bnl/dev/finepaper/.worktrees/plugin-host-foundation` on branch `plugin-host-foundation`.
- Use subagent-driven development. Recommended subagent model/effort: GPT-5.5 with `xhigh`; minimum effort is `high`.
- Commit after every task.
- Do not change package loading, generator behavior, connection checking, or NoC logic in this phase.
- `GraphProjectSerializer` may be used only as a projection bridge between `ProjectService::document()` and the existing editor shell.
- If `MainWindow` changes introduce link errors in tests that compile `mainwindow.cpp` directly, fix only the missing target dependencies.

## Scope Check

This plan implements only Phase 2:

```text
Project Plugin And V1 Source Of Truth
```

Follow-up phases still own package plugin loading, editor command rebinding, connection checking, tool pipeline, NoC completion, onboarding skill, hardening, and final review.

## File Structure

Create:

- `qt/inc/project/projectservice.h`: durable V1 document service API.
- `qt/src/project/projectservice.cpp`: create/load/save/replace/apply-patch implementation.
- `qt/inc/project/projectplugin.h`: internal Project Plugin factory.
- `qt/src/project/projectplugin.cpp`: project plugin activation guard.
- `qt/test/projectservice_test.cpp`: service create/load/save and patch tests.
- `qt/test/projectplugin_test.cpp`: plugin host activation tests.
- `qt/test/plugin_architecture_phase2_scan_test.cpp`: Phase 2 architecture scan gate.

Modify:

- `qt/inc/app/appcontext.h`: add `ProjectService* projectService`.
- `qt/inc/app/mainwindow.h`: own `ProjectService`.
- `qt/src/app/mainwindow.cpp`: create/open/save through `ProjectService`.
- `qt/xmake.lua`: add tests and direct `mainwindow.cpp` test dependencies.

## Task 1: ProjectService

**Files:**
- Create: `qt/inc/project/projectservice.h`
- Create: `qt/src/project/projectservice.cpp`
- Create: `qt/test/projectservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Create failing ProjectService test**

Create `qt/test/projectservice_test.cpp` with these behaviors:

```cpp
#include "ipcraft/core/project_patch.h"
#include "ipcraft/schemaids.h"
#include "project/projectservice.h"

#include <QCoreApplication>
#include <QDir>
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
```

- [ ] **Step 2: Add xmake target**

Add this target near `projectdocument_test` in `qt/xmake.lua`:

```lua
add_qt_test_target("projectservice_test", "test/projectservice_test.cpp", {
    "src/project/projectservice.cpp",
    "src/project/projectreader.cpp",
    "src/project/projectwriter.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcraft/core/project_design.cpp",
    "src/ipcraft/core/project_patch.cpp",
    "inc/project/projectservice.h",
    "inc/project/projectreader.h",
    "inc/project/projectwriter.h",
    "inc/project/projectdocument.h",
    "inc/ipcraft/core/project_design.h",
    "inc/ipcraft/core/project_patch.h"
})
```

- [ ] **Step 3: Run failing test**

Run:

```bash
xmake run -P qt projectservice_test
```

Expected: build fails because `project/projectservice.h` does not exist.

- [ ] **Step 4: Implement ProjectService header**

Create `qt/inc/project/projectservice.h`:

```cpp
#pragma once

#include "ipcraft/core/project_patch.h"
#include "project/projectdocument.h"
#include "project/projectreader.h"

#include <QObject>
#include <QString>

struct ProjectServiceResult {
    bool success = false;
    QString error;
    ipcraft::DiagnosticStore diagnostics;
};

class ProjectService : public QObject {
    Q_OBJECT

public:
    explicit ProjectService(QObject* parent = nullptr);

    bool hasDocument() const;
    const ProjectDocument& document() const;
    QString currentPath() const;

    void clear();
    ProjectServiceResult createNew(const QString& projectName);
    ProjectServiceResult loadFile(const QString& path);
    ProjectServiceResult saveFile(const QString& path);
    ProjectServiceResult replaceDocument(ProjectDocument document);
    ProjectServiceResult replaceDocumentFromProjection(ProjectDocument document);
    ipcraft::core::PatchApplyResult applyDesignPatch(
        const ipcraft::core::ProjectDesign& project,
        const ipcraft::core::ProjectPatch& patch) const;

signals:
    void currentDocumentChanged();

private:
    ProjectDocument m_document;
    QString m_currentPath;
    bool m_hasDocument = false;
};
```

- [ ] **Step 5: Implement ProjectService source**

Create `qt/src/project/projectservice.cpp`:

```cpp
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "project/projectwriter.h"

#include <QFileInfo>

namespace {

ProjectServiceResult successResult() {
    return {true, {}, {}};
}

ProjectServiceResult failureResult(const QString& error,
                                   ipcraft::DiagnosticStore diagnostics = {}) {
    ProjectServiceResult result;
    result.success = false;
    result.error = error;
    result.diagnostics = std::move(diagnostics);
    return result;
}

QString cleanProjectName(const QString& projectName) {
    const QString trimmed = projectName.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Untitled") : trimmed;
}

} // namespace

ProjectService::ProjectService(QObject* parent) : QObject(parent) {}

bool ProjectService::hasDocument() const {
    return m_hasDocument;
}

const ProjectDocument& ProjectService::document() const {
    return m_document;
}

QString ProjectService::currentPath() const {
    return m_currentPath;
}

void ProjectService::clear() {
    m_document = ProjectDocument{};
    m_currentPath.clear();
    m_hasDocument = false;
    emit currentDocumentChanged();
}

ProjectServiceResult ProjectService::createNew(const QString& projectName) {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectName = cleanProjectName(projectName);
    document.name = document.projectName;
    return replaceDocument(std::move(document));
}

ProjectServiceResult ProjectService::loadFile(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (ProjectReader::detectKind(absolutePath) != ProjectFileKind::Project) {
        return failureResult(QStringLiteral("Unsupported document format: ") + absolutePath);
    }

    const ProjectReadResult readResult = ProjectReader::readFile(absolutePath);
    if (!readResult.success) {
        return failureResult(readResult.error, readResult.diagnostics);
    }

    m_document = readResult.document;
    m_currentPath = absolutePath;
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::saveFile(const QString& path) {
    if (!m_hasDocument) {
        return failureResult(QStringLiteral("No project document is open."));
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(absolutePath, m_document);
    if (!writeResult.success) {
        return failureResult(writeResult.error);
    }

    m_currentPath = absolutePath;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocument(ProjectDocument document) {
    m_document = std::move(document);
    if (m_document.schema.isEmpty()) {
        m_document.schema = ipcraft::schemaids::projectV1;
    }
    if (m_document.projectName.trimmed().isEmpty()) {
        m_document.projectName = QStringLiteral("Untitled");
    }
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromProjection(ProjectDocument document) {
    return replaceDocument(std::move(document));
}

ipcraft::core::PatchApplyResult ProjectService::applyDesignPatch(
    const ipcraft::core::ProjectDesign& project,
    const ipcraft::core::ProjectPatch& patch) const {
    return ipcraft::core::applyPatch(project, patch);
}
```

- [ ] **Step 6: Run ProjectService test**

Run:

```bash
xmake run -P qt projectservice_test
```

Expected: output contains `projectservice_test passed`.

- [ ] **Step 7: Commit**

Run:

```bash
git add qt/inc/project/projectservice.h qt/src/project/projectservice.cpp qt/test/projectservice_test.cpp qt/xmake.lua
git commit -m "feat: add project service source of truth"
```

## Task 2: ProjectPlugin And AppContext

**Files:**
- Modify: `qt/inc/app/appcontext.h`
- Create: `qt/inc/project/projectplugin.h`
- Create: `qt/src/project/projectplugin.cpp`
- Create: `qt/test/projectplugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write ProjectPlugin test**

Create `qt/test/projectplugin_test.cpp`:

```cpp
#include "app/appcontext.h"
#include "app/pluginhost.h"
#include "app/workbenchservice.h"
#include "project/projectplugin.h"
#include "project/projectservice.h"

#include <QApplication>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testProjectPluginActivatesWithProjectService() {
    WorkbenchService workbench;
    ProjectService projectService;
    AppContext context;
    context.workbench = &workbench;
    context.projectService = &projectService;

    PluginHost host(context);
    require(host.registerPlugin(createProjectPlugin()), "project plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "project plugin activation should succeed");
    require(result.activatedPluginIds == QStringList{QStringLiteral("finepaper.project")},
            "activation result should contain project plugin id");
}

void testProjectPluginRequiresProjectService() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;

    PluginHost host(context);
    require(host.registerPlugin(createProjectPlugin()), "project plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(!result.success, "project plugin activation should fail without ProjectService");
    require(result.error.contains(QStringLiteral("ProjectService")),
            "activation error should mention ProjectService");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    try {
        testProjectPluginActivatesWithProjectService();
        testProjectPluginRequiresProjectService();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "projectplugin_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add AppContext service pointer**

Modify `qt/inc/app/appcontext.h`:

```cpp
#pragma once

class ProjectService;
class WorkbenchService;

struct AppContext {
    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
};
```

- [ ] **Step 3: Add ProjectPlugin header**

Create `qt/inc/project/projectplugin.h`:

```cpp
#pragma once

#include "app/pluginhost.h"

#include <memory>

std::unique_ptr<IAppPlugin> createProjectPlugin();
```

- [ ] **Step 4: Add ProjectPlugin source**

Create `qt/src/project/projectplugin.cpp`:

```cpp
#include "project/projectplugin.h"

#include "app/appcontext.h"

#include <stdexcept>

namespace {

class ProjectPlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.project");
    }

    void activate(AppContext& context) override {
        if (!context.projectService) {
            throw std::runtime_error("ProjectService is required before activating ProjectPlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createProjectPlugin() {
    return std::make_unique<ProjectPlugin>();
}
```

- [ ] **Step 5: Add xmake target**

Add near `pluginhost_foundation_test`:

```lua
target("projectplugin_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/projectplugin_test.cpp")
    add_files("src/app/workbenchservice.cpp")
    add_files("src/app/pluginhost.cpp")
    add_files("src/project/projectservice.cpp")
    add_files("src/project/projectplugin.cpp")
    add_files("src/project/projectreader.cpp")
    add_files("src/project/projectwriter.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
    add_files("src/ipcraft/core/project_patch.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "projectplugin_test passed"
    })
```

- [ ] **Step 6: Run tests**

Run:

```bash
xmake run -P qt projectplugin_test
xmake run -P qt projectservice_test
xmake run -P qt pluginhost_foundation_test
```

Expected: each output contains its passed line.

- [ ] **Step 7: Commit**

Run:

```bash
git add qt/inc/app/appcontext.h qt/inc/project/projectplugin.h qt/src/project/projectplugin.cpp qt/test/projectplugin_test.cpp qt/xmake.lua
git commit -m "feat: add project plugin activation"
```

## Task 3: MainWindow ProjectService Rebinding

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add MainWindow ProjectService ownership**

Modify `qt/inc/app/mainwindow.h`:

- Add forward declaration:

```cpp
class ProjectService;
```

- Add member before `m_projectStateService`:

```cpp
    std::unique_ptr<ProjectService> m_projectService;
```

- [ ] **Step 2: Include and initialize ProjectService**

Modify `qt/src/app/mainwindow.cpp`:

- Add include:

```cpp
#include "project/projectservice.h"
```

- Add constructor initializer after `m_ipCatalogService`:

```cpp
      m_projectService(std::make_unique<ProjectService>()),
```

- [ ] **Step 3: Rebind project creation**

In `MainWindow::createProjectAt`, replace direct `GraphProjectSerializer::toProject` and `ProjectWriter::writeFile` with:

```cpp
    clearDocument();

    const ProjectServiceResult createResult =
        m_projectService->createNew(QFileInfo(projectPath).completeBaseName());
    if (!createResult.success) {
        qWarning() << "Failed to create project document" << createResult.error;
        QMessageBox::warning(this, "Create Project Failed", createResult.error);
        return false;
    }

    const ProjectServiceResult saveResult = m_projectService->saveFile(projectPath);
    if (!saveResult.success) {
        qWarning() << "Failed to create project at" << projectPath << saveResult.error;
        QMessageBox::warning(this, "Create Project Failed", saveResult.error);
        return false;
    }
```

Keep the existing state updates after save.

- [ ] **Step 4: Rebind project load**

In `MainWindow::loadDocument`, remove direct `ProjectReader::detectKind/readFile`. Use:

```cpp
    const ProjectServiceResult loadResult = m_projectService->loadFile(absolutePath);
    if (!loadResult.success) {
        qWarning() << "Failed to read project" << absolutePath << loadResult.error;
        QMessageBox::warning(this, "Open Failed", loadResult.error);
        return false;
    }

    const ProjectDocument& document = m_projectService->document();
```

Then pass `document` to `GraphProjectSerializer::loadProject(document, *m_graph)` and `m_projectIpService->loadFromDocument(document)`.

- [ ] **Step 5: Rebind project save**

In `MainWindow::saveDocument`, replace direct writer usage with:

```cpp
    ProjectDocument document =
        GraphProjectSerializer::toProject(*m_graph, QFileInfo(absolutePath).completeBaseName());
    m_projectStateService->writeToDocument(document);
    const ProjectServiceResult replaceResult =
        m_projectService->replaceDocumentFromProjection(std::move(document));
    if (!replaceResult.success) {
        qWarning() << "Failed to update project document before save" << replaceResult.error;
        QMessageBox::warning(this, "Save Failed", replaceResult.error);
        return false;
    }

    const ProjectServiceResult saveResult = m_projectService->saveFile(absolutePath);
    if (!saveResult.success) {
        qWarning() << "Failed to save project to" << absolutePath << saveResult.error;
        QMessageBox::warning(this, "Save Failed", saveResult.error);
        return false;
    }
```

- [ ] **Step 6: Clear ProjectService on document close**

In `MainWindow::clearDocument`, add:

```cpp
    if (m_projectService) {
        m_projectService->clear();
    }
```

before `setCurrentDocumentPath(QString())`.

- [ ] **Step 7: Add xmake dependency for direct MainWindow test**

In `ipcatalogpanel_test` target, add:

```lua
    add_files("src/project/projectservice.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
    add_files("src/ipcraft/core/project_patch.cpp")
```

near the existing `projectreader/projectwriter` dependencies.

- [ ] **Step 8: Run verification**

Run:

```bash
xmake build -P qt qt
xmake run -P qt projectservice_test
xmake run -P qt projectplugin_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt projectdocument_test
```

Expected: all pass.

- [ ] **Step 9: Commit**

Run:

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/xmake.lua
git commit -m "feat: route main window project io through project service"
```

## Task 4: Phase 2 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase2_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write scan test**

Create `qt/test/plugin_architecture_phase2_scan_test.cpp`:

```cpp
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

QString readText(const QString& path) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read " + path).toStdString());
    }
    return QString::fromUtf8(source.readAll());
}

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void requireNotContains(const QString& text, const QString& needle, const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void testProjectPluginFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/project/projectservice.h"),
        QStringLiteral("qt/src/project/projectservice.cpp"),
        QStringLiteral("qt/inc/project/projectplugin.h"),
        QStringLiteral("qt/src/project/projectplugin.cpp")
    };
    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing project plugin file: ") + file);
    }
}

void testMainWindowUsesProjectServiceForDurableIo() {
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));
    requireContains(source, QStringLiteral("m_projectService->createNew"), QStringLiteral("mainwindow source"));
    requireContains(source, QStringLiteral("m_projectService->loadFile"), QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_projectService->replaceDocumentFromProjection"),
                    QStringLiteral("mainwindow source"));
    requireContains(source, QStringLiteral("m_projectService->saveFile"), QStringLiteral("mainwindow source"));
    requireNotContains(source, QStringLiteral("ProjectReader::readFile"), QStringLiteral("mainwindow source"));
    requireNotContains(source, QStringLiteral("ProjectWriter::writeFile"), QStringLiteral("mainwindow source"));
}

void testProjectServiceKeepsV1AndPatchBoundary() {
    const QString header = readText(QStringLiteral("qt/inc/project/projectservice.h"));
    const QString source = readText(QStringLiteral("qt/src/project/projectservice.cpp"));
    requireContains(header, QStringLiteral("ProjectDocument"), QStringLiteral("project service header"));
    requireContains(header, QStringLiteral("applyDesignPatch"), QStringLiteral("project service header"));
    requireContains(source, QStringLiteral("ipcraft::schemaids::projectV1"), QStringLiteral("project service source"));
    requireContains(source, QStringLiteral("ipcraft::core::applyPatch"), QStringLiteral("project service source"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testProjectPluginFilesExist();
    testMainWindowUsesProjectServiceForDurableIo();
    testProjectServiceKeepsV1AndPatchBoundary();
    std::cout << "plugin_architecture_phase2_scan_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add xmake target**

Add near `plugin_architecture_phase1_scan_test`:

```lua
target("plugin_architecture_phase2_scan_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")
    set_rundir("$(projectdir)/..")

    add_includedirs("inc")
    add_files("test/plugin_architecture_phase2_scan_test.cpp")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "plugin_architecture_phase2_scan_test passed"
    })
```

- [ ] **Step 3: Run Phase 2 verification**

Run:

```bash
xmake run -P qt projectservice_test
xmake run -P qt projectplugin_test
xmake run -P qt plugin_architecture_phase2_scan_test
xmake run -P qt projectdocument_test
xmake run -P qt ipcatalogpanel_test
xmake build -P qt qt
```

Expected: all pass.

- [ ] **Step 4: Commit**

Run:

```bash
git add qt/test/plugin_architecture_phase2_scan_test.cpp qt/xmake.lua
git commit -m "test: add project plugin phase two scan"
```

## Phase 2 Review Gate

After Task 4, produce a review note:

```text
phase status: pass / pass-with-debt / blocked
tests run
architecture scan status
three-IP anchor status
critical findings
debt findings
schema compatibility status
next phase recommendation
```

For Phase 2, the three-IP anchor status may remain:

```text
not fully exercised; project durable I/O path changed, package/tool/editor anchor workflows remain later phases
```

Critical blockers:

- `MainWindow` still calls `ProjectReader::readFile` or `ProjectWriter::writeFile` directly.
- `ProjectService` does not preserve `ipcraft.project.v1`.
- `ProjectService` cannot create, load, save, or apply a patch through tests.
- Existing `projectdocument_test` or `ipcatalogpanel_test` regresses.

Expected debt:

- `GraphProjectSerializer` remains a projection bridge.
- Existing editor commands still mutate Graph before service synchronization.
- Full editor gesture patch rebinding waits for Phase 4.

## Self-Review

- Spec coverage: ProjectService owns durable V1 documents; ProjectPlugin activates through AppContext; MainWindow durable I/O goes through ProjectService; scan gate checks boundary.
- Placeholder scan: no placeholder task content is intended.
- Type consistency: `ProjectServiceResult`, `ProjectService`, and `createProjectPlugin()` are used consistently by tests and implementation.
