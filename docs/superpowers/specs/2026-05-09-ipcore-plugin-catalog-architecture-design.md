# IP Core, Feature Plugin, And Catalog Architecture Design

## Summary

Finepaper should stop treating a runtime plugin, a concrete IP core, and the active editor workspace as the same concept. The current Qt app exposes a global active-IP combo box, filters a simple module palette by that selection, and runs generation/DRC by inferring one owning plugin from graph modules. That was useful for early NoC/RaveNoC work, but it will not scale to a real IP library.

The next architecture should split the model into three layers:

- **Qt Core** owns project documents, graph editing, command history, docking, persistence, and safe process invocation.
- **Feature plugins** add behavior for a domain or capability, such as NoC topology tools, NoC connection semantics, or a deadlock analyzer launcher.
- **IP core packages** describe concrete hardware IP, such as RaveNoC or the Finepaper demo NoC, including metadata, internal editable modules, generator/DRC commands, vendor RTL, and tests.

The UI should move from "choose an active plugin from a toolbar combo" to an IP Catalog similar to an EDA tool: a searchable library, a project IP instance list, and context-aware tools/modules for the selected IP instance.

## Current Problems

The current code has several temporary assumptions that should not become v1 architecture:

- `plugins/` means both "runtime extension mechanism" and "concrete IP package".
- `plugins/ravenoc` is actually an IP core package, not a feature plugin.
- `spec/noc/ravenoc.yml` uses `finepaper.extension.v1`, but it models a concrete IP core.
- `modules.xml` is partly generated and partly hand-edited; connection-rule metadata has already drifted from spec sources.
- `MainWindow` owns active IP selection, topology actions, and many cross-cutting services directly.
- `Palette` is a `QListWidget` of module types, not an IP Catalog.
- `ModuleRegistry` requires globally unique type names, which will fail for common names such as `Endpoint`, `Router`, or `Tile`.
- Project-visible names still say `plugins` and `plugin_state`, even though the stored data is becoming IP-core state.
- Generator input still uses names such as `finepaper-plugin-graph-v1` and `generic_graph_v1`.
- `Graph::toJsonDocument()` exports generator input directly; graph storage and IP-core export should be separate boundaries.
- Topology presets mutate `Graph` directly and are not command/undo scoped.
- `framework/` duplicates the NoC generator under `plugins/noc/generator`.

## Goals

- Define stable terminology before the v1 project format is frozen.
- Make spec files the source of truth for IP core metadata and generated runtime artifacts.
- Reserve `plugins/` for feature/type plugins, not concrete IP packages.
- Introduce `ipcores/` as the source location for concrete IP core packages.
- Replace the active-IP combo and simple module palette with an IP Catalog workflow.
- Scope module creation, topology tools, generation, validation, and property editing to a selected project IP instance.
- Split connection checking into core structural checks, feature-plugin rules, IP-core constraints, and final DRC.
- Remove pre-v1 compatibility paths instead of preserving temporary schemas.
- Add drift checks so generated metadata cannot be hand-edited silently.
- Use subagents for independent read-only exploration, implementation slices, and review work with reasoning effort at least `high`; use `xhigh` for architecture-critical reviews.

## Mainline Scope

The first implementation should optimize for the core product flow, not for every boundary condition:

1. Discover known IP core packages from the repository.
2. Show them in an IP Catalog.
3. Add/select one IP core instance in the project.
4. Edit its internal graph with scoped modules and tools.
5. Save/open the project with the new IP core vocabulary.
6. Run validation/generation through that IP core.

Edge handling such as missing packages, untrusted packages, multi-vendor installation flows, native plugin loading, and external marketplace behavior should be recorded as future work unless it blocks the mainline flow.

## Non-Goals

- Do not implement a plugin trust/signature system in this architecture pass.
- Do not design third-party native plugin loading as part of the first functional path.
- Do not preserve old pre-v1 `.fpproj`, legacy NoC JSON, or temporary `ip_instances` compatibility.
- Do not require every future IP category to be modeled in the first implementation.
- Do not build a complete commercial-grade IP marketplace.

## Function-First Stance

This is a pre-v1 architecture cleanup. Prefer direct replacement over fallback layers:

- rename project-visible concepts before v1 instead of supporting both old and new keys;
- move concrete IP packages to `ipcores/` instead of keeping duplicate source layouts;
- generate runtime metadata from spec instead of allowing manual patches;
- use final DRC/generation validation for complex rules instead of adding editor-time escape hatches;
- delete temporary compatibility code in the same node that replaces it.

## Terminology

### Feature Plugin

