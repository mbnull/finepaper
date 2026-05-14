# Finepaper Spec Generator

`spec_generator` validates editable IP core package metadata and writes the
package-local Ipcraft runtime manifest consumed by the Qt editor and package
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

`ipcore.yml` is constrained authoring YAML, not a Qt runtime input. It uses
`schema: ipcraft.package.v1`, rejects unknown fields, duplicate mapping keys,
anchors, aliases, merge keys, custom tags, multi-document streams, and implicit
YAML features that would make schema validation ambiguous. Lists of named
objects use explicit `id` fields so references stay stable.

Qt consumes the generated `ipcraft.json` runtime manifest and referenced view
XML. Qt does not parse authoring YAML. Package roots are supplied by application
settings at startup or passed explicitly by tests and tools.

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser supports `ipcraft.package.v1` source packages. Module names are
package-defined; NoC editor behavior is selected through `noc.v1` package
metadata. Unknown fields are errors.

Qt-visible connection points are interface anchors. Each interface should provide one editor-visible `port` whose `id` is the interface id, and each view may provide pixel coordinates in an `<anchors>` block.

`extensions` are schema/specgen extension namespaces such as `noc.v1`. They add
authoring rules, defaults, and semantic mappings. `plugin`, when present in a
manifest, means a future Qt dynamic plugin descriptor; it is not a schema
extension and is not required for baseline package loading.

IP-XACT files are optional. A package can edit, validate, and generate without
shipping an IP-XACT XML file, but every interface, mode, and connection class in
the package must still be mappable to IP-XACT connection semantics. When an
IP-XACT root is present, Qt can run the optional strict connection sub-pass in
addition to its built-in validation.

Package `validate` and `generate` commands must declare `input_schema`, usually
`ipcraft.noc.project.v1`. The Qt editor runs built-in validation before invoking
package `validate` or `generate` commands.

## Package Manifest

The editable package source under `ipcores/<package>/` is the source of truth.
Do not edit package manifests by hand. Change the matching constrained
`ipcores/<package>/ipcore.yml`, `views/`, `generator/`, or `vendor/` content,
then validate or rebuild the runtime manifest:

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
