# Node 3 Project IP Services Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make IP cores and project IP instances first-class project services, and change saved project v1 vocabulary from plugin-owned records to IP-core-owned records.

**Architecture:** Keep the existing runtime plugin discovery layer as the loader for now, but expose it through IP-core-facing services before the UI rewrite. The persisted project schema becomes `ipcores`, `ipcore_state`, and module `ipcore`; old saved-project keys are rejected because the product is still pre-v1. Project state remains outside `Graph`, but the service API now speaks IP instances instead of plugin state.

**Tech Stack:** C++23, Qt Widgets/Core JSON APIs, xmake Qt test targets.

---

## File Structure

- Create `qt/inc/ipcore/ipcatalogservice.h`: IP-core-facing catalog read model over discovered runtime bundles and loaded module types.
- Create `qt/src/ipcore/ipcatalogservice.cpp`: catalog entry construction, lookup, and selectable entry filtering.
- Create `qt/inc/project/projectipservice.h`: project-level add/select/remove service for IP instances backed by `ProjectStateService`.
- Create `qt/src/project/projectipservice.cpp`: default IP instance record creation and single-`kind: noc` enforcement.
- Create `qt/inc/workspace/activeworkspacecontroller.h`: read-only active workspace model used by future catalog/workspace UI.
- Create `qt/src/workspace/activeworkspacecontroller.cpp`: active workspace recomputation from selected project IP instance and catalog metadata.
- Create `qt/test/ipcatalogservice_test.cpp`: catalog service tests.
- Create `qt/test/projectipservice_test.cpp`: project IP service and active workspace tests.
- Modify `qt/inc/project/projectdocument.h`: persisted document records become IP-core records.
- Modify `qt/inc/project/pluginstate.h`: keep the file path for this node, but replace the record with `ProjectIpInstanceRecord`; full filename cleanup is deferred to Node 8.
- Modify `qt/inc/project/projectstateservice.h`: rename the public state API from plugin-state words to IP-instance words.
- Modify `qt/src/project/projectstateservice.cpp`: update storage, dependency insertion, and signal payload names.
- Modify `qt/src/project/projectreader.cpp`: read `ipcores`, `ipcore_state`, and module `ipcore`; reject `plugins`, `plugin_state`, module `plugin`, and `ip_instances`.
- Modify `qt/src/project/projectwriter.cpp`: write only `ipcores`, `ipcore_state`, and module `ipcore`.
- Modify `qt/src/project/graphprojectserializer.cpp`: populate/read `ProjectDocument::ipcores`, `ipcoreState`, and `ProjectModuleRecord::ipcoreId`.
- Modify `qt/inc/app/generationartifacts.h` and `qt/src/app/generationartifacts.cpp`: generation helper JSON uses `ipcore_state`.
- Modify direct callers needed to compile, including `qt/src/app/mainwindow.cpp`, `qt/src/validation/drcrunner.cpp`, `qt/inc/validation/drcrunner.h`, `qt/src/connection/connectionruleservice.cpp`, `qt/inc/connection/connectionruleservice.h`, `qt/src/nodeeditor/nodeeditorwidget.cpp`, `qt/inc/commands/addconnectioncommand.h`, and `qt/src/commands/addconnectioncommand.cpp`.
- Modify tests that assert saved-project or state-service vocabulary: `qt/test/projectdocument_test.cpp`, `qt/test/propertypanel_test.cpp`, `qt/test/validation_test.cpp`, and small compile-only call sites.
- Modify `qt/xmake.lua`: add new production sources and test targets.

---

## Final Node 3 Data Contracts

### `IpCatalogEntry`

Defined in `qt/inc/ipcore/ipcatalogservice.h`:

```cpp
struct IpCatalogEntry {
    QString id;
    QString name;
    QString version;
    QString kind;
    QString runtimeRootPath;
    QString sourceRootPath;
    QString modulesPath;
    QString graphicsPath;
    QStringList moduleTypes;
    QHash<QString, PluginInstanceParameterDescriptor> instanceParameters;
    PluginCommandDescriptor generator;
    PluginCommandDescriptor drc;
    QVector<TopologyPresetDescriptor> topologyPresets;

    bool hasModules() const;
    bool isSelectable() const;
};
```

