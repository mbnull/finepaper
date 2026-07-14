# Default NoC Design Engine and Workbench Design

**Status:** Architecture Approved — V1 Normative Revision 4; wire contracts not frozen
**Revision 4 approved for review:** 2026-07-14
**Original date:** 2026-07-12

## 0. Normative Document Set

This specification is not implementation-complete without its appendices:

- [Appendix A — ProjectDesign V1 Contract](./appendix-a-project-design-v1-contract.md)
- [Appendix B — Patch, Command, Ownership, and Reconciliation Contract](./appendix-b-patch-command-reconciliation-contract.md)
- [Appendix C — NoC Package, Contract, Provider, Tool, and Diagnostic Contracts](./appendix-c-package-contract-provider-tool-contract.md)
- [Appendix D — Current-to-Target Migration and Cutover Map](./appendix-d-current-to-target-cutover-map.md)
- [Appendix E — Gate Acceptance Matrix](./appendix-e-gate-acceptance-matrix.md)
- [Appendix F — Gate 0 Core Canonical Models and Projections](./appendix-f-core-canonical-models.md)
- [Gate 0 Core Canonical Models JSON Schema](../../contracts/schemas/ipcraft.core-canonical-models.v1.schema.json)
- [Gate 0 Default Engine Bundle JSON Schema](../../contracts/schemas/ipcraft.engine-bundle.v1.schema.json)
- [Gate 0 Core Canonical Projection Vectors](../../contracts/vectors/core-canonical-projection-v1.json)
- [Gate 0 Stable Error Catalog](../../contracts/error-codes-v1.json)

The main specification defines product and architecture decisions. Appendices define fields, wire formats, ownership, commands, migration boundaries, and executable Gate criteria. In case of conflict, the more specific appendix controls implementation and the conflict must be corrected in the main specification before freeze.

## 1. Objective

Replace the current split-state Qt architecture and fragmented workbench without creating a second competing project IR. The existing pre-release project models are replaced by the first unified `ProjectDesign V1` authoritative representation owned by one `DesignSession`; the V1 application exposes a NoC Profile that permits exactly one top-level NoC. Common NoC behavior comes from an exact, immutable, installable, schema-driven Default NoC Engine Bundle behind a versioned Host contract; declarative Packages describe IP differences, and optional out-of-process Extension Providers cover only behavior the default engine cannot express.

The first product stage supports Mesh NoCs, AXI5/ACE/CHI boundary Interfaces, stable Router Access Slots, typed connected Domains, structured external DRC results, and safe generation. It does not attempt to become a general SoC composition platform.

## 2. Product Boundaries

- The V1 NoC Profile permits exactly one top-level NoC in a System Design.
- The authoritative representation is the evolved generic `ProjectDesign` foundation; no separate NoC project IR may be introduced.
- The IP Library is used only to choose the NoC Package during creation.
- The selected Package is fixed for the life of the System Design.
- External CPU, GPU, memory, or other endpoint IPs are not modeled or generated.
- A NoC Interface represents the logical NoC boundary presented to an external endpoint.
- Network Interface implementation grouping and internal transport are private generation concerns.
- Ordinary Router type selection, Router-local parameter editing, address/interleave modeling, chiplet modeling, and universal microarchitecture analysis are outside the first-stage UI scope.
- Compatibility and one-time import policy for the current early-release format is decided separately; the new design does not require permanent dual-format support.

## 3. Ubiquitous Language

The normative glossary is `CONTEXT.md` from the same repository revision as this specification. A mismatch is a specification defect that must be corrected; until corrected, this specification controls architectural behavior and `CONTEXT.md` controls canonical term names. The most important terms are:

- **System Design:** one user-owned NoC design.
- **NoC Interface:** a logical external boundary Interface with a public protocol Contract.
- **Access Slot:** a stable user-selected logical position at a Router.
- **Engine-Managed Structure:** persisted Router, Slot, and structural Link entities managed by the selected Structure Authority.
- **Domain:** a typed Router grouping; in V1 every Router belongs to exactly one Domain of every Package-declared Domain type.
- **Connected Domain:** a Domain whose Routers form one connected structural-Link subgraph.
- **Design Patch:** a revision-bound structured change validated and committed by the application.
- **Reconciliation:** deriving one candidate atomic materialization for the current Pending Topology Group from normalized topology input and current Derived State.
- **Extension Provider:** an optional external behavior extension, not the ordinary Package integration path.

## 4. Chosen Architecture

Each open System Design uses one authoritative `DesignSession` rather than synchronized mutable copies. The Session owns one evolved `ProjectDesign` aggregate constrained by the NoC Profile.

```text
Workbench UI
    ↓ Application commands
Application Layer
    ↓
DesignSession / ProjectDesign V1 / NoC Profile
    ├── Persistence port → JSON project repository
    ├── Extension port   → external Provider process
    ├── Tool port        → IP Core DRC / generator
    └── Projection       → Canvas / Navigator / Inspector / Problems

RunCoordinator
    ├── reconciliation jobs
    ├── DRC jobs
    ├── generation jobs
    └── process lifecycle/status projection
```

Source dependencies point inward:

```text
UI → Application → Domain
```

Persistence, processes, and external tools implement Application-owned ports. Domain code does not depend on Qt widgets, QtNodes/Graph projections, project-file APIs, `QProcess`, or Provider implementations.

The existing Graph and QtNodes model may be used temporarily as a projection during migration but cannot remain an authoritative design source.

## 5. DesignSession

`DesignSession` owns one `ProjectDesign` V1 aggregate plus session history and diagnostics:

```text
DesignSession
├── User-owned Design
├── Derived State
├── Session, topology-input, and derived-state revisions
├── At most one Pending Topology Group
├── Independent Draft Overlay
├── One formal authoritative command history
└── Design diagnostics
```

