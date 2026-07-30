# Finepaper Configurable Mesh NoC Package

This Package exposes the cumulative V3 editing model while keeping Router
topology fixed to a rectangular Mesh.

- The runtime is self-contained inside this Package root. Installing only
  `finepaper.noc@3.0.0` is sufficient for validation and generation; it does
  not locate or execute the V1 Package as a hidden sibling dependency.
- `clock` and `power` are ordinary Package-declared Domain types. Finepaper
  Core has no special branch for either name.
- `router.microarchitecture` provides sparse per-Router overrides for routing
  algorithm, virtual-channel count, and buffer depth.
- Endpoint parameters remain owned by each Endpoint instance. Their labels,
  descriptions, units, categories, and advanced flags are declared by this
  Package so generic editors do not need to recognize parameter ids.
- The shared generator maps the Router property set into the legacy RTL
  generator and consumes the materialized Endpoint parameter values.
- Runtime validation strictly parses Domain instances, memberships, relations,
  crossing policies, and edge overrides. Every actual Package-defined Domain
  crossing derived from the rectangular Mesh must resolve to one canonically
  oriented bidirectional-boundary policy; an override must reference a policy
  for that exact crossing pair.
- Generation emits `<design>_domain_constraints.json` as a deterministic,
  compiled constraints artifact. It contains normalized Domain instances
  and members, relations, policies, overrides, and the Mesh Router-Link and
  Endpoint-Attachment crossings with their effective properties. Each policy
  resolves one complete bidirectional physical boundary; `from` and `to` are
  the canonical Mesh/attachment orientation, not a single traffic channel.
  Changing Domain intent therefore changes a runtime-produced constraints
  artifact instead of merely changing the copied Design intent.
- The current legacy RTL backend does not yet instantiate CDC, isolation, or
  level-shifting cells from this artifact. A downstream IP Engine may consume
  it; direct RTL realization is the next implementation stage and is not
  implied by the capability booleans alone.
- `<design>_design_intent.json` is retained as a compatibility/debug snapshot;
  it is not the Domain implementation contract.
- `runtimeCapabilities.domainConfiguration` declares complete consumption of
  Domain instances, memberships, relations, crossing policies, and per-edge
  overrides. This is an explicit runtime compatibility promise, not inferred
  from the presence of `domainTypes`.

The V1 Package remains installed as `finepaper.noc@1.0.0` for existing Design
compatibility. New designs can select `finepaper.noc@3.0.0` explicitly.
