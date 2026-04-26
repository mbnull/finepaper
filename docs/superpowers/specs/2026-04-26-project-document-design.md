# Finepaper Project Document Design

## Summary

Finepaper will introduce a real editor project document format instead of treating the NoC generator JSON as the primary saved file. The project document stores the graph as plugin-owned module instances, explicit port-level connections, and module parameters, so reopening the file restores the edited design state rather than rebuilding an approximate NoC topology through hard-coded `xps` and `endpoints` import logic.

The existing NoC JSON shape remains available as the framework/generator export format and as a legacy import path. It should no longer be the default editor save format.

## Goals

- Save and reopen the editor design as a project file.
- Restore module instances, stable module IDs, module types, plugin ownership, parameters, connections, and collapsed/expanded state.
- Remove project save/load dependence on hard-coded `XP` and `Endpoint` arrays.
- Keep the NoC framework JSON output available for Ruby generation.
- Keep legacy `.json` graph files importable.
- Make the project document deterministic and testable.
- Provide clear errors when a project references missing plugins, missing module types, invalid parameters, or invalid connections.

## Non-Goals

- Persisting viewport zoom, scroll/pan, current selection, dock layout, window geometry, validation logs, or undo/redo history in the first phase.
- Replacing the Ruby generator input JSON.
- Changing the NoC runtime generator schema.
- Supporting multi-file project directories.
- Supporting binary project archives.
- Implementing automatic plugin installation from a project file.
- Solving IP-XACT import/export in this project-file change.

## File Format

The project file is JSON syntax with a project-specific extension:

```text
.fpproj
```

The loader should detect a Finepaper project by `kind: finepaper-project`, not only by extension. This allows recovery when a file has the wrong extension, while the UI still defaults to `.fpproj`.

Example:

```json
{
  "schema": "v1",
  "kind": "finepaper-project",
  "project": {
    "name": "my_noc",
    "version": "1.0"
  },
  "plugins": [
    {
      "id": "finepaper.noc",
      "version": "1.0"
    }
  ],
  "graph": {
    "modules": [
      {
        "id": "node_1",
        "plugin": "finepaper.noc",
        "type": "XP",
        "parameters": {
          "external_id": "xp_0_0",
          "display_name": "XP 0 0",
          "x": 0,
          "y": 0,
          "collapsed": true,
          "routing_algorithm": "xy",
          "vc_count": 2,
          "buffer_depth": 8
        }
      },
      {
        "id": "node_2",
        "plugin": "finepaper.noc",
        "type": "Endpoint",
        "parameters": {
          "external_id": "ep_cpu0",
          "display_name": "CPU 0",
          "x": -160,
          "y": 0,
          "type": "master",
          "protocol": "axi4",
          "data_width": 64,
          "qos_enabled": false,
          "buffer_depth": 16
        }
      }
    ],
    "connections": [
      {
        "id": "conn_1",
        "source": {
          "module": "node_1",
          "port": "ep0"
        },
        "target": {
          "module": "node_2",
          "port": "noc"
        }
      }
    ]
  }
}
```

## Saved State

The first phase saves only design state:

- project name and project file schema.
- plugin IDs and versions used by the graph.
- each module's stable internal ID.
- each module's plugin ID and type name.
- every module parameter value present in the editor model.
- every graph connection with source and target `{module, port}` references.

Collapsed/expanded state is saved because it is represented by the module parameter `collapsed`. Canvas placement is saved because `x` and `y` are parameters. Display names and framework-facing IDs are saved because they are ordinary module parameters.

## Excluded State

The first phase deliberately does not save:

- selected module or connection.
- viewport center, zoom, or scrollbars.
- dock visibility or dock placement.
- arrange action checked state.
- undo/redo command history.
- validation results and log panel contents.
- last generation output directory.

These can be added later under a `workspace` object without changing the graph schema.

## Legacy JSON Policy

The existing JSON format with top-level `xps`, `endpoints`, and `connections` is legacy editor input and current NoC generator input.

Behavior:

- Open accepts `.fpproj` and legacy `.json`.
- Finepaper project files load through the new project reader.
- Legacy `.json` files load through the existing NoC import path.
- After opening a legacy `.json`, the current document path remains empty and the document is treated as an unsaved project, so Save prompts for a `.fpproj` destination.
- Generate Verilog continues to export the framework-flavored NoC JSON into the selected output directory.
- Save Project writes `.fpproj`, not generator JSON.
- Export legacy/framework JSON can remain a separate action later if users need it outside generation.