`id` is the stable IP-core ID currently sourced from `PluginDescriptor::id`. `moduleTypes` is sorted and comes from `ModuleRegistry::availableTypesForPlugin(id)`. `isSelectable()` is true when `moduleTypes` is non-empty.

### `ProjectIpInstanceRecord`

Defined in `qt/inc/project/pluginstate.h` for this node:

```cpp
struct ProjectIpInstanceRecord {
    QString ipcoreId;
    QString instanceId;
    QString schema;
    QJsonObject state;
};
```

`state.kind` stores the IP kind, for example `noc`. `state.type` stores the display name copied from the IP catalog entry. `state.global_parameters` stores the copied manifest defaults.

### `ProjectDocument`

Defined in `qt/inc/project/projectdocument.h`:

```cpp
struct ProjectIpcoreRecord {
    QString id;
    QString version;
};

struct ProjectModuleRecord {
    QString id;
    QString ipcoreId;
    QString type;
    QJsonObject parameters;
};

struct ProjectDocument {
    QString schema = QStringLiteral("v1");
    QString kind = QStringLiteral("finepaper-project");
    QString name = QStringLiteral("Untitled");
    QString version = QStringLiteral("1.0");
    QVector<ProjectIpcoreRecord> ipcores;
    QVector<ProjectIpInstanceRecord> ipcoreState;
    QVector<ProjectModuleRecord> modules;
    QVector<ProjectConnectionRecord> connections;
};
```

Saved JSON keys for v1:

```json
{
  "schema": "v1",
  "kind": "finepaper-project",
  "project": { "name": "Demo", "version": "1.0" },
  "ipcores": [{ "id": "finepaper.ravenoc", "version": "1.0" }],
  "ipcore_state": [
    {
      "ipcore": "finepaper.ravenoc",
      "instance": "ravenoc_0",
      "schema": "finepaper.ravenoc-project-state-v1",
      "state": {
        "kind": "noc",
        "type": "RaveNoC",
        "global_parameters": { "flit_data_width": 32 }
      }
    }
  ],
  "graph": {
    "modules": [
      {
        "id": "xp_00",
        "ipcore": "finepaper.ravenoc",
        "type": "RaveTile",
        "parameters": {}
      }
    ],
    "connections": []
  }
}
```

The mainline v1 reader rejects these old keys:

- root `plugins`
- root `plugin_state`
- module `plugin`
- root `ip_instances`

### `ProjectIpService`

Behavior:

- `ensureInstanceForIpcore(const IpCatalogEntry&)` creates one default `ProjectIpInstanceRecord` if the project does not already contain that IP-core instance, then selects it.
- The default instance ID is `<last id token>_0`, lowercased and sanitized, matching the existing `defaultIpInstanceId()` behavior.
- The default record schema is `<ipcore-id>-project-state-v1`.
- The default state contains `kind`, `type`, and sorted `global_parameters` copied from `IpCatalogEntry::instanceParameters`.
- If the entry has `kind == "noc"` and a different existing IP instance has `state.kind == "noc"`, the call fails and does not mutate the project.
- `selectInstance(ipcoreId, instanceId)` succeeds only for existing records and emits `selectedIpInstanceChanged`.
- `removeInstance(ipcoreId, instanceId)` removes the record; if it was selected, selection moves to the first remaining record or clears.

### `ActiveWorkspaceController`

Read model:

```cpp
struct ActiveWorkspaceState {
    bool hasActiveIp = false;
    QString ipcoreId;
    QString instanceId;
    QString label;
    QString kind;
    QStringList moduleTypes;
    QVector<TopologyPresetDescriptor> topologyPresets;
};
```

It listens to `ProjectIpService::selectedIpInstanceChanged`, looks up the selected IP in `IpCatalogService`, and emits `activeWorkspaceChanged` when the read model changes.

---

## Task 3.1: Persisted Project Vocabulary Tests

**Files:**

- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/inc/project/pluginstate.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/xmake.lua` only if compile inputs need new headers

- [x] **Step 1: Add failing writer vocabulary assertions**

In `qt/test/projectdocument_test.cpp`, update the project round-trip/writer assertions so generated project JSON contains `ipcores`, `ipcore_state`, and module `ipcore`, and does not contain `plugins`, `plugin_state`, or module `plugin`.

Use this test shape:

```cpp
void testProjectWriterUsesIpcoreVocabulary() {
    QTemporaryDir tempDir;
    ProjectDocument document;
    document.name = QStringLiteral("ipcore_schema");
    document.ipcores.push_back(ProjectIpcoreRecord{QStringLiteral("finepaper.ravenoc"),
                                                   QStringLiteral("1.0")});

    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("finepaper.ravenoc-project-state-v1");
    state.state.insert(QStringLiteral("kind"), QStringLiteral("noc"));
    state.state.insert(QStringLiteral("global_parameters"), QJsonObject{{QStringLiteral("flit_data_width"), 32}});
    document.ipcoreState.push_back(state);

    ProjectModuleRecord module;
    module.id = QStringLiteral("tile_0");
    module.ipcoreId = QStringLiteral("finepaper.ravenoc");
    module.type = QStringLiteral("RaveTile");
    document.modules.push_back(module);

    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("schema.fpproj"));
    require(ProjectWriter::writeFile(path, document).success, "project should write");

    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project should reopen");
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    require(root.contains(QStringLiteral("ipcores")), "writer should emit ipcores");
    require(root.contains(QStringLiteral("ipcore_state")), "writer should emit ipcore_state");
    require(!root.contains(QStringLiteral("plugins")), "writer should not emit plugins");
    require(!root.contains(QStringLiteral("plugin_state")), "writer should not emit plugin_state");
    const QJsonObject firstModule = root.value(QStringLiteral("graph"))
                                        .toObject()
                                        .value(QStringLiteral("modules"))
                                        .toArray()
                                        .first()
                                        .toObject();
    require(firstModule.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "module should emit ipcore owner");
    require(!firstModule.contains(QStringLiteral("plugin")), "module should not emit plugin owner");
}
```

- [x] **Step 2: Add failing reader rejection tests**

Add tests that write small JSON files and assert read failure:

```cpp
void testProjectReaderRejectsOldPluginRootKeys() {
    QTemporaryDir tempDir;
    QJsonObject root = minimalProjectRoot();
    root.insert(QStringLiteral("plugins"), QJsonArray{});
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_plugins.fpproj"));
    writeJson(path, root);
    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old plugins key");
    require(result.error.contains(QStringLiteral("plugins")), "error should mention plugins");
}

void testProjectReaderRejectsOldPluginStateKey() {
    QTemporaryDir tempDir;
    QJsonObject root = minimalProjectRoot();
    root.insert(QStringLiteral("plugin_state"), QJsonArray{});
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_plugin_state.fpproj"));
    writeJson(path, root);
    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old plugin_state key");
    require(result.error.contains(QStringLiteral("plugin_state")), "error should mention plugin_state");
}

