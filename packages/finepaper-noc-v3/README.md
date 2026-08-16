# Finepaper Configurable Mesh NoC Package

This Package exposes the cumulative V3 editing model while keeping Router
topology fixed to a rectangular Mesh.

- The runtime is self-contained inside this Package root. Installing only
  `finepaper.noc@3.1.0` is sufficient for validation and generation; it does
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
- The optional, versioned `finepaper.noc.powerIntent` Design extension owns
  supplies, logic controls, Domain power states, system-state vectors, power
  switches, retention, isolation, level-shifter placement, and optional
  technology-cell mappings. Its JSON Schema and compiler live inside this
  Package; Application transports the extension without recognizing those
  fields. The Package explicitly declares `editor.kind`; Finepaper never
  infers editability from the extension id. Schema loading is bounded to 1 MiB,
  contained within the Package root, and restricts standard reference keywords
  to same-document targets so generic tooling never performs hidden file or
  network resolution.
- The same extension declares its three references into the Design Domain
  plane through `designExtensions[].domainReferences`. Each declaration binds
  an RFC 6901 pointer pattern to a Package Domain Type; the empty pointer targets
  the extension root and `*` visits one existing array item. Application and the
  generic editor can therefore reject dangling
  or wrong-type Domain ids without recognizing the Power extension namespace
  or any Power Intent field name. A missing optional path remains the JSON
  Schema's responsibility. This is a cross-plane reference contract, not an
  automatic projection of voltage, retention, or system-state coverage.
  Pointer size/depth, traversal work, and emitted diagnostics are bounded so a
  damaged Package or Design cannot turn generic validation into unbounded work.
- `router.microarchitecture` provides sparse per-Router overrides for routing
  algorithm, virtual-channel count, and buffer depth.
- Endpoint parameters remain owned by each Endpoint instance. Their labels,
  descriptions, units, categories, and advanced flags are declared by this
  Package so generic editors do not need to recognize parameter ids.
- This self-contained runtime maps the Router property set into its legacy RTL
  generator and consumes the materialized Endpoint parameter values. Its base
  generator components are byte-synchronized with V1 by a repository gate;
  V3 semantic extensions and dedicated stubs remain Package-local.
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
  typed parameters. It also lists every item not materialized directly in RTL;
  the backend does not insert placeholder isolation or level-shifter cells.
- When the Power extension is present, generation additionally emits the
  canonical logical `<design>_power_intent_plan.json`, hierarchy-bound
  `<design>_power_implementation.json`, IEEE 1801-2013 / UPF 2.1
  `<design>_power_intent.upf`, and
  `<design>_power_intent_evidence.json`. Direct Power crossings use the actual
  signal driver (including reverse `ready`) and exact emitted instance/pin
  facts. Only strategies backed by a typed recipe and complete Package intent
  produce UPF commands.
- Regenerating the same Design after removing the extension removes only that
  Design's four optional Power artifacts, so stale UPF or evidence cannot be
  advertised as current output.
- A `top-port` Power control is a real generated RTL input and is recorded in
  the hierarchy manifest; an `upf-port` control is created and connected only
  in UPF. Validation and generation share one top-level namespace preflight so
  controls cannot collide with generated ports, parameters, internal bundles,
  or SystemVerilog keywords. The same preflight rejects aliases between any
  generated top-level identifiers before the legacy process or output creation.
- A boundary that combines CDC and Power remains explicitly deferred until
  the asynchronous FIFO infrastructure has an owned supply Domain. The UPF
  renderer emits no isolation or level-shifter command for that boundary.
  Reset synchronizers and every asynchronous FIFO also carry separate deferred
  supply-ownership coverage because the current Design contract does not assign
  that infrastructure; this applies even when both FIFO endpoints share one
  supply Domain. Switchable Router Domains retain explicit deferred evidence
  for safe shutdown sequencing and power-state-aware routing connectivity.
  System-state vectors are preserved in the receipt rather than translated into
  an invented command.
- The renderer performs strict contract and Tcl-token validation, and tests
  execute the output against a strict `tclsh` command-shape stub. The evidence
  deliberately records commercial EDA semantic validation as `not-performed`;
  technology binding status and count come from commands actually emitted, and
  generated output is never labeled physically complete while deferred items
  remain.
- `<design>_design_intent.json` is retained as a compatibility/debug snapshot;
  it is not the Domain implementation contract.
- `runtimeCapabilities.domainConfiguration` declares complete consumption of
  Domain instances, memberships, relations, crossing policies, and per-edge
  overrides. This is an explicit runtime compatibility promise, not inferred
  from the presence of `domainTypes`.

The V1 Package remains installed as `finepaper.noc@1.0.0` for existing Design
compatibility. New designs can select `finepaper.noc@3.1.0` explicitly. The
Package minor version changed because `domainReferences` adds a validation
capability that older Finepaper builds do not understand; an installed 3.0.0
Package remains the exact dependency for existing 3.0.0 designs.
