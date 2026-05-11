# IP Creation Tool Architecture Design

## Summary

Finepaper will converge on an IP creation tool rather than a generic project-type editor. The core workflow is:

- create or open a saved Finepaper project;
- add and configure IP core instances;
- edit each IP instance's internal graph;
- validate the whole project;
- generate output files for every project IP instance.

The design removes the hidden ProjectType direction. IP core category metadata remains useful for catalog grouping and display, but it must not decide project mode or enforce global project-type behavior.

## Goals

- Make project IP instances the main product object.
- Store graph ownership by `{ipcoreId, instanceId}` so delete, save/load, validate, and generate have the same boundary.
- Delete an IP instance as one undoable project operation, including graph contents and state.
- Make Generate and Validate project-level actions over all IP instances.
- Keep Workspace Tools scoped to active-instance editing helpers only.
- Directly cut over IP core runtime terminology from plugin vocabulary before v1.
- Require a real project file before editing or writing generated output.
- Use simple application storage through `QSettings`, not SQLite, for local app state.

## Non-Goals

- Do not introduce ProjectType.
- Do not keep `PluginRegistry` as the IP core runtime registry.
- Do not preserve `plugin.json` as an IP core runtime manifest.
- Do not implement feature-extension loading in this pass.
- Do not use SQLite for project or application state.
- Do not make `.fpproj` depend on machine-local app settings.

## Architecture Boundaries

Finepaper has three conceptual layers.

`Finepaper Core` owns project documents, graph editing, command history, UI orchestration, save/load, validation orchestration, and generation orchestration.

`IpCore Runtime` owns concrete IP package metadata: internal module definitions, graphics metadata, instance parameters, topology presets, generator commands, and DRC commands.

`Feature Extension` is reserved for future editor behavior extensions such as rule providers, analyzers, or domain-specific wizards. No current IP core runtime path should depend on a feature-extension registry.

## IP Core Runtime Cutover

The pre-v1 code should directly replace plugin vocabulary in the concrete IP runtime path:

- `PluginRegistry` becomes `IpCoreRuntimeRegistry`.
- `PluginDescriptor` becomes `IpCoreRuntimeDescriptor`.
- `PluginCommandDescriptor` becomes `IpCoreCommandDescriptor`.
- `PluginInstanceParameterDescriptor` becomes `IpCoreInstanceParameterDescriptor`.
- `plugin.json` becomes `ipcore-runtime.json`.
- `FINEPAPER_PLUGIN_PATH` becomes `FINEPAPER_IPCORE_PATH`.

The IP core runtime registry discovers:

- repository-local `generated/ipcores`;
- directories listed in `FINEPAPER_IPCORE_PATH`;
- user-added IP core runtime paths stored in `QSettings`.

It must not scan `plugins/` for concrete IP cores. The `native` field is removed from IP core runtime metadata because there is no closed native extension loading path.

`spec_generator` emits `ipcore-runtime.json` directly, and generated `plugin.json` artifacts are removed.

## Project Lifecycle

Finepaper should not start in an editable unsaved `Untitled` project.

At startup:

- if a `.fpproj` path is provided, open it;
- otherwise show a project launcher with New Project, Open Project, and Recent Projects.

New Project requires a destination `.fpproj` path before the editor becomes active. Without an open saved project, users cannot add IP instances, edit the graph, validate, or generate.

The project root is `QFileInfo(projectPath).absolutePath()`. Default generated output goes under:

```text
<project-root>/generated/<instance-id>/
```

Application-local state is stored through an `AppSettings` wrapper over `QSettings`. Suggested keys include:

```text
projects/recent
projects/lastDirectory
ipcores/paths
ui/mainWindowGeometry
ui/mainWindowState
generation/lastOutputRoot
```

`QSettings` is not project storage. The `.fpproj` file remains the only user-visible project content format.

## Project And Graph Model

Project IP instance records remain:

```json
{
  "ipcore": "finepaper.ravenoc",
  "instance": "ravenoc_0",
  "schema": "finepaper.ravenoc-project-state-v1",
  "state": {}
}
```

Graph modules become instance-owned:

```json
{
  "id": "module_runtime_id",
  "ipcore": "finepaper.ravenoc",
  "instance": "ravenoc_0",
  "type": "RaveTile",
  "parameters": {}
}
```

`Module` gains `instanceId()` and `setInstanceId()`. `ProjectModuleRecord` gains `instanceId`.