A feature plugin provides behavior to the editor. In the first functional path it is built-in or manifest-driven. Native C++ plugin loading is outside this architecture pass.

Examples:

- `noc`: NoC type behavior, topology tools, common NoC connection rules, deadlock analysis launcher.
- future `axi`: AXI interface helpers or protocol-specific analyzers.

A feature plugin is not necessarily a hardware IP core.

### IP Core Package

An IP core package provides a concrete hardware IP definition and its generation/validation assets.

Examples:

- `finepaper.noc`: the Finepaper demo NoC IP core.
- `finepaper.ravenoc`: the RaveNoC IP core package.

An IP core package may declare:

- `ipcore.yml` as source of truth.
- internal editable modules and interfaces.
- instance parameters.
- topology presets.
- generator and DRC commands.
- vendor RTL and templates.
- tests.

### IP Core Instance

An IP core instance is one configured occurrence of an IP core inside a project. A project can contain multiple IP core instances when the project model supports it.

For v1, NoC may still be constrained to at most one active NoC fabric, but that should be expressed as a project rule for `kind: noc`, not as a global active plugin.

### Internal Module

An internal module is a node inside an IP core instance's editable internal graph, such as `RaveTile`, `RaveEndpoint`, `XP`, or `Endpoint`.

Internal modules are not the same as IP cores in the catalog.

## Target Directory Model

The target source layout is:

```text
plugins/
  noc/
    plugin.yml
    tools/
    analyzers/

ipcores/
  finepaper-noc/
    ipcore.yml
    views/
    generator/
    tests/

  ravenoc/
    ipcore.yml
    views/
    generator/
    vendor/
    tests/

generated/ipcores/
  finepaper.noc/
    plugin.json
    modules.xml
    graphics/

  finepaper.ravenoc/
    plugin.json
    modules.xml
    graphics/
```

The first implementation should move directly to the target source-of-truth layout instead of preserving old runtime paths. The rule is strict:

- developers edit `ipcores/*/ipcore.yml` and view XML;
- `plugin.json`, `modules.xml`, graphics overlays, and Ruby model files are generated artifacts;
- generated artifacts remain committed for simple development and packaging;
- generated artifacts must be covered by drift checks.

## Spec Model

Introduce this schema:

```yaml
schema: finepaper.ipcore.v1
id: finepaper.ravenoc
name: RaveNoC
version: "1.0"
kind: noc
runtime:
  generator: ...
  drc: ...
instance_parameters: ...
topology_presets: ...
modules: ...
```

Required first-pass fields:

- `id`, `name`, `version`, and `kind`.
- `runtime.generator` and `runtime.drc` when the IP core supports them.
- `instance_parameters` for fabric/IP-level configuration.
- `modules` for editable internal graph nodes.
- interface-level connection fields: `bus`, `role`, `connects_to`, `match`, `accepts`, `config`, `cardinality`, `autocomplete_group`, `topology_rule`.
- topology preset metadata.
- view/anchor metadata.

The existing `finepaper.extension.v1` schema should be removed during migration. Do not carry a long-lived alias.

## Project Model Direction

Use project-visible vocabulary that matches the target model:

```json
{
  "schema": "finepaper-project-v1",
  "kind": "finepaper-project",
  "ipcores": [
    { "id": "finepaper.ravenoc", "version": "1.0" }
  ],
  "ipcore_state": [
    {
      "ipcore": "finepaper.ravenoc",
      "instance": "ravenoc_0",
      "schema": "v1",
      "state": {}
    }
  ],
  "graph": {
    "modules": [],
    "connections": []
  }
}
```

The implementation should rename project-visible fields to the target vocabulary before v1. Do not keep `plugins` / `plugin_state` as the saved project shape.

Module records should reference the owning IP core, not a feature plugin:

```json
{
  "id": "rave_0_0",
  "ipcore": "finepaper.ravenoc",
  "type": "RaveTile",
  "parameters": {}
}
```

## Qt Service Boundaries

### IpCatalogService

Read-only catalog service over discovered IP core packages. It exposes `IpCatalogEntry` records:

- id, name, version, kind, description.
- tags/categories.
- available capabilities: generator, DRC, topology presets, analyzers.
- internal module summaries for preview, not direct mutation.

### ProjectIpService

Owns project IP core instances:

- add/remove/rename/select IP instance.
- initialize default `ipcore_state`.
- enforce project constraints such as at most one `kind: noc` instance.
- emit `activeIpInstanceChanged`.
- round-trip state through project documents.

### ActiveWorkspaceController

Coordinates the selected project IP instance with:

- active internal graph scope.
- internal module library.
- topology/tool actions.
- generation and validation target.
- property panel context.

