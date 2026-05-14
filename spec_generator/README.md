# Finepaper Spec Generator

`spec_generator` validates editable IP core package metadata and writes the
package-local Ipcraft manifest consumed by the Qt editor and package
generator/validation tools.

Inputs:

- `ipcores/<package>/ipcore.yml` for package identity, bus, module, parameter, interface, generator, DRC, and topology metadata.
- `ipcores/<package>/views/*.xml` for Qt-only graphics and interface-anchor placement.
- `ipcores/<package>/generator/` for the source generator and DRC implementation executed by Qt.
- `ipcores/<package>/vendor/` for vendored upstream RTL or support files when the package needs them.

Output:

- `ipcores/<package>/ipcraft.json`

`ipcraft.json` is the package manifest loaded by the Qt editor. It keeps package
identity, module/interface metadata, connection classes, views, topology
presets, and command descriptors next to the package source.

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser supports `ipcraft.package.v1` source packages. Module names are
package-defined; NoC editor behavior is selected through `noc.v1` package
metadata. Unknown fields are errors.

Qt-visible connection points are interface anchors. Each interface should provide one editor-visible `port` whose `id` is the interface id, and each view may provide pixel coordinates in an `<anchors>` block.

## Package Manifest

The editable package source under `ipcores/<package>/` is the source of truth.
Do not edit package manifests by hand. Change the matching
`ipcores/<package>/ipcore.yml`, `views/`, `generator/`, or `vendor/` content,
then validate or rebuild:

```bash
ruby spec_generator/bin/spec-gen check --ipcore ipcores/opennoc/ipcore.yml

ruby spec_generator/bin/spec-gen build --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
```

OpenNoC generation keeps the upstream Python mesh generator under
`ipcores/opennoc/vendor/OpenNoC` and wraps it from the Finepaper Ruby generator.
The first OpenNoC package generator version supports mesh topology only.

Before committing package manifest changes, run:

```bash
ruby spec_generator/bin/spec-gen --check
```
