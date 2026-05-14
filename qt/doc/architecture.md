# Architecture

## Design summary

The application follows a simple layered structure:

- Model: `Graph`, `Module`, `Connection`, `Port`, `Parameter`
- Command layer: undoable mutations wrapped in `Command` subclasses
- UI layer: Qt Widgets and QtNodes views driven by model signals
- IP core integration layer: startup package-root discovery, IP Catalog/active workspace state, local validators, and IP core generator runners

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

Before module metadata is used, `ModuleRegistry` loads package-local `ipcraft.json` manifests from package roots stored in application settings. Tests and tools can load manifests from explicit package-root paths. Startup discovery does not infer repository-local package roots from the current working directory or executable location.

The package authoring source is `ipcore.yml`, a constrained YAML format with explicit schema names, stable IDs, and strict key validation. Qt does not parse authoring YAML during runtime loading. Qt consumes the normalized `ipcraft.json` runtime manifest, referenced view XML, and declared command paths from each configured package root.

`IpCatalogService` turns those Ipcraft package manifests into `IpCatalogEntry` records. `ProjectIpService` owns project IP instances and the current selected instance. `ActiveWorkspaceController` combines the selected project instance with its catalog entry to expose the active workspace's IP core id, instance id, module types, and topology presets.

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

- IP cores discovered from package-local `ipcraft.json` manifests
- Project IP instances saved with the project
- Workspace Modules for the selected active IP instance
- Workspace Tools derived from the active IP catalog entry

Workspace module drags carry the IP core id, instance id, and module type. `NodeEditorWidget` accepts the payload only when it matches the current active workspace and the module type belongs to that IP core.

### 5. Module type system

Module definitions are data-driven.

`ModuleRegistry` loads `ModuleType` entries from startup-discovered package manifests. Module definitions must be owned by an Ipcraft package.

For each package, the provider stack is:

- `IpcraftModuleTypeSource` for the package-local manifest metadata
- `IpcraftModuleViewOverlay` for package view XML files
- `LayeredModuleProvider` to combine the source and optional overlays

Each loaded `ModuleType` stores the owning `ipcoreId`, which corresponds to the IP core id in the catalog. Type names are unique in the current registry; duplicate type names from later packages are skipped.

Editable concrete IP core packages live under `ipcores/<package>/` with `ipcraft.json`, `ipcore.yml`, `views/`, `generator/`, and `vendor/`. Qt resolves manifest-relative view and command paths against the package root, then stores that package root as `IpCatalogEntry::sourceRootPath` for generator and DRC process working directories.

Package metadata controls:

- palette naming
- module descriptions
- graph grouping
- external/display identity generation
- default ports and parameters
- port roles and bus-family metadata
- parameter descriptions and config visibility
- node color and editor layout through graphics overlays

`extensions` are schema/specgen extension descriptors such as `noc.v1`; they provide validation, defaults, and semantic mappings before the runtime manifest is consumed. `plugin`, when present, is Qt dynamic plugin metadata only. Dynamic plugins are optional editor behavior and are distinct from schema/specgen extensions.

`ModuleTypeMetadata` centralizes lookup helpers so UI and validation code can ask semantic questions such as:

- is this module a mesh router?
- does this module support collapse?
- what prefix should its external ID use?

### 6. Validation and generation

Validation is composed in `ValidationManager`. Validate and Generate both run Qt built-in validation before invoking package commands. Generate stops on built-in validation errors; Validate reports built-in diagnostics and only runs package validators for executable package instances.

Local validation:

- invalid connections
- unconnected ports for non-router modules

External validation:

- `ProjectValidationRunner` runs built-in package/project checks before external package commands
- for each project IP instance without blocking built-in diagnostics, `DRCRunner` receives that instance's `IpCatalogEntry`
- `IpCoreGraphExporter` writes instance-local project JSON to a temporary file
- runs that instance package's declared DRC command from `IpCatalogEntry::sourceRootPath`
- parses stderr into `ValidationResult` objects
- maps external IDs back to internal graph IDs when possible

The exported generator/DRC package command document uses schema `ipcraft.noc.project.v1`, includes the package id, graph name, current project instance state, module instances, and interface connections for one project IP instance. Project-level validation and generation iterate all project IP instances after built-in validation, while each exported command document remains instance-local.

Newly created `.fpproj` IP-instance records use `ipcraft.noc.instance-state.v1`; loaded `ipcore_state[].schema` values remain opaque project state and are preserved for compatibility.

IP-XACT files are optional package inputs. Even without an IP-XACT XML file, package interfaces, modes, and connection classes must be mappable to IP-XACT connection semantics. When a package declares an IP-XACT root, the strict IP-XACT sub-pass checks connection compatibility in addition to the built-in editor checks.

Generation uses `ProjectGenerationRunner` to run built-in validation once, then invoke each project IP instance's package generator. It writes one `ipcraft.noc.project.v1` JSON and generation manifest under that instance's output directory, plus a Finepaper `.fpproj` snapshot in the output root, before invoking each package command from its `IpCatalogEntry::sourceRootPath`.

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
3. `ValidationManager` passes all project IP instances and catalog entries to `ProjectValidationRunner`.
4. `ProjectValidationRunner` runs built-in package/project validation and skips package DRC only for instances with blocking built-in diagnostics.
5. `DRCRunner` uses `IpCoreGraphExporter` and each instance IP core's DRC command.
6. Results are pushed to `LogPanel`.
7. Selecting a log entry can highlight the related element in the editor.

## Operational assumptions

- Concrete IP core packages are editable directories under `ipcores/<package>/`; the bundled Finepaper NoC and RaveNoC packages use Ruby and provide `generator/bin/generate`.
- Package roots are configured through application settings for startup loading, or passed explicitly by tests and tools.
- Package-local `ipcraft.json` is the maintained Qt runtime input. Qt resolves module metadata, view XML, generator commands, and DRC commands against the package root.
- `ipcore.yml` is constrained authoring YAML for specgen. Qt does not parse it in the runtime path.
- Authored JSON module bundles are deprecated conversion inputs. IP-XACT XML is optional package metadata for strict connection checks and can also be a conversion input; it is not the preferred runtime layout.
- Reserve `plugins/` for optional Qt dynamic plugin binaries, not concrete IP core runtimes such as NoC or RaveNoC packages. Use `extensions` for schema/specgen extension metadata.
- Position is stored as module parameters such as `x` and `y`.
- Some editor-only state, such as transient selection, is intentionally omitted from graph export.

## Notes for maintainers

- Keep model mutations inside commands unless there is a strong reason not to.
- Preserve the distinction between editor project state and generator package input.
- Keep package discovery startup-only unless package reload is explicitly designed.
- Keep active workspace behavior scoped to the selected project IP instance.
- When adding new module categories or layouts, update metadata-driven checks instead of scattering type-name comparisons.
- If IP core generator output changes, update `DRCRunner::parseErrors()` and any ID-mapping assumptions together.
