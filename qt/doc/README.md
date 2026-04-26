# Finepaper Qt Editor

This project is a Qt Widgets application for building and validating SoC/NoC topologies with a node-editor UI. It uses a central `Graph` model, an undoable command layer, and a QtNodes-based canvas to keep the visual editor synchronized with the underlying design data.

## What the application does

- Shows available module types in a palette loaded from startup-discovered IP plugins.
- Lets users drag modules onto a canvas and connect compatible ports.
- Exposes module parameters in a property panel.
- Saves editor state as JSON.
- Exports framework-oriented JSON and invokes the active plugin generator to produce Verilog.
- Runs local validation plus plugin-backed DRC checks and shows findings in the log panel.

## Repository layout

- `src/`: application implementation.
- `inc/`: public headers for the application classes.
- `src/commands/`, `inc/commands/`: undoable editing commands.
- `test/`: lightweight executable tests for the graph model and command manager.
- `../plugins/noc/`: bundled NoC plugin with module definitions, graphics, and the Ruby sample generator.
- `bundles/modules.xml`: legacy local IP-core bundle fallback with ports, parameters, descriptions, and config metadata.
- `bundles/graphics/*.xml`: per-IP graphics overlays used by the editor.
- `deps/packages.lua`: xmake package declarations.
- `tools/convert_module_bundle.py`: converts deprecated authored JSON module bundles, module-bundle XML, or IP-XACT into the split XML bundle format.
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
- `PluginRegistry`: discovers startup-loaded IP plugins from `FINEPAPER_PLUGIN_PATH` and repository-local `plugins/`.
- `ModuleRegistry`: loads module definitions from plugin manifests, applies per-IP graphics XML files, and can still read older split presentation overlays when needed.

## Build and run

The project uses `xmake` and C++23.

```bash
xmake
xmake run qt
```

The app depends on the `nodeeditor` package declared in `xmake.lua`.

## Tests

Five test executables are declared in `xmake.lua`.

```bash
xmake build graph_test
xmake run graph_test

xmake build commandmanager_test
xmake run commandmanager_test

xmake build validation_test
xmake run validation_test

xmake build uiscale_test
xmake run uiscale_test

xmake build plugin_test
xmake run plugin_test
```

`graph_test` covers graph ownership, connection validation, parameter change forwarding, bundle loading, and JSON export behavior. `commandmanager_test` covers execute, undo, redo, and redo-stack invalidation. `validation_test` covers local topology validation. `uiscale_test` covers UI scaling helpers. `plugin_test` covers plugin manifest discovery, plugin-owned module loading, duplicate type handling, and generator argument substitution.

## Plugin integration

Generation and DRC validation are provided by the plugin that owns the modules in the current graph. Plugins are loaded once at startup; runtime installation, unloading, and refresh are not supported.

Plugin discovery works in this order:

1. Directories listed in `FINEPAPER_PLUGIN_PATH`, using the platform path-list separator.
2. A repository-local `plugins/` directory found from the current working directory or application directory.
3. Legacy bundle fallback paths when no plugin module definitions are available.

Each plugin is a directory containing `plugin.json`. The bundled NoC plugin lives at `../plugins/noc/` and declares:

- `modules.xml`
- `graphics/`
- `generator/bin/generate`
- `generator/template/`

The first manifest schema is:

```json
{
  "id": "finepaper.noc",
  "name": "NoC",
  "version": "1.0",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}", "-t", "generator/template"]
  },
  "native": {
    "enabled": false,
    "library": ""
  }
}
```

Manifest paths are resolved relative to the plugin directory. `{input}` and `{output}` are replaced with the exported design JSON path and selected output directory. Native plugin metadata is retained for future C++ dynamic-library support, but native libraries are not loaded in this version.

Legacy presentation XML discovery still uses `BUNDLE_UI_PATH` and `modules.ui.xml` when an older split bundle is being loaded.

If a graph contains modules from more than one plugin, generation and external DRC currently fail with a user-visible message because multi-plugin orchestration is not implemented yet.

## Typical user flow

1. Start the application.
2. Drag module types from the palette onto the canvas.
3. Connect output ports to input ports.
4. Select a module and edit parameters in the property panel.
5. Run validation to collect built-in and plugin DRC findings.
6. Save editor JSON or generate Verilog into a chosen output directory.

## Generated and saved data

- `saveGraph()` writes editor JSON through `Graph::saveToJson()`.
- `generateVerilog()` writes framework-flavored JSON to the selected output directory and then runs the active plugin generator.
- Application logs are written to the platform-local app data directory as `finepaper.log`.

## Module bundle format

The preferred runtime format is split into:

- `modules.xml` for the IP-core definition
- `graphics/<type>.xml` for the editor graphics of each IP

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

The bundled NoC plugin defines two module types today:

- `XP`: mesh-router style node with router and endpoint ports
- `Endpoint`: endpoint node with configurable interface parameters

If a module has no graphics overlay, the editor falls back to a simple node layout and infers port placement hints from each port description.

## Extension points

- Add new module types by extending `modules.xml` and optionally adding `graphics/<type>.xml`.
- Add new IP support by creating a plugin directory with `plugin.json`, `modules.xml`, optional `graphics/<type>.xml`, and an optional generator command.
- Add new validation rules in `BasicValidator` or extend `DRCRunner` parsing if plugin generator output changes.
- Add new editing operations by implementing `Command` subclasses in `src/commands/`.

## Converter

To convert a deprecated authored JSON module bundle into the split XML IP-core bundle format:

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

For a component-level view, see [architecture.md](/home/bnl/dev/finepaper/qt/doc/architecture.md).
