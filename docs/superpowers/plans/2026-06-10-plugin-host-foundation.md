# Plugin Host Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 1 of the plugin-extensible IP platform: `AppContext`, `PluginHost`, `WorkbenchService`, static internal plugin activation, and a first `MainWindow` workbench contribution boundary.

**Architecture:** This phase creates the smallest useful platform slice. It does not remove `Graph`, change project truth, or move NoC logic. It introduces a workbench contribution model and plugin host that existing UI can start consuming, so later phases can move features out of `MainWindow` without inventing another framework.

**Tech Stack:** C++23, Qt Widgets/Core, xmake Qt test targets, existing `qt/inc` and `qt/src` layout.

---

## Execution Rules

- Use subagent-driven development when executing this plan.
- Recommended subagent model/effort: GPT-5.5 with `xhigh` reasoning if available; minimum reasoning effort is `high`.
- Parallelize only non-conflicting tasks. Task 1 must land first. Tasks 2 and 3 can then run in parallel if the executor coordinates `qt/xmake.lua` edits. Task 4 depends on Tasks 1-3.
- Commit after every task using the exact commit command in that task.
- Keep every commit focused. Do not include unrelated working-tree files such as `.codegraph/`, generated docs, local outputs, or images.
- Do not edit implementation paths outside this phase boundary: no `Graph` truth-source changes, no package loading rewrites, no generator changes.

## Scope Check

The approved design covers ten phases. This plan implements only Phase 1:

```text
Kernel And Plugin Host Foundation
```

Follow-up plans must be written after the Phase 1 review gate for:

- Project Plugin and V1 source-of-truth work.
- Package Plugin and extension loading.
- Editor projection rebinding.
- Connection checking.
- Tool pipeline.
- NoC commercial workflow completion.
- IP onboarding skill/prompt.
- Hardening and final review.

## File Structure

Create:

- `qt/inc/app/workbenchservice.h`: value types and service API for action, panel, and editor contributions.
- `qt/src/app/workbenchservice.cpp`: contribution validation, duplicate-id rejection, and contribution storage.
- `qt/inc/app/appcontext.h`: small service bag passed to internal plugins.
- `qt/inc/app/pluginhost.h`: `IAppPlugin`, activation result, and plugin host API.
- `qt/src/app/pluginhost.cpp`: plugin registration, duplicate plugin-id rejection, and activation.
- `qt/test/workbenchservice_test.cpp`: focused tests for contribution registration.
- `qt/test/pluginhost_foundation_test.cpp`: focused tests for plugin registration and activation.
- `qt/test/plugin_architecture_phase1_scan_test.cpp`: text scan gate for Phase 1 boundary.

Modify:

- `qt/inc/app/mainwindow.h`: add `WorkbenchService` ownership and contribution registration helpers.
- `qt/src/app/mainwindow.cpp`: instantiate `WorkbenchService`, register built-in workbench contributions, and build the central editor/docks from service descriptors.
- `qt/xmake.lua`: add the three new test targets and include `workbenchservice.cpp` / `pluginhost.cpp` in the app target through the existing `src/**.cpp` glob.

## Task 1: WorkbenchService

**Files:**
- Create: `qt/inc/app/workbenchservice.h`
- Create: `qt/src/app/workbenchservice.cpp`
- Create: `qt/test/workbenchservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing WorkbenchService test**

Create `qt/test/workbenchservice_test.cpp`:

```cpp
#include "app/workbenchservice.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

WorkbenchActionContribution actionContribution(const QString& id) {
    WorkbenchActionContribution contribution;
    contribution.id = id;
    contribution.text = QStringLiteral("Action ") + id;
    contribution.menuPath = QStringLiteral("Tools");
    contribution.toolBar = QStringLiteral("Main");
    contribution.objectName = id + QStringLiteral("Action");
    contribution.factory = [id](QObject* parent) {
        auto* action = new QAction(QStringLiteral("Action ") + id, parent);
        action->setObjectName(id + QStringLiteral("Action"));
        return action;
    };
    return contribution;
}

WorkbenchPanelContribution panelContribution(const QString& id) {
    WorkbenchPanelContribution contribution;
    contribution.id = id;
    contribution.title = QStringLiteral("Panel ") + id;
    contribution.objectName = id + QStringLiteral("Dock");
    contribution.area = WorkbenchPanelArea::Left;
    contribution.factory = [id](QWidget* parent) {
        auto* label = new QLabel(QStringLiteral("Panel ") + id, parent);
        label->setObjectName(id + QStringLiteral("Panel"));
        return label;
    };
    return contribution;
}

