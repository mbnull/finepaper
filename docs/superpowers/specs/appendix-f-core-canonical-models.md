# Appendix F — Gate 0 Core Canonical Models and Projections

**Normative status:** V1 Revision 4 candidate; these Core models freeze at Gate 0.
**Rule:** Engine Host and Extension Provider envelopes may wrap these models but may not redefine their fields, identity, projection, or canonicalization.

Machine-readable companion artifacts:

- [Core canonical models schema](../../contracts/schemas/ipcraft.core-canonical-models.v1.schema.json)
- [Default Engine Bundle schema](../../contracts/schemas/ipcraft.engine-bundle.v1.schema.json)
- [Core projection/digest vectors](../../contracts/vectors/core-canonical-projection-v1.json)

## F1. Core Schema IDs

```text
ipcraft.topology-intent.v1
ipcraft.normalized-topology-input.v1
ipcraft.derived-state.v1
ipcraft.reconcile-applicability.v1
ipcraft.patch-body.v1
ipcraft.topology-impact-report.v1
ipcraft.candidate-transaction.v1
ipcraft.pipeline-plan.v1
ipcraft.output-manifest.v1
ipcraft.noc-side-effects.v1
```

Machine-readable Gate 0 schemas use these exact IDs or `$defs` with identical names and constraints.

## F2. Canonical Scalar and Reference Rules

- JSON is UTF-8, duplicate-key-free, and normalized with RFC 8785 after the array/set projections in F10.
- Digests are `sha256:` plus lowercase SHA-256 hex of canonical bytes.
- IDs are opaque non-empty strings.
- Host references are `{ "id": "host-id" }`.
- Candidate transaction-local references are `{ "localRef": "authority:r0" }` or `{ "localRef": "application:000001" }`.
- Authority localRefs start `authority:` and are unique within one transaction. Host side effects use `application:` followed by a zero-padded decimal sequence allocated in F11 order.
- A reference object contains exactly one of `id` or `localRef`.
- Random final Host IDs never enter candidate digest input.

## F3. Topology Intent

`ipcraft.topology-intent.v1` is recovery/internal user intent, not persisted as a second ProjectDesign:

```json
{
  "schema": "ipcraft.topology-intent.v1",
  "componentId": "component.noc",
  "topologyId": "topology.main",
  "topologyKind": "mesh",
  "globalConfig": {},
  "packageEntities": [],
  "packageRelations": []
}
```

Projection from authoritative ProjectDesign:

1. Copy only global fields whose Package declaration has `topologyDriving: true` into `globalConfig`, keyed by field key.
2. Copy only ownership=`user`, topology-driving Package Entities as `{id,typeKey,data}`.
3. Copy only ownership=`user`, topology-driving Package Relations as `{id,typeKey,sources,targets,data}` using Appendix A endpoint envelopes.
4. Omit display names, Interfaces, Attachments, Domains, Views, extensions, non-driving fields, diagnostics, and runs.
5. A Pending Group applies its latest topology-driving command proposals over this projection without mutating ProjectDesign.

## F4. Normalized Topology Input

```json
{
  "schema": "ipcraft.normalized-topology-input.v1",
  "intent": {},
  "mesh": {
    "rows": 2,
    "columns": 2,
    "routerTemplate": {
      "stableKey": "mesh-router",
      "identityCompatibilityVersion": 1,
      "properties": {}
    },
    "linkTemplate": {
      "stableKey": "mesh-link",
      "identityCompatibilityVersion": 1,
      "properties": {}
    },
    "slotTemplates": []
  },
  "dependencyContext": {
    "nocPackageLockId": "dep.noc",
    "nocPackageBundleDigest": "sha256:...",
    "defaultEngineLockId": "dep.default-engine",
    "defaultEngineBundleDigest": "sha256:...",
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
    "structureAuthority": {},
    "reconcileDependencySetDigest": "sha256:..."
  }
}
```

`intent` is the complete F3 object. `rows` and `columns` are resolved effective values of the Package-declared row/column fields. Templates are fully materialized schema-declared values; no filesystem paths, Package names, UI metadata, or undeclared defaults remain. `topologyInputDigest` is the digest of this complete object.

## F5. Derived State

```json
{
  "schema": "ipcraft.derived-state.v1",
  "topologyId": "topology.main",
  "routers": [],
  "structuralLinks": [],
  "accessSlots": [],
  "packageEntities": [],
  "packageRelations": []
}
```

- Router/Link/Slot shapes are Appendix A core objects.
- Only ownership=`engine` Package Entities/Relations are included.
- User Attachments, Domains/Memberships, user Package objects, topology intent, derivation metadata, UI state, and extensions are excluded.
- Authority-owned values must be schema-declared `properties` or engine-owned Package objects; Router/Link/Slot have no opaque extension fields.
- `derivedStateDigest` is the digest of this complete object.

## F6. Reconcile Applicability

