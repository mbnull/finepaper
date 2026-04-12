# Finepaper Qt Editor

This project is a Qt Widgets application for building and validating SoC/NoC topologies with a node-editor UI. It uses a central `Graph` model, an undoable command layer, and a QtNodes-based canvas to keep the visual editor synchronized with the underlying design data.

## What the application does

- Shows available module types in a palette loaded from a JSON bundle plus XML presentation metadata.
- Lets users drag modules onto a canvas and connect compatible ports.
- Exposes module parameters in a property panel.
- Saves editor state as JSON.
- Exports framework-oriented JSON and invokes the external Ruby generator to produce Verilog.
- Runs local validation plus framework-backed DRC checks and shows findings in the log panel.

## Repository layout

- `src/`: application implementation.
- `inc/`: public headers for the application classes.
- `src/commands/`, `inc/commands/`: undoable editing commands.
- `test/`: lightweight executable tests for the graph model and command manager.
- `bundles/modules.json`: local runtime/default module bundle used when no external bundle is found.
- `bundles/modules.ui.xml`: local editor presentation bundle for graphics and config-zone layout.
- `deps/packages.lua`: xmake package declarations.
- `docs/`: older working notes and reference material.
- `doc/`: maintained project documentation.

## Main runtime pieces

- `MainWindow`: owns the top-level panels, actions, and user workflows.
- `Graph`: source of truth for modules, connections, and parameter changes.
- `CommandManager`: executes undoable commands and manages undo/redo stacks.
- `NodeEditorWidget`: bridges `Graph` to QtNodes and translates UI actions into commands.
- `Palette`: lists available module types for drag-and-drop creation.
- `PropertyPanel`: auto-builds editors from module parameter types.
- `ValidationManager`: runs built-in validation and external DRC checks.
- `LogPanel`: shows validation, generation, and runtime messages.
- `ModuleRegistry`: loads module definitions from JSON bundles and overlays editor metadata from XML.

## Build and run

The project uses `xmake` and C++20.

```bash
xmake
xmake run qt
```

The app depends on the `nodeeditor` package declared in `xmake.lua`.

## Tests

Two test executables are declared in `xmake.lua`.

```bash
xmake build graph_test
xmake run graph_test

xmake build commandmanager_test
xmake run commandmanager_test
```

`graph_test` covers graph ownership, connection validation, parameter change forwarding, and JSON export behavior. `commandmanager_test` covers execute, undo, redo, and redo-stack invalidation.

## External framework integration

Generation and DRC validation depend on an external framework repository that contains:

- `bin/generate`
- `template/`

Framework discovery is handled by `FrameworkPaths` and works in this order:

1. `FRAMEWORK_PATH`
2. A `framework/` directory in or above the current working directory
3. A `framework/` directory near the application binary

Bundle discovery for module definitions works in this order:

1. `BUNDLE_PATH`
2. Framework bundle locations such as `framework/bundles/modules.json`
3. Repository-local bundle paths such as `bundles/modules.json`

Editor presentation metadata discovery works in this order:

1. `BUNDLE_UI_PATH`
2. Sidecar XML near `BUNDLE_PATH` when `BUNDLE_PATH` is set
3. Framework bundle locations such as `framework/bundles/modules.ui.xml` when `BUNDLE_PATH` is not set
4. Repository-local bundle paths such as `bundles/modules.ui.xml` when `BUNDLE_PATH` is not set

If the framework is missing, the editor can still start, but Verilog generation and external DRC validation will fail with user-visible messages.

## Typical user flow

1. Start the application.
2. Drag module types from the palette onto the canvas.
3. Connect output ports to input ports.
4. Select a module and edit parameters in the property panel.
5. Run validation to collect built-in and framework DRC findings.
6. Save editor JSON or generate Verilog into a chosen output directory.

## Generated and saved data

- `saveGraph()` writes editor JSON through `Graph::saveToJson()`.
- `generateVerilog()` writes framework-flavored JSON to the selected output directory and then runs `ruby bin/generate`.
- Application logs are written to the platform-local app data directory as `finepaper.log`.

## Module bundle format

Runtime module types are loaded from JSON and currently include metadata for:

- palette label
- graph grouping
- identity prefixes and numbering width
- default ports
- default parameters

Editor presentation metadata is loaded from XML and currently includes:

- node color
- editor layout / graphics profile
- collapse behavior
- node sizing and caption insets
- config-zone field order and labels

The local bundle defines two module types today:

- `XP`: mesh-router style node with router and endpoint ports
- `Endpoint`: endpoint node with configurable interface parameters

## Extension points

- Add new module types by extending `modules.json` and `modules.ui.xml` together.
- Add new validation rules in `BasicValidator` or extend `DRCRunner` parsing if the external framework output changes.
- Add new editing operations by implementing `Command` subclasses in `src/commands/`.

For a component-level view, see [architecture.md](/home/bnl/dev/finepaper/qt/doc/architecture.md).
