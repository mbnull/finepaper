# IpCraft Package Authoring Specification

IpCraft packages are the default third-party IP integration mechanism. Package authors should write YAML/JSON schemas, examples, validators, generators, and optional scripts. They should not need Qt/C++ plugins for ordinary IP.

## Package Layout

Recommended layout:

```text
my-package/
  package.yml
  components/
  interfaces/
  rules/
  views/
  capabilities/
  tools/
  templates/
  examples/
  tests/
  README.md
```

All paths in `package.yml` are relative to package root. Path escape is invalid.

## Minimal Package

`package.yml`:

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
  validators:
    - id: validate
      command: tools/validate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
  generators:
    - id: rtl
      command: tools/generate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
examples:
  - examples/basic_uart.fpproj
```

`components/uart.yml`:

```yaml
schema: ipcraft.component.v1
id: uart16550
name: UART 16550
category: peripheral
parameters:
  baud:
    type: integer
    default: 115200
    unit: baud
    label: Baud rate
    description: Serial baud rate.
    constraints:
      minimum: 1200
      maximum: 3000000
    ui:
      group: Serial
      importance: basic
interfaces:
  - id: axi_s
    type: vendor.axi4lite
    role: slave
    direction: target
  - id: serial
    type: vendor.serial
    role: external
    direction: bidirectional
```

## NoC Package

A generic NoC package declares NoC capability use without becoming core behavior.

```yaml
schema: ipcraft.package.v1
id: vendor.flexnoc
name: FlexNoC
version: 1.0.0
requires:
  capabilities:
    - ipcraft.capability.noc
capabilities:
  components:
    - components/flexnoc.yml
    - components/axi_nic.yml
  interfaces:
    - interfaces/noc_endpoint.yml
    - interfaces/noc_link.yml
    - interfaces/axi4.yml
  connection_rules:
    - rules/noc_connections.yml
  topologies:
    - capabilities/topologies.yml
  views:
    - views/flexnoc.topology.yml
  validators:
    - id: validate
      command: tools/validate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
  generators:
    - id: rtl
      command: tools/generate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
```

`capabilities/topologies.yml`:

```yaml
schema: ipcraft.capability.noc.v1
capabilityId: ipcraft.capability.noc
topologyFamilies:
  - explicit_graph
  - mesh
attachmentModel:
  supportsPayloadViaAdapter: true
  supportsDirectEndpoint: true
linkConfig:
  width: { type: integer, default: 64, unit: bits }
  latency: { type: integer, default: 1, unit: cycles }
  vc: { type: integer, default: 2 }
```

## Example NoC Implementation Package

Expected tree:

```text
packages/vendor-meshnoc/
  package.yml
  components/
    meshnoc.yml
    axi_nic.yml
    router.yml
  interfaces/
    noc_endpoint.yml
    noc_link.yml
    axi4.yml
  capabilities/
    noc.yml
  views/
    meshnoc.block.yml
    meshnoc.topology.yml
  tools/
    generate
    validate
  examples/
    mesh_2x2.fpproj
    cpu_nic_mesh.fpproj
```

`package.yml`:

```yaml
schema: ipcraft.package.v1
id: vendor.meshnoc
name: Vendor Mesh NoC
version: 1.0.0
requires:
  capabilities:
    - ipcraft.capability.noc
capabilities:
  components:
    - components/meshnoc.yml
    - components/axi_nic.yml
  interfaces:
    - interfaces/noc_endpoint.yml
    - interfaces/noc_link.yml
    - interfaces/axi4.yml
  connection_rules:
    - rules/connections.yml
  topologies:
    - capabilities/noc.yml
  views:
    - views/meshnoc.block.yml
    - views/meshnoc.topology.yml
  validators:
    - id: validate
      command: tools/validate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
  generators:
    - id: rtl
      command: tools/generate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
examples:
  - examples/mesh_2x2.fpproj
  - examples/cpu_nic_mesh.fpproj
```

Rules:

- Declares `ipcraft.capability.noc` capability requirement.
- Supports `parametric.mesh` initially.
- May support `explicit_graph` only when generator supports it.
- Provides AXI NIC or declares compatible NICs.
- Consumes `ipcraft.tool.input.v1`.
- Returns `ipcraft.tool.result.v1`.
- Requires no core special cases.

## NIC/Adapter Package

Adapter packages bridge payload IP protocols to fabric endpoint protocols.

```yaml
schema: ipcraft.component.v1
id: axi_to_noc_nic
name: AXI to NoC NIC
category: adapter
parameters:
  axi_data_width:
    type: integer
    default: 64
    unit: bits
    constraints: { enum: [32, 64, 128] }
  flit_width:
    type: integer
    default: 64
    unit: bits
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

Connection rules can recommend this component:

```yaml
schema: ipcraft.connection_rules.v1
adapterRecommendations:
  - id: axi_master_to_noc
    from: { interfaceType: vendor.axi4, role: master }
    to: { interfaceType: ipcraft.capability.noc.endpoint }
    adapterComponent: vendor.nic/axi_to_noc_nic
    message: Insert AXI-to-NoC NIC between AXI master and NoC attachment.
```

