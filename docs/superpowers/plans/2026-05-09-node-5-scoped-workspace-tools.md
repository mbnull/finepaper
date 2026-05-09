# Node 5 Scoped Workspace Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use reasoning effort `high` for implementation workers and `xhigh` for review of topology command behavior.

**Goal:** Make module creation, canvas creation menus, and topology presets operate through the selected project IP core instance and command history.

**Architecture:** The IP Catalog emits a scoped module drag payload, and `NodeEditorWidget` accepts creation only when the payload matches `ActiveWorkspaceController::state()`. Topology preset application becomes a `Command`, so MainWindow no longer mutates `Graph` directly or manually adjusts dirty state. Module records retain IP-core ownership from the active workspace so save/load observes the selected IP-core boundary.

**Tech Stack:** C++23, Qt 6 Widgets/Core JSON APIs, QtNodes, xmake Qt widget tests.

---

## File Structure

- Modify `qt/inc/graph/module.h`: add `ipcoreId()` / `setIpcoreId()` ownership accessors and copy ownership in `clone()`.
- Modify `qt/src/graph/module.cpp`: store IP-core ownership on modules.
- Modify `qt/inc/nodeeditor/nodeeditorwidget.h`: accept `ActiveWorkspaceController*`, expose `availableCreateModuleTypes()` for tests/menu construction, and update scoped create helpers.
- Modify `qt/src/nodeeditor/nodeeditorwidget.cpp`: create menu uses active workspace module types and topology-independent creation uses active IP ownership.
- Modify `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`: parse and validate `application/x-finepaper-module` payloads, remove legacy MIME acceptance, and create scoped modules.
- Modify `qt/inc/nodeeditor/nodeeditorentityfactory.h`: add `ipcoreId` parameter to `createModule()`.
- Modify `qt/src/nodeeditor/nodeeditorentityfactory.cpp`: reject module types outside the requested IP core and stamp the created module owner.
- Modify `qt/inc/panels/ipcatalogpanel.h`: no API change; signal names remain.
- Modify `qt/src/panels/ipcatalogpanel.cpp`: active module drags emit scoped JSON payloads.
- Modify `qt/src/panels/palette.cpp`: remove the unused legacy drag source so stale `application/x-moduletype` MIME disappears in Node 5.
- Modify `qt/inc/commands/addmodulecommand.h`: store expected IP-core ownership and reject mismatched module inserts.
- Modify `qt/src/commands/addmodulecommand.cpp`: enforce ownership before insertion.
- Modify `qt/inc/commands/commandmanager.h`: return rejected commands from `executeCommand()` so callers can inspect failure details.
- Modify `qt/src/commands/commandmanager.cpp`: keep accepted-command semantics and return failed commands to the caller.
- Create `qt/inc/commands/compositecommand.h`: command that executes children in order and undoes them in reverse order.
- Create `qt/src/commands/compositecommand.cpp`: rollback executed children when a child command fails.
- Create `qt/inc/commands/topologypresetcommand.h`: command wrapper for applying and undoing topology presets.
- Create `qt/src/commands/topologypresetcommand.cpp`: execute through `TopologyPresetBuilder`, undo only created connections/modules.
- Modify `qt/inc/topology/topologypresetbuilder.h`: rename request ownership from `pluginId` to `ipcoreId`.
- Modify `qt/src/topology/topologypresetbuilder.cpp`: stamp generated modules with `request.ipcoreId` and validate router ownership against `ipcoreId`.
- Modify `qt/src/project/graphprojectserializer.cpp`: persist module-owned IP-core IDs and restore them on load.
- Modify `qt/src/app/mainwindow.cpp`: pass `ActiveWorkspaceController` to `NodeEditorWidget`; topology menu/actions use active workspace and `TopologyPresetCommand`.
- Modify `qt/test/nodeeditor_geometry_test.cpp`: add scoped module drag/drop and active create-menu tests.
- Modify `qt/test/topology_preset_test.cpp`: add topology command undo/redo and ownership tests; update request field names.
- Modify `qt/test/commandmanager_test.cpp`: add composite command rollback and undo order tests.
- Modify `qt/test/projectdocument_test.cpp`: assert serialized module IP-core ownership comes from the module owner set by scoped creation.
- Modify `qt/test/ipcatalogpanel_test.cpp`: assert the panel still lists active modules after scoped MIME changes.
- Modify `qt/xmake.lua`: add new command sources and any new Q_OBJECT/header inputs required by changed test targets.

---

## Data Contracts

### Scoped Module MIME

Replace the Node 4 drag MIME:

```text
application/x-moduletype
```

with:

```text
application/x-finepaper-module
```

The payload is compact JSON:

```json
{
  "ipcore": "finepaper.ravenoc",
  "instance": "ravenoc_0",
  "type": "RaveTile"
}
```

`NodeEditorWidget` accepts the payload only when:

- `ActiveWorkspaceState::hasActiveIp == true`;
- `payload.ipcore == state.ipcoreId`;
- `payload.instance == state.instanceId`;
- `payload.type` exists in `state.moduleTypes`;
- `ModuleRegistry::instance().getType(payload.type)->pluginId == state.ipcoreId`.

