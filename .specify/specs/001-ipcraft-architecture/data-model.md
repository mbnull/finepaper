# IpCraft Data Model

This document defines the canonical domain records. JSON/YAML examples are illustrative; exact schema validation rules live in `contracts.md` and future schema files.

## ProjectDesign

`ProjectDesign` is the aggregate root.

Fields:

- `schema`: `ipcraft.project.v1`
- `id`: stable project id
- `name`: display name
- `packages`: locked package references
- `components`: component instances
- `interfaces`: optional resolved interface instance cache; canonical definitions come from components plus package schemas
- `connections`: semantic connections
- `topologies`: topology graphs or parametric topology specs, each with an explicit schema id
- `constraints`: project-level constraints
- `views`: view documents with layout
- `diagnostics`: persisted or latest diagnostics
- `artifacts`: artifact records
- `extensions`: package-owned extension blocks

Example:

```yaml
schema: ipcraft.project.v1
id: proj_uart
name: Minimal UART
packages:
  - id: vendor.uart16550
    version: 1.0.0
components:
  - id: uart0
    type: uart16550
    packageRef: vendor.uart16550@1.0.0
    identity:
      label: UART 0
    config:
      baud: 115200
    interfaces:
      - id: axi_s
        type: axi4lite
        role: slave
        direction: target
      - id: serial
        type: serial
        role: external
        direction: bidirectional
connections: []
topologies: []
views:
  - id: block.main
    schema: ipcraft.view.v1
    kind: block_diagram
    targetRef: project:proj_uart
    providerRef: ipcraft.ui.block_diagram
    layout:
      nodes:
        uart0: { x: 80, y: 120, pinned: true }
      edges: {}
extensions: []
```

## ComponentInstance

Represents one IP block in a project.

Fields:

- `id`
- `type`
- `packageRef`
- `config`: authored semantic config only
- `identity`: label, display name, human notes
- `interfaces`: resolved or explicit interface instances
- `metadata`: host-owned metadata
- `extensionData`: package-owned instance data

Forbidden in `config`: `x`, `y`, `node_width`, `node_height`, `collapsed`, `waypoints`, `zoom`, `pan`.

Example:

```yaml
id: nic_cpu0
type: axi_nic
packageRef: vendor.meshnoc@1.0.0
identity:
  label: CPU NIC
config:
  axi_data_width: 64
  flit_width: 64
extensionData:
  vendor.meshnoc:
    schema: vendor.meshnoc.nic.v1
    version: 1
    data:
      endpointClass: requester
```

## InterfaceInstance

Represents a connectable interface on a component.

Fields:

- `id`
- `ownerComponentId`
- `type`
- `role`
- `direction`
- `protocol`
- `clockRef`
- `resetRef`
- `config`
- `metadata`

Example:

```yaml
id: axi_m
ownerComponentId: cpu0
type: vendor.axi4
role: master
direction: initiator
protocol: axi4
clockRef: cpu0.aclk
resetRef: cpu0.aresetn
config:
  data_width: 64
```

## Connection

Semantic connection between component/interface endpoint refs. Topology attachments are not modeled as connections.

Fields:

- `id`
- `from`
- `to`
- `kind`
- `config`
- `constraints`
- `metadata`

Example:

```yaml
id: c_cpu_to_nic
from:
  component: cpu0
  interface: axi_m
to:
  component: nic_cpu0
  interface: axi_s
kind: interface
config:
  protocol: axi4
constraints:
  clockDomain: sys_clk
```

## TopologyGraph

Semantic topology graph, independent of visual layout. This record stores explicit graphs and expanded parametric graphs. Authored parametric topology requests use `ParametricTopology`.

Fields:

- `id`
- `schema`: `ipcraft.topology.graph.v1`
- `ownerComponentId` optional
- `kind`: `explicit_graph` or `expanded_parametric`
- `nodes`
- `links`
- `attachments`
- `routing` optional
- `metadata`

Example AnyNet:

```yaml
schema: ipcraft.topology.graph.v1
id: noc0.fabric
ownerComponentId: noc0
kind: explicit_graph
nodes:
  - id: r0
    kind: router
    ports: [p0, p1, local0]
  - id: r1
    kind: router
    ports: [p0, p1, local0]
  - id: sw0
    kind: switch
    ports: [uplink, down0, down1]
links:
  - id: l0
    from: { node: r0, port: p0 }
    to: { node: r1, port: p0 }
    direction: bidirectional
    kind: noc_link
    config: { width: 64, latency: 1, vc: 2, protocol: meshnoc_link }
  - id: l1
    from: { node: r0, port: p1 }
    to: { node: sw0, port: uplink }
    direction: directed
    kind: noc_link
attachments:
  - id: a_cpu0
    topologyId: noc0.fabric
    attachmentPoint: { node: r0, port: local0 }
    componentRef: nic_cpu0
    interfaceRef: noc_ep
```

## ParametricTopology

Authored request for a topology provider to expand into `TopologyGraph`.

Fields:

- `schema`: `ipcraft.topology.parametric.v1`
- `id`
- `kind`: `parametric`
- `ownerComponentId` optional
- `family`
- `providerRef` optional
- `parameters`
- `attachments`
- `constraints`
- `metadata`

Example mesh request:

```yaml
schema: ipcraft.topology.parametric.v1
id: noc0.fabric
ownerComponentId: noc0
kind: parametric
family: mesh
providerRef: ipcraft.capability.noc.topology.mesh
parameters:
  dimensions: [2, 2]
  routing: xy
attachments:
  - id: a_cpu0
    attachmentPoint: { tile: [0, 0], slot: local0 }
    componentRef: nic_cpu0
    interfaceRef: noc_ep
```

## TopologyNode

Fields:

- `id`
- `kind`: `router`, `endpoint`, `bridge`, `switch`, `adapter`, or `custom`
- `componentRef` optional
- `ports`
- `config`
- `metadata`

`kind` is descriptive; core must not make NoC-specific behavior decisions from it.

## TopologyLink

Fields:

- `id`
- `from` topology endpoint
- `to` topology endpoint
- `direction`
- `kind`
- `config`: width, latency, VC, protocol, or package-owned semantic link values
- `metadata`

Visual edge route and waypoints are not topology link config.

## Attachment

Connects project components/interfaces to topology attachment points.

Fields:

- `id`
- `topologyId`: required when the attachment is standalone, implied by the containing topology when embedded
- `attachmentPoint`
- `componentRef`
- `interfaceRef`
- `adapterRef` optional
- `config`

Example payload IP -> NIC -> NoC:

```yaml
components:
  - id: cpu0
    type: core
    packageRef: vendor.riscv@2.1.0
  - id: nic_cpu0
    type: axi_nic
    packageRef: vendor.meshnoc@1.0.0
  - id: noc0
    type: meshnoc
    packageRef: vendor.meshnoc@1.0.0
connections:
  - id: c_cpu_to_nic
    from: { component: cpu0, interface: axi_m }
    to: { component: nic_cpu0, interface: axi_s }
    kind: interface
topologies:
  - id: noc0.fabric
    schema: ipcraft.topology.parametric.v1
    ownerComponentId: noc0
    kind: parametric
    family: mesh
    providerRef: ipcraft.capability.noc.topology.mesh
    parameters:
      dimensions: [2, 2]
    attachments:
      - id: a_cpu0
        attachmentPoint: { tile: [0, 0], slot: local0 }
        componentRef: nic_cpu0
        interfaceRef: noc_ep
```

## ViewDocument

Project-bound view document plus view-local layout state. Package-authored reusable descriptors are separate `ipcraft.view.descriptor.v1` files and do not carry `targetRef` or layout.

Fields:

- `id`
- `schema`: `ipcraft.view.v1`
- `kind`
- `targetRef`
- `providerRef`
- `sourceRef`
- `layout`
- `presentationState`
- `metadata`

Example:

```yaml
schema: ipcraft.view.v1
id: noc0.anynet
kind: topology_graph
targetRef: topology:noc0.fabric
providerRef: ipcraft.ui.topology_graph
sourceRef: project.topologies[noc0.fabric]
layout:
  nodes:
    r0: { x: 100, y: 200, pinned: true }
    r1: { x: 240, y: 200, pinned: false }
  edges:
    l0:
      route: orthogonal
      waypoints:
        - [120, 200]
        - [220, 200]
  zoom: 1.0
  pan: [0, 0]
presentationState:
  selected: []
  collapsedGroups: []
```

