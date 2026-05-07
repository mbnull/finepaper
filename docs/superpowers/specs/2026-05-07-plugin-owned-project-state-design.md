# Plugin-Owned Project State Design

## Context

Finepaper currently stores IP instance parameters in the project document and mirrors one IP instance into `Graph`. That works for the current RaveNoC path, but it creates a bad long-term boundary: every plugin-specific concept pressures `Graph` to become a universal project model.

The desired direction is to keep `Graph` focused on editor topology and move plugin-specific parsing, parameters, validation, and generation behind C++ plugin interfaces. Project files should remain portable and should not lose plugin-owned state when a plugin is missing.

## Goals

- Keep `Graph` as the core topology model: modules, ports, connections, and generic module parameters.
- Store plugin-owned project state in `ProjectDocument`, not as first-class graph semantics.
- Let plugins provide Qt-facing adapters for parameter UI, validation, import/export, and generation.
- Preserve unknown or missing-plugin state across load/save without requiring the core app to parse its semantics.
- Keep undo, redo, dirty tracking, and save prompts owned by the core command system.

## Non-Goals

- Do not turn `Graph` into a universal IP/domain state model.
- Do not require `.fpproj` files to bundle plugin binaries.
- Do not require the core app to understand every plugin's parameter schema.
- Do not replace the existing module/connection graph editing model.

## Project File Model

`.fpproj` should contain core-readable topology plus opaque plugin state.

Core-readable data:

- Project schema and document metadata.
- Required plugin list with `id`, `version`, and state/schema version.
- Graph modules, ports, connections, and generic module parameters.
- Plugin state records keyed by plugin id and instance id.

Plugin-owned state:

- Stored as JSON objects that the core app can read, preserve, and write back.
- Parsed only by the owning plugin when available and compatible.
- Preserved byte-for-byte in meaning, though object key ordering may still follow the project writer's stable JSON formatting.

Missing-plugin behavior:

- The project opens with topology visible.
- Unknown plugin state remains attached to the document.
- Plugin-specific UI, validation, and generation are disabled for missing or incompatible plugins.
- Saving the project writes unknown plugin state back instead of dropping it.

## Core Responsibilities

The core app owns:

- Project file read/write lifecycle.
- Graph topology editing.
- Plugin dependency discovery and compatibility status.
- Command history, dirty tracking, undo, and redo.
- Routing plugin-owned parameter edits through commands.
- Displaying missing or incompatible plugin diagnostics.

The core app does not own:

- Plugin parameter semantics.
- Plugin-specific validation rules.
- Generator input shape beyond passing graph and plugin state to the plugin.
- Plugin-specific UI layout beyond hosting plugin-provided models/widgets.

## Plugin Interfaces

Plugins should be C++ libraries loaded by the Qt app. They should expose stable interfaces rather than only external commands.

Proposed interfaces:

- `IPluginProjectAdapter`: parse, validate, migrate, and serialize plugin state.
- `IPluginParameterProvider`: expose global/project parameter sections, labels, defaults, choices, editability, and grouping.
- `IPluginValidator`: run plugin-owned DRC against graph plus plugin state.
- `IPluginGenerator`: generate artifacts or generator inputs from graph plus plugin state.
- `IPluginUiProvider`: optional Qt widgets or item models for advanced plugin UI.

The first implementation can use metadata models instead of arbitrary plugin widgets. That keeps the host UI consistent while still letting plugins define sections, fields, validation, and defaults.

## Parameter UI

Global plugin parameters should not be hardcoded in `PropertyPanel`.

When no module is selected, the panel should ask available plugins for project/global parameter sections. Each section can be collapsible and should include:

- Section id and display label.
- Owning plugin id and optional instance id.
- Field descriptors: name, label, type, value, choices, read-only/configurable state, description, and range metadata.
- Default expanded state.

Edits should create core commands such as `SetPluginStateParameterCommand`. The command stores the plugin id, state path or field id, old value, and new value, then calls the plugin adapter or document state service to apply the change.

## Save And Load Flow

Load:

1. Read `.fpproj` into `ProjectDocument`.
2. Load graph topology into `Graph`.
3. Keep plugin state records in a project state service owned outside `Graph`.
4. Resolve plugins by id and version.
5. For available plugins, ask adapters to parse and migrate their state.
6. For missing plugins, keep raw state and report limited functionality.

Save:

1. Serialize graph topology from `Graph`.
2. Ask available plugin adapters to serialize current plugin state.
3. Merge raw missing-plugin state unchanged.
4. Write a complete `.fpproj` with dependency metadata and plugin state records.

## Migration Path

Current PR:

- Keep the existing single IP instance path.
- Reject multiple IP instances during project load so accepted data is never silently dropped.
- Route IP instance parameter edits through the command stack.
- Add tests for load rejection and undo/dirty behavior.

Next architecture step:

1. Introduce a project plugin state service separate from `Graph`.
2. Move current IP instance parameter storage from `Graph` into that service.
3. Add a RaveNoC plugin adapter that owns the current global parameters.
4. Update `PropertyPanel` to render plugin parameter sections from provider metadata.
5. Change generation and validation paths to consume `Graph + plugin state`.
6. Preserve unknown plugin state on load/save.
7. Remove the temporary core-owned IP instance special case once the plugin path covers it.

## Acceptance Criteria

- Loading and saving a project with known plugin state preserves all plugin-owned data.
- Loading and saving a project with missing plugin state does not drop that state.
- `Graph` remains free of plugin-specific IP semantics.
- Plugin global parameters appear in the property panel through provider metadata.
- Plugin parameter edits are undoable and mark the document dirty.
- Generation and validation receive graph topology plus plugin-owned state through plugin interfaces.
- Existing project files using the current IP instance shape migrate without data loss.

## Implementation Status 2026-05-08

This spec is validated against `master` at merge commit `4d1e9c7` plus follow-up test fix `f4584c2`.

Implemented:

- `.fpproj` stores opaque `plugin_state` records and preserves missing-plugin state across load/save.
- Legacy `ip_instances` migrate into `plugin_state` without dropping mixed legacy/current records.
- `Graph` no longer owns IP instance/global parameter state.
- `ProjectStateService` owns editable plugin state outside `Graph`.
- `PropertyPanel` renders plugin global parameters from provider metadata.
- Plugin state parameter edits go through command history and mark the document dirty.
- Generation and DRC receive `Graph + plugin_state`; the legacy `ip_instance` object is derived only at the external JSON boundary for compatibility.
- Generated Verilog output directories also receive a `.fpproj` project snapshot through `writeGeneratedProjectSnapshot()`.

Remaining architecture work:

- Replace manifest-only adapter construction with native C++ plugin interfaces loaded from plugin libraries.
- Let native plugins parse, validate, migrate, serialize, and generate from graph plus plugin state directly.
- Keep command-based external generator/DRC support as a compatibility fallback while native plugins are introduced.