Transient reconciliation, DRC, generation, cancellation, and process objects belong to `RunCoordinator`, not to the authoritative aggregate. `DesignSession` exposes only the design-relevant status projection needed by Save/Validate/Generate gates.

### 5.1 User-owned Design

- Package and Contract references
- Global configuration
- NoC Interfaces
- Attachments
- Domains and memberships
- Package-defined Entities and Relations

### 5.1.1 Fixed ProjectDesign V1 NoC Profile Mapping

```text
ProjectDesign V1
├── dependencies[]
│   exact NoC, Contract, Default Engine, Runtime, Provider, and tool dependency locks
├── components[]
│   exactly one top-level NoC Component
├── interfaces[]
│   NoC boundary Interfaces owned by that Component
├── connections[]
│   empty in the V1 NoC Profile
├── topologies[]
│   exactly one TopologyDocument owned by the NoC Component
│   ├── routers[]
│   ├── structuralLinks[]
│   ├── accessSlots[]
│   ├── attachments[]
│   ├── domains[]
│   ├── domainMemberships[]
│   ├── packageEntities[]
│   └── packageRelations[]
├── views[]
│   empty/reserved in V1; all active UI state is external
└── extensions[]
    schema-governed Package-private data
```

Router, Link, Slot, Domain, and other topology-scale objects must not become top-level Components, arbitrary extension blobs, or members of a parallel NoC document. System-level Connections remain empty because the V1 Profile does not model external endpoint IPs.

### 5.2 Derived State

- Routers
- Access Slots
- Structural Links
- Derivation metadata and the topology intent revision from which Derived State was materialized

These entities are persisted and have opaque stable IDs. Users cannot freely create, delete, reconnect, or mutate the Mesh skeleton. The Package selects exactly one `structureAuthority`: `default-engine` or `extension-provider`. That Authority changes the Mesh skeleton through validated Patches; the Application always owns ID allocation and invariant enforcement.

### 5.3 IDs

IDs are opaque identity values. Coordinates, names, types, and hierarchy are separate properties. Core code must never parse semantic meaning from an ID or guess entity equivalence. Reconciliation receives current Derived State with Host IDs and returns an explicit ID-based diff: existing entities are updated/deleted by Host ID, and new Authority/Application objects use one candidate transaction-wide localRef namespace. Candidate digest covers the local-reference graph/allocation order; the Application allocates final Host IDs only during atomic commit and stores the mapping in formal history. Once deleted, later ordinary recreation receives a new ID. Package template compatibility declarations determine update versus delete-plus-create.

## 6. Default NoC Design Engine

The Default Engine provides common NoC behavior so ordinary Packages do not duplicate it.

### 6.1 Package Schema Runtime

It loads and validates declarative Package content:

The new-path NoC Package root schema is `ipcraft.noc-package.v1`; legacy `ipcraft.package.v1` is not accepted by the new reader.

- Package identity and version
- Global, Interface, and Domain configuration schemas supported by V1
- Mesh topology declaration
- Router, Link, and Access Slot templates
- NoC Interface templates
- Domain type declarations
- Package Entity and Relation schemas
- Domain visual metadata and diagnostic presentation declarations supported by V1
- DRC and generation workflows

Schema-driven forms support type, default, range, enum, unit, required/read-only state, conditional visibility, conditional enablement, and configuration scope.

Before the first stable `1.0` baseline, Package, Contract, engine, tool, and project-format compatibility may be broken without an importer. Starting at `1.0`, every project pins exact Package, Contract, Default Engine, Runtime, Provider, and IP Core Tool dependency bundle digests plus required Host/Engine contracts. Stable dependencies are never silently upgraded. V1 promises verifiable replay under the same locked bundles, runtime closure, platform ABI class, invocation/environment profiles, Host side-effect contract, and tool input; it does not claim cross-OS bit-for-bit output identity.

Default Engine is itself an immutable installable Bundle and an exact Project dependency. Its `bundleManifestDigest` is the sole implementation identity; fixed metadata type ID `ipcraft.default-noc-engine` and version are metadata and `engineCompatibilityVersion` only classifies explicit migrations. `engineHostContractVersion` versions the Engine/Host ABI. Missing, revoked, corrupt, digest-mismatched, Host-incompatible, or platform-incompatible Engine opens degraded inspect mode with no fallback to the current application Engine. Host-owned candidate side effects are separately pinned by `hostSideEffectContractVersion`; V1 Hosts may not reinterpret that behavior silently.

### 6.2 Native Mesh Topology

V1 natively supports a declarative Mesh topology. Rows, columns, templates, and limits come from the Package. The Default Engine creates and reconciles Routers, undirected structural Links, and Access Slots while preserving stable identity for surviving entities.

The three shipped simple NoC Packages must use this path without an Extension Provider.

V1 Mesh fixes origin at row `0`, column `0`, grows rows downward and columns rightward, adds only bottom/right edges, and removes highest row/column coordinates when shrinking. Router retention requires unchanged coordinate plus matching template `stableKey` and `identityCompatibilityVersion`. Each orthogonally adjacent Router pair has one undirected Structural Link; retention requires unchanged endpoint IDs, horizontal/vertical axis, template stable key, and compatibility version. Slot retention requires the same parent Router ID, slot template stable key, and compatibility version. Coordinates and template keys are ordinary properties and never encoded or parsed from opaque host IDs.

### 6.3 V1 Configuration Runtime

V1 implements only user-editable `global`, `interface`, and `domain` scopes. Supported field types are boolean, integer, double, string, and enum. Supported metadata is default, required, read-only, min/max, unit, label/description, and simple equality-based `visibleWhen` or `enabledWhen` conditions.

Router, Link, and Access Slot templates may contain Package-declared read-only properties used by topology generation and Inspector display. V1 does not implement Router/Link/Slot overrides, entity-type inheritance, multi-layer inherited/derived value chains, an arbitrary condition language, a generic overlay DSL, or a complete package-extension configuration runtime. Reserved or unsupported scopes are not interpreted as editable V1 behavior; they follow the generic-schema or opaque-extension fallback below.