## Layout

View-local visual data only.

Fields:

- node positions
- edge waypoints
- pinned state
- collapsed groups
- zoom/pan
- view-local settings

Layout may be saved for user convenience but must not affect semantic config, resolved config, validation, or generation except for layout-specific tools.

## ExtensionBlock

Package-owned data attached to project, component, topology, connection, or view.

Fields:

- `ownerPackageId`
- `schemaId`
- `version`
- `data`
- `validationState`

Example:

```yaml
ownerPackageId: vendor.meshnoc
schemaId: vendor.meshnoc.project.v1
version: 1
data:
  schema: vendor.meshnoc.project.v1
  routingAlgorithm: xy
validationState:
  status: valid
```

## Artifact

Generated or imported output linked to source refs.

Fields:

- `id`
- `kind`
- `path`
- `generatedBy`
- `targetRef`
- `metadata`

Example:

```yaml
id: rtl_top
kind: rtl
path: run/meshnoc/rtl/meshnoc_top.sv
generatedBy:
  package: vendor.meshnoc
  tool: rtl
targetRef: component:noc0
metadata:
  language: systemverilog
```

## Diagnostic

Structured issue or informational record.

Fields:

- `id`
- `severity`: `info`, `warning`, `error`
- `message`
- `targetRef`
- `source`
- `suggestedPatch` optional

Example:

```yaml
id: diag_invalid_attachment
severity: error
message: NIC endpoint width 64 does not match NoC attachment width 128.
targetRef: topology:noc0.fabric/attachment:a_cpu0
source:
  kind: validator
  package: vendor.meshnoc
  tool: validate
suggestedPatch:
  schema: ipcraft.patch.v1
  ops:
    - op: set_config
      target: component:nic_cpu0
      path: /flit_width
      value: 128
```

## ProjectPatch

Validated mutation envelope.

Fields:

- `schema`: `ipcraft.patch.v1`
- `id`
- `description`
- `ops`
- `metadata`

Operations:

- `add_component`
- `remove_component`
- `set_config`
- `add_connection`
- `remove_connection`
- `add_topology_node`
- `add_topology_link`
- `set_attachment`
- `set_layout`
- `set_extension_data`
- `register_artifact`
- `add_diagnostic`

Example:

```yaml
schema: ipcraft.patch.v1
id: patch_add_anynet_link
ops:
  - op: add_topology_link
    topologyId: noc0.fabric
    link:
      id: l2
      from: { node: r1, port: p1 }
      to: { node: sw0, port: down0 }
      direction: bidirectional
      kind: noc_link
      config: { width: 64, latency: 2 }
  - op: set_layout
    viewId: noc0.anynet
    path: /edges/l2
    value:
      route: orthogonal
      waypoints: [[240, 220], [320, 220]]
```

Host validation rules:

- Validate op schema.
- Validate target refs.
- Validate package/schema ownership.
- Validate semantic constraints.
- Validate artifact paths.
- Validate layout path is view-local.
- Apply transactionally or not at all.

## Config Provenance

Every resolved field must be explainable.

Fields:

- `path`
- `value`
- `type`
- `sourceLayer`: `authored`, `default`, `inherited`, `derived`, `generated`, `runtime`
- `sourceRef`
- `packageOwner`
- `schemaOwner`
- `defaultOrExplicit`
- `overriddenBy`
- `validationStatus`
- `documentation`
- `unit`
- `allowedValues`
- `consumers`

Example:

```yaml
path: /components/nic_cpu0/config/flit_width
value: 64
type: integer
sourceLayer: default
sourceRef: package:vendor.meshnoc@1.0.0/components/axi_nic.yml#/parameters/flit_width
packageOwner: vendor.meshnoc
defaultOrExplicit: default
validationStatus: valid
documentation: https://example.invalid/meshnoc/flit-width
unit: bits
allowedValues: [32, 64, 128]
consumers:
  - validator: drc
  - generator: rtl
```
