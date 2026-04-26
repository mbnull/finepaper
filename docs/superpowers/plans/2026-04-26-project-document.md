# Project Document Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real `.fpproj` editor project format that restores module instances, parameters, explicit port-level connections, and collapsed state while keeping legacy NoC JSON as import/export compatibility.

**Architecture:** Add a focused `qt/inc/project` and `qt/src/project` layer for project data, JSON read/write, and `Graph` conversion. Keep `Graph` as the in-memory topology model and leave framework JSON generation on the existing `Graph::toJsonDocument(GraphJsonFlavor::Framework)` path. Wire `MainWindow` save/open dialogs to `.fpproj`, with `.json` handled as legacy import only.

**Tech Stack:** Qt C++23, `QJsonDocument`, `QJsonObject`, existing `Graph`, `ModuleRegistry`, xmake Qt test targets.

---

### Task 1: Project Document Core

**Files:**
- Create: `qt/inc/project/projectdocument.h`
- Create: `qt/inc/project/projectreader.h`
- Create: `qt/inc/project/projectwriter.h`
- Create: `qt/inc/project/graphprojectserializer.h`
- Create: `qt/src/project/projectreader.cpp`
- Create: `qt/src/project/projectwriter.cpp`
- Create: `qt/src/project/graphprojectserializer.cpp`
- Create/Test: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing project round-trip test**

Create `qt/test/projectdocument_test.cpp` with a test that registers two module types in `ModuleRegistry`, builds a graph with an XP-like module and an Endpoint-like module, sets parameters including `collapsed`, writes a project file through `GraphProjectSerializer` and `ProjectWriter`, reads it with `ProjectReader`, restores into a fresh `Graph`, and asserts:

```cpp
require(restored.modules().size() == 2, "project should restore both modules");
require(restored.connections().size() == 1, "project should restore explicit connection");
require(restored.getModule("node_1") != nullptr, "stable module id should be preserved");
require(boolParameter(restored.getModule("node_1"), "collapsed") == true,
        "collapsed parameter should be restored");
```

Add an xmake target:

```lua
add_qt_test_target("projectdocument_test", "test/projectdocument_test.cpp", {
    "src/**/projectreader.cpp",
    "src/**/projectwriter.cpp",
    "src/**/graphprojectserializer.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/**/projectdocument.h",
    "inc/**/projectreader.h",
    "inc/**/projectwriter.h",
    "inc/**/graphprojectserializer.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})
```

- [ ] **Step 2: Verify RED**

Run:

```bash
cd qt
xmake run projectdocument_test
```

Expected: FAIL to compile because project document headers and classes do not exist yet.

- [ ] **Step 3: Implement project document model and writer/reader**

Implement plain structs in `projectdocument.h`:

```cpp
struct ProjectModuleRecord { QString id; QString pluginId; QString type; QJsonObject parameters; };
struct ProjectConnectionEndpoint { QString moduleId; QString portId; };
struct ProjectConnectionRecord { QString id; ProjectConnectionEndpoint source; ProjectConnectionEndpoint target; };
struct ProjectDocument { QString schema = "v1"; QString kind = "finepaper-project"; QString name = "Untitled"; QString version = "1.0"; QVector<ProjectPluginRecord> plugins; QVector<ProjectModuleRecord> modules; QVector<ProjectConnectionRecord> connections; };
```

Implement `ProjectWriter::writeFile()` and `ProjectReader::readFile()` with deterministic JSON and clear `ProjectReadResult::error`.

- [ ] **Step 4: Implement graph serializer**

Implement:

```cpp
ProjectDocument GraphProjectSerializer::toProject(const Graph& graph, const QString& projectName);
GraphProjectLoadResult GraphProjectSerializer::loadProject(const ProjectDocument& project, Graph& graph);
```