WorkbenchEditorContribution editorContribution(const QString& id) {
    WorkbenchEditorContribution contribution;
    contribution.id = id;
    contribution.title = QStringLiteral("Editor ") + id;
    contribution.objectName = id + QStringLiteral("Editor");
    contribution.factory = [id](QWidget* parent) {
        auto* label = new QLabel(QStringLiteral("Editor ") + id, parent);
        label->setObjectName(id + QStringLiteral("Editor"));
        return label;
    };
    return contribution;
}

void testStoresContributionsInRegistrationOrder() {
    WorkbenchService service;

    require(service.addAction(actionContribution(QStringLiteral("generate"))),
            "action contribution should be accepted");
    require(service.addPanel(panelContribution(QStringLiteral("catalog"))),
            "panel contribution should be accepted");
    require(service.addEditor(editorContribution(QStringLiteral("node-editor"))),
            "editor contribution should be accepted");

    require(service.actions().size() == 1, "one action should be stored");
    require(service.panels().size() == 1, "one panel should be stored");
    require(service.editors().size() == 1, "one editor should be stored");
    require(service.actions().first().id == QStringLiteral("generate"),
            "action id should roundtrip");
    require(service.panels().first().area == WorkbenchPanelArea::Left,
            "panel area should roundtrip");
    require(service.editors().first().objectName == QStringLiteral("node-editorEditor"),
            "editor object name should roundtrip");
}

void testRejectsDuplicateIdsPerContributionType() {
    WorkbenchService service;

    require(service.addAction(actionContribution(QStringLiteral("validate"))),
            "first action should be accepted");
    require(!service.addAction(actionContribution(QStringLiteral("validate"))),
            "duplicate action id should be rejected");

    require(service.addPanel(panelContribution(QStringLiteral("properties"))),
            "first panel should be accepted");
    require(!service.addPanel(panelContribution(QStringLiteral("properties"))),
            "duplicate panel id should be rejected");

    require(service.addEditor(editorContribution(QStringLiteral("node-editor"))),
            "first editor should be accepted");
    require(!service.addEditor(editorContribution(QStringLiteral("node-editor"))),
            "duplicate editor id should be rejected");

    require(service.actions().size() == 1, "duplicate action should not be stored");
    require(service.panels().size() == 1, "duplicate panel should not be stored");
    require(service.editors().size() == 1, "duplicate editor should not be stored");
}

