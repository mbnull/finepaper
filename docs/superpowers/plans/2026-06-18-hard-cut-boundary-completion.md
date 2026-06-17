# Hard-Cut Boundary Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the remaining P0 fixture cleanup, P6 contract symbol boundary, P3 service identity cleanup, and P2 design-level editing boundary without restoring wrapper project output or Graph-backed durable edits.

**Architecture:** Public project persistence remains flat `ProjectDesign`. Contract strings move into domain-specific headers, while tests assert the public literal values instead of sharing production constants. Durable editor edits move toward `ProjectDesign + ProjectPatch + DesignEditingService`; Graph remains a projection and legacy graph commands remain test-only under `qt/legacy/graphcommands`.

**Tech Stack:** Qt 6 C++23, xmake test targets, existing `ProjectDesign`, `ProjectPatch`, `DesignEditingService`, `EditorProjectionService`, and architecture scan tests.

---

## File Structure

- Create `qt/inc/ipcraft/contract/projectkeys.h` for flat `ipcraft.project.v1` object keys.
- Create `qt/inc/ipcraft/contract/legacyprojectkeys.h` for legacy wrapper keys restricted to migration/tests/gates.
- Create `qt/inc/ipcraft/contract/packagekeys.h` for package manifest keys.
- Create `qt/inc/ipcraft/contract/flowkeys.h` for flow command keys.
- Create `qt/inc/ipcraft/diagnosticids.h` for stable diagnostic ids.
- Create `qt/inc/ipcraft/patchops.h` for current project patch operation names.
- Create `qt/inc/app/serviceids.h`, `qt/inc/app/pluginids.h`, and `qt/inc/app/interactionids.h` for app boundary identifiers.
- Modify `examples/contracts/negative_*/project.fpproj` so negative package/flow fixtures use valid flat project roots.
- Modify `qt/test/ipcraft_contract_examples_test.cpp` so schema validation excludes only explicit project-format-negative fixtures.
- Modify project contract files: `qt/src/project/projectwriter.cpp`, `qt/src/project/projectreader.cpp`, `qt/src/ipcraft/core/project_document_v1.cpp`, and `qt/src/project/projectdesignserializer.cpp`.
- Modify package/flow/plugin files: `qt/src/ipcraft/packagespec.cpp`, `qt/src/package/packagecoverage.cpp`, `qt/src/ipcraft/flowrunner.cpp`, `qt/src/app/pluginhost.cpp`, `qt/src/project/projectplugin.cpp`, `qt/src/package/packageplugin.cpp`, `qt/src/app/toolpipelineplugin.cpp`, `qt/src/app/staticplugincatalog.cpp`, `qt/src/app/topologypresetinteractionhandler.cpp`, and `qt/src/app/mainwindow.cpp`.
- Modify editing boundary files: `qt/inc/project/projectservice.h`, `qt/src/project/projectservice.cpp`, `qt/inc/project/designeditingservice.h`, `qt/src/project/designeditingservice.cpp`, `qt/inc/project/editorprojectionservice.h`, `qt/src/project/editorprojectionservice.cpp`, `qt/inc/nodeeditor/nodeeditorwidget.h`, `qt/src/nodeeditor/nodeeditorwidget.cpp`, `qt/inc/panels/propertypanel.h`, `qt/src/panels/propertypanel.cpp`, `qt/inc/app/topologypresetinteractionhandler.h`, `qt/src/app/topologypresetinteractionhandler.cpp`, and `qt/src/app/mainwindow.cpp`.
- Modify patch model files: `qt/inc/ipcraft/core/project_patch.h`, `qt/src/ipcraft/core/project_patch.cpp`, and `qt/test/ipcraft_patch_foundation_test.cpp`.
- Modify tests/gates: `qt/test/designeditingservice_test.cpp`, `qt/test/editorprojectionservice_test.cpp`, `qt/test/projectservice_test.cpp`, `qt/test/pluginhost_foundation_test.cpp`, `qt/test/projectplugin_test.cpp`, `qt/test/packageplugin_test.cpp`, `qt/test/toolpipelineplugin_test.cpp`, `qt/test/plugin_hard_cutover_scan_test.cpp`, and `qt/test/v1architecturegate_test.cpp`.
- Update `qt/xmake.lua` only if new test support files or headers need explicit target membership.