Legacy `application/x-moduletype` payloads must be ignored.

### Module Ownership

`Module` receives this API:

```cpp
QString ipcoreId() const { return m_ipcoreId; }
void setIpcoreId(const QString& ipcoreId);
```

`NodeEditorEntityFactory::createModule()` signature becomes:

```cpp
std::unique_ptr<Module> createModule(Graph* graph,
                                     const QString& moduleId,
                                     const QString& moduleType,
                                     const QString& ipcoreId);
```

The function returns `nullptr` when `ipcoreId` is empty or when the module type does not belong to that IP core.

`GraphProjectSerializer::toProject()` should write `module->ipcoreId()` for `ProjectModuleRecord::ipcoreId`. Existing direct test modules that have no owner can still be stamped by tests before serialization; scoped creation paths must always set the owner.

### Topology Preset Command

`TopologyPresetRequest` becomes:

```cpp
struct TopologyPresetRequest {
    QString ipcoreId;
    TopologyPresetDescriptor preset;
    QHash<QString, int> parameters;
};
```

`TopologyPresetCommand` public API:

```cpp
class TopologyPresetCommand : public Command {
public:
    TopologyPresetCommand(Graph* graph,
                          const ModuleRegistry* registry,
                          TopologyPresetRequest request);

    void execute() override;
    void undo() override;
    const TopologyPresetResult& result() const;

private:
    Graph* m_graph = nullptr;
    const ModuleRegistry* m_registry = nullptr;
    TopologyPresetRequest m_request;
    TopologyPresetResult m_result;
};
```

Undo removes `m_result.connectionIds` first, then removes `m_result.moduleIds` in reverse order.

---

## Task 5.1: Scoped Module Creation Tests

**Files:**

- Modify: `qt/test/nodeeditor_geometry_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add active workspace test harness**

In `qt/test/nodeeditor_geometry_test.cpp`, add these includes near the existing includes:

```cpp
#include "ipcore/ipcatalogservice.h"
#include "project/projectipservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
```

Add this helper code inside the anonymous namespace:

```cpp
constexpr auto ScopedModuleMime = "application/x-finepaper-module";

PluginDescriptor nodeEditorRavenocDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("noc");
    return descriptor;
}

PluginDescriptor nodeEditorFabricDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.fabric");
    descriptor.name = QStringLiteral("Fabric");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("fabric");
    return descriptor;
}

ModuleType scopedEditorType(const QString& name, const QString& ipcoreId) {
    ModuleType type;
    type.name = name;
    type.pluginId = ipcoreId;
    type.paletteLabel = name;
    type.defaultParameters.insert(QStringLiteral("x"), Parameter(QStringLiteral("x"), 0));
    type.defaultParameters.insert(QStringLiteral("y"), Parameter(QStringLiteral("y"), 0));
    type.defaultPorts.push_back(Port(QStringLiteral("out"),
                                     Port::Direction::Output,
                                     QStringLiteral("bus"),
                                     QStringLiteral("Out")));
    return type;
}

struct ScopedNodeEditorHarness {
    Graph graph;
    CommandManager commandManager;
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    PluginDescriptor ravenoc = nodeEditorRavenocDescriptor();
    PluginDescriptor fabric = nodeEditorFabricDescriptor();
    IpCatalogService catalog;
    ProjectStateService stateService;
    ProjectIpService projectIpService;
    ActiveWorkspaceController workspaceController;
    NodeEditorWidget editor;

    ScopedNodeEditorHarness()
        : catalog(QList<PluginDescriptor>{ravenoc, fabric}, &registry),
          projectIpService(&stateService),
          workspaceController(&projectIpService, &catalog),
          editor(&graph, &stateService, &workspaceController, &commandManager) {
        require(registry.registerType(scopedEditorType(QStringLiteral("RaveTile"),
                                                       QStringLiteral("finepaper.ravenoc"))),
                "RaveTile test type should register");
        require(registry.registerType(scopedEditorType(QStringLiteral("FabricSwitch"),
                                                       QStringLiteral("finepaper.fabric"))),
                "FabricSwitch test type should register");
        catalog = IpCatalogService(QList<PluginDescriptor>{ravenoc, fabric}, &registry);
        editor.resize(320, 240);
        editor.show();
        QCoreApplication::processEvents();
    }

    IpCatalogEntry ravenocEntry() const {
        const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("finepaper.ravenoc"));
        require(entry.has_value(), "RaveNoC entry should exist");
        return *entry;
    }

    void selectRavenoc() {
        const ProjectIpServiceResult result = projectIpService.ensureInstanceForIpcore(ravenocEntry());
        require(result.success, "RaveNoC instance should be selected");
        QCoreApplication::processEvents();
    }
};