Unknown business capabilities follow a fallback chain rather than causing Package rejection. If their data conforms to supported config/entity/relation schemas, the Default Engine uses the generic form, persistence, Patch, diagnostics, and read-only-summary path. Otherwise a namespaced extension is preserved and forwarded unchanged to DRC/Generator as opaque tool-managed data with no editable UI. The reader rejects only malformed core envelopes, missing required identity, duplicate IDs, invalid types for known standard fields, unsafe executable declarations, or data claiming a known capability while violating that capability's schema.

### 6.4 Interface and Attachment

V1 ships public AXI5, ACE, and CHI Interface Contract Packages. A NoC Package declares Interface templates referencing those Contracts.

```text
NoC Interface
├── stable ID
├── name
├── Contract reference and role
├── Contract capabilities and contractConfig
├── NoC Package nocConfig
└── Attachment
    ├── Router ID
    └── Access Slot ID
```

An Access Slot is a stable logical attachment resource whose selection affects generator input and observable external connection position, not an array index, canvas coordinate, or generated NI number. V1 fixes Slot capacity to one Interface. Slot templates declare allowed Contract/role, stable display position, and read-only properties. Multiple Slots may map to one generated NI, but NI implementation cardinality and grouping remain private to generation.

If a topology change removes the target, the Attachment is preserved as unresolved through an explicit Application candidate side effect. It blocks formal saving and generation until the user explicitly reattaches it or uses `DetachInterface` to clear the attachment intent without deleting the Interface. An immediate Interface capability/configuration edit that would make its existing resolved Slot illegal is rejected with an actionable instruction to Detach first.

V1 protocol support is Contract-level only. The product loads exact Contract identity/version/digest, renders and validates declared role, capability, and width fields, matches Contract/role generically to Slot declarations, persists and reopens the Interface, locates field diagnostics, and projects configuration to generation. It does not model external endpoint composition, allocate addresses, validate protocol transactions or coherence correctness, negotiate protocols at runtime, generate external IP, or model internal NoC transport. Default Engine code must not branch on AXI5, ACE, CHI, or future protocol IDs.

### 6.5 Domains

V1 supports one fixed semantic combination for every Package-declared Domain type:

```text
membership: exclusive
coverage: total
connectivity: structural-undirected
defaultAssignment: all
```

Every Router belongs to exactly one Domain of each declared type and may simultaneously belong to Domains of different types. A non-deletable Default Domain initially and automatically receives all Routers, including newly reconciled Routers. Connectivity ignores structural Link direction; a single Router is connected. V1 does not model disabled structural Links.

Domain changes use typed atomic commands rather than exposing raw membership Patches to UI: `MoveRoutersBetweenDomains`, `SplitDomain`, and `MergeDomains`. Each command must leave every affected source, target, or newly created Domain connected and fully covered; non-Default Domains may be deleted only when empty or as the source of a valid merge.

Topology reconciliation applies these side effects:

- Creating a Router atomically creates its membership in the Default Domain of every declared Domain type.
- Deleting a Router deletes its Domain memberships; membership is not preserved as unresolved because the member no longer exists.
- After any Router/Link/Slot topology materialization, the Host recomputes every Domain's structural-undirected connectivity. A disconnected remaining Domain may coexist with current Derived State, but Core Structural DRC blocks formal Save, Validate, and Generate until a typed Domain command repairs it.
- Empty Default Domains are a legal V1 special case. A non-Default Domain emptied by a user command or topology reconciliation is deleted in the same atomic causal transaction. The candidate impact report marks discarded name/configuration as data loss, so the Pending Group requires explicit candidate-digest confirmation; Undo restores its tombstone and identity.
- `SplitDomain` preserves the source Domain ID and creates one new Domain ID.
- `MergeDomains` names an explicit survivor. The Default Domain must survive when involved; survivor name/config remains and source config is not merged. UI shows an impact summary before discarding non-default source configuration.
- The wizard creates only each type's Default Domain and its initial name/configuration. Additional Domains are created through `SplitDomain` in the workbench.

### 6.6 Package Entities and Relations

Packages may define schema-governed Entity and Relation types without adding Package-specific Patch execution code. They receive stable IDs, persistence, undo, diagnostics, and either a supported generic Inspector form or a read-only opaque summary. They do not receive automatic canvas interaction or arbitrary overlay presentation and must not become unvalidated JSON bags.

When Derived-State deletion targets a user Package Relation, the Application converts the endpoint to unresolved in the same candidate only when its declaration allows it. A relation that forbids unresolved endpoints blocks the candidate; the user must discard the Group, repair/delete the relation, and retry.

### 6.7 Projection and Overlays

The Projection Layer incrementally derives Canvas, Navigator, Inspector, and Problems models from `DesignSession`. UI projections never become authoritative.

V1 presentation primitives are limited to Domain fill/border/pattern, diagnostic badges or target highlights, selection state, attachment previews, labels, and read-only Package Entity summaries. Arbitrary path, region, policy, or multi-view overlay DSLs are reserved and must be rejected if declared as supported V1 behavior.

## 7. Design Patches and History

Workbench UI and Application code use typed commands such as `ChangeGlobalConfiguration`, `CreateInterfaceFromTemplate`, `AttachInterface`, `DetachInterface`, `MoveRoutersBetweenDomains`, `SplitDomain`, and `MergeDomains`. Generic Patch is an internal validated mutation representation, not the business-command API exposed to UI.

The internal Patch algebra is:

```text
createEntity
updateEntity
deleteEntity
createRelation
updateRelation
deleteRelation
```