This replaces `MainWindow::m_activePluginId` as the source of active workspace truth.

### InternalModuleLibraryModel

Provides the module list for the active IP instance. Drag/drop and canvas context menus must both use this model. No UI path should list global module types directly.

### IpToolsModel

Provides tools and topology presets for the active IP instance. MainWindow should render actions from this model instead of directly rebuilding a topology menu from the active plugin.

## IP Catalog UI

The left dock should evolve into an IP Catalog panel with three functional zones:

1. **Catalog**: searchable categorized IP library.
2. **Project IPs**: instances already added to the current project.
3. **Active IP Workspace**: internal modules and tools for the selected IP instance.

The panel should emit intent signals such as:

- add IP core to project.
- select project IP instance.
- drag internal module type.
- run active IP tool.

It should not mutate `Graph` directly.

The toolbar active-IP combo should be removed once `ProjectIpService` and IP Catalog selection own active workspace state.

## Connection Semantics

Connection checking should be layered:

1. **Core structural checks**: module exists, port exists, no self-loop, no exact duplicate.
2. **Feature-plugin declarative rules**: common domain rules such as NoC bus/role/match/cardinality/topology constraints.
3. **IP-core constraints**: constraints from the concrete IP core spec, such as supported protocols, endpoint limits, and parameter-bound compatibility.
4. **Final DRC/analyzer**: generator-backed or external checks for full correctness.

The UI only captures gestures and displays options. It should not inspect NoC details, router sides, or endpoint classes directly.

The first pass should remain data/spec-driven. If a rule cannot be expressed declaratively, leave it to final DRC/generation validation rather than adding another editor-time fallback path.

## Generation And Validation

Generation belongs to the selected IP core instance, not to a feature plugin.

The export path should move out of `Graph::toJsonDocument()` into an exporter/service that receives:

- graph or active graph scope.
- IP core metadata.
- IP core instance state.
- design name/output directory.

Rename external schemas before v1:

- `finepaper-plugin-graph-v1` should become an IP-core-oriented schema name.
- `generic_graph_v1` should become an IP-core graph input format name.

Finalize replacement names in the first implementation plan. The old names must not survive into v1 artifacts.

## Task Inventory

### Node 0: Umbrella Spec And Checkpoint Protocol

- Write this umbrella spec.
- Commit the spec as the first archive marker.
- Keep visual companion artifacts and untracked local files out of the commit.

Archive marker: `archive: add ipcore plugin catalog architecture spec`.

### Node 1: Source-Of-Truth And Drift Cleanup

- Extend `spec_generator` schemas to emit `cardinality`, `autocomplete_group`, `topology_rule`, `mesh_col`, and `mesh_row`.
- Move all hand-edited connection metadata back into specs.
- Add drift checks that regenerate runtime artifacts to temporary directories and compare them with committed generated artifacts.
- Mark generated files as generated in docs or headers where practical.

Parallel work:

- one worker for `spec_generator` schema/emitter changes;
- one worker for NoC/RaveNoC source spec updates;
- one reviewer for generated artifact drift and test coverage.

Archive marker: `archive: complete node-1 spec source of truth`.

### Node 2: Vocabulary And Directory Migration

- Introduce `finepaper.ipcore.v1`.
- Create `ipcores/finepaper-noc` and `ipcores/ravenoc` source packages.
- Decide final generated artifact root and update discovery to include it.
- Reserve `plugins/` for feature plugins.
- Update docs to use feature plugin vs IP core consistently.
- Remove `finepaper.extension.v1` after generated artifacts are stable.

Parallel work:

- one worker for source tree migration;
- one worker for docs/spec updates;
- one worker for registry discovery changes.

Archive marker: `archive: complete node-2 ipcore vocabulary migration`.

### Node 3: Project Model And Services

- Introduce `IpCatalogService`.
- Introduce `ProjectIpService`.
- Introduce `ActiveWorkspaceController`.
- Rename project-visible concepts toward `ipcores` and `ipcore_state`.
- Remove hardcoded version `1.0` where dependency metadata can come from IP core metadata.

Parallel work:

- one worker for read-only catalog service;
- one worker for project-state/service model;
- one reviewer for project document vocabulary and pre-v1 cuts.

Archive marker: `archive: complete node-3 project ip services`.

### Node 4: IP Catalog UI

- Replace `Palette` with `IpCatalogPanel` or wrap it behind a new panel first.
- Add search and category filtering.
- Show project IP instances.
- Show active instance modules/tools.
- Remove toolbar active-IP combo after the service path is in place.
- Keep module drag/drop behavior working through `InternalModuleLibraryModel`.

