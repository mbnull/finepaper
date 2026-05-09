# Node 4 IP Catalog UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the left `Palette` workflow with an IP Catalog dock that lets users browse IP cores, add/select project IP instances, and see the active workspace modules/tools.

**Architecture:** `IpCatalogPanel` is a QWidget-only intent surface. It renders catalog and workspace state from `IpCatalogService`, `ProjectStateService`, `ProjectIpService`, and `ActiveWorkspaceController`, then emits add/select intent signals; it does not mutate `Graph`. `MainWindow` owns service mutation by connecting those panel signals to `ProjectIpService`, and module creation remains the existing node-editor drag/drop path until Node 5 scopes the MIME payload.

**Tech Stack:** C++23, Qt Widgets, xmake Qt widget tests.

---

## File Structure

- Create `qt/inc/panels/ipcatalogpanel.h`: QWidget API, object names, intent signals, refresh slots.
- Create `qt/src/panels/ipcatalogpanel.cpp`: concrete widget hierarchy, list population, search filtering, drag payload for active modules.
- Create `qt/inc/ipcore/internalmodulelibrarymodel.h`: active workspace module entry model.
- Create `qt/src/ipcore/internalmodulelibrarymodel.cpp`: filters module metadata by active IP-core module type list.
- Create `qt/inc/ipcore/iptoolsmodel.h`: active workspace tool entry model.
- Create `qt/src/ipcore/iptoolsmodel.cpp`: exposes topology presets and generator/DRC availability as read-only tool rows.
- Create `qt/test/ipcatalogpanel_test.cpp`: widget tests for panel and MainWindow integration.
- Modify `qt/inc/app/mainwindow.h`: replace palette fields with IP catalog fields; remove active combo field.
- Modify `qt/src/app/mainwindow.cpp`: install `ipCatalogDock`, connect panel intents, remove toolbar `activeIpCombo`.
- Modify `qt/src/app/mainwindow.ui`: keep stale designer skeleton aligned with `ipCatalogDock` / no `activeIpCombo` text.
- Modify `qt/inc/panels/palette.h` and `qt/src/panels/palette.cpp`: de-emphasize legacy palette by removing app-facing dependencies or marking it unused; do not delete in Node 4.
- Modify `qt/xmake.lua`: add new app moc headers/sources and `ipcatalogpanel_test`.

---

## Widget Hierarchy And Object Names

`IpCatalogPanel` must build this hierarchy:

```text
IpCatalogPanel (objectName: ipCatalogPanel)
  QLineEdit search/filter field (objectName: ipCatalogSearch)
  QListWidget catalog tree/list (objectName: ipCatalogList)
  QListWidget project IP instance list (objectName: projectIpList)
  QListWidget active workspace modules list (objectName: activeModuleList)
  QListWidget active workspace tools list (objectName: activeToolList)
```

The lists can be arranged as compact labeled sections in a single vertical layout:

```text
IP Cores
[search]
[catalog]
Project Instances
[instances]
Workspace Modules
[modules]
Workspace Tools
[tools]
```

Use object names exactly:

- `ipCatalogPanel`
- `ipCatalogSearch`
- `ipCatalogList`
- `projectIpList`
- `activeModuleList`
- `activeToolList`

No explanatory in-app feature text beyond section labels.

---

## Data Contracts

### `InternalModuleLibraryEntry`

Defined in `qt/inc/ipcore/internalmodulelibrarymodel.h`:

```cpp
struct InternalModuleLibraryEntry {
    QString moduleType;
    QString label;
    QString description;
    QString graphGroup;
};
```

`moduleType` is the canonical `ModuleType::name`. `label` comes from `ModuleTypeMetadata::paletteLabel()`. Missing metadata falls back to `moduleType`.

`InternalModuleLibraryModel` public API:

```cpp
class InternalModuleLibraryModel {
public:
    explicit InternalModuleLibraryModel(const ModuleRegistry* moduleRegistry = nullptr);
    QVector<InternalModuleLibraryEntry> entriesForModuleTypes(const QStringList& moduleTypes) const;
};
```