Creation paths must pass both `ipcoreId` and `instanceId`:

- catalog drag/drop payloads;
- canvas create menu;
- topology preset requests;
- module factory calls;
- add-module commands.

Load validation must reject a graph module whose `{ipcore, instance}` does not match a project `ipcore_state` record. It must also reject module types that do not belong to the recorded IP core.

## Delete Instance

Deleting an IP instance is a project-level undoable command, not a bare state mutation.

`RemoveIpInstanceCommand` executes atomically:

- capture the current active selection;
- capture the target `ProjectIpInstanceRecord`;
- capture owned modules where both `ipcoreId` and `instanceId` match;
- capture incident connections;
- remove incident connections;
- remove owned modules;
- remove the IP instance state record;
- select the next available instance, or clear active workspace.

Undo restores:

- the IP instance state record;
- modules;
- connections;
- previous active selection.

`IpCatalogPanel` only emits a removal intent. Command execution belongs in the main application orchestration layer.

## Workspace Tools

Workspace Tools are active-instance editing helpers only. They may include:

- topology presets;
- layout helpers;
- wizards;
- templates;
- other IP-specific graph editing aids.

Workspace Tools must not include Generate or DRC. The panel emits a tool intent with `{toolId, ipcoreId, instanceId}` and does not run external tools directly.

## Validation

Validation is project-level.

The validation flow is:

- run core structural validation over the graph and project records;
- iterate every `ProjectIpInstanceRecord`;
- resolve the matching IP core runtime descriptor;
- export that instance's graph subset;
- run the instance's DRC command when declared;
- show findings with instance context.

If an IP core has no DRC command, the default result is a warning. If a future IP core runtime schema marks DRC as required, missing DRC may become an error for that runtime.

## Generation

Generation is project-level.

The project must be saved before generation. The generator iterates every project IP instance and writes output under that instance's project-root output folder:

```text
<project-root>/generated/<instance-id>/
```

For each instance:

- resolve its IP core runtime descriptor;
- export instance-scoped generator input JSON;
- run its generator command;
- log stdout, stderr, and artifact paths with instance context.

Generation should write a project snapshot or generation manifest beside output artifacts so generated files can be traced back to the saved editor project.

## Instance-Scoped Export

`IpCoreGraphExporter` exports one IP instance at a time.

It accepts a graph, an IP core runtime/catalog entry, and one `ProjectIpInstanceRecord`. It exports only modules whose `ipcoreId` and `instanceId` both match the record. All exported connections must have both endpoints inside the same exported module set.

The generator JSON keeps top-level `ipcore`, `instance`, and `ipcore_state` fields.

## Legacy Cleanup

Delete or replace these pre-v1 paths:

- hidden `kind == noc` single-instance project restriction;
- Generate and DRC entries from `IpToolsModel`;
- active-only Generate and active-only DRC orchestration;
- plugin vocabulary in concrete IP runtime code, tests, generated artifacts, and maintained docs;
- `native` IP runtime metadata;
- `plugins/` scanning for IP cores;
- `FINEPAPER_PLUGIN_PATH`;
- `ConnectionRuleLayer::FeaturePlugin`, replacing it with `EditorRule`.

Keep `kind` only as IP core category/catalog metadata. It does not define a project type.

## Testing

Focused tests should cover:

- project reader/writer round-trips module `instance` ownership;
- load rejects modules whose `{ipcore, instance}` has no matching `ipcore_state`;
- drag/drop and canvas creation stamp both owner fields;
- topology presets stamp both owner fields;
- deleting an instance removes only matching modules, state, and incident connections;
- undo restores graph, state, and active selection;
- Workspace Tools no longer include Generate or DRC;
- Validate iterates all project instances;
- Generate iterates all project instances and uses project-root output paths;
- runtime discovery reads `ipcore-runtime.json` and not `plugin.json`;
- `spec_generator` emits current runtime artifacts with no stale `plugin.json`;
- `QSettings` app state does not affect `.fpproj` portability.

## Implementation Order

1. Introduce runtime vocabulary and `ipcore-runtime.json` cutover.
2. Add project launcher and `AppSettings` for saved-project lifecycle.
3. Add `instanceId` ownership to module, project records, serializer, and creation paths.
4. Make exporter instance-scoped.
5. Implement undoable IP instance deletion.
6. Remove Generate and DRC from Workspace Tools.
7. Convert Validate and Generate to project-level orchestration.
8. Remove legacy restrictions and stale plugin vocabulary.