A Patch carries its relevant base revision/applicability, schema/protocol version, source, operations, and preconditions. A candidate transaction may contain Authority and Application sub-patches sharing transaction-local refs. Only the Application allocates opaque Host IDs in canonical localRef order during atomic commit; Engine and Provider never choose them. Updates replace named top-level properties; omitted properties remain unchanged and nested objects are replaced rather than deep-merged. Deletes never cascade implicitly. The Patch engine performs revision checks, schema/reference/ownership validation, atomic commit, notification, and inverse/tombstone/mapping capture required for formal Undo.

V1 has exactly one topology transaction model: the **Pending Topology Group**. At most one Group may be open. A topology-driving command does not immediately mutate authoritative `ProjectDesign` and does not enter formal history. It updates the Group's proposed topology intent, increments `requestGeneration`, and schedules reconciliation against the last materialized Derived State. Multiple topology edits before materialization replace or coalesce within that same Group; there is never one accepted command per intermediate topology value.

While a Group is open:

- topology-bound commands that directly reference Router, Slot, structural Link, Attachment, or Domain membership cannot be submitted;
- ordinary user-owned edits may exist only in an independent **Draft Overlay**;
- Draft Overlay entries are proposals, not accepted commands, do not modify `ProjectDesign`, do not enter formal history, and block formal Save/Validate/Generate while present;
- Draft Overlay has its own local undo/redo and is excluded from reconciliation input.

An Authority response is first validated into an immutable candidate containing the Authority Patch, Application-computed Domain/Attachment/Package-Relation side effects, tombstones, impact report, and a `candidateDigest`; it never commits directly. A legal candidate without destructive impacts commits automatically. A candidate reporting data loss enters `ready-to-commit` and requires `ConfirmPendingTopologyGroup(candidateDigest)`. An impact that cannot be made legal automatically enters `blocked` and cannot be confirmed. Editing/retrying the Group or changing dependency/Authority/base design invalidates the candidate.

Successful materialization is one atomic transaction containing the final topology intent, exact candidate Patches/side effects, deleted-object tombstones, Host ID allocations, and new derivation metadata. No observer may see an intent-only or Patch-only intermediate state. This transaction then enters the single formal undo/redo history. Undo/Redo restores or reapplies the exact transaction and tombstoned IDs without invoking ordinary derivation; every revision counter remains monotonic.

After materialization, Draft Overlay entries are revalidated in original order against the new authoritative state and presented for independent submission. Each accepted edit becomes its own formal history command. A failed draft stays in the overlay with diagnostics; acceptance of one draft does not imply acceptance of the remaining drafts. Discarding a Group and discarding Draft Overlay are separate explicit actions.

## 8. Reconciliation

Reconciliation derives one candidate transaction for the single open Pending Topology Group. It uses separate monotonic causality values:

```text
sessionRevision        increments for every accepted user command/transaction
topologyInputRevision  reserves a new value when a Group opens and on topology Undo/Redo
topologyInputDigest    digest of the group's normalized final topology intent
requestGeneration      increments whenever that open group's intent changes or retries
derivedStateRevision   increments after every materialize, Undo, or Redo affecting Derived State
derivedStateDigest     digest of all Authority-owned derived objects and properties
```

```text
User opens/updates Pending Topology Group
    ↓
Selected Structure Authority reconciliation
    ↓
Candidate Derived State transaction
```

Only one Group and one active request generation exist. A newer generation supersedes an older response without creating another Group. State has two independent dimensions:

```text
derivedStateFreshness: current | stale
jobState: idle | running | failed
groupState: drafting | running | candidate-validating | ready-to-commit | blocked | failed
```

`stale+running` means the Group is deriving from the last materialized state; `stale+failed` retains that state and the uncommitted Group for retry or discard. Failure never places topology intent in authoritative design or formal history. Recovery stores the Group and Draft Overlay as disposable interaction state.

The request/applicability tuple is exactly:

```text
(groupId, requestGeneration,
 topologyInputRevision, topologyInputDigest,
 baseDerivedStateRevision, baseDerivedStateDigest,
 baseAuthoritativeDesignDigest,
 structureAuthority identity/version/bundle digest,
 Package bundle digest, reconcileDependencySetDigest,
 Default Engine lock/bundle digest,
 Engine Host contract version,
 Host side-effect contract version)
```

`sessionRevision` is provenance only and MUST NOT be an applicability precondition. `engineCompatibilityVersion` is migration classification only and is not a replay identity. A response can become a candidate only when every tuple member still matches and the Group remains open. The response contains an explicit diff over Host IDs supplied in `currentDerivedState`; it is revalidated against current ownership, references, and invariants before candidate creation and again before atomic commit.

The reconcile payload is closed and contains only canonical `normalizedTopologyInput`, canonical `currentDerivedState`, the tuple, required dependency locks, and declared capabilities. It never contains complete ProjectDesign, Attachments, Domains, Draft Overlay, display metadata, diagnostics, Views, runs, or non-driving Interface fields. A Provider may not base reconciliation on data excluded from the tuple's digests.

Topology input contains topology-driving global configuration, Package/Authority/Engine identities, template compatibility declarations, and user-owned Package Entity/Relation data explicitly declared topology-driving. Authority-owned Entity/Relation data may not be topology-driving, preventing derived-state feedback loops. `derivedStateDigest` contains Router/structural-Link/Slot objects, every ownership=`engine` Package Entity/Relation, and all Authority-owned properties that can affect generation or later reconciliation. It excludes user intent relations and presentation state.

Both digests use the Appendix A/B normalized set ordering followed by RFC 8785 canonical JSON and SHA-256. One debounce window creates at most one generation. Retry increments `requestGeneration` but not `topologyInputRevision`. Formal Undo/Redo of a materialized topology transaction allocates a new monotonic topology-input revision as well as Session/Derived-State revisions. Reopen recomputes all fingerprints and declares freshness current only when persisted derivation metadata matches. Recovery restores topology intent but never a validated candidate.

Formal Save/Validate/Generate require no open Pending Topology Group, an empty Draft Overlay, `current` freshness, and passing Core Structural DRC. Generate additionally requires the current ProjectDesign digest to equal the last formally saved digest.