void testProjectReaderRejectsOldModulePluginKey() {
    QTemporaryDir tempDir;
    QJsonObject root = minimalProjectRoot();
    QJsonObject module{{QStringLiteral("id"), QStringLiteral("tile_0")},
                       {QStringLiteral("plugin"), QStringLiteral("finepaper.ravenoc")},
                       {QStringLiteral("type"), QStringLiteral("RaveTile")},
                       {QStringLiteral("parameters"), QJsonObject{}}};
    QJsonObject graph = root.value(QStringLiteral("graph")).toObject();
    graph.insert(QStringLiteral("modules"), QJsonArray{module});
    root.insert(QStringLiteral("graph"), graph);
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("old_module_plugin.fpproj"));
    writeJson(path, root);
    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "v1 reader should reject old module plugin key");
    require(result.error.contains(QStringLiteral("plugin")), "error should mention plugin");
}
```

- [x] **Step 3: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: compile failures or test failures mentioning missing `ipcores` / old keys still accepted.

- [x] **Step 4: Implement project record rename**

Change `ProjectDocument` and `ProjectIpInstanceRecord` to the final data contracts above. Update project reader/writer to use only:

- root `ipcores`
- root `ipcore_state`
- state entry owner key `ipcore`
- module owner key `ipcore`

Reader validation must reject old keys before parsing the graph:

```cpp
if (root.contains(QStringLiteral("plugins"))) {
    return failure(QStringLiteral("Project plugins is a pre-v1 field and is not supported"));
}
if (root.contains(QStringLiteral("plugin_state"))) {
    return failure(QStringLiteral("Project plugin_state is a pre-v1 field and is not supported"));
}
if (root.contains(QStringLiteral("ip_instances"))) {
    return failure(QStringLiteral("Project ip_instances is a pre-v1 field and is not supported"));
}
```

While parsing modules:

```cpp
if (object.contains(QStringLiteral("plugin"))) {
    return failure(QStringLiteral("Project graph.modules.plugin is a pre-v1 field and is not supported"));
}
module.ipcoreId = object.value(QStringLiteral("ipcore")).toString();
```

- [x] **Step 5: Run focused test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: any remaining failures point to call sites still using old project record fields.

---

## Task 3.2: Project State API Migration

**Files:**

- Modify: `qt/inc/project/projectstateservice.h`
- Modify: `qt/src/project/projectstateservice.cpp`
- Modify: `qt/inc/commands/setpluginstateparametercommand.h`
- Modify: `qt/src/commands/setpluginstateparametercommand.cpp`
- Modify: direct callers in `qt/src/app/mainwindow.cpp`, `qt/src/panels/propertypanel.cpp`, `qt/src/nodeeditor/nodeeditorwidget.cpp`, `qt/src/validation/validationmanager.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/propertypanel_test.cpp`

- [x] **Step 1: Update tests for IP instance state API**

Rename project state test expectations to IP instance state:

- `service.ipInstanceRecords()` replaces `service.pluginStates()`.
- `ensureIpInstanceRecord(record)` replaces `ensurePluginStateRecord(record)`.
- `ProjectIpInstanceRecord::ipcoreId` replaces `ProjectPluginStateRecord::pluginId`.
- Failure strings should say "IP instance" or "ipcore state" when the behavior is project-service-owned.

- [x] **Step 2: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
```

Expected: compile failures until the API migration is implemented.

- [x] **Step 3: Implement state service API**

Final public API in `ProjectStateService`:

```cpp
const QVector<ProjectIpInstanceRecord>& ipInstanceRecords() const;
bool ensureIpInstanceRecord(const ProjectIpInstanceRecord& record);
bool removeIpInstanceRecord(const QString& ipcoreId, const QString& instanceId);

bool setParameter(const QString& ipcoreId,
                  const QString& instanceId,
                  const QString& section,
                  const QString& name,
                  const QJsonValue& value);
QJsonValue parameter(const QString& ipcoreId,
                     const QString& instanceId,
                     const QString& section,
                     const QString& name) const;
```

Signal payload names:

```cpp
void parameterChanged(const QString& ipcoreId,
                      const QString& instanceId,
                      const QString& section,
                      const QString& name);
void ipInstanceRecordsChanged();
```

`writeToDocument()` writes `document.ipcoreState` and adds missing `ProjectIpcoreRecord` dependencies to `document.ipcores`.

- [x] **Step 4: Update direct callers**

Keep `SetPluginStateParameterCommand` filename/class for this node, but change member names and comments to IP-core language where touched. Update all direct call sites to use `ipInstanceRecords()` and `ProjectIpInstanceRecord`.

- [x] **Step 5: Run focused tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
```

Expected: both pass or fail only on vocabulary assertions handled in later tasks.

---

## Task 3.3: Generation And Graph Serializer Boundary

**Files:**

- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/inc/app/generationartifacts.h`
- Modify: `qt/src/app/generationartifacts.cpp`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/inc/commands/addconnectioncommand.h`
- Modify: `qt/src/commands/addconnectioncommand.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [x] **Step 1: Update generation helper tests**

In `qt/test/projectdocument_test.cpp`, replace generation helper expectations:

- `ipcoreStateArray(records)` returns JSON objects with `ipcore`.
- `attachIpcoreState(root, records)` inserts `ipcore_state`.
- `writeGeneratedProjectSnapshot()` writes `.fpproj` snapshots with `ipcores` and `ipcore_state`.

