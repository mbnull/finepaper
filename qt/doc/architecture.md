# Architecture

## Design summary

The application follows a simple layered structure:

- Model: `Graph`, `Module`, `Connection`, `Port`, `Parameter`
- Command layer: undoable mutations wrapped in `Command` subclasses
- UI layer: Qt Widgets and QtNodes views driven by model signals
- Validation/integration layer: local validators plus the external framework runner

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

`ModuleRegistry` loads `ModuleType` entries from a `ModuleProvider`, currently `BundleProvider`.

Bundle metadata controls:

- palette naming
- node color
- editor layout specialization
- graph grouping
- external/display identity generation
- collapse support
- default ports and parameters

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

- `DRCRunner` exports framework JSON to a temporary file
- runs `ruby bin/generate`
- parses stderr into `ValidationResult` objects
- maps external IDs back to internal graph IDs when possible

Generation reuses the same framework discovery path and writes framework JSON into the chosen output directory before invoking the Ruby generator.

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

- The external framework uses Ruby and provides `bin/generate`.
- Module bundles are split between JSON runtime/default metadata and XML editor presentation metadata.
- Position is stored as module parameters such as `x` and `y`.
- Some editor-only state, such as `collapsed`, is intentionally omitted from framework export.

## Notes for maintainers

- Keep model mutations inside commands unless there is a strong reason not to.
- Preserve the distinction between editor JSON and framework JSON.
- When adding new module categories or layouts, update metadata-driven checks instead of scattering type-name comparisons.
- If framework output changes, update `DRCRunner::parseErrors()` and any ID-mapping assumptions together.