The result order follows `moduleTypes` order from `ActiveWorkspaceState`, which is already sorted by `IpCatalogService`.

### `IpToolEntry`

Defined in `qt/inc/ipcore/iptoolsmodel.h`:

```cpp
struct IpToolEntry {
    QString id;
    QString label;
    QString kind;
};
```

`IpToolsModel` public API:

```cpp
class IpToolsModel {
public:
    QVector<IpToolEntry> entriesForWorkspace(const ActiveWorkspaceState& state,
                                             const IpCatalogEntry& entry) const;
};
```

Rows:

- One row per topology preset: `id = "topology:" + preset.id`, `label = preset.label`, `kind = "topology"`.
- If `entry.generator.hasCommand()`: `id = "generate"`, `label = "Generate Verilog"`, `kind = "generator"`.
- If `entry.drc.hasCommand()`: `id = "drc"`, `label = "Run DRC"`, `kind = "drc"`.

Node 4 only displays tools. Execution remains existing menu/actions and later Node 5 tool scoping.

---

## `IpCatalogPanel` Behavior

Constructor:

```cpp
IpCatalogPanel(const IpCatalogService* catalogService,
               ProjectStateService* stateService,
               ProjectIpService* projectIpService,
               ActiveWorkspaceController* workspaceController,
               QWidget* parent = nullptr);
```

Signals:

```cpp
void addIpcoreRequested(const QString& ipcoreId);
void selectIpInstanceRequested(const QString& ipcoreId, const QString& instanceId);
void moduleDragStarted(const QString& moduleType);
```

Refresh rules:

- Catalog list shows `catalogService->selectableEntries()`.
- Search filters catalog rows by case-insensitive match on `IpCatalogEntry::id`, `name`, or `kind`.
- Catalog item stores `ipcoreId` in `Qt::UserRole`.
- Double-clicking/activating a catalog item emits `addIpcoreRequested(ipcoreId)`.
- Project IP instance list shows `stateService->ipInstanceRecords()` and stores `ipcoreId` / `instanceId` in item data roles.
- Selecting/activating a project IP instance emits `selectIpInstanceRequested(ipcoreId, instanceId)`.
- Active module list is rebuilt from `workspaceController->state().moduleTypes` through `InternalModuleLibraryModel`.
- Active tool list is rebuilt from `workspaceController->state()` plus `IpCatalogService::entry(state.ipcoreId)` through `IpToolsModel`.
- Active module rows are drag-enabled and use the existing Node 4-compatible MIME:

```text
application/x-moduletype
```

with the module type UTF-8 string as payload. Node 5 will replace this with the scoped `application/x-finepaper-module` payload.

---

## Task 4.1: Panel Widget Tests

**Files:**