std::unique_ptr<QMimeData> scopedModuleMime(const QString& ipcoreId,
                                            const QString& instanceId,
                                            const QString& moduleType) {
    QJsonObject object;
    object.insert(QStringLiteral("ipcore"), ipcoreId);
    object.insert(QStringLiteral("instance"), instanceId);
    object.insert(QStringLiteral("type"), moduleType);
    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setData(ScopedModuleMime,
                      QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mimeData;
}

bool sendScopedDrop(NodeEditorWidget& editor, QMimeData* mimeData) {
    QDragEnterEvent enter(QPoint(16, 16),
                          Qt::CopyAction,
                          mimeData,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &enter);

    QDropEvent drop(QPointF(48, 64),
                    Qt::CopyAction,
                    mimeData,
                    Qt::LeftButton,
                    Qt::NoModifier);
    QCoreApplication::sendEvent(&editor, &drop);
    QCoreApplication::processEvents();
    return drop.isAccepted();
}
```

- [x] **Step 2: Add failing scoped drop tests**

Add these tests before `main()`:

```cpp
void testScopedDropRejectsMissingActiveInstance() {
    ScopedNodeEditorHarness harness;
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop without selected active IP instance should be rejected");
    require(harness.graph.modules().empty(),
            "drop without selected active IP instance should not create a module");
}

void testScopedDropRejectsDifferentIpcore() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.fabric"),
                                     QStringLiteral("fabric_0"),
                                     QStringLiteral("FabricSwitch"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "drop for a different IP core should be rejected");
    require(harness.graph.modules().empty(),
            "drop for a different IP core should not create a module");
}

