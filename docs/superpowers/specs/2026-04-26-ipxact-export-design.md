# IP-XACT Export Design

## Summary

Finepaper will add an export-only IP-XACT path to `spec_generator`. The hand-written Finepaper NoC spec remains the source of truth. The generator will use a deliberately small IP-XACT subset to emit bus, abstraction, and component XML for the bundled NoC plugin.

This phase does not import IP-XACT, does not parse arbitrary IP-XACT XML, and does not attempt full IEEE 1685 coverage. The goal is to package the same `XP`, `Endpoint`, `ni_link`, and `router_link` definitions that already drive the Qt frontend and Ruby backend into a standards-shaped interchange format.

## Goals

- Keep `spec/noc/noc.yaml` as the single semantic source for the NoC IP definitions.
- Keep Qt-only graphics and manual placement outside the semantic spec in `spec/noc/views/*.xml`.
- Add enough IP-XACT metadata to export useful XML without making the YAML hard to write.
- Generate IP-XACT `busDefinition`, `abstractionDefinition`, and `component` XML.
- Validate the Finepaper IP-XACT subset before writing files.
- Make the exported XML deterministic and suitable for golden-file tests.
- Avoid copying the large YAML and IP-XACT implementation style from `../some_else/ipcore`; only borrow the useful separation between bus definitions, parameters, topology instances, and generated artifacts.

## Non-Goals

- Importing IP-XACT into Finepaper specs.
- Round-tripping IP-XACT.
- Parsing arbitrary IP-XACT XML.
- Supporting every IEEE 1685-2022 schema element.
- Exporting the Qt-drawn NoC topology as an IP-XACT `design` in this phase.
- Replacing the current Qt module bundle or Ruby model generation.
- Generating complete AMBA AXI or CHI bus definitions.
- Validating against the official IP-XACT XSD until the schema files are available locally.

## Background

The existing `spec_generator` already parses a constrained YAML subset and emits:

- Qt module bundle XML.
- Qt graphics overlays.
- Ruby model classes.

The external `../some_else/ipcore` project uses larger YAML configuration files. Its useful pattern is not the size of those files, but their separation of concerns:

- `busdef` describes protocol identity and VLNV-style metadata.
- `bus` describes interface roles and physical-to-logical signal mapping.
- parameter YAML files group parameter definitions by IP type.
- topology YAML files describe configured instances and links.
- generated files under tests act as stable golden artifacts.

Finepaper should keep its smaller spec compiler, while adopting this separation for IP-XACT export fields and tests.

## Export Scope

The first export produces module-level IP-XACT objects:

- `ni_link` bus definition.
- `ni_link_rtl` abstraction definition.
- `router_link` bus definition.
- `router_link_rtl` abstraction definition.
- `XP` component.
- `Endpoint` component.

The export does not describe a complete graph instance. A future topology export can consume the Qt graph JSON and emit an IP-XACT `design` or hierarchical top-level component, but that is a separate feature.

## IP-XACT Version

Use IEEE 1685-2022 XML names and namespace:

```xml
xmlns:ipxact="http://www.accellera.org/XMLSchema/IPXACT/1685-2022"
xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
xsi:schemaLocation="http://www.accellera.org/XMLSchema/IPXACT/1685-2022 http://www.accellera.org/XMLSchema/IPXACT/1685-2022/index.xsd"
```

The emitted XML should use the `ipxact` prefix consistently. Do not use the older `spirit` namespace used by older examples.

## VLNV Policy

Each exported top-level IP-XACT object needs a VLNV:

- `vendor`
- `library`
- `name`
- `version`

The Finepaper spec should provide defaults at the top level. Buses and modules may override `name` and `version` when needed, but should normally inherit `vendor`, `library`, and `version`.

Recommended defaults for the bundled NoC plugin:

```yaml
ipxact:
  vendor: finepaper.org
  library: NoC
  version: '1.0'
```

The emitter derives object names as follows unless explicitly overridden:

- bus definition name: bus key, for example `ni_link`
- abstraction definition name: `<bus>_rtl`, for example `ni_link_rtl`
- component name: module key, for example `XP`

## YAML Additions

The top-level `ipxact` section declares common export metadata:

```yaml
schema: v1
kind: noc-definition
name: NoC
version: '1.0'
ipxact:
  vendor: finepaper.org
  library: NoC
  version: '1.0'
  output_root: ipxact
```

Each bus can opt into IP-XACT export and describe bus-level properties:

```yaml
buses:
  ni_link:
    description: Endpoint-to-router NoC interface.
    ipxact:
      name: ni_link
      abstraction: ni_link_rtl
      direct_connection: true
      addressable: false
    compatibility:
      roles:
        initiator: [target]
        target: [initiator]
      match: [protocol, data_width]
    config:
      protocol:
        type: string
        enum: [axi4, chi]
        default: axi4
        description: Interface protocol.
      data_width:
        type: int
        enum: [32, 64, 128]
        default: 64
        description: Data width in bits.
    signals:
      - name: flit
        direction: initiator_to_target
        width: FLIT_WIDTH
      - name: valid
        direction: initiator_to_target
        width: 1
      - name: ready
        direction: target_to_initiator
        width: 1
```

Modules can optionally declare IP-XACT component metadata:

```yaml
modules:
  Endpoint:
    description: Endpoint interface block that terminates a local NoC connection.
    ipxact:
      name: Endpoint
      model_name: finepaper_endpoint
      view: RTL
```

Interfaces may use the existing bus, role, config, and port information. If exact physical pin mapping is known, an interface may add an explicit IP-XACT port map:

```yaml
modules:
  Endpoint:
    interfaces:
      noc:
        bus: ni_link
        role: initiator
        config:
          protocol: { parameter: protocol }
          data_width: { parameter: data_width }
        port:
          id: noc
          direction: input
          type: bus
          bus_type: ni_link
          role: attachment
          name: NoC
          description: NoC attachment input
        ipxact:
          port_maps:
            flit: noc_flit
            valid: noc_valid
            ready: noc_ready
```

The existing `port` and `ports` fields remain editor-facing abstractions. They are not automatically treated as RTL pins for IP-XACT. Without `ipxact.port_maps`, the component emitter exports the `busInterface` and omits `portMaps`.

## Signal Mapping

Finepaper bus signals become abstraction logical ports. The existing signal direction values map to IP-XACT role directions:

- `initiator_to_target`: `onInitiator direction=out`, `onTarget direction=in`
- `target_to_initiator`: `onInitiator direction=in`, `onTarget direction=out`
- `peer_to_peer`: exported as bidirectional role-compatible logical ports for the peer interface

When an interface declares explicit port maps, the component emitter maps logical signals to physical ports:

- logical port names come from bus `signals[].name`.
- physical port names come from explicit `interface.ipxact.port_maps`.
- missing `port_maps` means the `busInterface` is exported without physical `portMaps`.

The first phase does not expand one bus projection into many HDL wires unless the spec already lists those wires. If a future RTL interface needs separate `flit`, `valid`, and `ready` physical names, the spec should represent them explicitly instead of encoding them in a string template.

## Component Export

Each module produces one IP-XACT `component`.

The component contains:

- document name group from VLNV and descriptions.
- `busInterfaces`, one per Finepaper module interface.
- `abstractionTypes` with one RTL abstraction reference.
- `portMaps` from logical bus signals to physical module ports when explicit maps are declared.
- interface mode from Finepaper role:
  - `initiator` maps to IP-XACT `initiator`.
  - `target` maps to IP-XACT `target`.
  - `peer` maps to a conservative system-style interface until the router-link modeling is refined.
- `model/views` with an RTL view.
- `model/ports` for every physical port referenced by explicit port maps.
- `parameters` for Finepaper parameters that are not editor-only.
- `vendorExtensions` only for Finepaper metadata that IP-XACT cannot represent cleanly.

Editor-only fields such as canvas coordinates, collapse state, palette labels, and graphics layout must not become IP-XACT semantic parameters.

## Output Layout

The generated files should live under the NoC plugin because they package the NoC IP definition:

```text
plugins/noc/ipxact/
  busdefs/
    finepaper.org/NoC/ni_link/1.0/ni_link.xml
    finepaper.org/NoC/ni_link/1.0/ni_link_rtl.xml
    finepaper.org/NoC/router_link/1.0/router_link.xml
    finepaper.org/NoC/router_link/1.0/router_link_rtl.xml
  components/
    finepaper.org/NoC/XP/1.0/XP.xml
    finepaper.org/NoC/Endpoint/1.0/Endpoint.xml
```

