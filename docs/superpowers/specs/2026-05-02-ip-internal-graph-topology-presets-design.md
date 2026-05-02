# IP Internal Graph and Topology Presets Design

## Summary

Finepaper should treat each runtime plugin directory as one selectable IP core package. The current built-in `noc` package is an IP core with an editable internal graph made from XP and Endpoint nodes. The RaveNoC package should follow the same model: it is not a single large black-box module on the canvas, but an IP core whose internal NoC structure can be displayed, generated, edited, and passed to its generator.

The editor should load one active IP package at a time. The palette and canvas show only the node types that belong to the selected IP. Future system-level composition can combine multiple IP packages, but that is a separate layer above the current IP-internal editor.

This design also adds topology presets to the Qt frontend. Presets such as Mesh and Ring create ordinary graph nodes and connections inside the active IP workspace. The generated graph remains editable after creation.

## Goals

- Make the active editing unit an IP package, not a global collection of every plugin module.
- Keep `plugins/noc` as an IP package whose internal graph contains XP and Endpoint nodes.
- Change `plugins/ravenoc` from a single `RaveNoC` block into a RaveNoC IP package with editable internal node types.
- Add Qt controls for creating topology presets such as Mesh and Ring.
- Keep generated preset content as normal graph data so users can edit, delete, reconnect, and configure nodes.
- Keep future multi-IP composition outside this phase.

## Non-Goals

- Do not build a system-level graph that connects multiple IP packages in this phase.
- Do not load every plugin module into one shared palette.
- Do not model RaveNoC as a single opaque node in the internal editor.
- Do not replace the existing Ruby/ERB RaveNoC RTL generator in this phase.
- Do not require the Ring preset to be enabled for an IP whose generator cannot yet emit ring RTL.

## Current Problem

The existing Qt graph already supports editable nodes, ports, interface metadata, and compatibility checks. The built-in NoC flow benefits from that model because XP and Endpoint are separate node types with explicit `ni_link` and `router_link` connections.

RaveNoC currently exposes one module named `RaveNoC` with external AXI, IRQ, and clock/reset interfaces. That is a valid black-box integration shape, but it does not match the intended NoC editing experience. Users should be able to see and edit the RaveNoC topology itself, similarly to the current XP/Endpoint workflow.

Another issue is selection scope. As more IP packages arrive, the editor should not present every module from every package at once. The user should first choose the IP package, then edit that IP's internal graph.

## Architecture

### IP Package

An IP package is the unit represented by one runtime plugin directory:

- `plugins/noc`
- `plugins/ravenoc`
- future IP packages

Each package owns:

- runtime manifest metadata
- internal module definitions
- internal view definitions
- generator command and input format
- optional topology presets

The existing `plugin.json` remains the runtime manifest consumed by Qt, but the conceptual model is IP package first. The term plugin remains an implementation detail for loading packages from disk.

### Active IP Workspace

Qt should maintain one active IP package for the current graph editor session.

When the active package is `noc`, the palette shows only NoC internal node types such as XP and Endpoint.

When the active package is `ravenoc`, the palette shows only RaveNoC internal node types, for example RaveNoC router/tile and RaveNoC endpoint/attachment nodes.

Switching the active IP package is a workspace-level operation. It should not silently mix node types from different packages into the same internal graph.

### Internal Graph

Each IP package exposes an internal graph schema. The graph stores ordinary modules, ports, interfaces, parameters, and connections. Existing graph serialization should remain the base representation.

The built-in NoC package already has this shape:

- XP nodes
- Endpoint nodes
- `router_link` connections between routers
- `ni_link` connections between endpoints and routers

RaveNoC should gain an equivalent internal graph shape. The first implementation should avoid over-modeling upstream RTL internals, but it must expose enough structure for topology editing:

- a RaveNoC router/tile/node type
- a RaveNoC endpoint or AXI attachment type
- router-to-router link interfaces
- endpoint-to-router attachment interfaces
- per-node or per-endpoint parameters needed by the generator

The previous single `RaveNoC` module can remain as a generated integration artifact if needed by the RTL generator, but it should not be the primary editable node shown in the internal IP editor.

## Topology Presets