void testRejectsIncompleteContributions() {
    WorkbenchService service;

    WorkbenchActionContribution action = actionContribution(QStringLiteral("bad-action"));
    action.id.clear();
    require(!service.addAction(action), "action with empty id should be rejected");

    WorkbenchPanelContribution panel = panelContribution(QStringLiteral("bad-panel"));
    panel.factory = {};
    require(!service.addPanel(panel), "panel with no factory should be rejected");

    WorkbenchEditorContribution editor = editorContribution(QStringLiteral("bad-editor"));
    editor.objectName.clear();
    require(!service.addEditor(editor), "editor with empty objectName should be rejected");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    testStoresContributionsInRegistrationOrder();
    testRejectsDuplicateIdsPerContributionType();
    testRejectsIncompleteContributions();
    std::cout << "workbenchservice_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the failing xmake target**

Add this block near the other standalone test targets in `qt/xmake.lua`:

```lua
target("workbenchservice_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/workbenchservice_test.cpp")
    add_files("src/app/workbenchservice.cpp")
    add_files("inc/app/workbenchservice.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "workbenchservice_test passed"
    })
```

- [ ] **Step 3: Run the failing test**

Run:

```bash
xmake -P qt run workbenchservice_test
```

Expected: build fails because `app/workbenchservice.h` does not exist.

- [ ] **Step 4: Implement WorkbenchService header**

Create `qt/inc/app/workbenchservice.h`:

```cpp
#pragma once

#include <QList>
#include <QString>
#include <functional>

class QAction;
class QObject;
class QWidget;

enum class WorkbenchPanelArea {
    Left,
    Right,
    Bottom
};

struct WorkbenchActionContribution {
    QString id;
    QString text;
    QString menuPath;
    QString toolBar;
    QString objectName;
    std::function<QAction*(QObject*)> factory;
};

struct WorkbenchPanelContribution {
    QString id;
    QString title;
    QString objectName;
    WorkbenchPanelArea area = WorkbenchPanelArea::Left;
    std::function<QWidget*(QWidget*)> factory;
};

struct WorkbenchEditorContribution {
    QString id;
    QString title;
    QString objectName;
    std::function<QWidget*(QWidget*)> factory;
};

class WorkbenchService {
public:
    bool addAction(const WorkbenchActionContribution& contribution);
    bool addPanel(const WorkbenchPanelContribution& contribution);
    bool addEditor(const WorkbenchEditorContribution& contribution);

    QList<WorkbenchActionContribution> actions() const;
    QList<WorkbenchPanelContribution> panels() const;
    QList<WorkbenchEditorContribution> editors() const;

private:
    bool hasActionId(const QString& id) const;
    bool hasPanelId(const QString& id) const;
    bool hasEditorId(const QString& id) const;

    QList<WorkbenchActionContribution> m_actions;
    QList<WorkbenchPanelContribution> m_panels;
    QList<WorkbenchEditorContribution> m_editors;
};
```

- [ ] **Step 5: Implement WorkbenchService source**

Create `qt/src/app/workbenchservice.cpp`:

```cpp
#include "app/workbenchservice.h"

namespace {

bool isValidId(const QString& id) {
    return !id.trimmed().isEmpty();
}

bool isValidObjectName(const QString& objectName) {
    return !objectName.trimmed().isEmpty();
}

} // namespace

bool WorkbenchService::addAction(const WorkbenchActionContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.text.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasActionId(contribution.id)) {
        return false;
    }

    m_actions.append(contribution);
    return true;
}

bool WorkbenchService::addPanel(const WorkbenchPanelContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.title.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasPanelId(contribution.id)) {
        return false;
    }

    m_panels.append(contribution);
    return true;
}

bool WorkbenchService::addEditor(const WorkbenchEditorContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.title.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasEditorId(contribution.id)) {
        return false;
    }

    m_editors.append(contribution);
    return true;
}

QList<WorkbenchActionContribution> WorkbenchService::actions() const {
    return m_actions;
}

QList<WorkbenchPanelContribution> WorkbenchService::panels() const {
    return m_panels;
}

QList<WorkbenchEditorContribution> WorkbenchService::editors() const {
    return m_editors;
}

bool WorkbenchService::hasActionId(const QString& id) const {
    for (const WorkbenchActionContribution& contribution : m_actions) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}

bool WorkbenchService::hasPanelId(const QString& id) const {
    for (const WorkbenchPanelContribution& contribution : m_panels) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}

bool WorkbenchService::hasEditorId(const QString& id) const {
    for (const WorkbenchEditorContribution& contribution : m_editors) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 6: Run the test**

Run:

```bash
xmake -P qt run workbenchservice_test
```

Expected: output contains `workbenchservice_test passed`.

- [ ] **Step 7: Commit**

Run:

```bash
git add qt/inc/app/workbenchservice.h qt/src/app/workbenchservice.cpp qt/test/workbenchservice_test.cpp qt/xmake.lua
git commit -m "feat: add workbench contribution service"
```

## Task 2: AppContext And PluginHost

**Files:**
- Create: `qt/inc/app/appcontext.h`
- Create: `qt/inc/app/pluginhost.h`
- Create: `qt/src/app/pluginhost.cpp`
- Create: `qt/test/pluginhost_foundation_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing PluginHost test**

Create `qt/test/pluginhost_foundation_test.cpp`:

```cpp
#include "app/appcontext.h"
#include "app/pluginhost.h"
#include "app/workbenchservice.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QStringList>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class ContributingPlugin final : public IAppPlugin {
public:
    explicit ContributingPlugin(QString pluginId) : m_id(std::move(pluginId)) {}

    QString id() const override {
        return m_id;
    }

    void activate(AppContext& context) override {
        ++activateCount;

        WorkbenchActionContribution action;
        action.id = m_id + QStringLiteral(".action");
        action.text = QStringLiteral("Action");
        action.objectName = m_id + QStringLiteral("Action");
        action.factory = [](QObject* parent) {
            return new QAction(QStringLiteral("Action"), parent);
        };
        context.workbench->addAction(action);

        WorkbenchPanelContribution panel;
        panel.id = m_id + QStringLiteral(".panel");
        panel.title = QStringLiteral("Panel");
        panel.objectName = m_id + QStringLiteral("PanelDock");
        panel.factory = [](QWidget* parent) {
            return new QLabel(QStringLiteral("Panel"), parent);
        };
        context.workbench->addPanel(panel);
    }

    int activateCount = 0;

private:
    QString m_id;
};

void testActivatesPluginsWithAppContext() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;

    auto plugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* pluginPtr = plugin.get();

    PluginHost host(context);
    require(host.registerPlugin(std::move(plugin)), "plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "activation should succeed");
    require(result.activatedPluginIds == QStringList{QStringLiteral("project")},
            "activation result should include plugin id");
    require(pluginPtr->activateCount == 1, "plugin should activate exactly once");
    require(workbench.actions().size() == 1, "plugin should register one action");
    require(workbench.panels().size() == 1, "plugin should register one panel");
}

void testRejectsDuplicatePluginIds() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;

    PluginHost host(context);
    require(host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("package"))),
            "first plugin should register");
    require(!host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("package"))),
            "duplicate plugin id should be rejected");
    require(host.pluginIds() == QStringList{QStringLiteral("package")},
            "duplicate plugin should not be stored");
}

void testRejectsActivationWhenRequiredServicesAreMissing() {
    AppContext context;
    PluginHost host(context);
    require(host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("noc"))),
            "plugin should register before missing-service activation");

    const PluginActivationResult result = host.activatePlugins();
    require(!result.success, "activation should fail when workbench service is missing");
    require(result.error.contains(QStringLiteral("WorkbenchService")),
            "activation error should mention missing WorkbenchService");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    testActivatesPluginsWithAppContext();
    testRejectsDuplicatePluginIds();
    testRejectsActivationWhenRequiredServicesAreMissing();
    std::cout << "pluginhost_foundation_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the failing xmake target**

Add this block near `workbenchservice_test` in `qt/xmake.lua`:

```lua
target("pluginhost_foundation_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/pluginhost_foundation_test.cpp")
    add_files("src/app/workbenchservice.cpp")
    add_files("src/app/pluginhost.cpp")
    add_files("inc/app/appcontext.h")
    add_files("inc/app/workbenchservice.h")
    add_files("inc/app/pluginhost.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "pluginhost_foundation_test passed"
    })