The exact path layout mirrors the external project enough to be recognizable, but remains plugin-local and generated by `spec_generator`.

## Generator Architecture

Add a small IP-XACT export path inside `spec_generator`, separate from the Qt and Ruby emitters:

```text
spec_generator/
  lib/
    spec_generator.rb
    spec_generator/
      ipxact/
        subset_validator.rb
        xml_writer.rb
        bus_definition_emitter.rb
        abstraction_definition_emitter.rb
        component_emitter.rb
```

Responsibilities:

- `subset_validator.rb`: validates that the parsed Finepaper spec contains enough IP-XACT metadata and rejects unsupported shapes.
- `xml_writer.rb`: owns XML element construction, namespace attributes, escaping, indentation, and deterministic output.
- `bus_definition_emitter.rb`: emits one bus definition XML per bus.
- `abstraction_definition_emitter.rb`: emits one RTL abstraction XML per bus.
- `component_emitter.rb`: emits one component XML per module.

This should be an emitter or builder, not an IP-XACT parser. The only parser remains the Finepaper YAML parser.

## CLI

Extend `spec_generator/bin/spec-gen` with optional IP-XACT output:

```bash
ruby spec_generator/bin/spec-gen \
  --spec spec/noc/noc.yaml \
  --views spec/noc/views \
  --qt-bundle plugins/noc \
  --ruby-model plugins/noc/generator/src/ruby/model \
  --ipxact plugins/noc/ipxact
```

The default repository workflow may enable `--ipxact` once golden tests are in place. Until then, the option can be explicit to avoid changing existing generated output unexpectedly.

## Validation Rules

The subset validator should reject:

- missing top-level `ipxact.vendor`, `ipxact.library`, or `ipxact.version`.
- unknown fields in `ipxact` blocks.
- a bus exported without `direct_connection` or `addressable`.
- a bus signal without `name`, `direction`, or `width`.
- unsupported signal direction values.
- a module interface that references a bus without IP-XACT export metadata.
- a module interface that has no port projection.
- an `ipxact.port_maps` logical name that does not match a bus signal.
- duplicate physical port names in `ipxact.port_maps` with conflicting direction or width.
- parameters marked `emit: editor_only` being exported as IP-XACT parameters.
- a role that cannot be mapped to the supported IP-XACT interface modes.

The validator should continue the current spec generator style: fail fast with a precise `SpecGenerator::SpecError`.

## Testing

Add focused Ruby tests under `spec_generator/test`:

- valid NoC spec emits all expected IP-XACT files.
- generated XML is well formed.
- bus definition XML contains VLNV, direct connection, addressability, and description.
- abstraction XML contains logical ports for `flit`, `valid`, and `ready`.
- component XML contains bus interfaces for `Endpoint.noc`.
- component XML contains bus interfaces for all four `XP.local*` interfaces.
- component XML contains `portMaps` only when explicit `ipxact.port_maps` are declared.
- editor-only parameters are not exported.
- invalid IP-XACT metadata produces precise errors.

Golden XML files should be committed under a test fixture directory once the emitter output stabilizes. Tests should compare normalized XML or deterministic full text, not handwritten string fragments only.

## Migration Plan

Phase 1 defines the schema and emits module-level IP-XACT XML from the existing NoC spec.

Phase 2 wires the export into the default spec generation command and updates documentation.

Phase 3 can add optional schema validation if the official 1685-2022 XSD files are added locally.

Phase 4 can add topology instance export from Qt graph JSON as a separate design-level feature.

## Open Decisions

The first implementation should make one conservative choice for `router_link` peer interfaces. IP-XACT has richer modeling options than Finepaper needs right now, and router-to-router links are internal fabric connections rather than external endpoint protocols. If this becomes ambiguous during implementation, export `router_link` as a non-addressable direct connection with peer metadata in `vendorExtensions`, then refine after downstream tool feedback.

The current generated RTL has simplified flit ports, while the spec-level bus has `flit`, `valid`, and `ready` signals. The first emitter should describe the spec-level intent. If downstream tools require exact RTL pin matching, the spec must gain explicit per-signal physical port names before relying on the exported component XML as an RTL packaging artifact.