void testScopedDropRejectsLegacyModuleTypeMime() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setData("application/x-moduletype", QByteArray("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(!accepted, "legacy module MIME should be rejected");
    require(harness.graph.modules().empty(),
            "legacy module MIME should not create a module");
}

void testScopedDropCreatesOwnedModule() {
    ScopedNodeEditorHarness harness;
    harness.selectRavenoc();
    auto mimeData = scopedModuleMime(QStringLiteral("finepaper.ravenoc"),
                                     QStringLiteral("ravenoc_0"),
                                     QStringLiteral("RaveTile"));

    const bool accepted = sendScopedDrop(harness.editor, mimeData.get());

    require(accepted, "matching scoped module drop should be accepted");
    require(harness.graph.modules().size() == 1, "matching scoped drop should create one module");
    const Module* module = harness.graph.modules().front().get();
    require(module->type() == QStringLiteral("RaveTile"),
            "created module should use payload module type");
    require(module->ipcoreId() == QStringLiteral("finepaper.ravenoc"),
            "created module should keep active IP-core ownership");
    require(harness.commandManager.canUndo(),
            "scoped module creation should enter command history");
}

void testCreateMenuTypesFollowActiveWorkspace() {
    ScopedNodeEditorHarness harness;
    require(harness.editor.availableCreateModuleTypes().isEmpty(),
            "create menu should be empty without active workspace");

    harness.selectRavenoc();

    const QStringList moduleTypes = harness.editor.availableCreateModuleTypes();
    require(moduleTypes.size() == 1, "create menu should list active workspace modules only");
    require(moduleTypes.first() == QStringLiteral("RaveTile"),
            "create menu should list RaveNoC module type");
}
```

Call the tests from `main()` after the existing geometry tests:

```cpp
        testScopedDropRejectsMissingActiveInstance();
        testScopedDropRejectsDifferentIpcore();
        testScopedDropRejectsLegacyModuleTypeMime();
        testScopedDropCreatesOwnedModule();
        testCreateMenuTypesFollowActiveWorkspace();
```

- [x] **Step 3: Add failing project ownership serialization test**

In `qt/test/projectdocument_test.cpp`, add:

```cpp
void testProjectSerializerUsesModuleIpcoreOwnership() {
    ModuleType type = makeProjectXpType();
    type.name = QStringLiteral("ProjectDocOwnedXP");
    type.pluginId = QStringLiteral("finepaper.owned");
    ModuleRegistry::instance().registerType(type);

    Graph graph;
    auto module = instantiate(type, QStringLiteral("owned_node"));
    module->setIpcoreId(QStringLiteral("finepaper.owned"));
    require(graph.addModule(std::move(module)), "owned module should add");

    const ProjectDocument document = GraphProjectSerializer::toProject(graph, QStringLiteral("owned"));

    require(document.modules.size() == 1, "serializer should write one module");
    require(document.modules.first().ipcoreId == QStringLiteral("finepaper.owned"),
            "serializer should preserve module IP-core ownership");
}
```

Call it from `main()` after `testProjectWriterUsesIpcoreVocabulary()`.

- [x] **Step 4: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected:

- `nodeeditor_geometry_test` fails to compile because `NodeEditorWidget` does not accept `ActiveWorkspaceController*`, `availableCreateModuleTypes()` does not exist, and `Module::ipcoreId()` does not exist.
- `projectdocument_test` fails to compile because `Module::setIpcoreId()` does not exist.

---

## Task 5.2: Scoped Drag Payload And Node Editor Implementation

**Files:**

- Modify: `qt/inc/graph/module.h`
- Modify: `qt/src/graph/module.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorentityfactory.h`
- Modify: `qt/src/nodeeditor/nodeeditorentityfactory.cpp`
- Modify: `qt/inc/commands/addmodulecommand.h`
- Modify: `qt/src/commands/addmodulecommand.cpp`
- Modify: `qt/src/panels/ipcatalogpanel.cpp`
- Modify: `qt/src/panels/palette.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add module ownership storage**

Update `qt/inc/graph/module.h`:

```cpp
    QString ipcoreId() const { return m_ipcoreId; }
    void setIpcoreId(const QString& ipcoreId);
```

Add private storage:

```cpp
    QString m_ipcoreId;
```

Update `qt/src/graph/module.cpp`:

```cpp
void Module::setIpcoreId(const QString& ipcoreId) {
    m_ipcoreId = ipcoreId;
}
```

Update `Module::clone()`:

```cpp
    cloned->m_ipcoreId = m_ipcoreId;
```

- [x] **Step 2: Scope module factory creation**

Update `qt/inc/nodeeditor/nodeeditorentityfactory.h` signature:

```cpp
std::unique_ptr<Module> createModule(Graph* graph,
                                     const QString& moduleId,
                                     const QString& moduleType,
                                     const QString& ipcoreId);
```

Update `qt/src/nodeeditor/nodeeditorentityfactory.cpp`:

```cpp
std::unique_ptr<Module> createModule(Graph* graph,
                                     const QString& moduleId,
                                     const QString& moduleType,
                                     const QString& ipcoreId) {
    const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
    if (!type || ipcoreId.trimmed().isEmpty() || type->pluginId != ipcoreId) {
        return {};
    }

    auto module = std::make_unique<Module>(moduleId, moduleType);
    module->setIpcoreId(ipcoreId);
    for (const auto& port : type->defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }

    assignModuleIdentity(graph, module.get());
    return module;
}
```

- [x] **Step 3: Enforce IP-core ownership in AddModuleCommand**

Update `qt/inc/commands/addmodulecommand.h`:

```cpp
    AddModuleCommand(Graph* graph, std::unique_ptr<Module> module, QString expectedIpcoreId);
```

Add member:

```cpp
    QString m_expectedIpcoreId;
```

Update `qt/src/commands/addmodulecommand.cpp` constructor and execute:

```cpp
AddModuleCommand::AddModuleCommand(Graph* graph,
                                   std::unique_ptr<Module> module,
                                   QString expectedIpcoreId)
    : m_graph(graph),
      m_module(std::move(module)),
      m_expectedIpcoreId(std::move(expectedIpcoreId)) {
    if (m_module) {
        m_moduleId = m_module->id();
    }
}
```

Add this guard at the start of `execute()` after the empty/duplicate checks:

```cpp
    if (!m_expectedIpcoreId.isEmpty() && m_module->ipcoreId() != m_expectedIpcoreId) {
        return;
    }
```

- [x] **Step 4: Add active workspace dependency to NodeEditorWidget**

Update constructor declaration in `qt/inc/nodeeditor/nodeeditorwidget.h`:

```cpp
    NodeEditorWidget(Graph* graph,
                     ProjectStateService* projectStateService,
                     ActiveWorkspaceController* workspaceController,
                     CommandManager* commandManager,
                     QWidget* parent = nullptr);
```

Forward declare:

```cpp
class ActiveWorkspaceController;
```

Add public method:

```cpp
    QStringList availableCreateModuleTypes() const;
```

Add private helper declarations:

```cpp
    struct ScopedModulePayload {
        QString ipcoreId;
        QString instanceId;
        QString moduleType;
    };

    std::optional<ScopedModulePayload> scopedModulePayload(const QMimeData* mimeData) const;
    bool acceptsScopedModulePayload(const ScopedModulePayload& payload) const;
    bool createModuleAt(const ScopedModulePayload& payload, const QPointF& scenePos);
    QString activeIpcoreId() const;
```

Add member:

```cpp
    ActiveWorkspaceController* m_workspaceController;
```

Update constructor definition and `MainWindow::setupPanels()` call:

```cpp
    m_nodeEditor = new NodeEditorWidget(m_graph,
                                        m_projectStateService.get(),
                                        m_activeWorkspaceController.get(),
                                        m_commandManager.get(),
                                        this);
```

- [x] **Step 5: Implement scoped payload parsing**

In `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`, replace `draggedModuleType()` with:

```cpp
constexpr auto ScopedModuleMime = "application/x-finepaper-module";

std::optional<NodeEditorWidget::ScopedModulePayload>
NodeEditorWidget::scopedModulePayload(const QMimeData* mimeData) const {
    if (!mimeData || !mimeData->hasFormat(ScopedModuleMime)) {
        return std::nullopt;
    }

    const QJsonDocument document = QJsonDocument::fromJson(mimeData->data(ScopedModuleMime));
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    ScopedModulePayload payload;
    payload.ipcoreId = object.value(QStringLiteral("ipcore")).toString();
    payload.instanceId = object.value(QStringLiteral("instance")).toString();
    payload.moduleType = object.value(QStringLiteral("type")).toString();
    if (payload.ipcoreId.isEmpty() || payload.instanceId.isEmpty() || payload.moduleType.isEmpty()) {
        return std::nullopt;
    }
    return payload;
}
```

Add includes:

```cpp
#include "workspace/activeworkspacecontroller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <optional>
```

- [x] **Step 6: Implement active workspace acceptance**

In `qt/src/nodeeditor/nodeeditorwidget.cpp`, include:

```cpp
#include "workspace/activeworkspacecontroller.h"
```

Add:

```cpp
QString NodeEditorWidget::activeIpcoreId() const {
    if (!m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return {};
    }
    return m_workspaceController->state().ipcoreId;
}

QStringList NodeEditorWidget::availableCreateModuleTypes() const {
    if (!m_workspaceController || !m_workspaceController->state().hasActiveIp) {
        return {};
    }
    return m_workspaceController->state().moduleTypes;
}

bool NodeEditorWidget::acceptsScopedModulePayload(const ScopedModulePayload& payload) const {
    if (!m_workspaceController) {
        return false;
    }
    const ActiveWorkspaceState& state = m_workspaceController->state();
    if (!state.hasActiveIp ||
        payload.ipcoreId != state.ipcoreId ||
        payload.instanceId != state.instanceId ||
        !state.moduleTypes.contains(payload.moduleType)) {
        return false;
    }

    const ModuleType* type = ModuleRegistry::instance().getType(payload.moduleType);
    return type && type->pluginId == state.ipcoreId;
}
```

- [x] **Step 7: Replace drag/drop handlers**

Update `dragEnterEvent()`, `dragMoveEvent()`, `handleViewportDragEnter()`, `handleViewportDragMove()`, `handleViewportDrop()`, and `dropEvent()` to:

- parse `scopedModulePayload(event->mimeData())`;
- reject when parsing fails or `acceptsScopedModulePayload()` is false;
- call `m_view->beginPaletteDrag()` / `updatePaletteDrag()` with `payload.moduleType`;
- call `createModuleAt(*payload, scenePos)` on drop.

The drop handler must end with:

```cpp
    if (createModuleAt(*payload, m_view->mapToScene(event->position().toPoint()))) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
```

- [x] **Step 8: Scope context menu creation**

Update `showCanvasCreateMenu()`:

```cpp
    const QStringList moduleTypes = availableCreateModuleTypes();
    if (moduleTypes.isEmpty()) {
        return true;
    }
```

When an action is selected:

```cpp
    ScopedModulePayload payload;
    payload.ipcoreId = m_workspaceController->state().ipcoreId;
    payload.instanceId = m_workspaceController->state().instanceId;
    payload.moduleType = selectedAction->data().toString();
    return createModuleAt(payload, m_view->mapToScene(viewportPos));
```

Replace the old `createModuleAt(const QString&, const QPointF&)` implementation with:

```cpp
bool NodeEditorWidget::createModuleAt(const ScopedModulePayload& payload, const QPointF& scenePos) {
    if (!acceptsScopedModulePayload(payload)) {
        return false;
    }

    const QString moduleId = NodeEditorEntityFactory::generateEntityId();
    auto module = NodeEditorEntityFactory::createModule(m_graph,
                                                        moduleId,
                                                        payload.moduleType,
                                                        payload.ipcoreId);
    if (!module) {
        return false;
    }

    const QPointF clampedPos = clampNodePosition(QtNodes::InvalidNodeId, scenePos);
    if (module->parameters().contains(QStringLiteral("x"))) {
        module->setParameter(QStringLiteral("x"), static_cast<int>(clampedPos.x()));
    }
    if (module->parameters().contains(QStringLiteral("y"))) {
        module->setParameter(QStringLiteral("y"), static_cast<int>(clampedPos.y()));
    }

    auto command = std::make_unique<AddModuleCommand>(m_graph,
                                                      std::move(module),
                                                      payload.ipcoreId);
    m_commandManager->executeCommand(std::move(command));
    return m_graph->getModule(moduleId) != nullptr;
}
```

- [x] **Step 9: Emit scoped MIME from IP Catalog panel and remove legacy Palette MIME**

In `qt/src/panels/ipcatalogpanel.cpp`, replace `ModuleTypeMime` with:

```cpp
constexpr int IpcoreIdRole = Qt::UserRole + 2;
constexpr int ActiveInstanceIdRole = Qt::UserRole + 3;
constexpr auto ScopedModuleMime = "application/x-finepaper-module";
```

In `ModuleListWidget::startDrag()`, read all three fields and emit JSON:

```cpp
        const QString moduleType = item->data(Qt::UserRole).toString();
        const QString ipcoreId = item->data(IpcoreIdRole).toString();
        const QString instanceId = item->data(ActiveInstanceIdRole).toString();
        if (moduleType.trimmed().isEmpty() ||
            ipcoreId.trimmed().isEmpty() ||
            instanceId.trimmed().isEmpty()) {
            return;
        }

        QJsonObject object;
        object.insert(QStringLiteral("ipcore"), ipcoreId);
        object.insert(QStringLiteral("instance"), instanceId);
        object.insert(QStringLiteral("type"), moduleType);
        mimeData->setData(ScopedModuleMime,
                          QJsonDocument(object).toJson(QJsonDocument::Compact));
```

Add includes:

```cpp
#include <QJsonDocument>
#include <QJsonObject>
```

When active module list items are built, add:

```cpp
            item->setData(IpcoreIdRole, state.ipcoreId);
            item->setData(ActiveInstanceIdRole, state.instanceId);
```

In `qt/src/panels/palette.cpp`, remove the `DraggableListWidget` subclass, `QDrag`, `QMimeData`, and `QMouseEvent` includes. `Palette::setupUI()` should create a plain list widget:

```cpp
void Palette::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Module Types:"));

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);
}
```

After this change, `qt/src/panels/palette.cpp` must not contain `application/x-moduletype`.

- [x] **Step 10: Persist module ownership**

In `qt/src/project/graphprojectserializer.cpp`, change `toProject()` module ownership:

```cpp
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        const QString ipcoreId = module->ipcoreId().isEmpty()
            ? (type ? type->pluginId : QString())
            : module->ipcoreId();
```

In `populateGraph()`, after instantiating a module:

```cpp
        module->setIpcoreId(record.ipcoreId);
```

- [x] **Step 11: Run scoped creation tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: both tests pass.

---

## Task 5.3: Composite And Topology Preset Commands

**Files:**

- Create: `qt/inc/commands/compositecommand.h`
- Create: `qt/src/commands/compositecommand.cpp`
- Create: `qt/inc/commands/topologypresetcommand.h`
- Create: `qt/src/commands/topologypresetcommand.cpp`
- Modify: `qt/inc/commands/commandmanager.h`
- Modify: `qt/src/commands/commandmanager.cpp`
- Modify: `qt/inc/topology/topologypresetbuilder.h`
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify: `qt/test/commandmanager_test.cpp`
- Modify: `qt/test/topology_preset_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Add failing command-manager return and composite command tests**

In `qt/test/commandmanager_test.cpp`, add:

```cpp
#include "commands/compositecommand.h"
```

Add:

```cpp
void testExecuteCommandReturnsRejectedCommandOnFailure() {
    std::vector<std::string> events;
    CommandManager manager;

    std::unique_ptr<Command> rejected =
        manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha", false));

    require(rejected != nullptr, "failed command should be returned to caller");
    require(!rejected->wasExecuted(), "returned failed command should expose failed status");
    require(!manager.canUndo(), "failed command should not enter undo history");
    require(manager.currentStateId() == 0, "failed command should not advance state");
}

void testExecuteCommandReturnsNullOnAcceptedCommand() {
    std::vector<std::string> events;
    CommandManager manager;

    std::unique_ptr<Command> rejected =
        manager.executeCommand(std::make_unique<RecordingCommand>(events, "alpha"));

    require(rejected == nullptr, "accepted command should not be returned");
    require(manager.canUndo(), "accepted command should enter undo history");
    require(manager.currentStateId() != 0, "accepted command should advance state");
}
```

Add:

```cpp
void testCompositeCommandUndoRunsChildrenInReverseOrder() {
    std::vector<std::string> events;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta"));

    composite->execute();
    require(composite->wasExecuted(), "composite command should execute when all children execute");
    composite->undo();

    require(events.size() == 4, "composite should record two executes and two undos");
    require(events[0] == "execute:alpha", "first child should execute first");
    require(events[1] == "execute:beta", "second child should execute second");
    require(events[2] == "undo:beta", "second child should undo first");
    require(events[3] == "undo:alpha", "first child should undo last");
}

void testCompositeCommandRollsBackExecutedChildrenOnFailure() {
    std::vector<std::string> events;
    auto composite = std::make_unique<CompositeCommand>();
    composite->addCommand(std::make_unique<RecordingCommand>(events, "alpha"));
    composite->addCommand(std::make_unique<RecordingCommand>(events, "beta", false));

    composite->execute();

    require(!composite->wasExecuted(), "composite should fail when a child fails");
    require(events.size() == 3, "composite should roll back the executed child");
    require(events[0] == "execute:alpha", "first child should execute");
    require(events[1] == "execute:beta", "second child should attempt execute");
    require(events[2] == "undo:alpha", "first child should roll back");
}
```

Call all four new tests from `main()`.

- [x] **Step 2: Return rejected commands from CommandManager**

Update `qt/inc/commands/commandmanager.h`:

```cpp
    std::unique_ptr<Command> executeCommand(std::unique_ptr<Command> command);
```

Update `qt/src/commands/commandmanager.cpp` signature and final branches:

```cpp
std::unique_ptr<Command> CommandManager::executeCommand(std::unique_ptr<Command> command) {
    qDebug() << "Executing command"
             << "undoDepth" << m_undoStack.size()
             << "redoDepth" << m_redoStack.size()
             << "stateId" << m_currentStateId;
    command->execute();
    if (command->wasExecuted()) {
        HistoryEntry entry;
        entry.command = std::move(command);
        entry.beforeStateId = m_currentStateId;
        entry.afterStateId = m_nextStateId++;
        m_currentStateId = entry.afterStateId;
        m_undoStack.push(std::move(entry));
        while (!m_redoStack.empty()) {
            m_redoStack.pop();
        }
        qDebug() << "Command executed"
                << "undoDepth" << m_undoStack.size()
                << "redoDepth" << m_redoStack.size()
                << "stateId" << m_currentStateId;
        return nullptr;
    }

    qDebug() << "Command execution produced no state change";
    return command;
}
```

- [x] **Step 3: Implement CompositeCommand**

Create `qt/inc/commands/compositecommand.h`:

```cpp
#pragma once

#include "commands/command.h"

#include <memory>
#include <vector>

class CompositeCommand : public Command {
public:
    void addCommand(std::unique_ptr<Command> command);
    void execute() override;
    void undo() override;

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    int m_executedCount = 0;
};
```

Create `qt/src/commands/compositecommand.cpp`:

```cpp
#include "commands/compositecommand.h"

void CompositeCommand::addCommand(std::unique_ptr<Command> command) {
    if (command) {
        m_commands.push_back(std::move(command));
    }
}

void CompositeCommand::execute() {
    m_executed = false;
    m_executedCount = 0;
    for (auto& command : m_commands) {
        command->execute();
        if (!command->wasExecuted()) {
            for (int index = m_executedCount - 1; index >= 0; --index) {
                m_commands.at(static_cast<std::size_t>(index))->undo();
            }
            m_executedCount = 0;
            return;
        }
        ++m_executedCount;
    }
    m_executed = m_executedCount > 0;
}

void CompositeCommand::undo() {
    for (int index = m_executedCount - 1; index >= 0; --index) {
        m_commands.at(static_cast<std::size_t>(index))->undo();
    }
    m_executedCount = 0;
    m_executed = false;
}
```

- [x] **Step 4: Add failing topology command tests**

In `qt/test/topology_preset_test.cpp`, add:

```cpp
#include "commands/commandmanager.h"
#include "commands/topologypresetcommand.h"
```

Rename every `request.pluginId = ...` to:

```cpp
    request.ipcoreId = QStringLiteral("finepaper.noc");
```

For repository RaveNoC:

```cpp
    request.ipcoreId = plugins.first().id;
```

Add tests:

```cpp
void testTopologyPresetCommandIsUndoableAndRedoable() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    CommandManager manager;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 2);

    manager.executeCommand(std::make_unique<TopologyPresetCommand>(&graph, &registry, request));

    require(graph.modules().size() == 4, "command should create mesh modules");
    require(graph.connections().size() == 4, "command should create mesh connections");
    require(manager.canUndo(), "topology command should be undoable");
    const int dirtyState = manager.currentStateId();
    require(dirtyState != 0, "topology command should advance command history state");

    manager.undo();

    require(graph.modules().empty(), "undo should remove topology modules");
    require(graph.connections().empty(), "undo should remove topology connections");
    require(manager.currentStateId() == 0, "undo should restore clean command state");
    require(manager.canRedo(), "topology command should be redoable");

    manager.redo();

    require(graph.modules().size() == 4, "redo should recreate topology modules");
    require(graph.connections().size() == 4, "redo should recreate topology connections");
    require(manager.currentStateId() == dirtyState, "redo should restore dirty command state");
}