- Create: `qt/test/ipcatalogpanel_test.cpp`
- Create: `qt/inc/panels/ipcatalogpanel.h`
- Create: `qt/src/panels/ipcatalogpanel.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Write failing search/object-name test**

Create `qt/test/ipcatalogpanel_test.cpp` with a Qt widget test harness using `QApplication`, custom `require()`, and no QtTest dependency.

Test shape:

```cpp
void testSearchFiltersCatalogEntries() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(panel.objectName() == QStringLiteral("ipCatalogPanel"),
            "panel object name should be stable");
    auto* search = panel.findChild<QLineEdit*>(QStringLiteral("ipCatalogSearch"));
    auto* catalog = panel.findChild<QListWidget*>(QStringLiteral("ipCatalogList"));
    require(search != nullptr, "search field should exist");
    require(catalog != nullptr, "catalog list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("projectIpList")) != nullptr,
            "project IP list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeModuleList")) != nullptr,
            "active module list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeToolList")) != nullptr,
            "active tool list should exist");

    require(catalog->count() == 2, "catalog should start with two entries");
    search->setText(QStringLiteral("rave"));
    QCoreApplication::processEvents();
    require(catalog->count() == 1, "search should filter catalog entries");
    require(catalog->item(0)->data(Qt::UserRole).toString() == QStringLiteral("finepaper.ravenoc"),
            "search should keep matching RaveNoC entry");
}
```

`TestHarness` should build:

- two `PluginDescriptor`s (`finepaper.ravenoc`, `finepaper.fabric`);
- a local `ModuleRegistry(ModuleRegistry::LoadMode::Empty)` with one module type for each descriptor;
- `IpCatalogService`;
- `ProjectStateService`;
- `ProjectIpService`;
- `ActiveWorkspaceController`.

- [x] **Step 2: Write failing active workspace test**

Add:

```cpp
void testSelectingIpInstanceUpdatesActiveModuleAndToolLists() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.ensureInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    auto* moduleList = panel.findChild<QListWidget*>(QStringLiteral("activeModuleList"));
    auto* toolList = panel.findChild<QListWidget*>(QStringLiteral("activeToolList"));

    require(projectList->count() == 1, "project list should show one IP instance");
    require(moduleList->count() == 1, "active module list should show RaveTile");
    require(moduleList->item(0)->data(Qt::UserRole).toString() == QStringLiteral("RaveTile"),
            "active module row should store module type");
    require(toolList->count() >= 1, "active tool list should include topology/generator tools");
}
```

- [x] **Step 3: Write failing intent signal test**

Add:

```cpp
void testPanelEmitsAddAndSelectSignals() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    QString requestedAdd;
    QObject::connect(&panel, &IpCatalogPanel::addIpcoreRequested, &panel,
                     [&](const QString& ipcoreId) { requestedAdd = ipcoreId; });
    auto* catalog = panel.findChild<QListWidget*>(QStringLiteral("ipCatalogList"));
    QMetaObject::invokeMethod(catalog,
                              "itemActivated",
                              Qt::DirectConnection,
                              Q_ARG(QListWidgetItem*, catalog->item(0)));
    require(!requestedAdd.isEmpty(), "panel should expose add intent signal");

    require(harness.projectIpService.ensureInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    QString selectedIpcore;
    QString selectedInstance;
    QObject::connect(&panel, &IpCatalogPanel::selectIpInstanceRequested, &panel,
                     [&](const QString& ipcoreId, const QString& instanceId) {
                         selectedIpcore = ipcoreId;
                         selectedInstance = instanceId;
                     });
    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    projectList->setCurrentRow(0);
    QCoreApplication::processEvents();
    require(selectedIpcore == QStringLiteral("finepaper.ravenoc"),
            "panel should emit selected IP core");
    require(selectedInstance == QStringLiteral("ravenoc_0"),
            "panel should emit selected instance");
}
```

Include `<QMetaObject>` for this test helper.

- [x] **Step 4: Wire test target and verify failure**

Add `ipcatalogpanel_test` to `qt/xmake.lua` as a `qt.widgetapp` target, including:

- `src/panels/ipcatalogpanel.cpp`
- `src/ipcore/ipcatalogservice.cpp`
- `src/ipcore/internalmodulelibrarymodel.cpp`
- `src/ipcore/iptoolsmodel.cpp`
- `src/project/projectipservice.cpp`
- `src/project/projectstateservice.cpp`
- `src/workspace/activeworkspacecontroller.cpp`
- module/plugin registry support sources
- app/mainwindow sources for the MainWindow integration test added later
- Q_OBJECT headers `inc/**/ipcatalogpanel.h`, `inc/**/projectipservice.h`, `inc/**/activeworkspacecontroller.h`, `inc/**/mainwindow.h`

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
```

Expected: compile failure because `IpCatalogPanel` and models do not exist.

---

## Task 4.2: Implement Panel And Models

**Files:**