## 9. Extension Providers

Extension Providers are optional and used only when declarations and the Default Engine cannot express required behavior.

Each Package declares one structure Authority. With `default-engine`, Provider Patches cannot create/update/delete Router, structural Link, Access Slot, or engine-owned structural Package objects. With `extension-provider`, the declared Provider is the sole lifecycle owner for those objects and the Default Engine does not emit a competing structural Patch. In both modes the host allocates IDs and enforces schema, reference, Domain, Attachment, and transaction invariants.

- They run out of process.
- They may be implemented in any language.
- V1 uses one long-lived Provider process per open DesignSession and Package, with one serialized request active at a time.
- Messages use newline-delimited UTF-8 JSON with request IDs; the first message is a protocol/version/capability handshake.
- stderr is reserved for logs.
- Reconcile receives only the closed normalized topology-input/current-derived-state payload from Section 8, never a full project Snapshot.
- They return generic Patches, preview data, or structured Provider diagnostics.
- V1 Provider payloads are JSON only; binary preview or artifact payloads are not accepted through the protocol.
- The protocol does not expose project paths and contractually prohibits direct project-file access; Providers also cannot mutate `DesignSession`, inject Qt widgets, or define new core Patch executors.
- The host enforces timeout, process termination/restart for cancellation or hangs, a 16 MiB V1 maximum message size, protocol negotiation, crash handling, and Patch validation.
- Provider manifest never embeds its own bundle digest; the external dependency lock binds its manifest/path to the verified Provider bundle and runtime closure.
- Reproducible V1 Providers use a sealed Runtime Lock, fixed invocation/environment profile, locked module/library search paths, and `networkPolicy: prohibited`.
- Provider output must be deterministic for the same normalized Snapshot and declared dependency versions unless the response explicitly declares non-deterministic analysis metadata.

The host does not expose the project path and the protocol prohibits direct project-file access, but this is an architectural contract rather than an OS security boundary. Unless a future sandbox is enabled, a same-user process may still access files permitted by the operating system; the product must not claim otherwise.

## 10. DRC and Generation

### 10.1 Core Structural DRC

The product checks:

- ID uniqueness
- schema validity
- reference integrity
- required relations
- unresolved Attachments
- V1 Slot single-occupancy
- resolved Attachment Contract/role/capability compatibility with its Slot
- Domain cardinality
- connected-Domain connectivity
- Package and Contract availability
- reconciliation state

### 10.2 IP Core Semantic DRC

IP Core tools own semantic rules. The product does not define a universal executable DRC language. Tools return a versioned Diagnostic Report with stable rule IDs, severity, message, and affected Subject references.

### 10.3 IP Core Analysis

Microarchitecture analysis such as deadlock detection remains IP Core-owned because it depends on private routing and channel semantics. The product accepts its Diagnostic Reports but does not promise a universal third-party analysis model.

### 10.4 Safe Tool Execution

Semantic DRC and generation run asynchronously from immutable Snapshots through Host-controlled execution roots outside the project tree. External tools never receive project/report/archive paths. After termination, the Host validates exhaustive evidence and copies it into the project report archive. Standalone semantic DRC may target a current materialized design with no Group/Draft; Generate targets only a formally saved matching Snapshot and is one pipeline over exactly one Snapshot and dependency lock set:

```text
Core Structural DRC
    → optional IP Core semantic DRC
    → generator
    → artifact verification
    → rollback-safe promotion
```

Any blocking diagnostic, missing/stale/failed semantic DRC result, tool failure, invalid result manifest, or artifact verification failure stops the pipeline before promotion. One parent `pipelineRunId` owns ordered host/tool steps; every external process receives a distinct `invocationId`, directory, Tool Input/Result, stdout/stderr, timeout, and tool identity. Tools emit invocation-scoped stdout NDJSON progress/events, reserve stderr for raw logs, and may write `ipcraft.tool-result.v1`; exit success without a valid matching result is failure. The host always writes a normalized Step Result, including when timeout/cancellation/crash prevents a raw Tool Result. A Pipeline Result aggregates all steps and failure attribution.

```text
pipelineRunId
snapshotSessionRevision
snapshotDigest
Package/Contract/Engine/tool bundle and runtime locks
startedAt / completedAt
status
```

The parent directory retains canonical `project-design.json`, pipeline plan/result, normalized step results, and per-invocation inputs, optional raw Tool Results, diagnostics, artifact manifest, and logs. Results from an older Snapshot remain viewable as historical runs and are marked stale. They cannot replace current Problems without a stale badge and cannot promote artifacts to canonical `output/`. Only the latest eligible successful Generate pipeline for the formally saved current Snapshot digest may promote output; late completion or cancellation of an older pipeline never changes canonical output.

Promoted output is `current-canonical` only when its Snapshot digest equals both current authoritative ProjectDesign and formally saved digests, dependency locks still match, and no Pending Group/Draft Overlay exists. Otherwise it remains available as `last-successful-stale` with explicit reasons.

Generation writes to a run-specific sibling staging directory on the same supported filesystem, validates an artifact manifest, renames the previous successful output to a recoverable backup, promotes staging, and rolls back on failure before cleaning the backup. The guarantee is transactional and rollback-safe on supported filesystems rather than universally atomic across platforms, volumes, open file handles, symlinks, or abrupt power loss. Failure or cancellation preserves a recoverable previous successful output. Raw stdout/stderr enters Output; structured diagnostics enter Problems and can locate design Subjects.

## 11. Project Format and Recovery

The creation wizard produces:

```text
my-noc/
├── my-noc.nocproj
├── .workspace/
│   ├── ui-state.json
│   ├── recovery.json
│   └── project.lock
├── output/
└── reports/
```

`.nocproj` is one formatted JSON serialization of the new unified `ProjectDesign V1` under the NoC Profile. It is parsed with standard Qt JSON APIs and atomically saved with `QSaveFile`. It does not serialize QObject or Qt-private types and must not introduce a second NoC-only persistence model. The existing pre-release V1 schema is replaced rather than treated as a compatibility baseline.

