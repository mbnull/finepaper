# Finepaper Spec Generator

`spec_generator` turns the hand-written NoC definition spec into the files consumed by the current Qt editor and Ruby framework.

Inputs:

- `spec/noc/noc.yaml` for bus, module, parameter, and interface definitions.
- `spec/noc/views/*.xml` for Qt-only graphics and interface-anchor placement.

Outputs:

- `plugins/noc/modules.xml`
- `plugins/noc/graphics/*.xml`
- `plugins/noc/generator/src/ruby/model/xp.rb`
- `plugins/noc/generator/src/ruby/model/endpoint.rb`

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser intentionally supports only the `schema: v1` NoC subset. Module names are spec-defined; NoC backend model generation is selected by semantic `graph_group` values such as `xps` and `endpoints`. Unknown fields are errors.

Qt-visible connection points are interface anchors. Each interface should provide one editor-visible `port` whose `id` is the interface id, and each view may provide pixel coordinates in an `<anchors>` block.
