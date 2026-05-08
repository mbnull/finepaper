# Architecture

## Design summary

The application follows a simple layered structure:

- Model: `Graph`, `Module`, `Connection`, `Port`, `Parameter`
- Command layer: undoable mutations wrapped in `Command` subclasses
- UI layer: Qt Widgets and QtNodes views driven by model signals
- Plugin/integration layer: startup plugin discovery, local validators, and plugin generator runners

The main rule in the codebase is that the `Graph` is the source of truth. UI widgets should not mutate persistent design state directly. User actions are converted into commands, commands modify the graph, and graph signals update the UI.

## Component relationships

### 1. Application startup

`src/main.cpp` creates `QApplication`, installs file logging, constructs `MainWindow`, and enters the event loop.

`MainWindow` creates and owns:

- one `Graph`
- one `CommandManager`
- one `NodeEditorWidget`
- one `Palette`
- one `PropertyPanel`
- one `LogPanel`
- one `ValidationManager`

Before module metadata is used, `PluginRegistry` discovers plugin manifests from `FINEPAPER_PLUGIN_PATH` and repository-local `plugins/` directories. Discovery is startup-only. The registry stores directory plugin metadata and native plugin metadata, but native libraries are not loaded in this version.

### 2. Core model

`Graph` owns all `Module` and `Connection` instances through `std::unique_ptr`.

Important properties of the model:

- module IDs must be unique
- connections are validated before insertion
- removing a module also removes attached connections
- module parameter changes are forwarded through `Graph::parameterChanged`
- JSON export supports both editor and framework flavors

`Module` contains:

- stable internal ID
- module type name
- port list
- typed parameter map

`Connection` contains:

- connection ID
- source `PortRef`
- target `PortRef`

### 3. Command layer

The command layer provides mutation isolation and undo/redo.

Current command families include:

- add/remove module
- add/remove connection
- set parameter
- load graph
- arrange nodes

`CommandManager` executes commands, pushes successful commands onto the undo stack, and clears redo state when a new command succeeds.

### 4. UI synchronization

`NodeEditorWidget` is the key adapter between the model and QtNodes.

From model to UI:

- listens to `Graph` signals
- creates or removes visual nodes/connections
- refreshes visual presentation when module parameters change

From UI to model:

- interprets drag-and-drop from the palette
- turns connection creation/deletion into commands
- turns node moves into parameter updates
- emits `moduleSelected` for the property panel

`PropertyPanel` reflects the currently selected module by generating widgets from parameter types:

- `QString` -> `QLineEdit`
- `int` -> `QSpinBox`
- `double` -> `QDoubleSpinBox`
- `bool` -> `QCheckBox`

All edits are committed through `SetParameterCommand`.

`Palette` is read-only UI that exposes registered module types as drag sources.

### 5. Module type system

Module definitions are data-driven.

`ModuleRegistry` loads `ModuleType` entries from startup-discovered plugin manifests. Module definitions must be plugin-owned.

For each plugin, the provider stack is:

- `XmlModuleTypeSource` for the IP-core bundle metadata
- `XmlModuleGraphicsOverlay` for per-IP graphics files
- `LayeredModuleProvider` to combine the source and optional overlays

Each loaded `ModuleType` stores the owning `pluginId`. Type names are unique in the current registry; duplicate type names from later plugins are skipped.

Bundle metadata controls:

- palette naming
- module descriptions
- graph grouping
- external/display identity generation
- default ports and parameters
- port roles and bus-family metadata
- parameter descriptions and config visibility
- node color and editor layout through graphics overlays

`ModuleTypeMetadata` centralizes lookup helpers so UI and validation code can ask semantic questions such as:

- is this module a mesh router?
- does this module support collapse?
- what prefix should its external ID use?

### 6. Validation and generation

Validation is composed in `ValidationManager`.

Local validation:

- invalid connections
- unconnected ports for non-router modules

External validation:

- `DRCRunner` exports plugin graph JSON to a temporary file
- resolves the single plugin used by the graph
- runs that plugin's declared generator command
- parses stderr into `ValidationResult` objects
- maps external IDs back to internal graph IDs when possible

Generation uses the same plugin generator resolution and writes plugin graph JSON plus a Finepaper project snapshot into the chosen output directory before invoking the plugin command. If the graph uses modules from multiple plugins, generation fails with a clear message because cross-plugin orchestration is reserved for a later phase.

## Data flow examples

### Add a module

1. User drags a type from `Palette`.
2. `NodeEditorWidget` creates `AddModuleCommand`.
3. Command inserts a `Module` into `Graph`.
4. `Graph` emits `moduleAdded`.
5. `NodeEditorWidget` creates the visual node.

### Edit a parameter

1. User changes a widget in `PropertyPanel`.
2. Panel creates `SetParameterCommand`.
3. Command updates `Module::setParameter`.
4. `Module` emits `parameterChanged`.
5. `Graph` forwards the change.
6. UI components refresh affected state.

### Validate the design

1. User triggers validation from the main window.
2. `ValidationManager` runs `BasicValidator`.
3. `ValidationManager` runs `DRCRunner`.
4. Results are pushed to `LogPanel`.
5. Selecting a log entry can highlight the related element in the editor.

## Operational assumptions

- Plugins are directories with `plugin.json`; the bundled NoC plugin uses Ruby and provides `generator/bin/generate`.
- Module bundles are preferably expressed as plugin-owned `modules.xml` plus per-IP graphics files. Authored JSON module bundles are deprecated conversion inputs; IP-XACT remains a conversion input, not the preferred runtime layout.
- Native plugin metadata may be present in `plugin.json`, but C++ dynamic libraries are not loaded yet.
- Position is stored as module parameters such as `x` and `y`.
- Some editor-only state, such as transient selection, is intentionally omitted from plugin graph export.

## Notes for maintainers

- Keep model mutations inside commands unless there is a strong reason not to.
- Preserve the distinction between editor project state and plugin graph input.
- Keep plugin discovery startup-only unless runtime reload is explicitly designed.
- When adding new module categories or layouts, update metadata-driven checks instead of scattering type-name comparisons.
- If plugin generator output changes, update `DRCRunner::parseErrors()` and any ID-mapping assumptions together.
