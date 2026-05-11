# Finepaper Qt Editor

This project is a Qt Widgets application for building and validating SoC/NoC topologies with a node-editor UI. It uses a central `Graph` model, an undoable command layer, and a QtNodes-based canvas to keep the visual editor synchronized with the underlying design data.

## What the application does

- Shows discovered IP cores in the IP Catalog, lets users add/select project IP instances, and exposes the active workspace's module types.
- Lets users drag active workspace modules onto a canvas, use canvas creation menus, and connect compatible ports.
- Exposes module parameters in a property panel.
- Saves editor state as a `.fpproj` project.
- Exports `finepaper-ipcore-graph-v1` JSON and invokes the active IP core generator to produce Verilog.
- Runs local validation plus active IP core-backed DRC checks and shows findings in the log panel.

## Repository layout

- `src/`: application implementation.
- `inc/`: public headers for the application classes.
- `src/commands/`, `inc/commands/`: undoable editing commands.
- `test/`: lightweight executable tests for the graph model and command manager.
- `../ipcores/<package>/`: editable concrete IP core packages with `ipcore.yml`, `views/`, `generator/`, and `vendor/`.
- `../generated/ipcores/<ipcore-id>/`: committed runtime metadata generated from IP core packages.
- `deps/packages.lua`: xmake package declarations.
- `tools/convert_module_bundle.py`: converts deprecated authored JSON module bundles, module-bundle XML, or IP-XACT into the split XML bundle format.
- `docs/`: older working notes and reference material.
- `doc/`: maintained project documentation.

## Main runtime pieces

- `MainWindow`: owns the top-level panels, actions, and user workflows.
- `Graph`: source of truth for modules, connections, and parameter changes.
- `CommandManager`: executes undoable commands and manages undo/redo stacks.
- `NodeEditorWidget`: bridges `Graph` to QtNodes and translates UI actions into commands.
- `IpCatalogPanel`: lists discovered IP cores, project IP instances, active workspace modules, and workspace tools.
- `IpCatalogService`: projects startup-discovered runtime manifests into selectable IP catalog entries.
- `ActiveWorkspaceController`: exposes the selected project IP instance and its available module types/topology presets.
- `PropertyPanel`: auto-builds editors from module parameter types.
- `ValidationManager`: runs built-in validation and external DRC checks.
- `LogPanel`: shows validation, generation, and runtime messages.
- `IpCoreRuntimeRegistry`: discovers startup-loaded runtime manifests from `FINEPAPER_IPCORE_PATH` and repository-local generated IP core bundles.
- `ModuleRegistry`: loads module definitions from runtime manifests and applies per-IP graphics XML files.
- `IpCoreGraphExporter`: serializes the active IP core graph as the generator/DRC handoff format.

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

xmake build ipcoreruntime_test
xmake run ipcoreruntime_test
```

`graph_test` covers graph ownership, connection validation, parameter change forwarding, bundle loading, and JSON export behavior. `commandmanager_test` covers execute, undo, redo, and redo-stack invalidation. `validation_test` covers local topology validation. `uiscale_test` covers UI scaling helpers. `ipcoreruntime_test` covers runtime manifest discovery, bundle-owned module loading, duplicate type handling, and generator argument substitution.

## IP Core Runtime Integration

Generation and DRC validation are provided by the active IP core selected in the IP Catalog workspace. Runtime metadata is loaded once at startup; runtime installation, unloading, and refresh are not supported.

Runtime bundle discovery works in this order:

1. Directories listed in `FINEPAPER_IPCORE_PATH`, using the platform path-list separator.
2. Repository-local `generated/ipcores/` directories found from the current working directory or application directory.

Each runtime bundle is a generated directory containing `ipcore-runtime.json`. Editable concrete IP core packages live under `../ipcores/<package>/`; for example, `../ipcores/finepaper-noc/` and `../ipcores/ravenoc/`. The committed generated bundles live under `../generated/ipcores/finepaper.noc/` and `../generated/ipcores/finepaper.ravenoc/` and declare:

- `ipcore-runtime.json`
- `modules.xml`
- `graphics/`

The first manifest schema is:

```json
{
  "id": "finepaper.noc",
  "name": "NoC",
  "version": "1.0",
  "source_root": "../../../ipcores/finepaper-noc",
  "kind": "noc",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "input_format": "ipcore_graph_v1",
    "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}", "-t", "generator/template"]
  },
  "drc": {
    "command": "ruby",
    "input_format": "ipcore_graph_v1",
    "args": ["generator/bin/drc", "-i", "{input}", "-o", "{output}"]
  },
}
```

`modules` and `graphics` paths are resolved against the generated runtime bundle directory. `source_root` points back to the editable IP core package and becomes `IpCatalogEntry::sourceRootPath`; generator/DRC commands run from that source root so paths such as `generator/bin/generate` and `generator/template` resolve against `ipcores/<package>/`. `{input}` and `{output}` are replaced with the exported `finepaper-ipcore-graph-v1` JSON path and selected output directory.

`IpCoreGraphExporter` serializes only the active workspace's selected IP instance. Generation and external DRC fail with a user-visible message if a module or connection references a different IP core than the selected active workspace.

Use `plugins/` wording only for feature plugins or editor behavior extensions. Concrete NoC and RaveNoC IP packages should be described as IP core packages under `ipcores/` with generated runtime bundles under `generated/ipcores/`.

## Typical user flow

1. Start the application.
2. Add or select an IP core instance in the IP Catalog.
3. Drag module types from Workspace Modules, or use the canvas creation menu, to add modules for the active workspace.
4. Connect output ports to input ports.
5. Select a module or IP instance and edit parameters in the property panel.
6. Run validation to collect built-in and active IP core DRC findings.
7. Save a Finepaper project or generate Verilog into a chosen output directory.

## Generated and saved data

- `saveGraph()` writes a `.fpproj` through `ProjectWriter`.
- `generateVerilog()` writes `finepaper-ipcore-graph-v1` JSON through `IpCoreGraphExporter` and a `.fpproj` snapshot to the selected output directory, then runs the active IP core generator from `IpCatalogEntry::sourceRootPath`.
- Application logs are written to the platform-local app data directory as `finepaper.log`.

## Module bundle format

The preferred runtime format is generated from `ipcores/<package>/ipcore.yml` and split into:

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

The bundled Finepaper NoC IP core defines two module types today:

- `XP`: mesh-router style node with router and endpoint ports
- `Endpoint`: endpoint node with configurable interface parameters

If a module has no graphics overlay, the editor falls back to a simple node layout and infers port placement hints from each port description.

## Extension points

- Add new module types by editing the source package `ipcore.yml` and optionally adding `views/<type>.xml`, then regenerating `generated/ipcores/<ipcore-id>/`.
- Add new concrete IP support by creating an `ipcores/<package>/` source package with `ipcore.yml`, `views/`, `generator/`, and `vendor/`, then generating a runtime bundle under `generated/ipcores/<ipcore-id>/`.
- Add new validation rules in `BasicValidator` or extend `DRCRunner` parsing if IP core generator output changes.
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
