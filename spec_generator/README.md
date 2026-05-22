# Finepaper Spec Generator

`spec_generator` validates editable IP core package metadata and writes the
package-local Ipcraft runtime package spec consumed by the Qt editor, headless
tools, and package generator/validation paths.

Inputs:

- `ipcores/<package>/ipcore.yml` for package identity, bus, module, parameter, interface, generator, DRC, and topology metadata.
- `ipcores/<package>/views/*.xml` for Qt-only graphics and interface-anchor placement.
- `ipcores/<package>/generator/` for the source generator and DRC implementation executed by Qt.
- `ipcores/<package>/vendor/` for vendored upstream RTL or support files when the package needs them.

Output:

- `ipcores/<package>/ipcraft.json`

`ipcraft.json` is the normalized runtime package spec loaded by the Qt editor
and headless tooling. It uses `schema: ipcraft.package.v1`. Runtime package
files must be self-contained after normalization: runtime code loads
`ipcraft.package.v1` directly and does not depend on `ipcore.yml`.

`ipcore.yml` is constrained authoring YAML, not a Qt runtime input. It uses
`schema: ipcraft.package.v1`, rejects unknown fields, duplicate mapping keys,
anchors, aliases, merge keys, custom tags, multi-document streams, and implicit
YAML features that would make schema validation ambiguous. Lists of named
objects use explicit `id` fields so references stay stable.

Qt, the headless API, and `ipcraft-cli` consume the generated `ipcraft.json`
runtime package spec and referenced view XML. They do not parse authoring YAML.
Package roots are supplied by application settings at startup or passed
explicitly by tests and tools. `ipcore.yml` may be used by specgen to produce
`ipcraft.package.v1`, but it is outside the runtime loading path.

Run from the repository root:

```bash
ruby spec_generator/bin/spec-gen
```

The parser supports `ipcraft.package.v1` source packages. Module names are
package-defined; NoC editor behavior is selected through `noc.v1` package
metadata stored under the runtime spec's `native.ipcraft.editor` escape hatch.
Unknown fields are errors.

Qt-visible connection points are interface anchors. Package sources declare
interfaces under `interfaces`, and view XML provides editor-visible coordinates
with `<anchors>` entries whose `ref` values match interface ids.

`extensions` are schema/specgen extension namespaces such as `noc.v1` and
`ipcraft.views`. They add authoring rules, defaults, and semantic mappings.
Optional runtime sections are emitted only when explicitly declared by the
authoring package and must have their matching extension enabled. Legacy
editor-only command/module metadata remains under `native.ipcraft.editor` and
does not implicitly enable public `emitters`, `flows`, or `artifacts`.
`plugin`, when present in a runtime package spec, means an optional Qt dynamic
plugin descriptor; it is not a schema extension and is not required for baseline
package loading.

IP-XACT files are optional. A package can edit, validate, and generate without
shipping an IP-XACT XML file, but every interface, mode, and connection class in
the package must still be mappable to IP-XACT connection semantics. When an
IP-XACT root is present, Qt can run the optional strict connection sub-pass in
addition to its built-in validation.

Legacy package-local `validate` and `generate` command descriptors may exist
inside `native.ipcraft.editor` for current Qt compatibility. Public V1
FlowRunner behavior is declared separately through explicit `flows`; specgen
does not synthesize public flows from legacy commands.

## Runtime Package Spec

The editable package source under `ipcores/<package>/` is the source of truth.
Do not edit generated runtime specs by hand. Change the matching constrained
`ipcores/<package>/ipcore.yml`, `views/`, `generator/`, or `vendor/` content,
then validate or rebuild the runtime package spec:

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