This prevents a project Save from overwriting a generator input file with a different schema.

## Architecture

Add a small project document layer under Qt:

```text
qt/inc/project/
  projectdocument.h
  projectreader.h
  projectwriter.h
  graphprojectserializer.h

qt/src/project/
  projectdocument.cpp
  projectreader.cpp
  projectwriter.cpp
  graphprojectserializer.cpp
```

Responsibilities:

- `ProjectDocument`: plain data representation of the project file.
- `ProjectReader`: parses JSON, checks `schema` and `kind`, and returns either a project document or a precise error.
- `ProjectWriter`: writes deterministic JSON from a project document.
- `GraphProjectSerializer`: converts between `Graph` and `ProjectDocument`, using `ModuleRegistry` to instantiate plugin-owned module types.

`Graph` should remain the in-memory topology model. It should not own file format detection, project schema validation, or plugin compatibility policy.

## Main Window Flow

Rename UI concepts from graph file to project file where user-facing:

- `New` creates an empty project.
- `Open` opens `.fpproj` or imports legacy `.json`.
- `Save` writes the current `.fpproj`.
- `Save As` defaults to `.fpproj`.
- `Generate` writes temporary/framework JSON into the selected output directory and invokes the plugin generator.

`m_currentDocumentPath` should represent only a project path. If a legacy JSON file is imported, `m_currentDocumentPath` remains empty and dirty tracking treats the graph as an unsaved project until the user saves it as `.fpproj`.

## Validation Rules

Project load should fail with a clear error when:

- the root is not an object.
- `schema` is not `v1`.
- `kind` is not `finepaper-project`.
- `graph.modules` or `graph.connections` is missing or has the wrong type.
- a module has no `id`, `plugin`, or `type`.
- a module ID is duplicated.
- the referenced plugin is not loaded.
- the referenced module type is not loaded from that plugin.
- a parameter is not defined by the module type in the first implementation.
- a parameter value does not match the existing parameter type.
- a connection references a missing module or port.
- a connection is rejected by `Graph::isValidConnection`.

Warnings are acceptable for plugin version mismatch when the plugin ID and module type still load. Missing plugin or missing module type is fatal because the editor cannot recreate the node accurately.

## Serialization Details

The writer should save module parameters in stable key order and connections in graph order. It should preserve module IDs rather than regenerating IDs on every save.

Connection IDs should also be preserved when present. If a project is imported from legacy JSON and no connection IDs are available, deterministic IDs can be generated during import.

The project writer should not emit framework-only fields such as the XP `endpoints` shortcut list. That list belongs to the NoC exporter.

## Existing Export Separation

The current `Graph::toJsonDocument(GraphJsonFlavor::Framework)` can remain as the generation path during the first implementation. The new project writer should not call that framework flavor.

After the project document is working, the hard-coded NoC export can be moved behind a `NocFrameworkExporter` class. That extraction is useful, but not required for the first working project file.

## Testing

Add focused Qt tests for:

- writing an empty project document.
- saving and reopening XP and Endpoint modules with all parameters restored.
- saving and reopening explicit port-level connections.
- restoring collapsed state through the `collapsed` parameter.
- preserving stable module IDs across save/load.
- rejecting duplicate module IDs.
- rejecting missing plugin or missing module type.
- rejecting invalid parameter type.
- rejecting invalid connection references.
- opening a legacy NoC JSON file through the legacy path.
- ensuring Save after legacy import targets `.fpproj` rather than overwriting the legacy JSON file.

Existing graph and generator tests should continue to cover the framework JSON export path.

## Migration Plan

Phase 1 adds project read/write data structures and tests without changing the default UI save path.

Phase 2 wires `MainWindow` Open/Save/Save As to `.fpproj` while keeping legacy JSON import.

Phase 3 updates labels, dialogs, tooltips, and docs to distinguish Project Save from NoC JSON Generate/Export.

Phase 4 optionally extracts `NocFrameworkExporter` from `Graph::toJsonDocument(GraphJsonFlavor::Framework)` so project serialization and generator export are fully separated.

## Open Decisions

The first project schema rejects unknown parameters for loaded module types. This is strict but makes schema drift visible. If plugin evolution becomes frequent, a later revision can preserve unknown parameters in a compatibility block.

The project schema does not include a `workspace` section yet. Viewport and dock state can be added later without breaking `graph.modules` and `graph.connections`.
