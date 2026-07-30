# Finepaper Configurable Mesh NoC Package

This Package exposes the cumulative V3 editing model while keeping Router
topology fixed to a rectangular Mesh.

- `clock` and `power` are ordinary Package-declared Domain types. Finepaper
  Core has no special branch for either name.
- `router.microarchitecture` provides sparse per-Router overrides for routing
  algorithm, virtual-channel count, and buffer depth.
- Endpoint parameters remain owned by each Endpoint instance. Their labels,
  descriptions, units, categories, and advanced flags are declared by this
  Package so generic editors do not need to recognize parameter ids.
- The shared generator maps the Router property set into the legacy RTL
  generator, consumes the materialized Endpoint parameter values, and emits
  the complete Design as a generation-intent artifact.

The V1 Package remains installed as `finepaper.noc@1.0.0` for existing Design
compatibility. New designs can select `finepaper.noc@3.0.0` explicitly.