void testTopologyPresetCommandStampsModuleOwnership() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.registerType(routerType(QStringLiteral("XP"), QStringLiteral("finepaper.noc"))),
            "router type should register");

    Graph graph;
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.noc");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 1);
    request.parameters.insert(QStringLiteral("cols"), 1);

    TopologyPresetCommand command(&graph, &registry, request);
    command.execute();

    require(command.wasExecuted(), "topology command should execute");
    require(graph.modules().size() == 1, "one-node mesh should create one module");
    require(graph.modules().front()->ipcoreId() == QStringLiteral("finepaper.noc"),
            "topology command should stamp module IP-core ownership");
}
```

Call both from `main()`.

- [x] **Step 5: Implement topology preset command**

Create `qt/inc/commands/topologypresetcommand.h` with the API from the data contract.

Create `qt/src/commands/topologypresetcommand.cpp`:

```cpp
#include "commands/topologypresetcommand.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"

TopologyPresetCommand::TopologyPresetCommand(Graph* graph,
                                             const ModuleRegistry* registry,
                                             TopologyPresetRequest request)
    : m_graph(graph),
      m_registry(registry),
      m_request(std::move(request)) {}

void TopologyPresetCommand::execute() {
    m_executed = false;
    m_result = {};
    if (!m_graph || !m_registry) {
        return;
    }
    m_result = TopologyPresetBuilder::apply(m_graph, *m_registry, m_request);
    m_executed = m_result.success;
}