```

- [ ] **Step 3: Run the failing test**

Run:

```bash
xmake -P qt run pluginhost_foundation_test
```

Expected: build fails because `app/appcontext.h` and `app/pluginhost.h` do not exist.

- [ ] **Step 4: Add AppContext**

Create `qt/inc/app/appcontext.h`:

```cpp
#pragma once

class WorkbenchService;

struct AppContext {
    WorkbenchService* workbench = nullptr;
};
```

- [ ] **Step 5: Add PluginHost header**

Create `qt/inc/app/pluginhost.h`:

```cpp
#pragma once

#include "app/appcontext.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class IAppPlugin {
public:
    virtual ~IAppPlugin() = default;
    virtual QString id() const = 0;
    virtual void activate(AppContext& context) = 0;
};

struct PluginActivationResult {
    bool success = false;
    QString error;
    QStringList activatedPluginIds;
};

class PluginHost {
public:
    explicit PluginHost(AppContext context);

    bool registerPlugin(std::unique_ptr<IAppPlugin> plugin);
    PluginActivationResult activatePlugins();
    QStringList pluginIds() const;

private:
    bool hasPluginId(const QString& id) const;

    AppContext m_context;
    QVector<std::unique_ptr<IAppPlugin>> m_plugins;
    bool m_activated = false;
};
```

- [ ] **Step 6: Implement PluginHost**

Create `qt/src/app/pluginhost.cpp`:

```cpp
#include "app/pluginhost.h"

#include <utility>

namespace {

bool isValidPluginId(const QString& id) {
    return !id.trimmed().isEmpty();
}

} // namespace

PluginHost::PluginHost(AppContext context) : m_context(context) {}

bool PluginHost::registerPlugin(std::unique_ptr<IAppPlugin> plugin) {
    if (!plugin) {
        return false;
    }

    const QString id = plugin->id();
    if (!isValidPluginId(id) || hasPluginId(id) || m_activated) {
        return false;
    }

    m_plugins.push_back(std::move(plugin));
    return true;
}