- [x] **Step 2: Run tests to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
```

Expected: failures around old `plugin_state` expectations.

- [x] **Step 3: Update serializer and generation code**

Change `GraphProjectSerializer::toProject()` to collect `ipcoreIds` from `ModuleType::pluginId` and store them as `ProjectIpcoreRecord`. Keep `ModuleType::pluginId` as the runtime loader ownership field for this node.

Change load validation messages:

```cpp
if (record.ipcoreId.isEmpty()) {
    return failure(QStringLiteral("Module %1 is missing ipcore").arg(record.id));
}
if (type->pluginId != record.ipcoreId) {
    return failure(QStringLiteral("Module %1 requires ipcore %2").arg(record.id, record.ipcoreId));
}
```

Update generation helpers:

```cpp
QJsonArray ipcoreStateArray(const QVector<ProjectIpInstanceRecord>& records);
void attachIpcoreState(QJsonObject& root,
                       const QVector<ProjectIpInstanceRecord>& records);
```

DRC/generator generic graph input now carries `ipcore_state`, not `plugin_state`.

- [x] **Step 4: Update connection rule state plumbing**

Rename provider aliases and constructor parameters to IP instance records:

```cpp
using IpInstanceRecordsProvider = std::function<QVector<ProjectIpInstanceRecord>()>;
ConnectionRuleService(const Graph* graph, QVector<ProjectIpInstanceRecord> ipInstanceRecords);
```

Keep behavior unchanged.

- [x] **Step 5: Run focused tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
```

Expected: all three pass.

---

## Task 3.4: IP Catalog Service

**Files:**

- Create: `qt/inc/ipcore/ipcatalogservice.h`
- Create: `qt/src/ipcore/ipcatalogservice.cpp`
- Create: `qt/test/ipcatalogservice_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Write failing catalog service test**

Create `qt/test/ipcatalogservice_test.cpp` with:

```cpp
void testCatalogEntryCopiesDiscoveredMetadata() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    ModuleType tile;
    tile.name = QStringLiteral("RaveTile");
    tile.pluginId = QStringLiteral("finepaper.ravenoc");
    require(registry.registerType(tile), "test module type should register");

    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("0.1");
    descriptor.kind = QStringLiteral("noc");
    descriptor.runtimeRootPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc");
    descriptor.sourceRootPath = QStringLiteral("/tmp/ipcores/ravenoc");
    descriptor.modulesPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc/modules.xml");
    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("integer");
    width.defaultValue = 32;
    descriptor.instanceParameters.insert(width.name, width);

    IpCatalogService service({descriptor}, &registry);
    const std::optional<IpCatalogEntry> entry = service.entry(QStringLiteral("finepaper.ravenoc"));
    require(entry.has_value(), "catalog should expose discovered IP core");
    require(entry->id == descriptor.id, "entry should keep id");
    require(entry->kind == QStringLiteral("noc"), "entry should keep kind");
    require(entry->moduleTypes == QStringList{QStringLiteral("RaveTile")},
            "entry should expose loaded module types");
    require(entry->isSelectable(), "entry with modules should be selectable");
    require(entry->instanceParameters.contains(QStringLiteral("flit_data_width")),
            "entry should keep instance parameters");
}
```

Add `main()` that prints `ipcatalogservice_test passed`.

- [x] **Step 2: Wire xmake target and verify failure**

Add:

```lua
add_qt_test_target("ipcatalogservice_test", "test/ipcatalogservice_test.cpp", {
    "src/ipcore/ipcatalogservice.cpp",
    "src/modules/moduleregistry.cpp",
    "src/modules/moduleprovider.cpp",
    "src/plugins/pluginregistry.cpp",
    "src/common/frameworkpaths.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "inc/ipcore/ipcatalogservice.h",
    "inc/**/moduleregistry.h",
    "inc/**/plugindescriptor.h"
})
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogservice_test
```

Expected: compile failure because the service does not exist.

- [x] **Step 3: Implement catalog service**

Implement:

```cpp
class IpCatalogService {
public:
    IpCatalogService(QList<PluginDescriptor> descriptors,
                     const ModuleRegistry* moduleRegistry);
    static IpCatalogService fromRuntimeRegistries();

