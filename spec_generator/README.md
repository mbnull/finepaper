# Finepaper Spec Generator

`spec_generator` turns the hand-written NoC definition spec into the files consumed by the current Qt editor and Ruby framework.

Inputs:

- `spec/noc.yaml` for bus, module, parameter, and interface definitions.
- `spec/views/*.xml` for Qt-only graphics and interface placement.

Outputs:

- `qt/bundles/modules.xml`
- `qt/bundles/graphics/*.xml`
- `framework/src/ruby/model/xp.rb`
- `framework/src/ruby/model/endpoint.rb`

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser intentionally supports only the `schema: v1` NoC subset used by `XP` and `Endpoint`. Unknown fields are errors.