Use `ModuleRegistry::instance().getType(record.type)` and require `type.pluginId == record.pluginId`. Instantiate modules from default ports/parameters, apply project parameters, then add connections only after all modules exist and `Graph::isValidConnection()` accepts them.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
cd qt
xmake run projectdocument_test
```

Expected: `projectdocument_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/project qt/src/project qt/test/projectdocument_test.cpp qt/xmake.lua
git commit -m "feat(qt): add project document serialization"
```

### Task 2: Project Validation Tests

**Files:**
- Modify/Test: `qt/test/projectdocument_test.cpp`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`

- [ ] **Step 1: Add failing validation tests**

Extend `projectdocument_test.cpp` with tests for:

```cpp
testReaderRejectsWrongKind();
testLoadRejectsDuplicateModuleIds();
testLoadRejectsMissingModuleType();
testLoadRejectsInvalidParameterType();
testLoadRejectsInvalidConnectionReference();
```

Each test should assert `!result.success` and check the error string contains the relevant ID or field name.

- [ ] **Step 2: Verify RED**

Run:

```bash
cd qt
xmake run projectdocument_test
```

Expected: FAIL because the stricter validation behavior is incomplete.

- [ ] **Step 3: Implement strict validation**

Update reader and serializer to reject:

- non-`finepaper-project` kind.
- non-`v1` schema.
- missing or duplicate module IDs.
- missing plugin/type.
- module type not found or plugin mismatch.
- unknown parameter names.
- parameter value type mismatch against default parameter type.
- connection endpoint module or port not found.
- invalid connection according to `Graph::isValidConnection`.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
cd qt
xmake run projectdocument_test
```

Expected: `projectdocument_test passed`.

- [ ] **Step 5: Commit**

```bash
git add qt/test/projectdocument_test.cpp qt/src/project/projectreader.cpp qt/src/project/graphprojectserializer.cpp
git commit -m "test(qt): cover project document validation"
```

### Task 3: MainWindow Project Open/Save

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify/Test: `qt/test/projectdocument_test.cpp`

- [ ] **Step 1: Add failing file-detection test**

Add a small test helper around a project file and a legacy JSON file that verifies:

```cpp
ProjectReader::detectKind(projectPath) == ProjectFileKind::Project;
ProjectReader::detectKind(legacyJsonPath) == ProjectFileKind::LegacyJson;
```

- [ ] **Step 2: Verify RED**

Run:

```bash
cd qt
xmake run projectdocument_test
```

Expected: FAIL because file kind detection is not implemented.

- [ ] **Step 3: Implement file kind detection and MainWindow wiring**

Add `ProjectFileKind` detection to `ProjectReader`. Update `MainWindow`:

- Save dialogs default to `Finepaper Project (*.fpproj)`.
- Open dialog accepts `Finepaper Project (*.fpproj);;Legacy NoC JSON (*.json);;All Files (*)`.
- `saveDocument()` writes `.fpproj` through `ProjectWriter` and `GraphProjectSerializer`.
- `loadDocument()` loads `.fpproj` through project reader/serializer.
- `.json` load falls back to existing `m_graph->loadFromJson(path)` and leaves `m_currentDocumentPath` empty, dirty state true.
- Generate Verilog remains unchanged and still emits framework JSON.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
cd qt
xmake run projectdocument_test
xmake build qt
```

Expected: test passes and Qt app builds.

- [ ] **Step 5: Commit**

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/inc/project qt/src/project qt/test/projectdocument_test.cpp
git commit -m "feat(qt): save editor projects as fpproj"
```

### Task 4: Full Verification

**Files:**
- No source edits unless verification exposes a defect.

- [ ] **Step 1: Run focused tests**

```bash
cd qt
xmake run projectdocument_test
xmake run graph_test
xmake run plugin_test
```

Expected: outputs include `projectdocument_test passed`, `graph_test passed`, and `plugin_test passed`.

- [ ] **Step 2: Run build and whitespace check**

```bash
cd qt
xmake build qt
cd ..
git diff --check
```

Expected: build exits 0 and `git diff --check` prints no errors.

- [ ] **Step 3: Commit verification fixes if any**

If Step 1 or Step 2 required source fixes:

Stage the exact files changed by the fix, then run:

```bash
git commit -m "fix(qt): stabilize project document workflow"
```

If no fixes were needed, do not create an empty commit.
