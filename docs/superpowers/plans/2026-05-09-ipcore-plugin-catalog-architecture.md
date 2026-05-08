# IP Core Plugin Catalog Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. When dispatching subagents, set reasoning effort to `high`; use `xhigh` for architecture-critical review tasks.

**Goal:** Replace the current active-plugin workflow with a spec-driven IP core architecture and IP Catalog workflow.

**Architecture:** This is a program-level implementation plan for the umbrella spec. Each node ends with an archive commit and may produce a node-specific detailed code plan before code changes begin. The implementation optimizes the mainline flow: discover repository IP cores, show them in an IP Catalog, add/select one IP core instance, edit its scoped internal graph, save/open the project, and run validation/generation through that IP core.

**Tech Stack:** C++23, Qt 6 Widgets, QtNodes, xmake, Ruby `spec_generator`, Ruby NoC/RaveNoC generators, JSON `.fpproj` project files.

---

## Source Spec

Source spec:

- `docs/superpowers/specs/2026-05-09-ipcore-plugin-catalog-architecture-design.md`

This plan intentionally decomposes the umbrella spec into node plans. The umbrella spec covers multiple independent subsystems, so a single code-level plan with exact snippets for every node would be too large to execute safely. Before implementation of each node, write or expand a node-specific detailed plan that follows the full `superpowers:writing-plans` format.

## Archive Protocol

Every major node must end with an archive commit:

```bash
git status --short
git diff --check
git add <node files>
git commit -m "archive: complete node-N <short node name>"
```

Rules:

- Archive commits must contain only the files owned by that node.
- Do not add `.codex`, `.superpowers`, screenshots, local images, or other local helper artifacts.
- Run the node's listed verification commands before claiming completion.
- Use subagents for independent implementation or review slices; reasoning effort must be at least `high`.
- Use `xhigh` for architecture-critical review of Nodes 3, 6, 7, and 9.

## Node Overview

| Node | Name | Primary Goal | Archive Marker |
| --- | --- | --- | --- |
| 0 | Umbrella Spec | Architecture spec and task inventory | `archive: add ipcore plugin catalog architecture spec` |
| 1 | Source Of Truth | Spec generator owns runtime metadata | `archive: complete node-1 spec source of truth` |
| 2 | Vocabulary And Directories | Move concrete IP sources to `ipcores/` | `archive: complete node-2 ipcore vocabulary migration` |
| 3 | Project/IP Services | Introduce catalog, project IP, active workspace services | `archive: complete node-3 project ip services` |
| 4 | IP Catalog UI | Replace Palette/active combo with IP Catalog dock | `archive: complete node-4 ip catalog ui` |
| 5 | Scoped Workspace Tools | Scope module creation/tools and make presets undoable | `archive: complete node-5 scoped workspace tools` |
| 6 | Connection Semantics | Split structural, feature, IP-core, and DRC checks | `archive: complete node-6 connection semantics split` |
| 7 | Generation Boundary | Move export/generation/DRC to IP-core boundary | `archive: complete node-7 ipcore generation boundary` |
| 8 | Historical Cleanup | Remove old names, duplicate framework paths, stale docs | `archive: complete node-8 historical cleanup` |
| 9 | Final V1 Gate | Full verification and release-readiness pass | `archive: complete node-9 v1 architecture gate` |

Node 0 is already complete in commit `af783f3`.

---

## Node 1: Source Of Truth And Drift Cleanup

**Goal:** Make source specs reproduce committed runtime metadata and prevent silent hand edits.

**Files:**

- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `spec_generator/README.md`
- Modify: `spec/noc/noc.yaml`
- Modify: `spec/noc/ravenoc.yml`
- Regenerate: `plugins/noc/modules.xml`
- Regenerate: `plugins/noc/graphics/Endpoint.xml`
- Regenerate: `plugins/noc/graphics/XP.xml`
- Regenerate: `plugins/noc/generator/src/ruby/model/endpoint.rb`
- Regenerate: `plugins/noc/generator/src/ruby/model/xp.rb`
- Regenerate: `plugins/ravenoc/plugin.json`
- Regenerate: `plugins/ravenoc/modules.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveEndpoint.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveTile.xml`

### Task 1.1: Write Node 1 Detailed Plan