Topology presets are declared per IP package and rendered by Qt as toolbar actions.

The first preset actions are:

- Mesh
- Ring

Clicking a preset opens a small parameter dialog:

- Mesh: rows, columns
- Ring: node count

After confirmation, Qt creates normal graph modules and connections according to the active IP package's preset rules. The result is not locked. Users can continue editing node parameters and connections.

An IP package can expose only the presets it supports. If RaveNoC generation only supports mesh initially, the RaveNoC package should expose Mesh first and leave Ring disabled or absent until generator support exists.

## Preset Spec Shape

The source spec should describe presets close to the IP package, not hard-code topology logic only in Qt.

A future-compatible shape can look like this:

```yaml
topology_presets:
  mesh:
    label: Mesh
    parameters:
      rows: { type: int, default: 2, min: 1, max: 16 }
      cols: { type: int, default: 2, min: 1, max: 16 }
    nodes:
      router:
        module: RaveTile
        id_pattern: tile_{row}_{col}
    links:
      east_west:
        from: { node: router, port: east }
        to: { node: router, port: west }
      north_south:
        from: { node: router, port: south }
        to: { node: router, port: north }

  ring:
    label: Ring
    parameters:
      nodes: { type: int, default: 4, min: 2, max: 64 }
    nodes:
      router:
        module: RaveTile
        id_pattern: tile_{index}
    links:
      next:
        from: { node: router, port: east }
        to: { node: router, port: west }
```

The exact schema can be refined during implementation, but the important boundary is that Qt reads preset intent from the active IP package and performs graph creation through shared graph APIs.

## Qt UX

The Qt frontend should add an active IP selector and topology preset controls.

Minimal first version:

- an active IP selector in the toolbar or project setup area
- a topology preset button or menu in the node editor toolbar
- preset actions filtered by active IP package
- a parameter dialog for Mesh/Ring dimensions
- graph creation through undoable commands where practical

The palette should reflect the active IP package. Users should not see RaveNoC internal nodes while editing the built-in NoC IP, and vice versa.

## Generator Flow

Generators should consume the active IP internal graph.

For the built-in NoC package, this preserves the existing XP/Endpoint graph flow.

For RaveNoC, the generator should derive RaveNoC configuration and wrapper artifacts from the internal graph. A mesh preset graph can be converted into rows, columns, tile coordinates, endpoint attachments, and RaveNoC parameters. This keeps the user-facing graph editable while still producing the upstream RaveNoC integration artifacts.

## Migration

This should be implemented incrementally:

1. Add active IP package selection without changing existing NoC behavior.
2. Add topology preset data structures and Qt Mesh/Ring creation for the existing NoC package.
3. Convert RaveNoC metadata from one `RaveNoC` module to internal editable node types.
4. Teach the RaveNoC generator to consume the internal graph shape.
5. Enable RaveNoC Mesh preset. Enable Ring only when generator semantics are defined.

Existing project files should continue loading where possible. A project containing a single black-box `RaveNoC` module can either remain valid as a legacy graph or be migrated by a dedicated importer later. This phase does not need automatic black-box-to-internal migration.

## Validation

Qt graph validation should continue to enforce:

- no self-loop connections
- port direction compatibility
- bus family compatibility
- interface role compatibility
- match-field compatibility
- single-use occupancy for ports that should only connect once

Preset generation should additionally validate:

- requested dimensions are within spec limits
- generated node IDs are unique
- required module and port names exist in the active IP package
- every generated connection passes normal graph validation

## Testing

Add tests for:

- active IP package filtering of module types
- Mesh preset graph creation for the built-in NoC package
- Ring preset graph creation where supported
- generated preset connections passing existing graph validation
- RaveNoC internal node metadata loading from spec-generated runtime files
- RaveNoC generator accepting the new internal graph input shape

Existing regression tests should continue to run:

- `ruby spec_generator/test/spec_generator_test.rb`
- `ruby plugins/noc/generator/test/test_generator.rb`
- `ruby plugins/ravenoc/generator/test/test_generator.rb`
- `ruby plugins/ravenoc/generator/test/test_smoke.rb`
- Qt `graph_test`
- Qt `plugin_test`
- Qt `projectdocument_test`

