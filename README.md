# Finepaper

Finepaper is a focused NoC creation tool. A design names one runtime-loaded
NoC Package and records only user intent: an N×M Mesh, Package parameters, and
NoC-facing Endpoints attached to Mesh Routers. Routers, links, automatic slots
and GUI geometry are derived.

The target architecture is described in
[the Chinese architecture document](docs/architecture/package-driven-target-architecture.zh.md).

## Current vertical slice

- Runtime discovery of directory Packages.
- `finepaper.noc` Package with a real Ruby RTL generator, isolated behind the
  Package process boundary.
- Shared C++ application layer for the CLI and the Qt Widgets GUI.
- Deterministic Mesh projection and Endpoint-to-Router attachment only.
- CLI for CI/scripts: Package list/check, design create/validate/generate, and
  one-shot `run`.
- GUI pages for Start, Overview, Topology, Parameters, Validate and Generate.

## Build and try it

```bash
xmake build finepaper
xmake build finepaper-gui
xmake build finepaper-tests

./build/linux/x86_64/release/finepaper package list --package-root packages --json
./build/linux/x86_64/release/finepaper run examples/mesh-2x2.request.json \
  --package-root packages --output /tmp/finepaper-output --json
./build/linux/x86_64/release/finepaper-gui --package-root packages
./build/linux/x86_64/release/finepaper-tests
```

In the GUI, use **Package → Install Package Directory…** and select the
directory that contains a Package `package.json`. Finepaper validates it,
adds it to the active catalog, selects it for creation, and remembers that
directory for later GUI sessions.

Every generation creates a separate `runs/op-*` directory below the chosen
output root. The input design is copied and normalized there; the generator may
not modify the original design.