- [ ] **Step 1: Create a node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-1-spec-source-of-truth.md`.

The plan must include code-level tasks for:

- adding interface metadata fields to both base and IP-core spec parsers;
- updating emitters to write `cardinality`, `autocomplete_group`, and `topology_rule`;
- moving `mesh_col` and `mesh_row` into `spec/noc/noc.yaml`;
- adding spec generator tests that assert generated XML contains the new attributes;
- adding a drift check command or test.

- [ ] **Step 2: Review the node plan**

Run:

```bash
rg -n "T[B]D|T[O]DO|place[h]older|similar t[o]|add appropriat[e]|handle edge case[s]" \
  docs/superpowers/plans/2026-05-09-node-1-spec-source-of-truth.md
```

Expected: no output.

### Task 1.2: Implement Spec Metadata Ownership

- [ ] **Step 1: Add failing generator tests**

Add tests in `spec_generator/test/spec_generator_test.rb` that assert generated NoC/RaveNoC `modules.xml` includes:

```xml
cardinality="one"
autocomplete_group="endpoint_attachment"
autocomplete_group="router_side"
topology_rule="opposite_side"
```

Also add assertions that NoC `XP` contains `mesh_col` and `mesh_row` parameters from `spec/noc/noc.yaml`.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: failures showing missing generated metadata attributes.

- [ ] **Step 3: Implement schema and emitter support**

Modify `spec_generator/lib/spec_generator.rb`:

- allow interface fields `cardinality`, `autocomplete_group`, `topology_rule`;
- validate them as strings when present;
- emit them into `<interface ...>` attributes for base NoC and IP-core outputs.

- [ ] **Step 4: Move hand-edited metadata into specs**

Modify:

- `spec/noc/noc.yaml`
- `spec/noc/ravenoc.yml`

Add the connection metadata currently present only in runtime `modules.xml`.

- [ ] **Step 5: Regenerate runtime artifacts**

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --spec spec/noc/noc.yaml \
  --views spec/noc/views \
  --qt-bundle plugins/noc \
  --ruby-model plugins/noc/generator/src/ruby/model

ruby spec_generator/bin/spec-gen \
  --extension spec/noc/ravenoc.yml \
  --views spec/noc/views \
  --bundle plugins/ravenoc
```

Expected: generated artifacts update without manual edits.

- [ ] **Step 6: Add drift check**

Add a spec generator test or documented command that regenerates both runtime bundles into temporary directories and compares them with committed generated artifacts.

The check must fail if `plugins/noc/modules.xml` or `plugins/ravenoc/modules.xml` is hand-edited without changing spec source.

