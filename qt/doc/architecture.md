# Architecture

## Design summary

The application follows a simple layered structure:

- Model: `Graph`, `Module`, `Connection`, `Port`, `Parameter`
- Command layer: undoable mutations wrapped in `Command` subclasses
- UI layer: Qt Widgets and QtNodes views driven by model signals
- IP core integration layer: startup runtime bundle discovery, IP Catalog/active workspace state, local validators, and IP core generator runners

The main rule in the codebase is that the `Graph` is the source of truth. UI widgets should not mutate persistent design state directly. User actions are converted into commands, commands modify the graph, and graph signals update the UI.

## Component relationships

### 1. Application startup

`src/main.cpp` creates `QApplication`, installs file logging, constructs `MainWindow`, and enters the event loop.

`MainWindow` creates and owns:

- one `Graph`
- one `CommandManager`
- one `NodeEditorWidget`
- one `IpCatalogPanel`
- one `IpCatalogService`
- one `ActiveWorkspaceController`
- one `ProjectIpService`
- one `PropertyPanel`
- one `LogPanel`
- one `ValidationManager`

Before module metadata is used, `IpCoreRuntimeRegistry` discovers generated runtime manifests from `FINEPAPER_IPCORE_PATH` and repository-local `generated/ipcores/` directories. Discovery is startup-only.

`IpCatalogService` turns those runtime manifests into `IpCatalogEntry` records. `ProjectIpService` owns project IP instances and the current selected instance. `ActiveWorkspaceController` combines the selected project instance with its catalog entry to expose the active workspace's IP core id, instance id, module types, and topology presets.

### 2. Core model

`Graph` owns all `Module` and `Connection` instances through `std::unique_ptr`.

Important properties of the model:

- module IDs must be unique
- connections are validated before insertion
- removing a module also removes attached connections
- module parameter changes are forwarded through `Graph::parameterChanged`
- project persistence and graph export are separate boundaries

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

- accepts scoped module drag-and-drop from the active workspace module list
- offers a canvas creation menu for active workspace module types
- turns connection creation/deletion into commands
- turns node moves into parameter updates
- emits `moduleSelected` for the property panel

`PropertyPanel` reflects the currently selected module by generating widgets from parameter types:

- `QString` -> `QLineEdit`
- `int` -> `QSpinBox`
- `double` -> `QDoubleSpinBox`
- `bool` -> `QCheckBox`

All edits are committed through `SetParameterCommand`.

`IpCatalogPanel` provides four workspace-facing lists:

- IP cores discovered from generated runtime manifests
- Project IP instances saved with the project
- Workspace Modules for the selected active IP instance
- Workspace Tools derived from the active IP catalog entry

Workspace module drags carry the IP core id, instance id, and module type. `NodeEditorWidget` accepts the payload only when it matches the current active workspace and the module type belongs to that IP core.

### 5. Module type system

Module definitions are data-driven.

`ModuleRegistry` loads `ModuleType` entries from startup-discovered runtime manifests. Module definitions must be owned by a generated IP core runtime bundle.

For each runtime bundle, the provider stack is:

- `XmlModuleTypeSource` for the IP-core bundle metadata
- `XmlModuleGraphicsOverlay` for per-IP graphics files
- `LayeredModuleProvider` to combine the source and optional overlays

Each loaded `ModuleType` stores the owning `ipcoreId`, which corresponds to the IP core id in the catalog. Type names are unique in the current registry; duplicate type names from later runtime bundles are skipped.

Editable concrete IP core packages live under `ipcores/<package>/` with `ipcore.yml`, `views/`, `generator/`, and `vendor/`. The committed generated runtime metadata lives under `generated/ipcores/<ipcore-id>/` with `ipcore-runtime.json`, `modules.xml`, and `graphics/`. In `ipcore-runtime.json`, `source_root` points back to the editable package. Qt resolves `modules` and `graphics` against the generated runtime root, then stores the source package path as `IpCatalogEntry::sourceRootPath` for generator and DRC process working directories.

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

- `DRCRunner` receives the active `IpCatalogEntry` and selected project IP instance
- `IpCoreGraphExporter` writes active IP-core graph JSON to a temporary file
- runs that package's declared DRC command from `IpCatalogEntry::sourceRootPath`
- parses stderr into `ValidationResult` objects
- maps external IDs back to internal graph IDs when possible

The exported generator/DRC document uses schema `finepaper-ipcore-graph-v1`, includes the selected IP core id, selected instance id, `ipcore_state`, modules, and connections, and rejects graph content outside the active IP core.

Generation uses the same active IP catalog entry and project instance. It writes `finepaper-ipcore-graph-v1` JSON plus a Finepaper `.fpproj` snapshot into the chosen output directory before invoking the package command from `IpCatalogEntry::sourceRootPath`.

## Data flow examples

### Add a module

1. User selects or creates a project IP instance in the IP Catalog.
2. `ActiveWorkspaceController` exposes the selected instance's module types.
3. User drags a type from Workspace Modules or chooses a type from the canvas creation menu.
4. `NodeEditorWidget` validates the scoped module payload against the active workspace.
5. `NodeEditorWidget` creates `AddModuleCommand`.
6. Command inserts a `Module` into `Graph`.
7. `Graph` emits `moduleAdded`.
8. `NodeEditorWidget` creates the visual node.

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
3. `ValidationManager` resolves the active workspace to an `IpCatalogEntry` and selected project IP instance.
4. `ValidationManager` runs `DRCRunner`.
5. `DRCRunner` uses `IpCoreGraphExporter` and the IP core's DRC command.
6. Results are pushed to `LogPanel`.
7. Selecting a log entry can highlight the related element in the editor.

## Operational assumptions

- Concrete IP core packages are editable directories under `ipcores/<package>/`; the bundled Finepaper NoC and RaveNoC packages use Ruby and provide `generator/bin/generate`.
- Runtime bundles are generated under `generated/ipcores/<ipcore-id>/` and contain `ipcore-runtime.json`, `modules.xml`, and per-IP graphics files.
- In `ipcore-runtime.json`, `source_root` points back to the source package. Qt resolves module and graphics metadata against the generated runtime root and executes generator/DRC commands in `IpCatalogEntry::sourceRootPath`.
- Authored JSON module bundles are deprecated conversion inputs; IP-XACT remains a conversion input, not the preferred runtime layout.
- Reserve `plugins/` wording for feature plugins or editor behavior extensions, not concrete NoC or RaveNoC IP packages.
- Position is stored as module parameters such as `x` and `y`.
- Some editor-only state, such as transient selection, is intentionally omitted from graph export.

## Notes for maintainers

- Keep model mutations inside commands unless there is a strong reason not to.
- Preserve the distinction between editor project state and generator graph input.
- Keep runtime bundle discovery startup-only unless runtime reload is explicitly designed.
- Keep active workspace behavior scoped to the selected project IP instance.
- When adding new module categories or layouts, update metadata-driven checks instead of scattering type-name comparisons.
- If IP core generator output changes, update `DRCRunner::parseErrors()` and any ID-mapping assumptions together.