- Create: `qt/inc/panels/ipcatalogpanel.h`
- Create: `qt/src/panels/ipcatalogpanel.cpp`
- Create: `qt/inc/ipcore/internalmodulelibrarymodel.h`
- Create: `qt/src/ipcore/internalmodulelibrarymodel.cpp`
- Create: `qt/inc/ipcore/iptoolsmodel.h`
- Create: `qt/src/ipcore/iptoolsmodel.cpp`

- [x] **Step 1: Implement module model**

`InternalModuleLibraryModel::entriesForModuleTypes()` should:

- lookup each type in the provided registry or `ModuleRegistry::instance()`;
- skip missing types;
- set `label = ModuleTypeMetadata::paletteLabel(type)`;
- set `description = ModuleTypeMetadata::description(type)`;
- set `graphGroup = type->graphGroup`.

- [x] **Step 2: Implement tools model**

`IpToolsModel::entriesForWorkspace()` should return an empty vector when `state.hasActiveIp == false`. Otherwise append topology preset rows, generator row, and DRC row as defined above.

- [x] **Step 3: Implement panel layout**

Use a single `QVBoxLayout`, section labels, `QLineEdit`, and `QListWidget`s. Set list drag behavior:

```cpp
m_activeModuleList->setDragEnabled(true);
m_activeModuleList->setDragDropMode(QAbstractItemView::DragOnly);
```

Use a small local `ModuleListWidget` subclass in `ipcatalogpanel.cpp` that starts a `QDrag` with `application/x-moduletype` from `Qt::UserRole`.

- [x] **Step 4: Implement refresh paths**

Connect:

- search `textChanged` -> catalog refresh;
- catalog `itemActivated` and `itemDoubleClicked` -> `addIpcoreRequested`;
- project list `currentItemChanged` and `itemActivated` -> `selectIpInstanceRequested`;
- `ProjectStateService::ipInstanceRecordsChanged` -> project list refresh;
- `ProjectIpService::selectedIpInstanceChanged` -> project list selection sync;
- `ActiveWorkspaceController::activeWorkspaceChanged` -> active module/tool refresh.

Avoid duplicate select emissions by checking the currently selected stored `ipcoreId`/`instanceId`.

- [x] **Step 5: Run panel test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
```

Expected: tests pass except the MainWindow integration test, which is added in Task 4.3.

---

## Task 4.3: MainWindow Integration

**Files:**

- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/app/mainwindow.ui`
- Modify: `qt/inc/panels/palette.h`
- Modify: `qt/src/panels/palette.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add failing MainWindow integration test**

In `ipcatalogpanel_test.cpp`, add:

```cpp
void testMainWindowUsesIpCatalogDockWithoutActiveCombo() {
    MainWindow window;
    require(window.findChild<QDockWidget*>(QStringLiteral("ipCatalogDock")) != nullptr,
            "MainWindow should expose IP catalog dock");
    require(window.findChild<IpCatalogPanel*>(QStringLiteral("ipCatalogPanel")) != nullptr,
            "MainWindow should own IP catalog panel");
    require(window.findChild<QComboBox*>(QStringLiteral("activeIpCombo")) == nullptr,
            "MainWindow should remove toolbar active IP combo");
    require(window.findChild<QDockWidget*>(QStringLiteral("paletteDock")) == nullptr,
            "MainWindow should remove legacy palette dock");
}
```

- [x] **Step 2: Replace MainWindow palette ownership**

Header changes:

- forward declare `IpCatalogPanel` and `ActiveWorkspaceController`;
- remove `Palette* m_palette`, `QDockWidget* m_paletteDock`, `QComboBox* m_activeIpCombo`;
- add `std::unique_ptr<ActiveWorkspaceController> m_activeWorkspaceController`;
- add `IpCatalogPanel* m_ipCatalogPanel`;
- add `QDockWidget* m_ipCatalogDock`.

Constructor initialization:

```cpp
m_activeWorkspaceController(
    std::make_unique<ActiveWorkspaceController>(m_projectIpService.get(), m_ipCatalogService.get()))