- [ ] **Step 7: Run verification**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby plugins/noc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
CCACHE_DISABLE=1 xmake run -P qt plugin_test
git diff --check
```

Expected:

- Ruby spec generator tests pass.
- NoC and RaveNoC generator tests pass.
- `plugin_test` passes.
- `git diff --check` has no output.

- [ ] **Step 8: Archive Node 1**

Run:

```bash
git status --short
git add spec_generator spec/noc plugins/noc plugins/ravenoc docs/superpowers/plans/2026-05-09-node-1-spec-source-of-truth.md
git commit -m "archive: complete node-1 spec source of truth"
```

---

## Node 2: Vocabulary And Directory Migration

**Goal:** Move concrete IP core sources out of `plugins/` and into `ipcores/`, while generated runtime artifacts remain discoverable.

**Files:**

- Create: `ipcores/finepaper-noc/ipcore.yml`
- Create: `ipcores/finepaper-noc/views/Endpoint.xml`
- Create: `ipcores/finepaper-noc/views/XP.xml`
- Move/Create: `ipcores/finepaper-noc/generator/**`
- Create: `ipcores/ravenoc/ipcore.yml`
- Create: `ipcores/ravenoc/views/RaveEndpoint.xml`
- Create: `ipcores/ravenoc/views/RaveTile.xml`
- Move/Create: `ipcores/ravenoc/generator/**`
- Move/Create: `ipcores/ravenoc/vendor/**`
- Create/Regenerate: `generated/ipcores/finepaper.noc/**`
- Create/Regenerate: `generated/ipcores/finepaper.ravenoc/**`
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `spec_generator/README.md`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/test/plugin_test.cpp`

### Task 2.1: Write Node 2 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-2-ipcore-vocabulary-migration.md`.

The plan must decide the generator working directory. Use this mainline choice:

- generated runtime manifests live in `generated/ipcores/<ipcore-id>/`;
- generator/vendor assets live in `ipcores/<package>/`;
- runtime manifest includes a source/package root path or generated command paths that resolve to the source package root.

- [ ] **Step 2: Review the node plan**

Run:

```bash
rg -n "T[B]D|T[O]DO|place[h]older|mayb[e]|eventuall[y]|support bot[h]|fallbac[k]" \
  docs/superpowers/plans/2026-05-09-node-2-ipcore-vocabulary-migration.md
```

Expected: no output.

### Task 2.2: Implement IP Core Vocabulary

- [ ] **Step 1: Migrate schemas**

Replace `finepaper.extension.v1` with `finepaper.ipcore.v1`.

Move metadata from:

```yaml
extension:
  id: finepaper.ravenoc
  name: RaveNoC
  version: "1.0"
```

to top-level:

```yaml
id: finepaper.ravenoc
name: RaveNoC
version: "1.0"
```

- [ ] **Step 2: Move source packages**

Move editable IP core source content under:

```text
ipcores/finepaper-noc/
ipcores/ravenoc/
```

Keep generated runtime output under:

```text
generated/ipcores/finepaper.noc/
generated/ipcores/finepaper.ravenoc/
```

- [ ] **Step 3: Update discovery**

Modify `PluginRegistry` discovery so Qt can discover generated runtime manifests under `generated/ipcores`.

- [ ] **Step 4: Update tests and docs**

Update:

- `spec_generator/test/spec_generator_test.rb`
- `qt/test/plugin_test.cpp`
- `spec_generator/README.md`
- `qt/doc/README.md`
- `qt/doc/architecture.md`

Use "IP core" for concrete packages and reserve "feature plugin" for editor behavior.

- [ ] **Step 5: Run verification**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
CCACHE_DISABLE=1 xmake run -P qt plugin_test
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
git diff --check
```

Expected: all tests pass.

- [ ] **Step 6: Archive Node 2**

Run:

```bash
git status --short
git add ipcores generated spec_generator qt docs
git commit -m "archive: complete node-2 ipcore vocabulary migration"
```

---

## Node 3: Project Model And IP Services

**Goal:** Make IP cores and project IP instances first-class services before UI rewrites.

**Files:**

- Create: `qt/inc/ipcore/ipcatalogservice.h`
- Create: `qt/src/ipcore/ipcatalogservice.cpp`
- Create: `qt/inc/project/projectipservice.h`
- Create: `qt/src/project/projectipservice.cpp`
- Create: `qt/inc/workspace/activeworkspacecontroller.h`
- Create: `qt/src/workspace/activeworkspacecontroller.cpp`
- Create: `qt/test/ipcatalogservice_test.cpp`
- Create: `qt/test/projectipservice_test.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/inc/project/projectstateservice.h`
- Modify: `qt/src/project/projectstateservice.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

### Task 3.1: Write Node 3 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-3-project-ip-services.md`.

The plan must define:

- `IpCatalogEntry` fields;
- `ProjectIpInstanceRecord` fields;
- `ProjectIpService` add/select/remove behavior;
- `ActiveWorkspaceController` read model;
- final v1 project JSON keys: `ipcores`, `ipcore_state`, and module `ipcore`.

- [ ] **Step 2: Include schema-breaking test strategy**

The node plan must include tests that old `plugins`, `plugin_state`, and module `plugin` project keys are rejected in the mainline v1 reader.

### Task 3.2: Implement Services

- [ ] **Step 1: Add service tests first**

Add tests for:

- catalog entries from discovered IP core metadata;
- project service creates default state for an IP core;
- project service enforces at most one `kind: noc` instance;
- active workspace changes when the selected IP instance changes.

- [ ] **Step 2: Implement services**

Create:

- `IpCatalogService`
- `ProjectIpService`
- `ActiveWorkspaceController`

Keep these services focused and independent from widgets.

- [ ] **Step 3: Rename project vocabulary**

Update project document read/write to use:

- `ipcores`
- `ipcore_state`
- module `ipcore`

Remove saved-project use of:

- `plugins`
- `plugin_state`
- module `plugin`

- [ ] **Step 4: Run verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogservice_test
CCACHE_DISABLE=1 xmake run -P qt projectipservice_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
git diff --check
```

Expected: all tests pass.

- [ ] **Step 5: Archive Node 3**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-3-project-ip-services.md
git commit -m "archive: complete node-3 project ip services"
```

---

## Node 4: IP Catalog UI

**Goal:** Replace the left `Palette` workflow with an IP Catalog dock.

**Files:**

- Create: `qt/inc/panels/ipcatalogpanel.h`
- Create: `qt/src/panels/ipcatalogpanel.cpp`
- Create: `qt/inc/ipcore/internalmodulelibrarymodel.h`
- Create: `qt/src/ipcore/internalmodulelibrarymodel.cpp`
- Create: `qt/inc/ipcore/iptoolsmodel.h`
- Create: `qt/src/ipcore/iptoolsmodel.cpp`
- Create: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/app/mainwindow.ui`
- Modify: `qt/inc/panels/palette.h`
- Modify: `qt/src/panels/palette.cpp`
- Modify: `qt/xmake.lua`

### Task 4.1: Write Node 4 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-4-ip-catalog-ui.md`.

The plan must include a concrete widget hierarchy:

```text
IpCatalogPanel
  Search/filter field
  Catalog tree/list
  Project IP instance list
  Active workspace modules/tools list
```

The plan must specify object names for widget tests:

- `ipCatalogPanel`
- `ipCatalogSearch`
- `ipCatalogList`
- `projectIpList`
- `activeModuleList`
- `activeToolList`

### Task 4.2: Implement UI

- [ ] **Step 1: Add widget tests first**

Add tests for:

- search filters catalog entries;
- selecting an IP instance updates active module list;
- panel emits add/select signals;
- MainWindow has `ipCatalogDock`;
- MainWindow no longer has `activeIpCombo`.

- [ ] **Step 2: Implement panel and models**

Implement:

- `IpCatalogPanel`
- `InternalModuleLibraryModel`
- `IpToolsModel`

Use intent signals; the panel must not mutate `Graph`.

- [ ] **Step 3: Integrate MainWindow**

Replace `m_paletteDock` with `m_ipCatalogDock`.

Remove toolbar active-IP combo once selection is driven by `ProjectIpService`.

- [ ] **Step 4: Run verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcatalogpanel_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
CCACHE_DISABLE=1 xmake run -P qt plugin_test
git diff --check
```

Expected: all tests pass.

- [ ] **Step 5: Archive Node 4**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-4-ip-catalog-ui.md
git commit -m "archive: complete node-4 ip catalog ui"
```

---

## Node 5: Scoped Module Creation And Workspace Tools

**Goal:** Make all module creation and IP tools use the active IP core instance.

**Files:**

- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/src/nodeeditor/events/nodeeditorwidget_events.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorentityfactory.h`
- Modify: `qt/src/nodeeditor/nodeeditorentityfactory.cpp`
- Modify: `qt/inc/commands/addmodulecommand.h`
- Modify: `qt/src/commands/addmodulecommand.cpp`
- Create: `qt/inc/commands/compositecommand.h`
- Create: `qt/src/commands/compositecommand.cpp`
- Create: `qt/inc/commands/topologypresetcommand.h`
- Create: `qt/src/commands/topologypresetcommand.cpp`
- Modify: `qt/inc/topology/topologypresetbuilder.h`
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/topology_preset_test.cpp`
- Modify: `qt/test/commandmanager_test.cpp`
- Modify: `qt/test/nodeeditor_geometry_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/xmake.lua`

### Task 5.1: Write Node 5 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-5-scoped-workspace-tools.md`.

The plan must specify the drag MIME payload shape. Use:

```text
application/x-finepaper-module
```

Payload fields:

```json
{
  "ipcore": "finepaper.ravenoc",
  "instance": "ravenoc_0",
  "type": "RaveTile"
}
```

### Task 5.2: Implement Scoped Creation And Tools

- [ ] **Step 1: Add failing tests**

Add tests for:

- drag/drop rejects module payload without active instance;
- drag/drop rejects module payload for a different IP core;
- context menu lists only active instance module types;
- topology preset command is undoable;
- topology preset command marks the document dirty through command history.

- [ ] **Step 2: Implement scoped module creation**

Update node editor creation paths to use `InternalModuleLibraryModel` and active workspace state.

- [ ] **Step 3: Implement undoable topology preset command**

Move preset mutation behind a command object. Do not mutate `Graph` directly from MainWindow or UI panels.

- [ ] **Step 4: Run verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt topology_preset_test
CCACHE_DISABLE=1 xmake run -P qt commandmanager_test
CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
git diff --check
```

Expected: all tests pass.

- [ ] **Step 5: Archive Node 5**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-5-scoped-workspace-tools.md
git commit -m "archive: complete node-5 scoped workspace tools"
```

---

## Node 6: Connection Semantics Split

**Goal:** Make connection checking an explicit layered service.

**Files:**

- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`

### Task 6.1: Write Node 6 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-6-connection-semantics-split.md`.

The plan must define the exact layers:

1. structural module/port checks;
2. feature-plugin declarative rules;
3. IP-core declarative constraints;
4. DRC/generation final validation.

### Task 6.2: Implement Layered Connection Checking

- [ ] **Step 1: Add failing tests**

Extend `connectionruleservice_test` for:

- dispatcher ordering;
- cardinality rejection;
- topology rule rejection;
- node-body autocomplete;
- project-load rejection reason;
- IP-core constraint rejection.

- [ ] **Step 2: Split service internals**

Refactor `ConnectionRuleService` so rule layers are explicit and testable.

- [ ] **Step 3: Keep Graph structural**

Ensure `Graph::isValidConnection()` checks only structural integrity.

- [ ] **Step 4: Run verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt connectionruleservice_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
git diff --check
```

Expected: all tests pass.

- [ ] **Step 5: Architecture review**

Dispatch an `xhigh` reviewer subagent to inspect the connection semantics split before archiving.

- [ ] **Step 6: Archive Node 6**

Run:

```bash
git status --short
git add qt docs/superpowers/plans/2026-05-09-node-6-connection-semantics-split.md
git commit -m "archive: complete node-6 connection semantics split"
```

---

## Node 7: Generation, DRC, And Export Boundary

**Goal:** Move generator/DRC export out of `Graph` and target active IP core instances.

**Files:**

- Create: `qt/inc/ipcore/ipcoregraphexporter.h`
- Create: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Create: `qt/test/ipcoregraphexporter_test.cpp`
- Modify: `qt/inc/graph/graph.h`
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/inc/plugins/generatorrunner.h`
- Modify: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/src/app/generationartifacts.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `ipcores/finepaper-noc/generator/src/ruby/parser/json_parser.rb`
- Modify: `ipcores/finepaper-noc/generator/test/test_generator.rb`
- Modify: `ipcores/finepaper-noc/generator/test/expected/**`
- Modify: `ipcores/ravenoc/generator/src/ruby/ravenoc_generator.rb`
- Modify: `ipcores/ravenoc/generator/test/test_generator.rb`
- Modify: `ipcores/ravenoc/generator/test/test_smoke.rb`
- Modify: `ipcores/ravenoc/generator/test/expected/**`
- Modify: `qt/xmake.lua`

### Task 7.1: Write Node 7 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-7-ipcore-generation-boundary.md`.

The plan must finalize replacement names for:

- `finepaper-plugin-graph-v1`
- `generic_graph_v1`
- `plugin_state`

Use these default names unless the node plan chooses better names:

- `finepaper-ipcore-graph-v1`
- `ipcore_graph_v1`
- `ipcore_state`

### Task 7.2: Implement Export Boundary

- [ ] **Step 1: Add exporter tests**

Move graph export expectations out of `graph_test` into `ipcoregraphexporter_test`.

- [ ] **Step 2: Implement exporter**

Create `IpCoreGraphExporter` that receives graph, active IP core metadata, and IP core instance state.

- [ ] **Step 3: Update generator and DRC runners**

Generation and DRC must resolve from active IP core instance/catalog metadata, not by inferring one plugin from graph modules.

- [ ] **Step 4: Update Ruby generators**

Update NoC and RaveNoC parsers/tests for the new schema/input format/state vocabulary.

- [ ] **Step 5: Run verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt ipcoregraphexporter_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
git diff --check
```

Expected: all tests pass.

- [ ] **Step 6: Architecture review**

Dispatch an `xhigh` reviewer subagent for export/generation vocabulary and boundary correctness.

- [ ] **Step 7: Archive Node 7**

Run:

```bash
git status --short
git add qt ipcores generated docs/superpowers/plans/2026-05-09-node-7-ipcore-generation-boundary.md
git commit -m "archive: complete node-7 ipcore generation boundary"
```

---

## Node 8: Historical Cleanup

**Goal:** Remove stale code, duplicate generator paths, and old terminology after target paths work.

**Files:**

- Remove or quarantine: `framework/**`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`
- Modify: `spec_generator/README.md`
- Modify: tests and logs containing old live terminology.

### Task 8.1: Write Node 8 Detailed Plan

- [ ] **Step 1: Create node-level plan**

Create `docs/superpowers/plans/2026-05-09-node-8-historical-cleanup.md`.

The plan must include an allowlist for archived specs/plans so historical documents do not block stale-term scans.

### Task 8.2: Cleanup

- [ ] **Step 1: Remove duplicate framework path**

Remove or quarantine `framework/` after confirming NoC generator tests use `ipcores/finepaper-noc/generator`.

- [ ] **Step 2: Sweep live terminology**

Run:

```bash
rg -n "plugin_state|finepaper-plugin-graph-v1|generic_graph_v1|finepaper.extension.v1|ip_instance|plugins/ravenoc|plugins/noc/generator" \
  qt spec_generator ipcores generated plugins
```

Expected: no live-code hits except explicit rejection tests or planned feature-plugin paths.

- [ ] **Step 3: Update docs**

Update current architecture docs to match implementation reality.

- [ ] **Step 4: Run verification**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
CCACHE_DISABLE=1 xmake test -P qt
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
git diff --check
```

Expected: all tests pass.

- [ ] **Step 5: Archive Node 8**

Run:

```bash
git status --short
git add -A
git commit -m "archive: complete node-8 historical cleanup"
```

---

## Node 9: Final V1 Architecture Gate

**Goal:** Verify the full mainline flow and produce final readiness notes.

**Files:**

- Create: `docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md`
- Create or modify release/readiness notes if the project already has a suitable doc.

### Task 9.1: Write Gate Checklist

- [ ] **Step 1: Create node-level gate plan**

Create `docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md`.

The plan must list every command below and the expected pass condition.

### Task 9.2: Run Final Verification

- [ ] **Step 1: Run Qt tests**

Run:

```bash
CCACHE_DISABLE=1 xmake test -P qt
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: all Qt tests pass and app build succeeds.

- [ ] **Step 2: Run spec and drift checks**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: spec tests and drift checks pass.

- [ ] **Step 3: Run generator suites**

Run:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected: all generator tests pass.

- [ ] **Step 4: Run stale-term scan**

Run:

```bash
rg -n "plugin_state|finepaper-plugin-graph-v1|generic_graph_v1|finepaper.extension.v1|ip_instance|activeIpCombo|Palette dock" \
  qt spec_generator ipcores generated plugins
```

Expected: no live-code hits except explicit rejection tests or feature-plugin paths.

- [ ] **Step 5: Verify mainline user flow**

Use the Qt app or a UI smoke test to verify:

- IP Catalog lists repository IP cores.
- User can add/select one NoC IP core instance.
- Active workspace modules/tools appear for that instance.
- User can create a simple topology.
- Project save/open round-trips the IP core instance.
- Validation/generation runs through that IP core.
- Generated output contains a project snapshot.

- [ ] **Step 6: Archive Node 9**

Run:

```bash
git status --short
git add docs/superpowers/plans/2026-05-09-node-9-final-v1-gate.md
git commit -m "archive: complete node-9 v1 architecture gate"
```

---

## Parallelization Policy

Use subagents only when tasks have disjoint write scopes.

Safe parallel examples:

- Node 1: spec parser/emitter worker, NoC/RaveNoC spec worker, drift review worker.
- Node 2: source tree migration worker, docs terminology worker, registry discovery worker.
- Node 4: panel/model worker, MainWindow integration worker, screenshot/widget reviewer.
- Node 7: Qt exporter worker, Ruby parser worker, generated artifact reviewer.

Unsafe parallel examples:

- two workers editing `spec_generator/lib/spec_generator.rb`;
- two workers editing `MainWindow`;
- one worker renaming project schema while another edits `ProjectReader` tests;
- implementation and cleanup of the same stale term at the same time.

## Execution Handoff

Recommended execution mode:

1. Use `superpowers:subagent-driven-development`.
2. For each node, first create the node-level detailed plan.
3. Execute the node with fresh workers whose write scopes do not overlap.
4. Run node verification.
5. Run one review subagent.
6. Commit the node archive marker.
7. Move to the next node.

The current next executable unit is Node 1.