```json
{
  "schema": "ipcraft.reconcile-applicability.v1",
  "groupId": "group-id",
  "requestGeneration": 3,
  "topologyInputRevision": 4,
  "topologyInputDigest": "sha256:...",
  "baseDerivedStateRevision": 7,
  "baseDerivedStateDigest": "sha256:...",
  "baseAuthoritativeDesignDigest": "sha256:...",
  "structureAuthority": {
    "kind": "default-engine",
    "lockId": "dep.default-engine",
    "identity": "ipcraft.default-noc-engine",
    "version": "1.0.0",
    "bundleDigest": "sha256:..."
  },
  "packageBundleDigest": "sha256:...",
  "reconcileDependencySetDigest": "sha256:...",
  "defaultEngineLockId": "dep.default-engine",
  "defaultEngineBundleDigest": "sha256:...",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1"
}
```

Every field participates in applicability equality and candidate digest. `identity`/`version` are retained provenance metadata; digest/contract fields control equality. Session revision and Engine compatibility version are excluded.

## F7. Patch Body and Candidate Transaction

An Engine/Provider returns `ipcraft.patch-body.v1`:

```json
{
  "schema": "ipcraft.patch-body.v1",
  "applicability": {},
  "operations": []
}
```

The Host constructs source-attributed `ipcraft.patch.v1` sub-patches inside one `ipcraft.candidate-transaction.v1`:

```json
{
  "schema": "ipcraft.candidate-transaction.v1",
  "transactionId": "transaction-id",
  "kind": "topology-materialization",
  "applicability": {},
  "topologyIntent": {},
  "authorityPatch": {},
  "applicationPatch": {},
  "tombstones": [],
  "allocationOrder": [],
  "impactReport": {},
  "candidateDigest": "sha256:..."
}
```

Allowed `kind`: `topology-materialization`, `default-engine-migration`.

- `authorityPatch` source is the exact selected Authority lock.
- `applicationPatch` source is `application-reconcile` or `application-migration` and contains only F11 side effects plus, for Engine migration, exact dependency/derivation replacement.
- Operations retain semantic order within each sub-patch. Candidate combined order is Authority operations followed by Application operations.
- `allocationOrder` is every create localRef sorted by Unicode scalar-value order. It is derived, included for audit, and must equal the creates in the two patches.
- Tombstones sort by `(subjectKind,id)` and contain complete inverse values.
- Candidate digest is computed over the entire normalized object with `candidateDigest` omitted and with impact presentation messages absent.
- Commit allocates Host IDs in `allocationOrder`, rewrites all references, persists the mapping in formal history, and publishes no IDs on reject/discard.

## F8. Impact Report

```json
{
  "schema": "ipcraft.topology-impact-report.v1",
  "impacts": [
    {
      "code": "domain.non_default_deleted",
      "severity": "warning",
      "dataLoss": true,
      "subjects": [{ "kind": "domain", "id": "domain-id" }],
      "details": { "discardedConfig": true },
      "resolution": "confirm-or-discard"
    }
  ]
}
```

Impact code, severity, dataLoss, Subjects, details, and resolution participate in candidate digest. Localized title/message/help URLs do not exist in this object and are derived by the UI from `code`.

Stable V1 impact codes:

```text
attachment.target_removed
domain.non_default_deleted
domain.disconnected
package_relation.endpoint_unresolved
package_relation.endpoint_blocks_candidate
engine_migration.dependency_replaced
```

## F9. Pipeline Plan and Output Manifest

`ipcraft.pipeline-plan.v1`:

```json
{
  "schema": "ipcraft.pipeline-plan.v1",
  "pipelineRunId": "pipeline-run-id",
  "kind": "generate",
  "snapshotSessionRevision": 12,
  "snapshotDigest": "sha256:...",
  "formallySavedProjectDigest": "sha256:...",
  "dependencySetDigest": "sha256:...",
  "defaultEngineBundleDigest": "sha256:...",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "steps": [
    { "stepId": "structural-drc", "kind": "host" },
    { "stepId": "semantic-drc", "kind": "external-tool", "toolLockId": "dep.tool.drc" },
    { "stepId": "generator", "kind": "external-tool", "toolLockId": "dep.tool.generator" },
    { "stepId": "artifact-verify", "kind": "host" },
    { "stepId": "promotion", "kind": "host" }
  ]
}
```

`steps` is ordered and must match the fixed Pipeline kind. Standalone Validate contains only structural and optional semantic DRC.

`ipcraft.output-manifest.v1`:

```json
{
  "schema": "ipcraft.output-manifest.v1",
  "pipelineRunId": "pipeline-run-id",
  "snapshotDigest": "sha256:...",
  "dependencySetDigest": "sha256:...",
  "defaultEngineBundleDigest": "sha256:...",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "artifactManifestDigest": "sha256:...",
  "completedAt": "2026-07-14T00:00:00Z"
}
```

Freshness is computed against all exact fields plus current authoritative/saved design and Group/Draft state; it is not stored in this manifest.

## F10. Canonical Set/Sequence Table

