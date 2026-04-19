# Finepaper Qt Editor

This project is a Qt Widgets application for building and validating SoC/NoC topologies with a node-editor UI. It uses a central `Graph` model, an undoable command layer, and a QtNodes-based canvas to keep the visual editor synchronized with the underlying design data.

## What the application does

- Shows available module types in a palette loaded from XML IP-core bundles and per-IP graphics overlays.
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
- `../framework/src/ruby/model/modules/`: canonical hand-edited module catalog used by the framework and the editor.
- `../framework/template/module_*.erb`, `../framework/template/ipxact_component.xml.erb`: ERB templates used to generate frontend and IP-XACT views of the catalog.
- `../framework/bundles/`: generated frontend bundle consumed by the editor at runtime.
- `../framework/ipxact/`: generated IP-XACT component XML emitted from the same catalog.
- `deps/packages.lua`: xmake package declarations.
- `tools/convert_module_bundle.py`: converts authored JSON, module-bundle XML, or IP-XACT into the split XML bundle format.
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
- `ModuleRegistry`: loads module definitions from the framework-generated `modules.xml`, applies per-IP graphics XML files, and can still read older split presentation overlays when needed.

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
2. Framework bundle locations such as `framework/bundles/modules.xml`
3. Framework bundle locations such as `framework/bundles/modules.json`
4. Repository-local bundle paths such as `bundles/modules.xml`
5. Repository-local bundle paths such as `bundles/modules.json`

Graphics overlay discovery works in this order:

1. `BUNDLE_GRAPHICS_PATH`
2. Graphics directories near `BUNDLE_PATH` such as `graphics/`
3. Framework bundle locations such as `framework/bundles/graphics/`
4. Repository-local bundle paths such as `bundles/graphics/`

Legacy presentation XML discovery still uses `BUNDLE_UI_PATH` and `modules.ui.xml` when an older split bundle is being loaded.

If the framework is missing, the editor can still start, but Verilog generation and external DRC validation will fail with user-visible messages.

The current source-of-truth flow is:

1. Edit the JSON files under `framework/src/ruby/model/modules/`.
2. Run `ruby framework/bin/export_modules`.
3. The editor consumes `framework/bundles/modules.xml` plus `framework/bundles/graphics/*.xml`.
4. Secondary exchange artifacts are emitted into `framework/ipxact/*.component.xml`.

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

The preferred runtime format is split into:

- `modules.xml` for the IP-core definition
- `graphics/<type>.xml` for the editor graphics of each IP

That runtime bundle is generated from the framework catalog in `framework/src/ruby/model/modules/`. The editor should treat the generated bundle as read-only.

The IP-core bundle can describe:

- palette label and module description
- graph grouping
- identity prefixes and numbering width
- default ports plus port descriptions, roles, and bus-family metadata
- default parameters plus labels, descriptions, and configurable visibility
- config-zone field order and labels when custom ordering is needed

Each graphics overlay can describe:

- node color
- editor layout / graphics profile
- collapse behavior
- node sizing and caption insets

The generated bundle defines two module types today:

- `XP`: mesh-router style node with router and endpoint ports
- `Endpoint`: endpoint node with configurable interface parameters

If a module has no graphics overlay, the editor falls back to a simple node layout and infers port placement hints from each port description.

## Extension points

- Add or update module types by editing `framework/src/ruby/model/modules/`, then regenerate with `ruby framework/bin/export_modules`.
- Add new validation rules in `BasicValidator` or extend `DRCRunner` parsing if the external framework output changes.
- Add new editing operations by implementing `Command` subclasses in `src/commands/`.

## Converter

To convert authored JSON into the split XML IP-core bundle format:

```bash
python3 tools/convert_module_bundle.py \
  --json path/to/modules.json \
  --ui path/to/modules.ui.xml \
  --output-dir path/to/output_bundle
```

To convert IP-XACT into the same split format:

```bash
python3 tools/convert_module_bundle.py \
  --ipxact path/to/component.xml \
  --output-dir path/to/output_bundle
```

To split an existing `module-bundle` XML file into `modules.xml` plus per-IP graphics files:

```bash
python3 tools/convert_module_bundle.py \
  --xml path/to/modules.xml \
  --output-dir path/to/output_bundle
```

To regenerate the checked-in frontend bundle and the secondary IP-XACT export from the framework catalog:

```bash
ruby ../framework/bin/export_modules
```

For a component-level view, see [architecture.md](/home/bnl/dev/finepaper/qt/doc/architecture.md).