PluginActivationResult PluginHost::activatePlugins() {
    PluginActivationResult result;

    if (!m_context.workbench) {
        result.success = false;
        result.error = QStringLiteral("WorkbenchService is required before activating plugins.");
        return result;
    }

    if (m_activated) {
        result.success = true;
        result.activatedPluginIds = pluginIds();
        return result;
    }

    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        plugin->activate(m_context);
        result.activatedPluginIds.append(plugin->id());
    }

    m_activated = true;
    result.success = true;
    return result;
}

QStringList PluginHost::pluginIds() const {
    QStringList ids;
    ids.reserve(m_plugins.size());
    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        ids.append(plugin->id());
    }
    return ids;
}

bool PluginHost::hasPluginId(const QString& id) const {
    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        if (plugin->id() == id) {
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 7: Run the PluginHost test**

Run:

```bash
xmake -P qt run pluginhost_foundation_test
```

Expected: output contains `pluginhost_foundation_test passed`.

- [ ] **Step 8: Run WorkbenchService regression test**

Run:

```bash
xmake -P qt run workbenchservice_test
```

Expected: output contains `workbenchservice_test passed`.

- [ ] **Step 9: Commit**

Run:

```bash
git add qt/inc/app/appcontext.h qt/inc/app/pluginhost.h qt/src/app/pluginhost.cpp qt/test/pluginhost_foundation_test.cpp qt/xmake.lua
git commit -m "feat: add app plugin host foundation"
```

## Task 3: MainWindow Workbench Consumption

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`

- [ ] **Step 1: Add MainWindow declarations**

Modify `qt/inc/app/mainwindow.h`.

Add this forward declaration near the other class declarations:

```cpp
class WorkbenchService;
```

Add these private methods after `setupDocks()`:

```cpp
    void registerBuiltinWorkbenchContributions();
    Qt::DockWidgetArea dockAreaForWorkbenchPanel(WorkbenchPanelArea area) const;
```

Add this member before `m_commandManager`:

```cpp
    std::unique_ptr<WorkbenchService> m_workbenchService;
```

Add this include near the top because `WorkbenchPanelArea` is used in the private method signature:

```cpp
#include "app/workbenchservice.h"
```

- [ ] **Step 2: Include WorkbenchService in MainWindow source**

Add this include near the existing `mainwindow.cpp` includes:

```cpp
#include "app/workbenchservice.h"
```

- [ ] **Step 3: Initialize WorkbenchService in the constructor**

Modify the `MainWindow::MainWindow` initializer list in `qt/src/app/mainwindow.cpp`.

Replace:

```cpp
      m_graph(new Graph(this)),
      m_commandManager(std::make_unique<CommandManager>()),
```

with:

```cpp
      m_graph(new Graph(this)),
      m_workbenchService(std::make_unique<WorkbenchService>()),
      m_commandManager(std::make_unique<CommandManager>()),
```

- [ ] **Step 4: Register built-in contributions after panels are created**

In `MainWindow::MainWindow`, replace:

```cpp
    setupPanels();
    setupConnections();
```

with:

```cpp
    setupPanels();
    registerBuiltinWorkbenchContributions();
    setupConnections();
```

- [ ] **Step 5: Add built-in contribution registration**

Add this method to `qt/src/app/mainwindow.cpp` after `setupPanels()`:

```cpp
void MainWindow::registerBuiltinWorkbenchContributions() {
    if (!m_workbenchService) {
        return;
    }

    WorkbenchEditorContribution editor;
    editor.id = QStringLiteral("builtin.node-editor");
    editor.title = QStringLiteral("Node Editor");
    editor.objectName = QStringLiteral("nodeEditorPanel");
    editor.factory = [this](QWidget*) { return m_nodeEditor; };
    m_workbenchService->addEditor(editor);

    WorkbenchPanelContribution catalog;
    catalog.id = QStringLiteral("builtin.ip-catalog");
    catalog.title = QStringLiteral("IP Catalog");
    catalog.objectName = QStringLiteral("ipCatalogDock");
    catalog.area = WorkbenchPanelArea::Left;
    catalog.factory = [this](QWidget*) { return m_ipCatalogPanel; };
    m_workbenchService->addPanel(catalog);

    WorkbenchPanelContribution properties;
    properties.id = QStringLiteral("builtin.properties");
    properties.title = QStringLiteral("Properties");
    properties.objectName = QStringLiteral("propertyDock");
    properties.area = WorkbenchPanelArea::Right;
    properties.factory = [this](QWidget*) { return m_propertyPanel; };
    m_workbenchService->addPanel(properties);

    WorkbenchPanelContribution log;
    log.id = QStringLiteral("builtin.activity-log");
    log.title = QStringLiteral("Activity Log");
    log.objectName = QStringLiteral("logDock");
    log.area = WorkbenchPanelArea::Bottom;
    log.factory = [this](QWidget*) { return m_logPanel; };
    m_workbenchService->addPanel(log);
}
```

- [ ] **Step 6: Add dock-area mapping**

Add this method to `qt/src/app/mainwindow.cpp` after `setupDocks()`:

```cpp
Qt::DockWidgetArea MainWindow::dockAreaForWorkbenchPanel(WorkbenchPanelArea area) const {
    switch (area) {
    case WorkbenchPanelArea::Left:
        return Qt::LeftDockWidgetArea;
    case WorkbenchPanelArea::Right:
        return Qt::RightDockWidgetArea;
    case WorkbenchPanelArea::Bottom:
        return Qt::BottomDockWidgetArea;
    }
    return Qt::LeftDockWidgetArea;
}
```

- [ ] **Step 7: Build central editor from WorkbenchService**

Replace `MainWindow::createCentralContent()` with:

```cpp
QWidget* MainWindow::createCentralContent() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* editorWidget = m_nodeEditor;
    if (m_workbenchService && !m_workbenchService->editors().isEmpty()) {
        const WorkbenchEditorContribution editor = m_workbenchService->editors().first();
        if (editor.factory) {
            if (QWidget* contributedEditor = editor.factory(central)) {
                editorWidget = contributedEditor;
            }
        }
    }

    layout->addWidget(editorWidget);
    return central;
}
```

- [ ] **Step 8: Build docks from WorkbenchService**

In `MainWindow::setupDocks()`, replace:

```cpp
    m_ipCatalogDock = createDock("IP Catalog", m_ipCatalogPanel, Qt::LeftDockWidgetArea, "ipCatalogDock");
    m_propertyDock = createDock("Properties", m_propertyPanel, Qt::RightDockWidgetArea, "propertyDock");
    m_logDock = createDock("Activity Log", m_logPanel, Qt::BottomDockWidgetArea, "logDock");
```

with:

```cpp
    if (m_workbenchService) {
        for (const WorkbenchPanelContribution& panel : m_workbenchService->panels()) {
            QWidget* content = panel.factory ? panel.factory(this) : nullptr;
            if (!content) {
                continue;
            }

            QDockWidget* dock = createDock(panel.title,
                                           content,
                                           dockAreaForWorkbenchPanel(panel.area),
                                           panel.objectName);
            if (panel.id == QStringLiteral("builtin.ip-catalog")) {
                m_ipCatalogDock = dock;
            } else if (panel.id == QStringLiteral("builtin.properties")) {
                m_propertyDock = dock;
            } else if (panel.id == QStringLiteral("builtin.activity-log")) {
                m_logDock = dock;
            }
        }
    }
```

- [ ] **Step 9: Keep dock resize guarded**

In `MainWindow::setupDocks()`, replace:

```cpp
    resizeDocks({m_ipCatalogDock, m_propertyDock}, {280, 320}, Qt::Horizontal);
    resizeDocks({m_logDock}, {180}, Qt::Vertical);
```

with:

```cpp
    if (m_ipCatalogDock && m_propertyDock) {
        resizeDocks({m_ipCatalogDock, m_propertyDock}, {280, 320}, Qt::Horizontal);
    }
    if (m_logDock) {
        resizeDocks({m_logDock}, {180}, Qt::Vertical);
    }
```

- [ ] **Step 10: Build the app target**

Run:

```bash
xmake build -P qt qt
```

Expected: build succeeds.

- [ ] **Step 11: Run foundation tests**

Run:

```bash
xmake -P qt run workbenchservice_test
xmake -P qt run pluginhost_foundation_test
```

Expected: both outputs contain their `... passed` lines.

- [ ] **Step 12: Commit**

Run:

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp
git commit -m "feat: consume workbench contributions in main window"
```

## Task 4: Phase 1 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase1_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the scan test**

Create `qt/test/plugin_architecture_phase1_scan_test.cpp`:

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

void testFoundationFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/app/workbenchservice.h"),
        QStringLiteral("qt/src/app/workbenchservice.cpp"),
        QStringLiteral("qt/inc/app/appcontext.h"),
        QStringLiteral("qt/inc/app/pluginhost.h"),
        QStringLiteral("qt/src/app/pluginhost.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing foundation file: ") + file);
    }
}