## Blackbox Verilog Package

Blackbox packages expose imported RTL as components.

```yaml
schema: ipcraft.package.v1
id: vendor.blackbox
name: Blackbox Verilog
version: 1.0.0
capabilities:
  components:
    - components/blackbox_verilog.yml
  interfaces:
    - interfaces/raw_signal.yml
  validators:
    - id: validate
      command: tools/validate
      input: ipcraft.tool.input.v1
      output: ipcraft.tool.result.v1
```

`components/blackbox_verilog.yml`:

```yaml
schema: ipcraft.component.v1
id: blackbox_verilog
name: Blackbox Verilog Module
category: rtl
parameters:
  top_module:
    type: string
    label: Top module
    description: Verilog/SystemVerilog module name.
  files:
    type: array
    items: { type: string }
    label: RTL files
    description: Package-relative or project-relative RTL file list.
  parameters:
    type: object
    label: Verilog parameters
interfaces:
  - id: raw
    type: vendor.raw_signal_bundle
    role: external
    direction: bidirectional
```

Example project config:

```yaml
components:
  - id: gpio0
    type: blackbox_verilog
    packageRef: vendor.blackbox@1.0.0
    config:
      top_module: gpio_peripheral
      files: [rtl/gpio_peripheral.sv]
      parameters: { WIDTH: 32 }
```

## View Descriptor Authoring

Package view descriptors describe how generic providers should present package data.

Allowed:

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

Forbidden:

```yaml
paint:
  qtCommands:
    - drawLine(0, 0, 10, 10)
logic:
  generateTopology: "for i in rows"
```

## Tool Authoring

Tools must read `ipcraft.tool.input.v1` and write `ipcraft.tool.result.v1`.

Tool input can be delivered by stdin or a host-provided file path. The package manifest must declare the chosen convention if the host supports both.

Minimal tool result:

```yaml
schema: ipcraft.tool.result.v1
status: ok
diagnostics: []
artifacts: []
```

Error result:

```yaml
schema: ipcraft.tool.result.v1
status: error
diagnostics:
  - schema: ipcraft.diagnostic.v1
    id: diag_missing_topology
    severity: error
    code: tool.missing_topology
    message: Generator target does not include a resolved topology.
    targetRef: component:noc0
    source: { package: vendor.meshnoc, tool: rtl }
```

Tools must not:

- read `.fpproj` as implied input
- inspect Qt graph files
- depend on UI coordinates
- write artifacts outside output directory
- mutate project files directly

## Package CLI

### `ipcraft package init`

Creates a minimal package skeleton.

Example:

```bash
ipcraft package init vendor.uart16550 --name "UART 16550"
```

Required output:

- `package.yml`
- `components/`
- `interfaces/`
- `rules/`
- `views/`
- `tools/`
- `examples/`
- initial README

### `ipcraft package check`

Validates manifest, referenced files, schemas, examples, command declarations, and path confinement.

Example:

```bash
ipcraft package check packages/vendor-meshnoc
```

Required checks:

- manifest schema valid
- all referenced files exist
- component/interface/rule schemas valid and view files validate as `ipcraft.view.descriptor.v1`
- examples load as `ipcraft.project.v1`
- package-local paths do not escape root
- tool input/output schemas declared
- native plugins optional and valid when present

### `ipcraft package preview`

Shows discoverable capabilities without executing generators.

Example:

```bash
ipcraft package preview packages/vendor-meshnoc
```

Required output:

- components
- interfaces
- connection rules
- topology providers
- views
- validators
- generators
- templates
- examples
- diagnostics

### `ipcraft package test`

Runs package examples and validators in a controlled local tool execution policy.

Example:

```bash
ipcraft package test packages/vendor-meshnoc
```

Required behavior:

- builds tool input projections for examples
- runs validators declared by package
- optionally runs smoke generators marked safe
- validates tool results
- reports structured diagnostics

### `ipcraft package run-generator`

Runs a selected generator against a selected project/example.

Example:

```bash
ipcraft package run-generator packages/vendor-meshnoc --project examples/cpu_nic_mesh.fpproj --target noc0 --generator rtl --out run/meshnoc
```

Required behavior:

- builds `ipcraft.tool.input.v1`
- allows tool input preview
- executes command with package-local environment
- validates `ipcraft.tool.result.v1`
- registers artifacts only through host result handling

### `ipcraft package pack`

Creates a distributable package archive.

Example:

```bash
ipcraft package pack packages/vendor-meshnoc --out dist/vendor-meshnoc-1.0.0.fppkg
```

Required behavior:

- runs `package check`
- includes referenced files
- excludes build/run outputs unless declared
- rejects path escapes
- writes package metadata and checksum manifest

## Package Author Acceptance Criteria

A package is acceptable when:

- `package check` passes.
- Public examples load and validate.
- Components expose human-readable parameter metadata.
- Connection rules are data-driven.
- Validators/generators consume tool protocol.
- No native plugin is required for basic use.
- Preview shows all capabilities and diagnostics.
- Package tests can run without hidden host internals.