    QList<IpCatalogEntry> entries() const;
    QList<IpCatalogEntry> selectableEntries() const;
    std::optional<IpCatalogEntry> entry(const QString& id) const;
};
```

Sort entries by display label, then id. `fromRuntimeRegistries()` copies `PluginRegistry::instance().plugins()` and uses `&ModuleRegistry::instance()`.

- [x] **Step 4: Run focused test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogservice_test
```

Expected: pass.

---

## Task 3.5: Project IP Service And Active Workspace

**Files:**

- Create: `qt/inc/project/projectipservice.h`
- Create: `qt/src/project/projectipservice.cpp`
- Create: `qt/inc/workspace/activeworkspacecontroller.h`
- Create: `qt/src/workspace/activeworkspacecontroller.cpp`
- Create: `qt/test/projectipservice_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Write failing Project IP service tests**

Create `qt/test/projectipservice_test.cpp` with test cases:

```cpp
void testProjectIpServiceCreatesDefaultStateAndSelectsIt();
void testProjectIpServiceRejectsSecondNocInstance();
void testProjectIpServiceRemoveClearsSelection();
void testActiveWorkspaceChangesWhenSelectionChanges();
```

The first test should assert:

```cpp
ProjectStateService stateService;
ProjectIpService service(&stateService);
const ProjectIpServiceResult result = service.ensureInstanceForIpcore(ravenocEntry());
require(result.success, "IP service should create NoC instance");
require(stateService.ipInstanceRecords().size() == 1, "state service should store one instance");
const ProjectIpInstanceRecord& record = stateService.ipInstanceRecords().first();
require(record.ipcoreId == QStringLiteral("finepaper.ravenoc"), "record should keep ipcore id");
require(record.instanceId == QStringLiteral("ravenoc_0"), "record should use default instance id");
require(record.state.value(QStringLiteral("kind")).toString() == QStringLiteral("noc"),
        "record should keep kind");
require(record.state.value(QStringLiteral("global_parameters")).toObject()
            .value(QStringLiteral("flit_data_width")).toInt() == 32,
        "record should copy default global parameter");
require(service.selectedIpInstance().has_value(), "new instance should be selected");
```

The active workspace test should create two catalog entries with module types and verify `ActiveWorkspaceController::state()` changes `ipcoreId`, `label`, `kind`, `moduleTypes`, and `topologyPresets` after `ProjectIpService::selectInstance()`.

- [x] **Step 2: Wire xmake target and verify failure**

Add:

```lua
add_qt_test_target("projectipservice_test", "test/projectipservice_test.cpp", {
    "src/project/projectipservice.cpp",
    "src/project/projectstateservice.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/workspace/activeworkspacecontroller.cpp",
    "src/modules/moduleregistry.cpp",
    "src/modules/moduleprovider.cpp",
    "src/plugins/pluginregistry.cpp",
    "src/common/frameworkpaths.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "inc/project/projectipservice.h",
    "inc/project/projectstateservice.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/workspace/activeworkspacecontroller.h",
    "inc/**/moduleregistry.h",
    "inc/**/plugindescriptor.h"
})
```

Because `ProjectIpService` and `ActiveWorkspaceController` use `Q_OBJECT`, also add their headers to the `qt` app target and this test target for moc:

```lua
add_files("inc/**/projectipservice.h")
add_files("inc/**/activeworkspacecontroller.h")
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectipservice_test
```

Expected: compile failure because services do not exist.

- [x] **Step 3: Implement Project IP service**

Implement:

```cpp
struct ProjectIpInstanceRef {
    QString ipcoreId;
    QString instanceId;
};

struct ProjectIpServiceResult {
    bool success = false;
    QString error;
    ProjectIpInstanceRecord record;
};
```

`ProjectIpService` public API:

```cpp
explicit ProjectIpService(ProjectStateService* stateService, QObject* parent = nullptr);

