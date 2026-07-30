# Finepaper Configurable Mesh NoC Package

This Package exposes the cumulative V3 editing model while keeping Router
topology fixed to a rectangular Mesh.

- The runtime is self-contained inside this Package root. Installing only
  `finepaper.noc@3.0.0` is sufficient for validation and generation; it does
  not locate or execute the V1 Package as a hidden sibling dependency.
- V3 Router Links and Endpoint attachments expose a bidirectional
  payload/valid/ready contract. The generated XP currently uses a registered,
  one-to-one forwarding shell so every transfer has real backpressure and no
  payload is broadcast. This is the hardware foundation for per-edge Domain
  bridges; it is intentionally not presented as a complete routing or
  virtual-channel implementation.
- Domain configuration remains data-driven: users may create, rename, edit,
  relate, and assign any instances allowed by this Package's `domainTypes`.
  `runtime/domain-realization.json` separately declares how every supported
  type, property, relation, and crossing policy lowers to implementation
  roles and recipes. Validate and Generate both compile that contract and
  fail closed on an unmapped value; product ids are not branched on in Core or
  Application.
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
- Validate and Generate additionally compile those constraints through the
  Package-owned realization mapping. Generation emits the deterministic
  `<design>_domain_implementation.json` typed plan, including Domain/entity
  bindings, derived relations, stage ordering, bidirectional CDC/isolation,
  and resolved per-direction voltage translation.
- The legacy renderer receives that plan as explicit typed metadata rather
  than hiding it in global parameters. Before writing RTL it verifies plan
  headers, entity/edge membership, role cardinality, traffic orientation, and
  recipe parameters against the concrete Mesh graph.
- Every active timing Domain receives an explicit clock binding and a local
  asynchronous-assert/synchronous-release reset synchronizer. A single active
  Domain preserves the legacy `clk` top-level ABI; a multi-Domain design uses
  collision-safe tokenized clock ports. The compatibility mode is recorded in
  generated RTL and implementation evidence instead of being inferred later.
- Each timing-Domain crossing on a Router Link or Endpoint attachment
  instantiates two `fp_async_ready_valid_fifo` cells, one for each traffic
  orientation. Same-Domain edges keep their direct payload/valid/ready bundle.
  Network interfaces are instantiated per Endpoint in the Endpoint timing
  Domain, while identical modules are still reused by complete NI signature.
- FIFO/reset primitives are regression-tested independently and through
  generated multi-clock tops, including unsafe parameter rejection,
  asynchronous reset assertion, synchronous release, bidirectional traffic,
  ordering, and backpressure.
- Generation emits
  `<design>_domain_implementation_evidence.json`, which maps active Domain
  ports/reset instances and every physical edge to concrete RTL hierarchy and
  typed parameters. It also lists every unmaterialized plan item. Power
  isolation, level shifting, and derived-clock relations are currently
  explicit deferred items, so `claims.completePlan` remains false when they are
  present; the backend does not generate placeholder power logic.
- `<design>_design_intent.json` is retained as a compatibility/debug snapshot;
  it is not the Domain implementation contract.
- `runtimeCapabilities.domainConfiguration` declares complete consumption of
  Domain instances, memberships, relations, crossing policies, and per-edge
  overrides. This is an explicit runtime compatibility promise, not inferred
  from the presence of `domainTypes`.

The V1 Package remains installed as `finepaper.noc@1.0.0` for existing Design
compatibility. New designs can select `finepaper.noc@3.0.0` explicitly.