## Task 1: P0 Negative Contract Fixtures

**Files:**
- Modify: `examples/contracts/negative_extension_required/project.fpproj`
- Modify: `examples/contracts/negative_flow_missing_executable/project.fpproj`
- Modify: `examples/contracts/negative_malformed_package/project.fpproj`
- Modify: `examples/contracts/negative_path_escape/project.fpproj`
- Modify: `qt/test/ipcraft_contract_examples_test.cpp`

- [ ] **Step 1: Write/adjust failing schema gate**

Change `testAllPositiveContractProjectsMatchPublicProjectSchema()` into an all-contract gate that skips only directories whose name or marker explicitly indicates project-format-negative behavior. Expected current failure before fixture conversion: negative project fixtures fail schema validation because they still use wrapper keys.

Run: `env CCACHE_DISABLE=1 xmake run -P qt ipcraft_contract_examples_test`

- [ ] **Step 2: Convert negative fixtures to flat ProjectDesign**

Each fixture should keep `schema`, `id`, `name`, `packages`, `components`, `interfaces`, `connections`, `topologies`, `views`, `diagnostics`, `artifacts`, `extensions`, and `metadata` at the root. Preserve package ids/versions from the old `instances[].package`; encode each former instance as one `components[]` item with `id`, `type`, `packageRef`, and empty `config`.

- [ ] **Step 3: Verify P0 gate**

Run: `env CCACHE_DISABLE=1 xmake run -P qt ipcraft_contract_examples_test`

- [ ] **Step 4: Commit**

Run:

```bash
git add examples/contracts/negative_*/project.fpproj qt/test/ipcraft_contract_examples_test.cpp
git commit -m "test: validate negative contract projects as flat design"
```

## Task 2: P6 Symbol Headers And First Migration

**Files:**
- Create: `qt/inc/ipcraft/contract/projectkeys.h`
- Create: `qt/inc/ipcraft/contract/legacyprojectkeys.h`
- Create: `qt/inc/ipcraft/contract/packagekeys.h`
- Create: `qt/inc/ipcraft/contract/flowkeys.h`
- Create: `qt/inc/ipcraft/diagnosticids.h`
- Create: `qt/inc/ipcraft/patchops.h`
- Create: `qt/inc/app/serviceids.h`
- Create: `qt/inc/app/pluginids.h`
- Create: `qt/inc/app/interactionids.h`
- Modify targeted production files listed in File Structure.
- Modify scan tests listed in File Structure.

- [ ] **Step 1: Add failing P6 scan gates**

Extend `plugin_hard_cutover_scan_test` and/or `v1architecturegate_test` to fail on raw production occurrences of:

```text
ServiceKey::fromLiteral("finepaper.
flow.command_policy_violation
finepaper.project / finepaper.package / finepaper.tool-pipeline / finepaper.noc-plugin plugin ids
command / executable / framework_tool / args / env / allow / capture / stdout / stderr / cwd / timeout_ms / native in FlowRunner command parsing without flowkeys.h
project / instances / composition / layout / migration / native in current writer path
```

Allow tests to assert public literal values and allow legacy wrapper keys in `legacyprojectkeys.h`, migration/read-only compatibility code, legacy tests, and gates.