| Location | Semantics | Canonical rule |
|---|---|---|
| Project `dependencies` | set | `lockId` |
| `components`, `interfaces`, `connections`, `topologies`, `views` | set | `id` |
| every topology entity/relation array | set | `id` |
| Package/Contract roles, fields, capabilities | set | `key` |
| Interface templates | set | `key` |
| Router/Link/Slot templates | set | `stableKey` |
| Domain types | set | `key` |
| Package entity/relation type declarations | set | `typeKey` |
| Slot `allowedContracts` | set | `(contractId,version,bundleManifestDigest)` in Package; `contractLockId` in Project |
| every `roles[]` | set | Unicode scalar-value string order; duplicates invalid |
| Relation `sources`/`targets` | set | resolved before unresolved, then `(subject.kind,subject.id)` or `(intendedSubject.kind,intendedSubject.id,reasonCode)` |
| SubjectRef lists | set unless explicitly diagnostic emission order | `(kind,id)` |
| `extensions[]` | set | unique `(ownerLockId,schema,version)` |
| Bundle files/artifacts | set | normalized `path` |
| Engine `supportedPlatformAbis`, `migrationFromCompatibilityVersions` | set | Unicode scalar-value string order; duplicates invalid |
| Provider/Tool capability declarations | set | Unicode scalar-value string order |
| Pipeline `steps` | ordered | declared order retained |
| Patch `operations` | ordered | declared order retained |
| command/draft/local undo records | ordered | sequence retained |
| candidate `impacts` | set | `(code, canonical subjects, canonical details, resolution)` |
| candidate `tombstones` | set | `(subjectKind,id)` |
| candidate `allocationOrder` | derived ordered | `localRef` Unicode scalar-value order |
| diagnostic array | ordered | producer emission order retained; sequence required for streamed diagnostics |

Every set key is unique; collisions are schema errors. For any set not named above, the owning schema MUST add a key before Gate 0 freeze rather than infer one. Undirected Link endpoints are normalized with the smaller Router reference first; localRefs compare by their literal string.

## F11. Host Side-effect Contract V1

`ipcraft.noc-side-effects.v1` is stable for all V1 Hosts:

1. Process Authority operations in order and build the candidate local-reference graph.
2. For every created Router, create one Membership in each Domain type's Default Domain. Sort by `(domainTypeKey, routerRef)` and assign `application:000001...` localRefs in that order.
3. For every deleted Router, delete its Domain Memberships; for every deleted Router/Slot, convert affected Attachments to unresolved with `attachment.target_removed`.
4. For each user Package Relation endpoint targeting a deleted object: convert to unresolved and add `package_relation.endpoint_unresolved` when allowed; otherwise add `package_relation.endpoint_blocks_candidate` and mark candidate blocked.
5. After proposed Router/Link changes, recompute every Domain's structural-undirected connectivity. Disconnection adds blocking Core DRC/impact regardless of whether the cause was Router deletion, Link deletion/change, or membership placement.
6. Delete any empty non-Default Domain, tombstone its full value, and add data-loss impact `domain.non_default_deleted`. Empty Default Domain remains legal.
7. Attachment/Package-Relation conversions, Membership creates/deletes, Domain cleanup, impacts, and Application localRefs are deterministic functions of the frozen canonical inputs; Packages/Engines cannot override them in V1.
8. A different Host side-effect contract version is incompatible for replay. Migration must explicitly replace provenance through a confirmed candidate.

## F12. Default Engine Migration Candidate

Engine migration uses `candidate-transaction.kind: default-engine-migration` and always requires confirmation. Applicability includes both current and target Engine locks:

```json
{
  "defaultEngineLockId": "dep.default-engine",
  "currentDefaultEngineBundleDigest": "sha256:...",
  "targetDefaultEngineBundleDigest": "sha256:...",
  "targetEngineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1"
}
```

The dependency `lockId` remains stable while its exact Engine metadata/digest is replaced. The target Engine returns a Patch body against current Derived State. The Host candidate includes an `application-migration` update of the exact dependency array and derivation provenance plus normal F11 side effects. Commit is atomic. Formal Undo/Redo uses stored forward/inverse Patches and localRef→Host-ID mappings without loading either Engine.

## F13. Golden Projection Vector Requirements

Gate 0 vectors MUST include at least:

1. Project arrays randomly permuted but producing identical Topology Intent, Derived State, and digests.
2. Slot allowed-contract/role order permutations producing identical digest.
3. Package Relation endpoint permutations and resolved→unresolved side effects.
4. 2×2→2×3 Authority creates with Host Default-Membership localRefs and deterministic allocation order.
5. Destructive Domain cleanup candidate whose localized UI language changes without changing candidate digest.
6. Provider and Default Engine Patch bodies producing the same Host-constructed Patch/candidate form.
7. Engine migration candidate/Undo restoring exact dependency lock, Derived State, provenance, and Host IDs.
8. Output freshness changes for accepted-unsaved edit, Pending Group, Draft Overlay, Engine digest mismatch, Host-side-effect contract mismatch, and saved/current equality.