The root schema ID is `ipcraft.project-design.v1` and the profile marker is `ipcraft.profile.noc` version `1`. The new reader accepts only that root for `.nocproj`; legacy `ipcraft.project.v1` roots return `project.legacy_format_unsupported` and are never inferred or imported by field shape.

Formal Save is disabled while a Pending Topology Group is open, Draft Overlay is non-empty, reconciliation is not current, or Core Structural DRC fails. This is deliberate: `.nocproj` represents a reproducible, regenerable design, not a collaborative intermediate draft. Pending intent, drafts, failures, and structurally incomplete interaction state exist only in the active session and disposable `.workspace/recovery.json`. Recovery is an interaction convenience rather than project state. The UI must display the exact Save blockers; on reopen, the user may restore and retry/discard the Group, independently submit/discard drafts, or discard all recovery and return to the last formally saved design.

Recovery follows `ipcraft.recovery.v1` in Appendix B: it stores the last authoritative design, the single open Group if any, Draft Overlay and local draft-undo data, but not formal undo history, process state, pending runs, or staging paths. It is atomically debounced, binds to the digest of the last formally saved project, restores with empty formal history, and is ignored/quarantined on digest or schema mismatch.

One host process holds an exclusive project-level mutation lock for Save, recovery, shared UI state, reports, and output promotion. A second process may open read-only but cannot write `.workspace`, `reports`, or `output`; its UI state remains memory-only. Save also uses the last formally saved canonical digest as an optimistic precondition, so unexpected external modification produces `project.concurrent_modification` rather than overwrite.

Providers and IP tools never directly read or write the project directory. Tool inputs are emitted into controlled staging by the application.

Dependency execution availability has two states, with a separate informational upgrade overlay:

- `exact`: all pinned identities, versions, and digests match; normal operation is allowed.
- `degraded-inspect`: an exact dependency/Engine/runtime/Host contract is missing, revoked, mismatched, or incompatible; the project exposes persisted data with no fallback and disables normal editing/reconciliation/Save/DRC/Generate. The only possible mutation is an explicitly supported target-Engine migration candidate that does not execute the missing source Engine.
- `upgrade-available`: informational only on an otherwise `exact` resolution; it is not a mutually exclusive availability state and never selects a digest.

Package identity and version cannot change in place. After the stable `1.0` baseline, `Clone and Migrate` creates a new project using target-Package migration steps, reconciles it, and requires Core Structural DRC before its first formal Save. Failure discards the incomplete clone and leaves the original reproducible project unchanged. Before `1.0`, old Contracts, schemas, cores, tools, and projects may be discarded without migration support.

Default Engine migration is a distinct explicit candidate transaction and may update the existing design. Normal applicability still describes the current base; separate migration provenance carries complete current and target exact locks. It runs the exact target Engine Bundle through its declared Host contract, always requires candidate confirmation, and atomically replaces the Engine dependency lock, Derived State, Host side effects, and provenance. Undo applies the saved inverse transaction and ID mapping without executing either Engine; it may return the Session to degraded inspect mode if the restored Bundle is unavailable. Different Engine digests are never selected automatically merely because compatibility versions match.

## 12. V1 UI/UX

### 12.1 Start and Creation

The start screen contains Recent Designs, New NoC Design, and Open Design. The IP Library appears only in the creation wizard and developer mode.

Wizard steps:

1. Choose one of the available NoC Packages.
2. Choose design name and project directory.
3. Configure global Mesh parameters.
4. Establish required initial Domains.
5. Review and create.

The selected Package cannot be switched in place after creation.

### 12.2 Workbench

```text
┌──────────────────────────────────────────────────────┐
│ Project / Topology       Configure Validate Generate │
├──────────────┬────────────────────────┬───────────────┤
│ Navigator    │ Mesh Canvas            │ Inspector     │
│ Add Interface│                        │               │
│ Interfaces   │ Router / Slot          │ Selected item │
│ Unattached   │ Interface attachment   │ configuration │
│ Domains      │ Domain overlays        │               │
├──────────────┴────────────────────────┴───────────────┤
│ Problems | Output | Reconciliation Status            │
└──────────────────────────────────────────────────────┘
```

The bottom area is collapsed by default. Problems opens for user-actionable failures; raw logs do not replace structured diagnostics.

### 12.3 Interface Interaction

- Drag AXI5, ACE, or CHI templates from the left pane to a Router.
- One legal Slot attaches immediately.
- Multiple legal Slots open an anchored Slot picker.
- Disabled Slots remain visible with reasons.
- Canceling a new attachment leaves the Interface Unattached.
- Canceling reattachment preserves the existing Attachment.
- Equivalent keyboard operations are required.

### 12.4 Domain Interaction

Connected Domains are edited by selecting a Domain, choosing a seed Router, and growing or shrinking along structural adjacency. Eligible boundary Routers are highlighted, but the resulting action is committed only through an atomic `MoveRoutersBetweenDomains`, `SplitDomain`, or `MergeDomains` command. Operations that would leave either side disconnected or violate total coverage are prevented with an explanation.

### 12.5 Global Configuration and Draft Overlay

Creation uses the wizard. Later edits use a dedicated configuration surface. With no open Group, Apply validates and submits one typed command. A topology-driving Apply opens or updates the single Pending Topology Group. While it remains open, ordinary configuration/name/interface edits are stored only in Draft Overlay; Apply queues a draft proposal rather than an accepted command. Draft Undo/Redo is local, Escape removes the active draft gesture, and the UI clearly distinguishes draft values from accepted values. After materialization, drafts are revalidated and submitted independently. Automatic-update preference belongs to workspace state, not the design file.