void TopologyPresetCommand::undo() {
    if (!m_graph || !m_result.success) {
        return;
    }
    for (const QString& connectionId : m_result.connectionIds) {
        m_graph->removeConnection(connectionId);
    }
    for (int index = m_result.moduleIds.size() - 1; index >= 0; --index) {
        m_graph->removeModule(m_result.moduleIds.at(index));
    }
    m_executed = false;
}

const TopologyPresetResult& TopologyPresetCommand::result() const {
    return m_result;
}
```

- [x] **Step 6: Rename topology request ownership and stamp modules**

In `qt/inc/topology/topologypresetbuilder.h`, rename:

```cpp
    QString pluginId;
```

to:

```cpp
    QString ipcoreId;
```

In `qt/src/topology/topologypresetbuilder.cpp`, update `instantiateModule()` signature:

```cpp
std::unique_ptr<Module> instantiateModule(const ModuleType& type,
                                          const QString& id,
                                          const QString& ipcoreId,
                                          int row,
                                          int col)
```

Set ownership:

```cpp
    module->setIpcoreId(ipcoreId);
```

Update create calls:

```cpp
graph->addModule(instantiateModule(routerType, id, request.ipcoreId, row, col))
```

and:

```cpp
graph->addModule(instantiateModule(routerType, id, request.ipcoreId, 0, index))
```

Update router validation:

```cpp
    if (!routerType || routerType->pluginId != request.ipcoreId) {
        return failure(QStringLiteral("Router module %1 is not part of active IP %2")
                           .arg(request.preset.routerModule, request.ipcoreId));
    }
