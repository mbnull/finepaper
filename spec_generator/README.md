# Finepaper Spec Generator

`spec_generator` turns editable IP core package metadata into the committed runtime metadata consumed by the current Qt editor and the IP core generator/DRC tools.

Inputs:

- `ipcores/<package>/ipcore.yml` for package identity, bus, module, parameter, interface, generator, DRC, and topology metadata.
- `ipcores/<package>/views/*.xml` for Qt-only graphics and interface-anchor placement.
- `ipcores/<package>/generator/` for the source generator and DRC implementation executed by Qt.
- `ipcores/<package>/vendor/` for vendored upstream RTL or support files when the package needs them.

Outputs:

- `generated/ipcores/<ipcore-id>/ipcore-runtime.json`
- `generated/ipcores/<ipcore-id>/modules.xml`
- `generated/ipcores/<ipcore-id>/graphics/*.xml`

`ipcore-runtime.json` is the runtime manifest loaded by `IpCoreRuntimeRegistry` and surfaced as an `IpCatalogEntry` by the Qt editor. It keeps `source_root`, generator, DRC, topology preset, and instance parameter metadata. Generator and DRC commands consume `ipcore_graph_v1` input, which the editor writes as `finepaper-ipcore-graph-v1` JSON through `IpCoreGraphExporter`.

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser intentionally supports only `finepaper.ipcore.v1` source packages. Module names are spec-defined; NoC backend model generation is selected by semantic `graph_group` values such as `xps` and `endpoints`. Unknown fields are errors.

Qt-visible connection points are interface anchors. Each interface should provide one editor-visible `port` whose `id` is the interface id, and each view may provide pixel coordinates in an `<anchors>` block.

## Generated Runtime Artifacts

The editable packages under `ipcores/<package>/` are the source of truth. The generated runtime metadata under `generated/ipcores/<ipcore-id>/` is committed for simple local development and packaging:

- `generated/ipcores/finepaper.noc/ipcore-runtime.json`
- `generated/ipcores/finepaper.noc/modules.xml`
- `generated/ipcores/finepaper.noc/graphics/*.xml`
- `generated/ipcores/finepaper.ravenoc/ipcore-runtime.json`
- `generated/ipcores/finepaper.ravenoc/modules.xml`
- `generated/ipcores/finepaper.ravenoc/graphics/*.xml`
- `generated/ipcores/finepaper.opennoc/ipcore-runtime.json`
- `generated/ipcores/finepaper.opennoc/modules.xml`
- `generated/ipcores/finepaper.opennoc/graphics/*.xml`

Do not edit generated runtime artifacts by hand. Change the matching `ipcores/<package>/ipcore.yml`, `views/`, `generator/`, or `vendor/` content, then regenerate:

```bash
ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/finepaper-noc/ipcore.yml \
  --runtime-bundle generated/ipcores/finepaper.noc

ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/ravenoc/ipcore.yml \
  --runtime-bundle generated/ipcores/finepaper.ravenoc

ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/opennoc/ipcore.yml \
  --runtime-bundle generated/ipcores/finepaper.opennoc
```

OpenNoC generation keeps the upstream Python mesh generator under `ipcores/opennoc/vendor/OpenNoC` and wraps it from the Finepaper Ruby generator. The first OpenNoC runtime generator version supports mesh topology only.

Before committing generated runtime metadata, run:

```bash
ruby spec_generator/bin/spec-gen --check
```