```

`setupPanels()` creates `m_ipCatalogPanel` instead of `m_palette`.

- [x] **Step 3: Connect panel intents in MainWindow**

Connect:

```cpp
connect(m_ipCatalogPanel, &IpCatalogPanel::addIpcoreRequested, this, [this](const QString& ipcoreId) {
    const std::optional<IpCatalogEntry> entry = m_ipCatalogService->entry(ipcoreId);
    if (!entry.has_value()) return;
    const ProjectIpServiceResult result = m_projectIpService->ensureInstanceForIpcore(*entry);
    if (!result.success) {
        QMessageBox::warning(this, "IP Catalog", result.error);
        return;
    }
    setActivePluginId(ipcoreId);
});
connect(m_ipCatalogPanel,
        &IpCatalogPanel::selectIpInstanceRequested,
        this,
        [this](const QString& ipcoreId, const QString& instanceId) {
            if (m_projectIpService->selectInstance(ipcoreId, instanceId)) {
                setActivePluginId(ipcoreId);
            }
        });
```

Keep `setActivePluginId()` for existing topology/generator/palette-scoped internals, but remove active combo handling and `populateActiveIpSelector()`.

- [x] **Step 4: Replace dock wiring**

`setupDocks()` should create:

```cpp
m_ipCatalogDock = createDock("IP Catalog", m_ipCatalogPanel, Qt::LeftDockWidgetArea, "ipCatalogDock");
```

`resizeDocks()` and View menu should use `m_ipCatalogDock`.

Startup layout logs should say `IP catalog dock`.

- [x] **Step 5: Update designer skeleton and palette files**

`mainwindow.ui` should not define `activeIpCombo`; it can remain a minimal skeleton with `mainToolBar`. Add a comment-free object naming update only if needed.

`Palette` should no longer be referenced from `MainWindow`. Keep files buildable. If modifying, reduce constructor unused members:

```cpp
explicit Palette(QWidget* parent = nullptr);
```

Only do this if compile warnings or stale ownership make it necessary; full deletion is Node 8.

- [x] **Step 6: Run integration test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
```

Expected: pass.

---

## Task 4.4: Verification And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-4-ip-catalog-ui.md`

- [x] **Step 1: Run required verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake build -P qt qt
git diff --check
```

Expected: all commands pass.

- [x] **Step 2: Run Node 4 stale UI scan**

Run:

```bash
rg -n "activeIpCombo|paletteDock|new Palette|m_palette" qt/inc/app qt/src/app qt/test/ipcatalogpanel_test.cpp
```

Expected: no hits except negative assertions in `ipcatalogpanel_test.cpp`.

- [x] **Step 3: Supervisor preflight**

Send current diff, verification evidence, and this checklist to Turing. Required approval points:

- `IpCatalogPanel` widget hierarchy and object names match this plan.
- Panel emits add/select intent signals and does not mutate `Graph`.
- MainWindow has `ipCatalogDock`.
- MainWindow no longer has `activeIpCombo` or `paletteDock`.
- Required tests passed.
- `.codex/`, `.superpowers/`, and `image.png` remain uncommitted.

- [x] **Step 4: Archive Node 4**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-4-ip-catalog-ui.md
git add -f qt/inc/ipcore/internalmodulelibrarymodel.h \
  qt/src/ipcore/internalmodulelibrarymodel.cpp \
  qt/inc/ipcore/iptoolsmodel.h \
  qt/src/ipcore/iptoolsmodel.cpp
git commit -m "archive: complete node-4 ip catalog ui"
```

Expected: commit succeeds and untracked helper artifacts remain uncommitted.

---

## Self-Review

- Spec coverage: This plan defines the required widget hierarchy and exact test object names, covers add/select signals, covers MainWindow `ipCatalogDock`, and removes `activeIpCombo`.
- Scope control: Node 4 keeps legacy module drag MIME `application/x-moduletype`; Node 5 owns scoped module payload `application/x-finepaper-module`.
- Risk callout: MainWindow still needs `m_activePluginId` for topology presets and generation until Node 5 scopes all workspace tools through active IP instances.