void testMainWindowConsumesWorkbenchService() {
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));

    requireContains(header, QStringLiteral("WorkbenchService"), QStringLiteral("mainwindow header"));
    requireContains(source,
                    QStringLiteral("registerBuiltinWorkbenchContributions"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_workbenchService->panels()"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_workbenchService->editors()"),
                    QStringLiteral("mainwindow source"));
}

void testPhaseOnePlanDocumentsReviewGate() {
    const QString plan =
        readText(QStringLiteral("docs/superpowers/plans/2026-06-10-plugin-host-foundation.md"));
    requireContains(plan, QStringLiteral("Phase 1"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("review gate"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("workbenchservice_test"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("pluginhost_foundation_test"), QStringLiteral("phase plan"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testFoundationFilesExist();
    testMainWindowConsumesWorkbenchService();
    testPhaseOnePlanDocumentsReviewGate();
    std::cout << "plugin_architecture_phase1_scan_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the xmake target**

Add this block near the other standalone scan/foundation test targets in `qt/xmake.lua`:

```lua
target("plugin_architecture_phase1_scan_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/plugin_architecture_phase1_scan_test.cpp")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "plugin_architecture_phase1_scan_test passed"
    })
```

- [ ] **Step 3: Run the scan test**

Run:

```bash
xmake -P qt run plugin_architecture_phase1_scan_test
```

Expected: output contains `plugin_architecture_phase1_scan_test passed`.

- [ ] **Step 4: Run all Phase 1 verification commands**

Run:

```bash
xmake -P qt run workbenchservice_test
xmake -P qt run pluginhost_foundation_test
xmake -P qt run plugin_architecture_phase1_scan_test
xmake build -P qt qt
```

Expected:

- `workbenchservice_test passed`
- `pluginhost_foundation_test passed`
- `plugin_architecture_phase1_scan_test passed`
- app target build succeeds

- [ ] **Step 5: Commit**

Run:

```bash
git add qt/test/plugin_architecture_phase1_scan_test.cpp qt/xmake.lua
git commit -m "test: add plugin architecture phase one scan"
```

## Phase 1 Review Gate

After Task 4, produce a short review note in the implementation session with:

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

For Phase 1, the three-IP anchor status is allowed to be:

```text
not exercised; no package/project/generator behavior changed in this phase
```

Critical blocker conditions for Phase 1:

- `MainWindow` cannot build.
- Workbench contributions cannot register editor or panel descriptors.
- PluginHost cannot activate static internal plugins.
- `MainWindow` still has no workbench-service consumption after Task 3.
- Any unrelated file is committed.

Non-critical debt examples for Phase 1:

- Existing `MainWindow` still owns many old services.
- Existing `Graph` still exists as the editor backing model.
- Existing actions are not yet registered through `WorkbenchService`.

These debts are expected inputs to Phase 2 and Phase 3 plans, not reasons to expand this phase.

## Self-Review

Spec coverage:

- Kernel/AppContext foundation: covered by Tasks 1 and 2.
- PluginHost/static plugin activation: covered by Task 2.
- Workbench contribution model: covered by Tasks 1 and 3.
- MainWindow starts becoming a shell: covered by Task 3.
- Phase governance evidence: covered by Task 4 and the review gate.

Out of scope by design:

- Project source-of-truth replacement.
- Package extension loading.
- Editor projection rebinding.
- Connection checking.
- Tool pipeline.
- NoC commercial workflow.
- IP onboarding skill.
- Final `qt-cpp-review`.

Placeholder scan:

- This plan contains no placeholder markers or unspecified implementation steps.

Type consistency:

- `WorkbenchService`, `WorkbenchActionContribution`, `WorkbenchPanelContribution`, `WorkbenchEditorContribution`, `WorkbenchPanelArea`, `AppContext`, `IAppPlugin`, `PluginHost`, and `PluginActivationResult` use the same names across tests and implementation steps.