When a candidate has destructive impacts, the workbench shows the immutable impact report and enables Confirm only for its displayed candidate digest. A blocked candidate explains the user-owned Relation or invariant that prevents confirmation and offers Discard; changing topology input invalidates the panel immediately. Non-destructive candidates commit without an extra click.

### 12.6 Accessibility and Feedback

- Drag-and-drop actions have keyboard equivalents.
- Domain identity is not represented by color alone.
- Illegal drops and disabled actions show causes.
- Empty states explain the next valid action.
- Incomplete controls are not exposed.
- Selection and viewport survive incremental projection updates.

The V1 layout may evolve later; stable domain semantics and Application commands must not depend on the current pane arrangement.

## 13. Developer Mode

Developer tools are selected at process startup, for example `--developer` or an environment option. Ordinary mode does not register Package Inspector, raw capability browsers, schema viewers, or plugin debugging actions. Production still records support bundles, crash logs, Package/version information, and user-requested raw tool Output; developer mode changes visibility and tooling, not the existence of supportable diagnostics. Developer mode uses a clearly marked layout and must not leak framework concepts into the normal workbench.

## 14. Delivery Sequence

1. Gate 0 produces machine-readable Core/Appendix F schemas, golden vectors, error catalog, normalization/projection/localRef rules, exact Default Engine/Host/side-effect contracts, exhaustive Bundle/Runtime/Tool contracts, and freezes them in `CORE-FREEZE.md`.
2. Split new and legacy build libraries, then implement one headless `ProjectDesign V1`, `DesignSession`, JSON repository, Core Structural DRC, Patch engine, Pending Topology Group, Draft Overlay, and unified formal history.
3. Implement the minimal Default Engine Mesh slice and adapt one simple NoC Package declaratively.
4. Complete the Gate B/C headless flow including pending-group races, exact causal Undo, formal Save, tool pipeline, and output provenance.
5. Implement the Extension Provider protocol and single structure-Authority mode required for `RAMCS-V1`.
6. Run `RAMCS-V1`, close all generic Extension gaps, and freeze Provider/Extension ABI in `EXTENSION-FREEZE.md`.
7. Implement the creation wizard, three-pane workbench, Slot picker, Domain commands, Problems, Draft Overlay, and projection over the frozen headless flow.
8. Migrate the remaining two simple NoC Packages; all three remain Provider-free.
9. Harden recovery, run concurrency, Bundle/runtime verification, rollback-safe output promotion, project locking, performance, and support logs.
10. Run blinded complex-IP acceptance without core changes.
11. Hard-cut the legacy workbench, old split-state services, Graph authority, second undo path, and superseded save path.

An internal-only launch option may select the new workbench during development. It is removed at hard cutover rather than becoming a supported dual product mode.

## 15. Verification

Required tests include:

- Domain tests for IDs, references, Patches, ownership, undo, and Domain connectivity.
- Property tests over randomized Patch sequences and revision conflicts.
- `.nocproj` round-trip and atomic-save failure tests.
- Reconciliation tests for stale results, timeout, failure, recovery, and identity preservation.
- Contract tests proving all three simple NoC Packages load without core-specific branches or Providers.
- UI tests for wizard, Interface drag/drop, Slot picker, Domain editing, disabled Save/Generate, and recovery.
- Provider protocol tests for crash, malformed messages, excessive payloads, invalid Patch, old revision, and cancellation.
- Tool tests for staging isolation, structured diagnostics, cancellation, and preservation of previous outputs.

## 16. Confidential Complex-IP Spike and Hidden Acceptance

After Gate 0 Core freeze, an architecture owner performs the confidential **Restricted Advanced Mesh Capability Spike V1 (`RAMCS-V1`)** covering Extension-Provider structure Authority, identity preservation, engine-owned Package Entities/Relations, closed reconcile payload, timeout/retry, diagnostics, and the saved-Snapshot generation pipeline. Reference names, fields, rules, and fixtures do not enter public implementation guidance; only neutral capability requirements and tests are published. Core gaps require a Core unfreeze ADR; Extension ABI may change until Gate D exits.

After `RAMCS-V1` meets every Appendix E exit criterion, Extension ABI is frozen and a separate full hidden acceptance Package is introduced. It must use the Default Engine for common capabilities and an Extension Provider only for genuinely exceptional behavior.

Acceptance rules:

- Adaptation does not add Package-specific branches to the core.
- No core logic checks the acceptance Package ID or private field names.
- Provider and tool code remain outside the core.
- Removing the acceptance Package leaves all core and public-Package tests passing.
- Package IDs, instance IDs, field ordering, and selected test inputs may be randomized to deter fixture hardcoding.
- A genuine frozen-contract gap fails acceptance and requires a formal ADR to unfreeze, revise, and repeat the freeze; it cannot be bypassed by a Package-specific core exception.

## 17. Explicitly Deferred

- Multiple NoCs in one System Design
- Full external endpoint IP modeling and generation
- External Interface Template catalogs and compatibility browsing
- In-place Package switching
- User editing of the Mesh skeleton
- User selection of Router types
- V1 Router-local configuration UI
- Address/interleave domain model and specialized UI
- Chiplet-specific modeling
- Universal executable DRC rule language
- Universal third-party deadlock analysis
- Arbitrary Qt UI injection by Packages
- Backward compatibility with the current project format

## 18. Final Architectural Invariants

