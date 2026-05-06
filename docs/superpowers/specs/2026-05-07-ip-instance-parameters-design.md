# IP Instance Parameters Design

## Summary

Finepaper should support parameters that belong to an IP instance instead of to an internal graph node. A single canvas is the project workspace. The project can contain multiple IP instances, and each IP instance can own configuration values such as RaveNoC flit width, routing algorithm, virtual channel count, or AXI data width.

These values are not graph-global parameters and they are not module instance parameters. They are IP instance parameters.

The first implementation should use this model for RaveNoC. RaveNoC's fabric-wide RTL configuration should move out of `RaveTile.parameters` and into RaveNoC IP instance parameters. The RaveNoC generator should read those values from the IP instance, not from the first tile in the internal graph.

## Goals

- Add a spec-level way for IP plugins to declare IP instance parameters.
- Store parameter values per IP instance in project/generator documents.
- Let the Qt property panel edit IP instance parameters when no node is selected.
- Show multiple IP instances as separate parameter sections in one project canvas.
- Default all IP parameter sections to expanded when no node is selected.
- Move RaveNoC fabric-wide parameters from `RaveTile` metadata to RaveNoC instance metadata.
- Remove the implicit RaveNoC generator rule that reads fabric-wide parameters from the first tile.

## Non-Goals

- Do not preserve compatibility with temporary old RaveNoC graph shapes that stored fabric-wide parameters on the first tile.
- Do not implement full system-level IP-to-IP connection semantics in this phase.
- Do not introduce RaveNoC-specific Qt widgets.
- Do not treat IP instance parameters as canvas-global project parameters.

## Current Problem

RaveNoC currently exposes many fabric-wide parameters on `RaveTile`, including `flit_data_width`, `virtual_channels`, `routing_algorithm`, `priority`, `axi_data_width`, `axi_cdc_required`, and `bypass_cdc`.

That is misleading. Those values configure the generated RaveNoC fabric as a whole. They cannot safely vary per router tile. The generator currently hides this by reading parameters from the first logical tile and ignoring the same parameters on other tiles.

This creates three problems:

- the UI suggests each tile has independent configuration that is not actually independent;
- generator behavior depends on an implicit "first tile wins" rule;
- future multi-IP projects need a way to distinguish parameters for each IP instance.

## Terminology

### Project

A project is the saved editor document represented by one canvas workspace. It can contain project metadata, IP instances, internal modules, connections, and visual state.

### IP Type

An IP type is declared by a plugin or extension spec. `finepaper.ravenoc` declaring a RaveNoC IP type is the first target.

### IP Instance

An IP instance is one configured occurrence of an IP type inside the project. Multiple IP instances can use the same IP type:

- `ravenoc_0`
- `ravenoc_1`
- `axi_dma_0`

Each instance owns its own parameter values.

### Internal Module

An internal module is a node inside an IP instance's editable internal graph. For RaveNoC, `RaveTile` and `RaveEndpoint` are internal modules.

## Spec Model

Extension specs should declare IP instance parameters at the IP level.

For the current extension spec shape, add a top-level `instance_parameters` field:

```yaml
schema: finepaper.extension.v1
kind: noc
extension:
  id: finepaper.ravenoc
  name: RaveNoC
  version: '1.0'
instance_parameters:
  flit_data_width:
    type: int
    default: 32
    min: 8
    max: 512
    label: Flit data width
    description: FLIT_DATA_WIDTH macro value.
  virtual_channels:
    type: int
    default: 3
    min: 1
    max: 16
    label: Virtual channels
    description: N_VIRT_CHN macro value.
  routing_algorithm:
    type: string
    enum: [xy, yx]
    labels: { xy: XY, yx: YX }
    default: xy
    label: Routing algorithm
    description: ROUTING_ALG macro value.
modules:
  RaveTile:
    parameters:
      display_name: { type: string, default: Rave Tile }
      external_id: { type: string, default: rave_00 }
      x: { type: int, default: 0, configurable: false }
      y: { type: int, default: 0, configurable: false }
      mesh_col: { type: int, default: 0, configurable: false }
      mesh_row: { type: int, default: 0, configurable: false }
```

`instance_parameters` uses the same validation rules as module parameters:

- `type` is required and must be `string`, `int`, or `bool`;
- `default` is required;
- `enum`, `labels`, `label`, `description`, `configurable`, `min`, and `max` have the same meaning as module parameters;
- defaults must match the declared type;
- enum defaults must be inside the enum.

The spec generator should emit these parameters into runtime plugin metadata so Qt can load labels, defaults, choices, and bounds without hard-coding plugin behavior.

## Document Model

The project document should store IP instances explicitly.

A future project-oriented shape can be:

```json
{
  "schema": "finepaper-project-v1",
  "parameters": {},
  "ip_instances": [
    {
      "id": "ravenoc_0",
      "plugin": "finepaper.ravenoc",
      "type": "RaveNoC",
      "parameters": {
        "flit_data_width": 32,
        "virtual_channels": 3,
        "routing_algorithm": "xy"
      },
      "modules": [],
      "connections": []
    },
    {
      "id": "axi_dma_0",
      "plugin": "finepaper.axi_dma",
      "type": "AxiDma",
      "parameters": {}
    }
  ]
}
```

The existing generator export can remain single-IP while still using the same ownership model:

```json
{
  "schema": "finepaper-plugin-graph-v1",
  "ip_instance": {
    "id": "ravenoc_0",
    "plugin": "finepaper.ravenoc",
    "type": "RaveNoC",
    "parameters": {
      "flit_data_width": 32,
      "virtual_channels": 3,
      "routing_algorithm": "xy"
    }
  },
  "modules": [],
  "connections": []
}
```

This lets the first implementation keep one active generated IP while avoiding a schema that implies one canvas-wide global parameter map.

## Qt Property Panel

The property panel should present parameters by ownership scope.

When no node is selected and the project has content, show:

- `Project`
- one section per IP instance

Each IP instance section should use a label like:

```text
RaveNoC / ravenoc_0
```

The section body contains that IP instance's parameters. All IP instance sections are expanded by default in the unselected state.

When a node is selected, show:

- the selected node's owning IP instance section;
- `Selection`, containing the selected node's module parameters.

Other IP instance sections can remain visible after the active section, but the selected node's owner should be first. This keeps the property panel useful in both project-level and node-level workflows.

Visual treatment should use lightweight section headers rather than bare rows:

- section title;
- horizontal divider or header background;
- optional collapse/expand affordance;
- compact form rows inside the section.

The first implementation can make sections always expanded. Collapse state can be added without changing the data model.

## RaveNoC Parameter Move

Move these RaveNoC parameters from `RaveTile.parameters` to `instance_parameters`:

- `flit_data_width`
- `flit_type_width`
- `flit_buffer_depth`
- `virtual_channels`
- `routing_algorithm`
- `priority`
- `max_packet_flits`
- `axi_addr_width`
- `axi_data_width`
- `axi_cdc_required`
- `bypass_cdc`

Keep these parameters on `RaveTile`:

- `display_name`
- `external_id`
- `x`
- `y`
- `mesh_col`
- `mesh_row`

`RaveEndpoint` remains a normal internal module with endpoint-local metadata.

## Generator Flow

RaveNoC generator input should provide IP instance parameters separately from internal modules.

The generator should build RaveNoC configuration from:

- rows and columns derived from the RaveTile internal graph;
- fabric-wide settings read from `ip_instance.parameters`;
- endpoint bindings derived from `RaveEndpoint` connections.

The generator must not read fabric-wide RaveNoC settings from any `RaveTile`.

If a required IP instance parameter is missing, the export layer should normally supply the spec default. If the generator receives an incomplete input anyway, it should fail with a clear missing-parameter error instead of silently reading a tile fallback.

## Validation

Spec validation should reject:

- unknown top-level fields except the newly supported `instance_parameters`;
- invalid parameter types;
- missing defaults;
- enum defaults outside enum;
- labels whose keys are outside enum;
- non-integer `min` or `max` for integer parameters.

Qt validation should reject or report:

- project IP instances that reference missing plugins;
- IP instances whose parameter names are not declared by that IP type;
- IP instance parameter values whose type does not match metadata.

RaveNoC generator validation should continue to enforce RaveNoC-specific constraints:

- legal mesh dimensions;
- `flit_buffer_depth` is a power of two;
- `flit_data_width` is supported;
- `axi_data_width` equals `flit_data_width`;
- routing and priority values map to known upstream macros;
- `axi_cdc_required` matches the derived NoC size.

## Migration Policy

No automatic compatibility layer is required for temporary RaveNoC graphs that stored fabric-wide settings on the first tile.

This project is still in rapid development. The earlier behavior was a temporary implementation shortcut, not a stable user-facing file format.

After this change, newly generated RaveNoC graphs should store fabric-wide settings only on the IP instance.

## Testing

Add tests for:

- `spec_generator` accepting and emitting `instance_parameters`;
- `spec_generator` rejecting invalid instance parameter declarations;
- Qt plugin loading of IP instance parameter metadata;
- project serialization preserving IP instance parameter values;
- property panel rendering project/IP parameter sections when no node is selected;
- property panel rendering selected-node parameters separately from the owning IP instance parameters;
- RaveNoC generator reading fabric-wide parameters from `ip_instance.parameters`;
- RaveNoC generator rejecting missing fabric-wide IP instance parameters;
- RaveNoC internal graph generation deriving rows and columns from tiles while using IP instance parameters for RTL configuration.

Existing tests should continue to cover:

- module parameter metadata loading;
- project save/load;
- generic plugin graph export;
- RaveNoC generator output and smoke verification.