- [ ] **Step 2: Run gates to verify RED**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
env CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test
```

Expected: fail on raw semantic literals before migration.

- [ ] **Step 3: Add symbol headers**

Use inline functions returning `QString` or `ServiceKey`, for example:

```cpp
namespace app::serviceids {
inline ServiceKey project() { return ServiceKey::fromLiteral("finepaper.project"); }
}
```

Use domain namespaces such as `ipcraft::contract::projectkeys`, `ipcraft::contract::legacyprojectkeys`, `ipcraft::contract::packagekeys`, `ipcraft::contract::flowkeys`, `ipcraft::diagnosticids`, and `ipcraft::patchops`.

- [ ] **Step 4: Migrate targeted production code**

Replace raw semantic contract literals in the files listed in File Structure. Do not replace UI labels, object names, human-readable messages, file names, or test expected literals.

- [ ] **Step 5: Verify P6**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
env CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_flowrunner_test
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_contract_examples_test
```

- [ ] **Step 6: Commit**

Run:

```bash
git add qt/inc/ipcraft/contract qt/inc/ipcraft/diagnosticids.h qt/inc/ipcraft/patchops.h qt/inc/app/serviceids.h qt/inc/app/pluginids.h qt/inc/app/interactionids.h qt/src qt/test qt/xmake.lua
git commit -m "refactor: centralize contract boundary symbols"
```

## Task 3: P3 Registry-Only Service Identity Cleanup