ProjectIpServiceResult ensureInstanceForIpcore(const IpCatalogEntry& entry);
bool selectInstance(const QString& ipcoreId, const QString& instanceId);
bool removeInstance(const QString& ipcoreId, const QString& instanceId);
std::optional<ProjectIpInstanceRef> selectedIpInstance() const;
```

Signals:

```cpp
void ipInstancesChanged();
void selectedIpInstanceChanged();
```

- [x] **Step 4: Implement Active Workspace controller**

Constructor:

```cpp
ActiveWorkspaceController(ProjectIpService* projectIpService,
                          const IpCatalogService* catalogService,
                          QObject* parent = nullptr);
```

It recomputes `ActiveWorkspaceState` from `ProjectIpService::selectedIpInstance()` and `IpCatalogService::entry(ipcoreId)`.

- [x] **Step 5: Run focused test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectipservice_test
```

Expected: pass.

---

## Task 3.6: Application Wiring Compile Pass

**Files:**

- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: other compile-only callers reported by the compiler

- [x] **Step 1: Update MainWindow project state calls**

Replace local default record creation in `mainwindow.cpp` with `ProjectIpService` where practical:

```cpp
m_projectIpService = std::make_unique<ProjectIpService>(m_projectStateService.get());
```

`ensureProjectStateRecordFromActivePlugin()` should convert the selected runtime plugin descriptor into an `IpCatalogEntry` through `IpCatalogService` and call `ensureInstanceForIpcore()`.

Connect dirty tracking to both:

```cpp
connect(m_projectStateService.get(), &ProjectStateService::parameterChanged, this, ...);
connect(m_projectIpService.get(), &ProjectIpService::ipInstancesChanged, this, ...);
```

- [x] **Step 2: Update property panel and command wiring**

The property panel should read/write through `ProjectStateService::ipInstanceRecords()`, `ProjectIpInstanceRecord::ipcoreId`, and `ProjectStateService::setParameter(ipcoreId, ...)`. Existing undo behavior through `SetPluginStateParameterCommand` must remain intact.

- [x] **Step 3: Run UI-adjacent tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
```

Expected: pass.

---

## Task 3.7: Node 3 Verification And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-3-project-ip-services.md`

- [x] **Step 1: Run required verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogservice_test
CCACHE_DISABLE=1 xmake run -P qt projectipservice_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
git diff --check
```

Expected: all commands pass.

- [x] **Step 2: Run Node 3 stale saved-project vocabulary scan**

Run:

```bash
rg -n '"plugins"|"plugin_state"|"plugin"\s*:' qt/src/project qt/test/projectdocument_test.cpp qt/src/app/generationartifacts.cpp qt/src/validation/drcrunner.cpp
```

Expected: no hits for saved-project JSON keys except rejection-test fixtures and error messages that explicitly mention the old rejected key.

- [x] **Step 3: Supervisor preflight**

Send the Node 3 diff, verification output, and this checklist to the standing supervisor. Required approval points:

- `IpCatalogEntry`, `ProjectIpInstanceRecord`, `ProjectIpService`, and `ActiveWorkspaceController` match this plan.
- Saved project writer emits only `ipcores`, `ipcore_state`, and module `ipcore`.
- Reader rejects old `plugins`, `plugin_state`, module `plugin`, and `ip_instances`.
- Required tests passed.
- `.codex/`, `.superpowers/`, and `image.png` remain uncommitted.

- [x] **Step 4: Archive Node 3**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-3-project-ip-services.md
git commit -m "archive: complete node-3 project ip services"
```

Expected: commit succeeds and untracked helper artifacts remain uncommitted.

---

## Self-Review

- Spec coverage: This plan defines `IpCatalogEntry`, `ProjectIpInstanceRecord`, `ProjectIpService`, `ActiveWorkspaceController`, and final v1 JSON keys. It includes explicit old-key rejection tests.
- Scope control: Runtime `PluginRegistry`, `PluginDescriptor`, and plugin command execution keep their existing names in Node 3. They are implementation details behind the new IP services and can be cleaned up in later nodes.
- Risk callout: External generator/DRC inputs change from `plugin_state` to `ipcore_state` in this node. Because compatibility is not required pre-v1, no legacy read fallback is planned.
