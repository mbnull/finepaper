# NoC Extension Spec Generation Design

## Summary

Finepaper already uses `spec/noc/noc.yaml` plus `spec/noc/views/*.xml` to generate the built-in NoC metadata consumed by the Qt editor. The RaveNoC adaptation should follow the same spec-first path instead of hand-writing `plugins/ravenoc/modules.xml` and `plugins/ravenoc/graphics/RaveNoC.xml`.

This design adds a new extension spec schema for NoC extensions:

```yaml
schema: finepaper.extension.v1
kind: noc
```

The `kind` field is intentionally retained. It describes the extension category Finepaper can understand and can later drive category-specific UI pages. This is not a generic plugin schema and does not attempt to model all IP types yet. The only supported extension kind in this phase is `noc`.

## Goals

- Define RaveNoC as a NoC extension spec instead of hand-written Qt metadata.
- Generate `plugins/ravenoc/plugin.json`, `plugins/ravenoc/modules.xml`, and `plugins/ravenoc/graphics/RaveNoC.xml` from the spec.
- Keep the existing built-in NoC spec flow working.
- Preserve the current Ruby/ERB RaveNoC RTL generator; only UI/runtime metadata becomes generated.
- Leave a declared `pages` section in the spec so Finepaper can later show NoC-specific configuration pages.

## Non-Goals

- Do not build the Qt page-rendering UI in this phase.
- Do not convert `spec/noc/noc.yaml` to the new schema in this phase.
- Do not generalize to arbitrary IP categories beyond `kind: noc`.
- Do not generate the RaveNoC RTL wrapper/filelist generator from this extension spec.

## Spec Shape

RaveNoC will live under:

- `spec/noc/ravenoc/extension.yaml`
- `spec/noc/ravenoc/views/RaveNoC.xml`

The extension YAML uses this shape:

```yaml
schema: finepaper.extension.v1
kind: noc

extension:
  id: finepaper.ravenoc
  name: RaveNoC
  version: "1.0"

runtime:
  generator:
    command: ruby
    input_format: generic_graph_v1
    args:
      - generator/bin/generate
      - -i
      - "{input}"
      - -o
      - "{output}"
      - -t
      - generator/template

modules:
  RaveNoC:
    palette_label: RaveNoC
    graph_group: noc_core
    description: Configurable RaveNoC mesh fabric backed by upstream RTL.
    identity:
      external_id_prefix: ravenoc
      display_prefix: RNoC
      width: 2
      supports_mesh_coordinates: false
    parameters:
      rows:
        type: int
        default: 2
        min: 1
        max: 16
        label: Rows
        description: RaveNoC mesh row count.
    interfaces:
      axi_mosi:
        label: AXI MOSI
        bus: axi4_struct_array
        role: target
        connects_to: initiator
        match: [data_width]
        port:
          id: axi_mosi
          direction: input
          type: bus
          bus_type: axi4_struct_array
          role: target
          name: AXI MOSI
          description: AXI request array input

pages:
  - id: configuration
    title: Configuration
    target: module:RaveNoC
    kind: parameter_sections
    sections:
      - title: Mesh
        fields: [rows, cols, routing_algorithm]
      - title: Flit
        fields: [flit_data_width, flit_buffer_depth, virtual_channels]
      - title: AXI
        fields: [axi_addr_width, axi_data_width, axi_cdc_required]
```

The exact RaveNoC spec will include every parameter, interface, and port currently present in `plugins/ravenoc/modules.xml`.

## Generated Outputs

`spec_generator` will generate the runtime bundle for this extension:

- `plugins/ravenoc/plugin.json`
- `plugins/ravenoc/modules.xml`
- `plugins/ravenoc/graphics/RaveNoC.xml`
- `plugins/ravenoc/pages.json`

`plugin.json` remains a runtime compatibility artifact for the existing plugin loader. The source of truth is the extension spec.

`pages.json` is emitted but not consumed by Qt yet. It should preserve the `pages` array exactly enough for future UI work:

```json
{
  "schema": "finepaper.extension.pages.v1",
  "kind": "noc",
  "extension": "finepaper.ravenoc",
  "pages": []
}
```

## Generator Changes

The existing `spec_generator` stays responsible for spec-to-runtime metadata generation.

It will support both schemas:

- Existing: `schema: v1`, `kind: noc-definition`
- New: `schema: finepaper.extension.v1`, `kind: noc`

The CLI will get an extension-oriented mode:

```bash
ruby spec_generator/bin/spec-gen \
  --extension spec/noc/ravenoc/extension.yaml \
  --views spec/noc/ravenoc/views \
  --bundle plugins/ravenoc
```

The old defaults and options continue to generate the built-in NoC bundle and Ruby model files.

Internally, the generator should reuse the Qt bundle emission path. The new extension parser can normalize the RaveNoC extension YAML into the same module representation used by `QtBundleEmitter`, then emit `plugin.json` and `pages.json` in addition to `modules.xml` and graphics XML.

## Validation

The extension parser must reject:

- Unknown top-level fields.
- Any `schema` other than `finepaper.extension.v1`.
- Any `kind` other than `noc`.
- Missing `extension.id`, `extension.name`, or `extension.version`.
- Missing or malformed `runtime.generator`.
- Modules without parameters or interfaces maps.
- Interfaces without a port projection.
- View anchors referencing interfaces not declared by the module.
- `pages` entries whose `target` module does not exist.
- `pages` entries whose `fields` reference unknown module parameters.

## Testing

Add focused tests in `spec_generator/test/spec_generator_test.rb`:

- Generates a RaveNoC runtime bundle from an extension spec.
- Generated `plugin.json` includes `finepaper.ravenoc`, Ruby command, and `generic_graph_v1`.
- Generated `modules.xml` includes `RaveNoC`, `noc_core`, key parameters, and routing choices.
- Generated `graphics/RaveNoC.xml` includes the view anchors.
- Generated `pages.json` preserves `kind: noc` and the parameter sections.
- Invalid `kind` fails with a clear error.
- Unknown page field fails with a clear error.

Then update `qt/test/plugin_test.cpp` only if the generated runtime bundle shape changes. The existing RaveNoC metadata load test should continue passing against generated files.

## Migration

The RaveNoC handwritten files are replaced by generated equivalents. The generated XML does not need to be byte-for-byte identical, but it must preserve the fields that Qt consumes:

- Plugin ID and generator command.
- Module identity, graph group, ports, interfaces, parameters, labels, choices, and min/max metadata.
- Graphics layout and anchors.

After implementation, run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --extension spec/noc/ravenoc/extension.yaml --views spec/noc/ravenoc/views --bundle plugins/ravenoc
xmake build plugin_test
xmake run plugin_test
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

`xmake` commands are run from the `qt/` directory in this repository.