- Each open System Design has exactly one authoritative `DesignSession` owning one `ProjectDesign` aggregate.
- At most one Pending Topology Group exists; topology intent becomes authoritative only with its Derived Patch and invariant side effects in one atomic transaction.
- Authority responses become digest-identified Topology Candidates; destructive candidates require exact confirmation and blocked candidates cannot commit.
- Draft Overlay never becomes authoritative, formal history, save content, or reconciliation input.
- No second NoC-only project IR or persistence model exists.
- Exactly one formal authoritative command history exists; Draft Overlay and open-Group local undo stacks are non-authoritative interaction state.
- UI and projections never mutate design state directly.
- Router, Slot, and Link identity is stable and opaque.
- Ordinary Packages are declarative and use Default Engine capabilities.
- Default Engine is an exact immutable installable dependency; digest mismatch or unsupported Engine/Host/side-effect contract never falls back silently.
- Extension Providers cannot bypass Patch validation; their protocol does not expose project paths and contractually prohibits direct project-file access, without claiming an OS sandbox.
- Core code does not branch on Package identity.
- Formal Save and semantic DRC require no pending Group/drafts and a current reconciled design; Generate additionally requires the formally saved digest to match.
- Failed tools and Providers cannot corrupt the last valid project or successful output.
- Every external Tool Invocation is isolated inside one parent Pipeline Run and has an independently attributable normalized result.

## 19. Implementation Execution Contract

This section is normative for implementation agents. An agent must not infer broader scope from reserved names or future-facing interfaces.

### 19.1 General Rules

- An implementation MUST satisfy one numbered delivery gate before starting the next.
- An implementation MUST NOT add a second project model, save path, undo manager, topology authority, or Package-ID branch.
- During Gates A–F, this prohibition applies strictly inside the new startup path. Frozen legacy source may remain buildable behind a mutually exclusive internal startup option, but the new composition root MUST NOT instantiate, synchronize through, or adapt any legacy authority or save path.
- Workbench UI MUST call typed Application commands and MUST NOT construct generic Patch operations.
- Generic Patch MUST remain internal and all-or-nothing.
- Malformed known core fields or declarations that claim a supported capability while violating its schema MUST fail explicitly. Unknown namespaced business capabilities MUST follow the generic-schema or opaque-extension fallback and MUST NOT be silently treated as supported editable behavior.
- Reserved features MUST NOT be implemented without a later approved specification.
- Existing behavior outside the active vertical slice MUST NOT be refactored merely for stylistic consistency.

### 19.2 Gate 0 — Core Contract Freeze

Before implementation coding, produce and freeze the machine-readable Core schemas, canonical normalization/golden vectors, error catalog, Pending Group/Topology Candidate/Draft Overlay state machine, Bundle/Runtime Closure contract, Tool/Step/Pipeline Result contracts, project-lock/output-freshness contract, complete normative review bundle, and build-boundary map required by Appendix E. Provider/Extension ABI remains unfrozen until Gate D.

### 19.3 Gate A — Project Foundation

Required output:

- one `ProjectDesign V1` schema and in-memory aggregate matching Section 5.1.1;
- one JSON reader/writer using atomic file replacement;
- one `DesignSession` and one formal authoritative command history;
- typed commands compiling to validated internal Patches;
- dependency locks and pre-1.0 compatibility policy.

Gate tests MUST prove schema rejection, JSON round trip, Patch atomicity, ownership rejection, undo/redo, and absence of a second NoC document.

### 19.4 Gate B — Headless Mesh Slice

Required scenarios:

1. Create a 2×2 Mesh.
2. Resize 2×2 to 2×3; existing Router/Link/Slot IDs are explicitly updated and retained where applicable, new objects receive new IDs.
3. Resize 3×3 to 2×2 and back; deleted objects receive new IDs when recreated.
4. Create an Interface, attach it to a Slot, remove the target through topology change, and produce unresolved intent without migration.
5. Execute Domain move, split, and merge while preserving total exclusive undirected connectivity.
6. Save only a current structurally valid design and reopen it identically.
7. Coalesce consecutive topology edits into one Pending Group, reject superseded generations/candidates, and create one exact causal history transaction only at materialization.
8. Auto-commit non-destructive candidates, confirm destructive candidates by digest, block illegal user-Relation impacts, and never recover a persisted candidate.
9. Keep ordinary pending-period edits in Draft Overlay with local undo and independent post-materialization validation/submission.

No Qt Widget is allowed in this gate.

### 19.5 Gate C — First Package and Tool Flow

One shipped NoC Package MUST be fully declarative and MUST NOT use an Extension Provider. A current Snapshot MUST pass through parent Pipeline staging with isolated semantic-DRC/Generator invocations, normalized step results, and one aggregate pipeline result; a failed pipeline MUST preserve the previous successful output.

### 19.6 Gate D — Extension ABI and RAMCS-V1

The architecture owner runs `RAMCS-V1` and records the exact Appendix E evidence. Public implementation agents receive only generic failing capability tests and neutral contracts. Extension ABI freezes only after every public Provider test and restricted exit criterion passes. UI implementation MUST NOT start before this gate passes.

### 19.7 Gate E — Workbench

The wizard, three-pane workbench, Interface template drag/drop, Slot picker, Domain typed commands, Inspector, Problems, keyboard equivalents, and reconciliation status MUST operate exclusively on the Gate C headless use cases.

### 19.8 Gate F — Public Packages and Hardening

All three shipped simple NoC Packages MUST use the Default Engine without Providers. Run revision ownership, stale diagnostics, cancellation, recovery, support output, and rollback-safe promotion MUST pass deterministic tests.

### 19.9 Gate G — Blinded Acceptance and Cutover

The hidden Package MUST adapt without core changes or Package-specific branches. Only after this passes may the implementation remove legacy Graph authority, split-state synchronization, the second command path, and the old save path.

### 19.10 Legacy Isolation Rules

- The legacy and new composition roots MUST be selected only at process startup and MUST be mutually exclusive.
- New-path code MUST NOT include or link stateful legacy authority types such as `ProjectDocument`, `ProjectService`, `ProjectDesignSerializer`, legacy Graph authority, old `CommandManager`, or the old `.fpproj` writer.
- Legacy code is frozen: only build fixes and critical crash fixes are allowed; new product behavior belongs exclusively to the new path.
- Shared code MUST be stateless or an explicitly versioned boundary utility. Converting new `ProjectDesign V1` into legacy records to reuse a Service is forbidden.
- Static architecture tests MUST scan the new path for prohibited includes and link dependencies.
