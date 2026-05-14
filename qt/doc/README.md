# Finepaper Qt Editor

This project is a Qt Widgets application for building and validating SoC/NoC topologies with a node-editor UI. It uses a central `Graph` model, an undoable command layer, and a QtNodes-based canvas to keep the visual editor synchronized with the underlying design data.

## What the application does

- Shows discovered IP cores in the IP Catalog, lets users add/select project IP instances, and exposes the active workspace's module types.
- Lets users drag active workspace modules onto a canvas, use canvas creation menus, and connect compatible ports.
- Exposes module parameters in a property panel.
- Saves editor state as a `.fpproj` project.
- Exports `ipcraft.noc.project.v1` JSON and invokes the active IP core generator to produce Verilog.
- Runs local validation plus active IP core-backed DRC checks and shows findings in the log panel.

## Repository layout

- `src/`: application implementation.
- `inc/`: public headers for the application classes.
- `src/commands/`, `inc/commands/`: undoable editing commands.
- `test/`: lightweight executable tests for the graph model and command manager.
- `../ipcores/<package>/`: editable concrete IP core packages with constrained authoring `ipcore.yml`, package-local runtime `ipcraft.json`, `views/`, `generator/`, and `vendor/`.
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
- `IpCatalogService`: projects startup-loaded Ipcraft package manifests into selectable IP catalog entries.
- `ActiveWorkspaceController`: exposes the selected project IP instance and its available module types/topology presets.
- `PropertyPanel`: auto-builds editors from module parameter types.
- `ValidationManager`: runs built-in validation and external DRC checks.
- `LogPanel`: shows validation, generation, and runtime messages.
- `IpCoreRuntimeRegistry`: compatibility-only descriptor registry retained for older descriptor-facing code paths.
- `ModuleRegistry`: loads module definitions from package-local `ipcraft.json` manifests and applies package view XML files.
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

`graph_test` covers graph ownership, connection validation, parameter change forwarding, package-backed loading, and JSON export behavior. `commandmanager_test` covers execute, undo, redo, and redo-stack invalidation. `validation_test` covers local topology validation. `uiscale_test` covers UI scaling helpers. `ipcoreruntime_test` covers compatibility registry behavior, package-owned module loading, duplicate type handling, and generator argument substitution.

## IP Core Package Integration

Generation and DRC validation are provided by the active IP core selected in the IP Catalog workspace. Package metadata is loaded once at startup; package installation, unloading, and refresh are not supported.

The package boundary is split intentionally:

- `ipcore.yml` is constrained authoring YAML for package developers. It uses explicit schema names and stable IDs, and rejects ambiguous YAML features such as duplicate keys, aliases, anchors, merge keys, custom tags, multi-document streams, and unknown fields outside documented extension namespaces.
- `ipcraft.json` is the normalized runtime manifest. Qt loads this file, referenced view XML, and declared commands from the package root.
- Qt does not parse authoring YAML during normal runtime loading.

Package root discovery is intentionally explicit:

- The application reads IP-core package roots persisted in `AppSettings`.
- Tests and tools may call `loadIpcraftPackageManifests({explicitRoot})` with explicit package-root search paths.

Default startup discovery does not walk upward from the current working directory or application directory. A package root is a directory that contains a package-local `ipcraft.json`; for example, `../ipcores/finepaper-noc/` and `../ipcores/ravenoc/`.

The package manifest schema is:

```json
{
  "schema": "ipcraft.manifest.v1",
  "id": "finepaper.noc",
  "name": "NoC",
  "version": "1.0",
  "extensions": {
    "noc.v1": {
      "enabled": true
    }
  },
  "parameters": {},
  "connection_classes": [],
  "modules": [
    {
      "id": "XP",
      "name": "XP",
      "graph_role": "host",
      "parameters": {},
      "interfaces": []
    }
  ],
  "views": [
    {
      "module": "XP",
      "file": "views/XP.xml"
    }
  ],
  "commands": {
    "generate": {
      "executable": "generator/bin/generate",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["-i", "{input}", "-o", "{output}"]
    },
    "validate": {
      "executable": "generator/bin/drc",
      "input_schema": "ipcraft.noc.project.v1",
      "args": ["-i", "{input}"]
    }
  }
}
```