Parallel work:

- one worker for panel/model shell;
- one worker for MainWindow docking/actions integration;
- one reviewer using screenshots or widget tests.

Archive marker: `archive: complete node-4 ip catalog ui`.

### Node 5: Scoped Module Creation And Workspace Tools

- Make drag/drop module creation validate active IP instance scope.
- Make canvas right-click creation use the same active-scoped module model.
- Move topology presets behind `IpToolsModel`.
- Make topology preset execution command/undo/dirty aware.
- Keep direct graph mutation out of UI panels.

Parallel work:

- one worker for node editor creation paths;
- one worker for tools/topology action boundaries;
- one reviewer for undo/dirty regressions.

Archive marker: `archive: complete node-5 scoped workspace tools`.

### Node 6: Connection Semantics Split

- Keep `Graph` structural only.
- Make `ConnectionRuleService` a dispatcher over structural, feature-plugin, and IP-core declarative rules.
- Move NoC-specific connection metadata into the NoC feature/IP-core metadata boundary.
- Keep Ruby/plugin DRC as final authority.
- Add tests for node-body autocomplete, cardinality, topology rules, and project-load rejection.

Parallel work:

- one worker for dispatcher/service shape;
- one worker for metadata/spec test fixtures;
- one architecture reviewer with `xhigh` reasoning.

Archive marker: `archive: complete node-6 connection semantics split`.

### Node 7: Generation, DRC, And Export Boundary

- Move plugin graph export out of `Graph`.
- Rename external schema/input format before v1.
- Make generation target the active IP core instance.
- Clean NoC/RaveNoC Ruby parsers of legacy standalone config paths if no longer needed.
- Remove `ip_instance` compatibility JSON.

Parallel work:

- one worker for Qt exporter/generator runner;
- one worker for Ruby generator/DRC inputs;
- one reviewer for produced artifacts and project snapshots.

Archive marker: `archive: complete node-7 ipcore generation boundary`.

### Node 8: Historical Cleanup

- Remove or quarantine duplicated `framework/` content.
- Remove stale docs that describe old plugin/IP conflation.
- Remove old naming from tests and logs.
- Remove inactive compatibility branches.
- Update architecture docs after implementation reality matches the new model.

Parallel work:

- one worker for code/docs terminology cleanup;
- one worker for duplicate generator cleanup;
- one reviewer for stale-string scans.

Archive marker: `archive: complete node-8 historical cleanup`.

### Node 9: Final V1 Gate

- Run full Qt test suite.
- Run spec generator tests and drift checks.
- Run NoC and RaveNoC generator/DRC tests.
- Run UI smoke tests or screenshot checks for IP Catalog.
- Verify the mainline project save/open round trip with one NoC IP core.
- Verify generated output contains project snapshot.
- Create final archive marker and release-readiness notes.

Archive marker: `archive: complete node-9 v1 architecture gate`.

## Review Gates

Every large node must finish with:

- tests relevant to that node;
- focused stale-term scan for names the node claims to remove;
- updated spec/plan notes if scope changed;
- one archive commit whose subject starts with `archive:`;
- no unrelated worktree cleanup or user-file changes.

Subagents should be used for independent implementation or review slices. Their reasoning effort must be at least `high`; architecture-critical reviews should use `xhigh`.

## Suggested Execution Order

The recommended order remains:

1. Node 0: umbrella spec.
2. Node 1: source-of-truth and drift cleanup.
3. Node 2: vocabulary and directory migration.
4. Node 3: project model and services.
5. Node 4: IP Catalog UI.
6. Node 5: scoped module creation and workspace tools.
7. Node 6: connection semantics split.
8. Node 7: generation/DRC/export boundary.
9. Node 8: historical cleanup.
10. Node 9: final v1 gate.

Nodes 1 and 2 can overlap after this spec is approved. Nodes 4 and 5 can overlap after Node 3 service contracts are stable. Nodes 6 and 7 can overlap after the source-of-truth drift checks are passing.

## Acceptance Criteria

- Developers can tell whether a file is source, generated runtime metadata, or tool output.
- `plugins/` no longer has to mean "concrete IP core package".
- IP cores are discoverable through a catalog model rather than a toolbar combo.
- Project state can represent IP core instances directly.
- Internal module creation is scoped to the active IP instance.
- Connection decisions are available to the frontend without embedding NoC/IP-specific rules in UI code.
- Generation and DRC are invoked through IP-core capabilities.
- Pre-v1 legacy paths are removed or rejected clearly.
- Drift checks prevent manual edits to generated metadata.