```

- [x] **Step 7: Run command tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt commandmanager_test
CCACHE_DISABLE=1 xmake run -P qt topology_preset_test
```

Expected: both tests pass.

---

## Task 5.4: MainWindow Topology Tool Scoping

**Files:**

- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Replace direct topology mutation in MainWindow**

Include:

```cpp
#include "commands/topologypresetcommand.h"
```

In `MainWindow::createTopologyPreset()`, replace `PluginRegistry::plugin(m_activePluginId)` lookup with:

```cpp
    if (!m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }
    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
```

Find preset from `workspace.topologyPresets`:

```cpp
    auto presetIt = std::find_if(workspace.topologyPresets.cbegin(),
                                 workspace.topologyPresets.cend(),
                                 [&](const TopologyPresetDescriptor& preset) {
                                     return preset.id == presetId;
                                 });
```

Build request:

```cpp
    TopologyPresetRequest request;
    request.ipcoreId = workspace.ipcoreId;
    request.preset = *presetIt;
```

Replace direct builder call and dirty marker adjustment with:

```cpp
    std::unique_ptr<Command> rejected =
        m_commandManager->executeCommand(std::make_unique<TopologyPresetCommand>(
            m_graph,
            &ModuleRegistry::instance(),
            request));
    if (auto* failed = dynamic_cast<TopologyPresetCommand*>(rejected.get())) {
        QMessageBox::warning(this, "Topology", failed->result().error);
        return;
    }

    syncDocumentStateFromHistory();
```

