# IpCraft Public Contracts

All public schemas are versioned by schema id. Schema ids are stable protocol names, not C++ class names. Implementations may use JSON or YAML syntax, but canonical test fixtures should be serializable to deterministic JSON.

Versioning rules for all schemas:

- `schema` is required and must equal the schema id.
- Breaking changes require a new schema id such as a future `ipcraft.project.vN`.
- Additive compatible fields must be optional and ignored only when the schema explicitly marks an extension namespace.
- Unknown top-level fields are rejected unless the schema explicitly provides an `extensions`, `metadata`, or package-owned block.
- Package-owned extension blocks must include owner package id, schema id, version, and data.
- Implementations must preserve unknown package-owned extension data they do not understand.

## `ipcraft.project.v1`

Project document contract. This is the persistent project file contract, not the generator input contract.

Required fields:

- `schema`
- `id`
- `name`
- `packages`
- `components`

Optional fields:

- `connections`
- `topologies`
- `constraints`
- `views`
- `diagnostics`
- `artifacts`
- `extensions`
- `metadata`

Validation rules:

- Package refs must include id and locked version.
- Component ids must be unique.
- Structural document loading validates ids, refs, object shapes, extension envelopes, and deterministic serialization only.
- Component `packageRef` must syntactically reference one of the declared project packages. Loading does not require package registry resolution.
- Component type existence, package default application, and package-declared interface existence are resolved validation performed by `PackageRegistry` and `ResolutionService` after package loading.
- Semantic connections must reference valid component ids during structural loading. Interface refs are accepted as stable endpoint names until package/interface resolution is available, unless the project explicitly declares instance interfaces.
- `connections[].kind: attachment` is forbidden. Topology attachment is owned only by `topologies[].attachments`.
- `topologies` entries must declare `schema` as either `ipcraft.topology.graph.v1` or `ipcraft.topology.parametric.v1`.
- Topology graph node/link/attachment ids must be unique within topology.
- View layout may reference semantic ids but must not create semantic objects.
- No layout fields are allowed in component config.
- No NoC implementation fields are allowed as core fields; they must live in extension blocks.
- Old NoC-specific schemas such as `ipcraft.noc.project.v1` are rejected by normal loading.

Minimal UART example:

```yaml
schema: ipcraft.project.v1
id: proj_uart_min
name: Minimal UART
packages:
  - id: vendor.uart16550
    version: 1.0.0
components:
  - id: uart0
    type: uart16550
    packageRef: vendor.uart16550@1.0.0
    config:
      baud: 115200
    identity:
      label: UART 0
connections: []
topologies: []
views:
  - id: block.main
    schema: ipcraft.view.v1
    kind: block_diagram
    targetRef: project:proj_uart_min
    providerRef: ipcraft.ui.block_diagram
    layout:
      nodes:
        uart0: { x: 96, y: 128, pinned: true }
      edges: {}
```

CPU -> NIC -> NoC example:

```yaml
schema: ipcraft.project.v1
id: proj_cpu_nic_noc
name: CPU NIC NoC Demo
packages:
  - id: vendor.riscv
    version: 2.1.0
  - id: vendor.memory
    version: 1.0.0
  - id: vendor.meshnoc
    version: 1.0.0
components:
  - id: cpu0
    type: core
    packageRef: vendor.riscv@2.1.0
    config: { xlen: 64 }
  - id: nic_cpu0
    type: axi_nic
    packageRef: vendor.meshnoc@1.0.0
    config: { axi_data_width: 64 }
  - id: noc0
    type: meshnoc
    packageRef: vendor.meshnoc@1.0.0
    config: { name: noc0 }
  - id: nic_mem0
    type: axi_nic
    packageRef: vendor.meshnoc@1.0.0
    config: { axi_data_width: 64 }
  - id: mem0
    type: memory_controller
    packageRef: vendor.memory@1.0.0
    config: { address_width: 32 }
connections:
  - id: c_cpu_to_nic
    from: { component: cpu0, interface: axi_m }
    to: { component: nic_cpu0, interface: axi_s }
    kind: interface
  - id: c_mem_to_nic
    from: { component: nic_mem0, interface: axi_m }
    to: { component: mem0, interface: axi_s }
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
      routing: xy
    attachments:
      - id: a_cpu
        topologyId: noc0.fabric
        attachmentPoint: { tile: [0, 0], slot: local0 }
        componentRef: nic_cpu0
        interfaceRef: noc_ep
      - id: a_mem
        topologyId: noc0.fabric
        attachmentPoint: { tile: [1, 1], slot: local0 }
        componentRef: nic_mem0
        interfaceRef: noc_ep
extensions:
  - ownerPackageId: ipcraft.capability.noc
    schemaId: ipcraft.capability.noc.extension.v1
    version: 1
    data:
      schema: ipcraft.capability.noc.extension.v1
      topologies:
        noc0.fabric:
          family: mesh
          dimensions: [2, 2]
          routing: xy
  - ownerPackageId: vendor.meshnoc
    schemaId: vendor.meshnoc.project.v1
    version: 1
    data:
      schema: vendor.meshnoc.project.v1
      flit_width: 64
views:
  - id: block.main
    schema: ipcraft.view.v1
    kind: block_diagram
    targetRef: project:proj_cpu_nic_noc
    providerRef: ipcraft.ui.block_diagram
    layout:
      nodes:
        cpu0: { x: 80, y: 120 }
        nic_cpu0: { x: 260, y: 120 }
        noc0: { x: 450, y: 160 }
        nic_mem0: { x: 640, y: 120 }
        mem0: { x: 820, y: 120 }
      edges:
        c_cpu_to_nic: { route: orthogonal, waypoints: [[180, 120], [240, 120]] }
```

Mesh NoC package example:

```yaml
schema: ipcraft.project.v1
id: proj_meshnoc_mesh
name: Vendor Mesh NoC 2x2 Mesh
packages:
  - id: vendor.meshnoc
    version: 1.0.0
components:
  - id: noc0
    type: meshnoc
    packageRef: vendor.meshnoc@1.0.0
    config:
      topology: mesh
      rows: 2
      columns: 2
topologies:
  - id: noc0.fabric
    schema: ipcraft.topology.graph.v1
    ownerComponentId: noc0
    kind: expanded_parametric
    nodes:
      - { id: r_0_0, kind: router, ports: [local0, east, south] }
      - { id: r_0_1, kind: router, ports: [local0, west, south] }
      - { id: r_1_0, kind: router, ports: [local0, east, north] }
      - { id: r_1_1, kind: router, ports: [local0, west, north] }
    links:
      - id: l_0_0_to_0_1
        from: { node: r_0_0, port: east }
        to: { node: r_0_1, port: west }
        direction: bidirectional
        kind: noc_link
        config: { width: 64, latency: 1, vc: 2 }
extensions:
  - ownerPackageId: ipcraft.capability.noc
    schemaId: ipcraft.capability.noc.extension.v1
    version: 1
    data:
      schema: ipcraft.capability.noc.extension.v1
      topologies:
        noc0.fabric:
          family: mesh
          dimensions: [2, 2]
  - ownerPackageId: vendor.meshnoc
    schemaId: vendor.meshnoc.project.v1
    version: 1
    data:
      schema: vendor.meshnoc.project.v1
      generator: rtl
      routing_algorithm: xy
views:
  - id: noc0.topology
    schema: ipcraft.view.v1
    kind: topology_graph
    targetRef: topology:noc0.fabric
    providerRef: ipcraft.ui.topology_graph
    layout:
      nodes:
        r_0_0: { x: 100, y: 100 }
        r_0_1: { x: 260, y: 100 }
        r_1_0: { x: 100, y: 260 }
        r_1_1: { x: 260, y: 260 }
      edges:
        l_0_0_to_0_1: { route: straight }
```

AnyNet explicit graph example:

```yaml
schema: ipcraft.project.v1
id: proj_anynet
name: AnyNet Explicit Fabric
packages:
  - id: vendor.anynet
    version: 1.0.0
components:
  - id: fabric0
    type: anynet_fabric
    packageRef: vendor.anynet@1.0.0
    config: {}
  - id: dma0
    type: dma
    packageRef: vendor.anynet@1.0.0
    config: {}
topologies:
  - id: fabric0.graph
    schema: ipcraft.topology.graph.v1
    ownerComponentId: fabric0
    kind: explicit_graph
    nodes:
      - { id: r0, kind: router, ports: [p0, p1, local0] }
      - { id: r1, kind: router, ports: [p0, p1, local0] }
      - { id: br0, kind: bridge, ports: [fabric, host] }
    links:
      - id: l0
        from: { node: r0, port: p0 }
        to: { node: r1, port: p0 }
        direction: bidirectional
        kind: fabric_link
        config: { width: 128, latency: 3 }
      - id: l1
        from: { node: r0, port: p1 }
        to: { node: br0, port: fabric }
        direction: directed
        kind: fabric_link
        config: { width: 64, latency: 1 }
    attachments:
      - id: a_dma0
        topologyId: fabric0.graph
        attachmentPoint: { node: br0, port: host }
        componentRef: dma0
        interfaceRef: fabric_ep
views:
  - id: fabric0.anynet
    schema: ipcraft.view.v1
    kind: topology_graph
    targetRef: topology:fabric0.graph
    providerRef: ipcraft.ui.topology_graph
    layout:
      nodes:
        r0: { x: 100, y: 200, pinned: true }
        r1: { x: 300, y: 160 }
        br0: { x: 220, y: 320 }
      edges:
        l0: { route: orthogonal, waypoints: [[140, 200], [260, 160]] }
        l1: { route: orthogonal, waypoints: [[120, 240], [220, 300]] }
```

Blackbox Verilog example:

```yaml
schema: ipcraft.project.v1
id: proj_blackbox
name: Blackbox Verilog Import
packages:
  - id: vendor.blackbox
    version: 1.0.0
components:
  - id: gpio0
    type: blackbox_verilog
    packageRef: vendor.blackbox@1.0.0
    config:
      top_module: gpio_peripheral
      files:
        - rtl/gpio_peripheral.sv
      parameters:
        WIDTH: 32
    identity:
      label: GPIO Peripheral
connections: []
extensions:
  - ownerPackageId: vendor.blackbox
    schemaId: vendor.blackbox.verilog.v1
    version: 1
    data:
      schema: vendor.blackbox.verilog.v1
      ports:
        - { name: clk, direction: input, width: 1 }
        - { name: rst_n, direction: input, width: 1 }
        - { name: gpio, direction: inout, width: 32 }
```

## `ipcraft.package.v1`

Package manifest contract.

Required fields:

- `schema`
- `id`
- `name`
- `version`
- `capabilities`

Optional fields:

- `description`
- `requires`
- `authors`
- `license`
- `homepage`
- `examples`
- `metadata`

Validation rules:

- `capabilities` is the only canonical location for package resource lists.
- `capabilities` may contain `components`, `interfaces`, `connection_rules`, `topologies`, `views`, `layout_providers`, `validators`, `generators`, `importers`, `exporters`, `templates`, `transforms`, and `native_plugins`.
- Top-level `components`, `interfaces`, `connection_rules`, `views`, `layout_providers`, `validators`, `generators`, `importers`, `exporters`, `templates`, `transforms`, and `native_plugins` are rejected.
- `id` is globally stable and reverse-DNS-like where possible.
- `version` is semantic versioning.
- Paths are package-root-relative and cannot escape package root.
- Referenced files must exist during `ipcraft package check`.
- Files listed under `capabilities.views` must validate as `ipcraft.view.descriptor.v1`, not project-bound `ipcraft.view.v1`.
- Commands declare input and output schemas.
- Native plugins are optional and cannot be required for basic component loading.

Example:

```yaml
schema: ipcraft.package.v1
id: vendor.uart16550
name: UART 16550
version: 1.0.0
capabilities:
  components:
    - components/uart.yml
  interfaces:
    - interfaces/axi4lite.yml
    - interfaces/serial.yml
  connection_rules:
    - rules/connections.yml
  views:
    - views/uart.block.yml
  generators:
    - id: rtl
      command: tools/generate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
  validators:
    - id: validate
      command: tools/validate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
examples:
  - examples/basic_uart.fpproj
```

## `ipcraft.component.v1`

Component type contract.

Required fields:

- `schema`
- `id`
- `name`
- `category`

Optional fields:

- `parameters`
- `interfaces`
- `ports`
- `signals`
- `identity`
- `display`
- `view`
- `extensionData`
- `generatorHints`
- `metadata`

Validation rules:

- Parameter defaults must match parameter type.
- Constraints must be valid for type.
- Interface refs must point to declared interface type ids.
- Parameter metadata is schema-driven and rendered by Inspector without package-specific UI code.

Example:

```yaml
schema: ipcraft.component.v1
id: axi_nic
name: AXI to NoC NIC
category: adapter
parameters:
  flit_width:
    type: integer
    default: 64
    unit: bits
    label: Flit width
    description: Width of each NoC flit.
    constraints:
      enum: [32, 64, 128]
    ui:
      group: Network Data Path
      importance: basic
    provenance:
      default_reason: Package default data path width.
    used_by:
      - generator: rtl
      - validator: drc
interfaces:
  - id: axi_s
    type: vendor.axi4
    role: slave
    direction: target
  - id: noc_ep
    type: ipcraft.capability.noc.endpoint
    role: endpoint
    direction: bidirectional
```

## `ipcraft.interface.v1`

Interface type contract.

Required fields:

- `schema`
- `id`
- `name`
- `protocolFamily`

Optional fields:

- `modes`
- `roles`
- `directions`
- `parameters`
- `clock`
- `reset`
- `compatibility`
- `signals`
- `metadata`
- future `ipxactMapping`

Validation rules:

- Interface instance direction and role must be allowed by the interface type.
- Width expressions must resolve through component/interface config.
- Clock/reset association rules are declarative.
- IP-XACT mapping is optional and not required by core.

Example:

```yaml
schema: ipcraft.interface.v1
id: vendor.axi4
name: AXI4
protocolFamily: axi
modes: [master, slave]
directions: [initiator, target]
parameters:
  data_width:
    type: integer
    default: 64
    constraints: { enum: [32, 64, 128, 256] }
clock:
  required: true
reset:
  required: true
compatibility:
  sameClockDomain: recommended
```

## `ipcraft.connection_rules.v1`

Data-driven connection rules.

Required fields:

- `schema`
- `rules`

Optional fields:

- `adapterRecommendations`
- `diagnostics`
- `metadata`

Validation rules:

- Rules declare allowed from/to interface type pairs.
- Directionality, cardinality, protocol compatibility, clock/reset constraints, and adapter recommendations are declarative.
- UI must call `ConnectionCompatibilityService` instead of hardcoding protocol rules.

Example:

```yaml
schema: ipcraft.connection_rules.v1
rules:
  - id: axi_master_to_slave
    from: { interfaceType: vendor.axi4, role: master }
    to: { interfaceType: vendor.axi4, role: slave }
    directionality: directed
    cardinality: many_to_one
    constraints:
      sameClockDomain: warning
      matchingDataWidth: error
    diagnostics:
      incompatible:
        severity: error
        message: AXI master must connect to an AXI slave with compatible width.
adapterRecommendations:
  - id: axi_to_noc_nic
    from: { interfaceType: vendor.axi4, role: master }
    to: { interfaceType: ipcraft.capability.noc.endpoint }
    adapterComponent: vendor.meshnoc/axi_nic
```

## `ipcraft.topology.graph.v1`

Semantic topology graph contract. This schema stores explicit graphs and already-expanded parametric graphs. Authored parametric topology requests use `ipcraft.topology.parametric.v1`.

Required fields:

- `schema`
- `id`
- `kind`
- `nodes`
- `links`

Optional fields:

- `ownerComponentId`
- `attachments`
- `routing`
- `metadata`

Validation rules:

- `kind` is `explicit_graph` or `expanded_parametric`.
- Node ids are unique.
- Link endpoint node/port refs exist.
- Attachment refs point to valid topology attachment points and project component/interface refs.
- Embedded attachments may omit `topologyId`; standalone attachment patch payloads must include it.
- `expanded_parametric` must contain expanded semantic nodes and links. Empty `nodes`/`links` cannot stand in for a parametric request.
- Layout fields are rejected.

Example:

```yaml
schema: ipcraft.topology.graph.v1
id: fabric0.graph
kind: explicit_graph
nodes:
  - { id: r0, kind: router, ports: [p0, p1, local0] }
  - { id: r1, kind: router, ports: [p0, p1, local0] }
links:
  - id: l0
    from: { node: r0, port: p0 }
    to: { node: r1, port: p0 }
    direction: bidirectional
    kind: fabric_link
    config: { width: 128, latency: 2 }
attachments:
  - id: a0
    attachmentPoint: { node: r0, port: local0 }
    componentRef: nic0
    interfaceRef: noc_ep
```

## `ipcraft.topology.parametric.v1`

Authored parametric topology request. Providers expand this schema into `ipcraft.topology.graph.v1` with `kind: expanded_parametric` before graph-level validation, visualization, validation, or generation.

Required fields:

- `schema`
- `id`
- `kind`
- `family`
- `parameters`

Optional fields:

- `ownerComponentId`
- `providerRef`
- `attachments`
- `constraints`
- `metadata`

Validation rules:

- `schema` must be `ipcraft.topology.parametric.v1`.
- `kind` must be `parametric`.
- `family` names a built-in or package/capability-provided topology family such as `mesh`, `ring`, `torus`, `tree`, `fat_tree`, or a package-defined id.
- Structural loading validates id shape, parameter object shape, attachment envelopes, and package-owned metadata without requiring provider execution.
- Resolved validation must find a provider for `providerRef` or `family`.
- Provider expansion must produce deterministic `ipcraft.topology.graph.v1`.
- Attachments may reference provider-defined attachment coordinates such as `{ tile, slot }`; after expansion they must resolve to concrete graph attachment points.
- Embedded attachments may omit `topologyId`; standalone attachment patch payloads must include it.
- Layout fields are rejected.

Example:

```yaml
schema: ipcraft.topology.parametric.v1
id: noc0.fabric
kind: parametric
ownerComponentId: noc0
family: mesh
providerRef: ipcraft.capability.noc.topology.mesh
parameters:
  dimensions: [2, 2]
  routing: xy
attachments:
  - id: a_cpu
    attachmentPoint: { tile: [0, 0], slot: local0 }
    componentRef: nic_cpu0
    interfaceRef: noc_ep
```

## Capability-owned schema: `ipcraft.capability.noc.v1`

NoC capability declaration consumed by packages that provide NoC fabrics, endpoints, adapters, topology families, validation, or generator projections. This schema belongs to `ipcraft-capability-noc`, not core.

Required fields:

- `schema`
- `capabilityId`
- `topologyFamilies`
- `attachmentModel`

Optional fields:

- `endpointInterfaces`
- `linkInterfaces`
- `linkConfig`
- `routing`
- `validators`
- `generators`
- `metadata`

Validation rules:

- `schema` must be `ipcraft.capability.noc.v1`.
- `capabilityId` must be `ipcraft.capability.noc`.
- `topologyFamilies` may include `explicit_graph`, `mesh`, `ring`, `torus`, `tree`, `fat_tree`, or package-defined family ids.
- `attachmentModel` declares whether payload IP attaches through adapters, direct NoC-aware endpoint interfaces, or both.
- This schema must not be required by `ipcraft-core`; it is loaded by capability/package layers.

Example:

```yaml
schema: ipcraft.capability.noc.v1
capabilityId: ipcraft.capability.noc
topologyFamilies:
  - mesh
  - explicit_graph
attachmentModel:
  supportsPayloadViaAdapter: true
  supportsDirectEndpoint: true
endpointInterfaces:
  - ipcraft.capability.noc.endpoint
linkInterfaces:
  - ipcraft.capability.noc.link
linkConfig:
  width: { type: integer, default: 64, unit: bits }
  latency: { type: integer, default: 1, unit: cycles }
  vc: { type: integer, default: 2 }
```

## Capability-owned schema: `ipcraft.capability.noc.extension.v1`

Project extension block schema for NoC-specific semantic data. It augments generic topology graphs with NoC family, routing, endpoint slot, and generator mapping information.

Required fields:

- `schema`
- `topologies`

Optional fields:

- `defaults`
- `routing`
- `metadata`

Validation rules:

- The extension block owner must be `ipcraft.capability.noc`.
- Each key under `topologies` must reference an existing `ProjectDesign.topologies[].id`.
- Attachment data references topology attachments by id or attachment point; it must not duplicate semantic connections.
- Layout coordinates are rejected.
- NoC family data may describe mesh, ring, torus, tree, fat tree, or explicit graph metadata, but core must treat it as package-owned extension data.

Example:

```yaml
schema: ipcraft.capability.noc.extension.v1
topologies:
  noc0.fabric:
    family: mesh
    dimensions: [2, 2]
    routing: xy
    attachments:
      a_cpu:
        endpointClass: requester
        generatorName: cpu0_ep
      a_mem:
        endpointClass: target
        generatorName: mem0_ep
```

## Package-owned schema: `vendor.meshnoc.project.v1`

Example package-specific NoC implementation extension schema. It is intentionally package-owned and must not appear in core logic. Other NoC packages define their own equivalent schemas.

Required fields:

- `schema`

Optional fields:

- `flit_width`
- `routing_algorithm`
- `generator`
- `packageOptions`
- `metadata`

Validation rules:

- The extension block owner must match the package id, here `vendor.meshnoc`.
- The package validator owns semantic validation for these fields.
- Core must preserve this extension data but must not inspect it for behavior.

Example:

```yaml
schema: vendor.meshnoc.project.v1
flit_width: 64
routing_algorithm: xy
generator: rtl
```

## `ipcraft.view.v1`

Project view document and layout wrapper. This schema is used inside `ipcraft.project.v1.views`, where a concrete project target exists. Package-authored reusable view descriptors use `ipcraft.view.descriptor.v1`.

Required fields:

- `schema`
- `id`
- `kind`
- `targetRef`
- `providerRef`

Optional fields:

- `sourceRef`
- `templates`
- `portGrouping`
- `labels`
- `badges`
- `propertyGroups`
- `layoutPreference`
- `interactionAffordances`
- `diagnosticsOverlay`
- `icons`
- `layout`
- `presentationState`
- `metadata`

Validation rules:

- Raw drawing primitives and Qt painting commands are rejected as primary behavior.
- View descriptor cannot contain generator, validator, topology generation, or business logic.
- Layout is view-local and cannot define semantic nodes, components, or links.
- `targetRef` is required because project view documents are target-bound.

Example:

```yaml
schema: ipcraft.view.v1
id: noc0.topology
kind: topology_graph
targetRef: topology:noc0.fabric
providerRef: ipcraft.ui.topology_graph
templates:
  node:
    label: "{id}"
    badges: ["{kind}"]
  edge:
    label: "{config.width}b"
layoutPreference:
  algorithm: force
interactionAffordances:
  allowAddNode: true
  allowAddLink: true
  allowManualLayout: true
diagnosticsOverlay:
  showErrors: true
layout:
  nodes:
    r0: { x: 100, y: 200, pinned: true }
```

## `ipcraft.view.descriptor.v1`

Package-authored reusable view descriptor. It tells a generic provider how to present package component/interface/topology data, but it is not bound to one project target and does not store project layout.

Required fields:

- `schema`
- `id`
- `kind`
- `providerRef`

Optional fields:

- `appliesTo`
- `sourceKinds`
- `templates`
- `portGrouping`
- `labels`
- `badges`
- `propertyGroups`
- `layoutPreference`
- `interactionAffordances`
- `diagnosticsOverlay`
- `icons`
- `metadata`

Validation rules:

- `targetRef`, `layout`, and `presentationState` are rejected because package descriptors are reusable.
- Raw drawing primitives and Qt painting commands are rejected as primary behavior.
- View descriptors cannot contain generator, validator, topology generation, or business logic.
- Project view creation may combine this descriptor with a project `targetRef` to produce `ipcraft.view.v1`.

Example:

```yaml
schema: ipcraft.view.descriptor.v1
id: uart.block
kind: block_diagram
providerRef: ipcraft.ui.block_diagram
templates:
  node:
    label: "{identity.label}"
    icon: icons/uart.svg
  ports:
    groups:
      bus: [axi_s]
      external: [serial]
propertyGroups:
  - id: serial
    label: Serial
    parameters: [baud]
```

## `ipcraft.tool.input.v1`

Generator/validator input projection. Tools do not consume `.fpproj`.

Required fields:

- `schema`
- `project`
- `package`
- `target`
- `projection`
- `outputDirectory`

Optional fields:

- `environment`
- `metadata`

Validation rules:

- Projection must be deterministic.
- Relevant package extension data may be included.
- `projection` is the only canonical location for resolved data. Top-level `resolvedComponents`, `resolvedInterfaces`, `resolvedConnections`, `topologies`, `constraints`, `extensions`, or `provenance` are rejected.
- `projection` may contain `components`, `interfaces`, `connections`, `topologies`, `constraints`, `extensions`, `provenance`, `artifacts`, and target-specific package-owned blocks.
- View layout is excluded except for layout-specific tools.
- Paths are normalized and confined by host policy.

Example:

```yaml
schema: ipcraft.tool.input.v1
project:
  id: proj_cpu_nic_noc
  name: CPU NIC NoC Demo
package:
  id: vendor.meshnoc
  version: 1.0.0
target:
  kind: generator
  id: rtl
  componentRef: noc0
projection:
  components:
    - id: noc0
      type: meshnoc
      resolvedConfig:
        flit_width: 64
  topologies:
    - id: noc0.fabric
      schema: ipcraft.topology.graph.v1
      kind: expanded_parametric
      attachments:
        - id: a_cpu
          componentRef: nic_cpu0
          interfaceRef: noc_ep
  extensions:
    ipcraft.capability.noc:
      topologies:
        noc0.fabric:
          family: mesh
outputDirectory: run/vendor.meshnoc/rtl
```

## `ipcraft.tool.result.v1`

Tool output contract.

Required fields:

- `schema`
- `status`

Optional fields:

- `diagnostics`
- `artifacts`
- `patches`
- `metrics`
- `logs`
- `metadata`

Validation rules:

- `status` is `ok`, `warning`, or `error`.
- Diagnostics must validate as `ipcraft.diagnostic.v1`.
- Artifacts must validate as `ipcraft.artifact.v1`.
- Patches are suggestions and require host validation.
- Artifact paths must be within allowed output roots.

Example:

```yaml
schema: ipcraft.tool.result.v1
status: ok
diagnostics:
  - schema: ipcraft.diagnostic.v1
    id: diag_route_ok
    severity: info
    message: Generated XY routing tables.
    targetRef: topology:noc0.fabric
    source: { package: vendor.meshnoc, tool: rtl }
artifacts:
  - schema: ipcraft.artifact.v1
    id: meshnoc_top
    kind: rtl
    path: run/vendor.meshnoc/rtl/meshnoc_top.sv
    generatedBy: { package: vendor.meshnoc, tool: rtl }
    targetRef: component:noc0
metrics:
  generatedFiles: 12
```

## `ipcraft.diagnostic.v1`

Structured diagnostic record.

Required fields:

- `schema`
- `id`
- `severity`
- `message`
- `targetRef`
- `source`

Optional fields:

- `code`
- `details`
- `suggestedPatch`
- `relatedRefs`
- `metadata`

Validation rules:

- Severity is `info`, `warning`, or `error`.
- Target refs must be stable project refs.
- Suggested patch must validate as `ipcraft.patch.v1`.

Example:

```yaml
schema: ipcraft.diagnostic.v1
id: diag_bad_link_width
severity: error
code: noc.link.width_mismatch
message: Link width 32 does not match endpoint width 64.
targetRef: topology:noc0.fabric/link:l0
source: { package: ipcraft.capability.noc, validator: attachment_rules }
relatedRefs:
  - component:nic_cpu0/interface:noc_ep
```

## `ipcraft.artifact.v1`

Generated or imported artifact record.

Required fields:

- `schema`
- `id`
- `kind`
- `path`
- `generatedBy`
- `targetRef`

Optional fields:

- `hash`
- `timestamp`
- `metadata`

Validation rules:

- Path must be relative to allowed output root.
- Artifact id must be unique within project artifact registry.
- `generatedBy` identifies package/tool/importer/exporter.

Example:

```yaml
schema: ipcraft.artifact.v1
id: filelist
kind: filelist
path: run/meshnoc/meshnoc_filelist.f
generatedBy:
  package: vendor.meshnoc
  tool: rtl
targetRef: component:noc0
metadata:
  format: simulator_filelist
```

## `ipcraft.patch.v1`

Mutation envelope.

Required fields:

- `schema`
- `ops`

Optional fields:

- `id`
- `description`
- `author`
- `metadata`

Validation rules:

- Ops are validated against current `ProjectDesign`.
- Patch applies transactionally.
- Semantic and layout ops are distinct.
- Plugin/tool patches are suggestions until host validates them.

Example:

```yaml
schema: ipcraft.patch.v1
id: patch_set_uart_baud
description: Set UART baud rate
ops:
  - op: set_config
    target: component:uart0
    path: /baud
    value: 921600
```

Patch op contracts:

- `add_component`: requires `component`; optional `after`.
- `remove_component`: requires `componentId`; optional `cascade` defaulting to `false`.
- `set_config`: requires `target`, JSON Pointer `path`, and `value`; `target` must be a component, interface, connection, topology node/link, attachment, or package-owned extension target whose schema allows the path.
- `add_connection`: requires `connection`.
- `remove_connection`: requires `connectionId`.
- `add_topology_node`: requires `topologyId` and `node`; valid only for `ipcraft.topology.graph.v1`.
- `add_topology_link`: requires `topologyId` and `link`; valid only for `ipcraft.topology.graph.v1`.
- `set_attachment`: requires `topologyId` and `attachment`; for parametric topologies the attachment point may remain provider-coordinate form until expansion validation.
- `set_layout`: requires `viewId`, JSON Pointer `path`, and `value`; path must stay inside the view's layout tree.
- `set_extension_data`: requires `ownerPackageId`, `schemaId`, `targetRef`, JSON Pointer `path`, and `value`.
- `register_artifact`: requires `artifact`.
- `add_diagnostic`: requires `diagnostic`.

Ref grammar:

- Component ref: `component:<componentId>`.
- Interface ref: `component:<componentId>/interface:<interfaceId>`.
- Connection ref: `connection:<connectionId>`.
- Topology ref: `topology:<topologyId>`.
- Topology node/link refs: `topology:<topologyId>/node:<nodeId>` and `topology:<topologyId>/link:<linkId>`.
- Attachment ref: `topology:<topologyId>/attachment:<attachmentId>`.
- View ref: `view:<viewId>`.

Patch apply rules:

- The host validates all ops against the current `ProjectDesign`, loaded package schemas, and capability constraints before mutating state.
- Apply is transactional: if any op fails, no op in the patch is committed.
- A successful apply records enough inverse data for undo/redo at the transaction boundary.
- Tool/plugin patches remain suggestions until a host command validates and applies them.