**Files:**
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/app/pluginhost.cpp`
- Modify: `qt/src/project/projectplugin.cpp`
- Modify: `qt/src/package/packageplugin.cpp`
- Modify: `qt/src/app/toolpipelineplugin.cpp`
- Modify: plugin tests that register services but do not assert public literals.

- [ ] **Step 1: Add/verify failing raw service id gate**

Ensure scan tests fail if production code outside `serviceids.h` contains `ServiceKey::fromLiteral("finepaper.`.

- [ ] **Step 2: Replace production service lookups**

Use `app::serviceids::project()`, `designEditing()`, `package()`, `toolPipeline()`, and `workbench()`.

- [ ] **Step 3: Keep missing-service failures**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt pluginhost_foundation_test
env CCACHE_DISABLE=1 xmake run -P qt projectplugin_test
env CCACHE_DISABLE=1 xmake run -P qt packageplugin_test
env CCACHE_DISABLE=1 xmake run -P qt toolpipelineplugin_test
```

- [ ] **Step 4: Commit**

Run:

```bash
git add qt/src/app/mainwindow.cpp qt/src/app/pluginhost.cpp qt/src/project/projectplugin.cpp qt/src/package/packageplugin.cpp qt/src/app/toolpipelineplugin.cpp qt/test
git commit -m "refactor: resolve services through service ids"
```

## Task 4: P2 Remove Normal EditorMutationTarget Surface

**Files:**
- Modify: `qt/inc/project/projectservice.h`
- Modify: `qt/src/project/projectservice.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/inc/app/topologypresetinteractionhandler.h`
- Modify: `qt/src/app/topologypresetinteractionhandler.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify tests using `project/editormutationtarget.h` by moving their fake interface include to legacy/test-only support or by dropping the fake where no longer needed.

- [ ] **Step 1: Add failing scan gates**

Extend gates so production code fails on `project/editormutationtarget.h`, `EditorMutationTarget`, `ProjectService : public EditorMutationTarget`, and `Graph*`/`EditorMutationTarget*` in `TopologyPresetInteractionHandler`.

- [ ] **Step 2: Run gates to verify RED**

Run: `env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test`

- [ ] **Step 3: Remove ProjectService inheritance and methods**

Delete normal `upsertEditorModuleRecord`, `removeEditorModuleRecord`, `upsertEditorConnectionRecord`, and `removeEditorConnectionRecord` declarations/definitions from `ProjectService`.

- [ ] **Step 4: Remove production constructor parameters/members**

Remove `EditorMutationTarget*` from `NodeEditorWidget`, `PropertyPanel`, and `TopologyPresetInteractionHandler`. Update `MainWindow` wiring accordingly.

- [ ] **Step 5: Keep legacy tests isolated**

Legacy graph command tests may keep a test/legacy fake target, but production includes must be clean.

- [ ] **Step 6: Verify and commit**

Run:

```bash
xmake build -P qt qt
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
env CCACHE_DISABLE=1 xmake run -P qt projectservice_test
env CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
env CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
```

Commit:

```bash
git add qt/inc qt/src qt/test qt/xmake.lua
git commit -m "refactor: remove editor mutation target from production"
```

## Task 5: P2 Expand ProjectPatch Contract

**Files:**
- Modify: `qt/inc/ipcraft/core/project_patch.h`
- Modify: `qt/src/ipcraft/core/project_patch.cpp`
- Modify: `qt/test/ipcraft_patch_foundation_test.cpp`

- [ ] **Step 1: Add failing patch tests**

Add tests for `component.add`, `component.remove`, `component.config.set`, `component.config.unset`, `connection.add`, `connection.remove`, `connection.config.set`, `connection.metadata.set`, `connection.class.set`, `view.layout.set`, `view.node_position.set`, `topology.add_or_update`, and `topology.remove`. Include a failure test proving layout keys in component config are rejected and original design remains unchanged.

Run: `env CCACHE_DISABLE=1 xmake run -P qt ipcraft_patch_foundation_test`

- [ ] **Step 2: Implement operation handlers**

Use `ipcraft::patchops` values as the current contract. Keep legacy `"add"` and `"set_config"` parsing only as compatibility aliases where existing tests require it.

- [ ] **Step 3: Validate candidate before commit**

Apply operations against a copied `ProjectDesign`, run `validateProjectDesign(candidate)`, and only assign `result.project = candidate` after success.

- [ ] **Step 4: Verify and commit**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_patch_foundation_test
env CCACHE_DISABLE=1 xmake run -P qt designeditingservice_test
```

Commit:

```bash
git add qt/inc/ipcraft/core/project_patch.h qt/src/ipcraft/core/project_patch.cpp qt/test/ipcraft_patch_foundation_test.cpp qt/test/designeditingservice_test.cpp
git commit -m "feat: expand project patch design operations"
```

## Task 6: P2 DesignEditingService Projection Refresh

**Files:**
- Modify: `qt/inc/project/designeditingservice.h`
- Modify: `qt/src/project/designeditingservice.cpp`
- Modify: `qt/inc/project/editorprojectionservice.h`
- Modify: `qt/src/project/editorprojectionservice.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/designeditingservice_test.cpp`
- Modify: `qt/test/editorprojectionservice_test.cpp`

- [ ] **Step 1: Add failing integration tests**

Add tests proving:
- `DesignEditingService::applyPatch` updates design and emits `designChanged`.
- service-level integration updates `ProjectService` from design editing.
- projection refresh rebuilds Graph from the current document/design without calling `syncProjectFromProjection`.
- projection failure reports `editor.projection_failed`, leaves ProjectDesign authoritative, and marks projection stale.
- undo/redo restores design-level state.

- [ ] **Step 2: Add view-only projection refresh API**

Add a method such as `EditorProjectionService::rebuildProjectionViewOnly(const ProjectDocument&)` that loads `Graph` and workspace state from the document but does not call `ProjectService::replaceDocumentFromProjection` and does not use `GraphProjectSerializer::toProject`.

- [ ] **Step 3: Wire MainWindow design changes**

After `DesignEditingService::designChanged`, call `ProjectService::replaceDesign(...)`, then refresh projection view-only. On failure, report `ipcraft::diagnosticids::editorProjectionFailed()`, mark stale, and reject further projection-derived durable edits until reload/refresh.

- [ ] **Step 4: Verify and commit**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt designeditingservice_test
env CCACHE_DISABLE=1 xmake run -P qt editorprojectionservice_test
env CCACHE_DISABLE=1 xmake run -P qt projectservice_test
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
```

Commit:

```bash
git add qt/inc/project qt/src/project qt/src/app/mainwindow.cpp qt/test
git commit -m "feat: refresh editor projection from design edits"
```

## Task 7: P2 UI Intent Paths And Topology Preset Planner

**Files:**
- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/inc/app/topologypresetinteractionhandler.h`
- Modify: `qt/src/app/topologypresetinteractionhandler.cpp`
- Create if needed: `qt/inc/topology/topologypresetpatchbuilder.h`
- Create if needed: `qt/src/topology/topologypresetpatchbuilder.cpp`
- Modify tests for node editor, property panel, topology preset, and gates.

- [ ] **Step 1: Add failing UI/preset tests**

Add tests proving NodeEditor does not mutate Graph directly for durable edits, PropertyPanel sends component/connection patches through `DesignEditingService`, and TopologyPresetInteractionHandler has no `Graph*`, `EditorMutationTarget*`, or `TopologyPresetCommand` dependency.

- [ ] **Step 2: Wire DesignEditingService into intent paths**

For supported paths, construct `ProjectPatch` with patchops values and call `DesignEditingService::applyPatch`. For unsupported canvas component creation, keep the UI disabled/rejected with an explicit diagnostic message such as `Design-level component creation is not available yet.` and tests proving Graph is not mutated durably.

- [ ] **Step 3: Add topology patch builder**

Implement a design-level planner that returns a patch containing component/connection/topology/view operations. Do not invoke `TopologyPresetCommand` in production.

- [ ] **Step 4: Move MainWindow undo/redo to design owner**

Use `DesignEditingService` for durable design edits. Keep `CommandManager` only for non-design legacy/test commands.

- [ ] **Step 5: Verify and commit**

Run:

```bash
xmake build -P qt qt
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
env CCACHE_DISABLE=1 xmake run -P qt designeditingservice_test
env CCACHE_DISABLE=1 xmake run -P qt editorprojectionservice_test
env CCACHE_DISABLE=1 xmake run -P qt topology_preset_test
env CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
env CCACHE_DISABLE=1 xmake run -P qt nodeeditor_geometry_test
```

Commit:

```bash
git add qt/inc qt/src qt/test qt/xmake.lua
git commit -m "feat: route editor intents through design patches"
```

## Task 8: Final Gates And Required Narrow Tests

**Files:**
- Modify: `qt/test/plugin_hard_cutover_scan_test.cpp`
- Modify: `qt/test/v1architecturegate_test.cpp`

- [ ] **Step 1: Add final gates**

Confirm gates fail on:
- production includes of `project/editormutationtarget.h`
- `ProjectService : public EditorMutationTarget`
- durable commands in `qt/inc/commands` and `qt/src/commands` holding `Graph*`
- production `qt/legacy/graphcommands` include/build references
- raw semantic IDs outside allowed symbol headers
- legacy wrapper keys in current writer path

- [ ] **Step 2: Run required narrow tests**

Run:

```bash
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_contract_examples_test
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_patch_foundation_test
env CCACHE_DISABLE=1 xmake run -P qt designeditingservice_test
env CCACHE_DISABLE=1 xmake run -P qt editorprojectionservice_test
env CCACHE_DISABLE=1 xmake run -P qt projectservice_test
env CCACHE_DISABLE=1 xmake run -P qt pluginhost_foundation_test
env CCACHE_DISABLE=1 xmake run -P qt projectplugin_test
env CCACHE_DISABLE=1 xmake run -P qt packageplugin_test
env CCACHE_DISABLE=1 xmake run -P qt toolpipelineplugin_test
env CCACHE_DISABLE=1 xmake run -P qt plugin_hard_cutover_scan_test
env CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test
env CCACHE_DISABLE=1 xmake run -P qt ipcraft_flowrunner_test
```

- [ ] **Step 3: Run final review**

Use code review after all tasks and fix any Critical/Important findings.

- [ ] **Step 4: Final commit if gates changed**

Run:

```bash
git add qt/test/plugin_hard_cutover_scan_test.cpp qt/test/v1architecturegate_test.cpp
git commit -m "test: enforce hard-cut architecture gates"
```