Remove the manual `m_cleanStateId = m_commandManager->currentStateId() - 1` block.

- [x] **Step 2: Rebuild topology menu from active workspace**

Update `MainWindow::rebuildTopologyMenu()`:

```cpp
    if (!m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }

    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    for (const TopologyPresetDescriptor& preset : workspace.topologyPresets) {
        QAction* action = m_topologyMenu->addAction(preset.label);
        action->setData(preset.id);
        connect(action, &QAction::triggered, this, &MainWindow::createTopologyPreset);
    }
```

Connect active workspace changes to menu rebuild in `setupConnections()`:

```cpp
    connect(m_activeWorkspaceController.get(),
            &ActiveWorkspaceController::activeWorkspaceChanged,
            this,
            &MainWindow::rebuildTopologyMenu);
```

- [x] **Step 3: Keep IP Catalog integration test green**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
```

Expected: test passes and MainWindow still exposes `ipCatalogDock` with no `activeIpCombo` or `paletteDock`.

---

## Task 5.5: Verification And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-5-scoped-workspace-tools.md`

- [x] **Step 1: Run required verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt topology_preset_test
CCACHE_DISABLE=1 xmake run -P qt commandmanager_test
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
CCACHE_DISABLE=1 xmake build -P qt qt
git diff --check
```

Expected: all commands pass.

- [x] **Step 2: Run stale MIME and direct topology scans**

Run:

```bash
rg -n "application/x-moduletype|TopologyPresetBuilder::apply\\(m_graph|m_cleanStateId = m_commandManager->currentStateId\\(\\) - 1" qt/inc/app qt/inc/panels qt/src/app qt/src/panels qt/test
```

Expected: no output.

Run:

```bash
rg -n "request\\.pluginId|pluginId = .*topology|activeIpCombo|paletteDock|new Palette|m_palette" qt/inc qt/src qt/test
```

Expected: no output except negative assertions in `qt/test/ipcatalogpanel_test.cpp`.

- [x] **Step 3: Supervisor preflight**

Send Turing:

- the current `git diff --stat`;
- verification command results;
- stale scan results;
- confirmation that `.codex/`, `.superpowers/`, and `image.png` remain uncommitted;
- confirmation that topology creation is undoable through `CommandManager`;
- confirmation that NodeEditor rejects legacy MIME and mismatched IP-core payloads.

- [x] **Step 4: Archive Node 5**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-5-scoped-workspace-tools.md
git commit -m "archive: complete node-5 scoped workspace tools"
```

Expected: commit succeeds and helper artifacts remain uncommitted.

---

## Self-Review

- Spec coverage: This plan implements scoped module drag/drop, active workspace create menus, topology preset command history, and module IP-core ownership during project serialization.
- Scope control: Connection semantic splitting remains Node 6; generation/DRC boundary changes remain Node 7; full removal of legacy files remains Node 8.
- Test coverage: Tests cover missing active instance, mismatched IP core, legacy MIME rejection, successful scoped creation, active menu filtering, topology undo/redo, command history dirty-state basis, and serialization of module IP-core ownership.