Manifest-relative paths are resolved against the package root. `views/<type>.xml` files provide editor graphics, and generator/DRC commands run from the same package root so executable paths such as `generator/bin/generate` and `generator/bin/drc` resolve inside `ipcores/<package>/`. `{input}` and `{output}` are replaced with the exported `ipcraft.noc.project.v1` JSON path and selected output directory.

Each command declaration must provide `input_schema`. Validate and Generate run Qt built-in validation first; package `validate` and `generate` commands run only after the editor-owned package, project, graph, connection, and command checks pass for the requested operation.

`extensions` are schema/specgen extension descriptors, for example `noc.v1`. They are used to validate and normalize package semantics. `plugin`, when present, means Qt dynamic plugin metadata. A Qt dynamic plugin is optional future editor behavior; it is separate from schema/specgen extensions and is not required for baseline package loading.

An IP-XACT XML file is optional. Package semantics are still required to map to IP-XACT connection concepts: modules map to components, interfaces to `busInterface`, project instances to component instances, and project connections to interconnections with active interfaces. When a package includes an IP-XACT root, Qt can run the strict IP-XACT connection sub-pass in addition to its built-in validation.

`IpCoreGraphExporter` serializes only the active workspace's selected IP instance. Generation and external DRC fail with a user-visible message if a module or connection references a different IP core than the selected active workspace.

New `.fpproj` IP-instance records use the public state schema `ipcraft.noc.instance-state.v1`. The project reader preserves loaded `ipcore_state[].schema` values so legacy or package-owned state records can still round-trip.

The `plugins/` directory name is reserved for optional Qt dynamic plugin binaries, not concrete IP core packages. Concrete NoC and RaveNoC IP packages should be described as package roots under `ipcores/`.

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
- `generateVerilog()` writes `ipcraft.noc.project.v1` JSON through `IpCoreGraphExporter` and a `.fpproj` snapshot to the selected output directory, then runs the active IP core generator from `IpCatalogEntry::sourceRootPath`.
- Application logs are written to the platform-local app data directory as `finepaper.log`.

## IP Core Package Format

The runtime input is the package-local `ipcraft.json` manifest in `ipcores/<package>/`. It is generated from the constrained `ipcore.yml` authoring source and describes:

- package identity, display name, and version
- module palette label and description
- graph grouping
- identity prefixes and numbering width
- default ports plus port descriptions, roles, and bus-family metadata
- default parameters plus labels, descriptions, and configurable visibility
- config-zone field order and labels when custom ordering is needed
- generator and DRC commands
- NoC topology presets
- optional IP-XACT root metadata

Each package view XML file can describe:

- node color
- editor layout / graphics profile
- collapse behavior
- node sizing and caption insets

The bundled Finepaper NoC IP core defines two module types today:

- `XP`: mesh-router style node with router and endpoint ports
- `Endpoint`: endpoint node with configurable interface parameters

If a module has no graphics overlay, the editor falls back to a simple node layout and infers port placement hints from each port description.

## Extension points

- Add new module types by editing the source package `ipcore.yml`, optionally adding `views/<type>.xml`, and rebuilding `ipcraft.json` with specgen.
- Add new concrete IP support by creating an `ipcores/<package>/` package root with constrained `ipcore.yml`, generated `ipcraft.json`, `views/`, `generator/`, and `vendor/`.
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

To split an existing `module-bundle` XML file into package metadata plus per-IP graphics files:

```bash
python3 tools/convert_module_bundle.py \
  --xml path/to/module-bundle.xml \
  --output-dir path/to/output_bundle
```

For a component-level view, see [architecture.md](architecture.md).
