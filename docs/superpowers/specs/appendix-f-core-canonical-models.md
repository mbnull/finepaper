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
- Persisted/Core Structural Link `endpointA` and `endpointB` are string Host IDs rather than reference envelopes; for comparison each is treated as `id:` + ID.
- Authority localRefs start `authority:` and are unique within one transaction. Host side effects use `application:` followed by a zero-padded decimal sequence allocated in F11 order.
- A reference object contains exactly one of `id` or `localRef`.
- The object-reference comparison token is the UTF-8 string `id:` + X for `{id:X}` and `localRef:` + X for `{localRef:X}`. Tokens compare by Unicode scalar-value order. Every `routerRef` or other object reference in a sort key uses this token.
- Composite canonical values compare by the UTF-8 bytes of their RFC 8785 canonical JSON after recursively applying this Appendix's nested set normalization.
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

- `authorityPatch` source is the exact selected Authority lock and includes its exact bundle digest for both `default-engine` and `extension-provider`.
- `applicationPatch` source is `application-reconcile` or `application-migration`, omits Authority bundle digest, and contains only F11 side effects plus, for Engine migration, exact dependency/derivation replacement.
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

## F10. Canonical Collection Rules

Every schema array carries `x-ipcraft-canonical`. Its value has one consistent shape:

```json
{ "kind": "set", "sortKey": ["id"] }
{ "kind": "ordered" }
{ "kind": "derived-ordered", "sortKey": ["unicodeScalarValue"] }
```

`sortKey` members name the comparison components below; `canonicalJson` and `*CanonicalJson` mean UTF-8 RFC 8785 canonical JSON bytes after nested normalization, `unicodeScalarValue` means literal string order, and `objectRefToken` means F2's token. `state-order` places resolved before unresolved. `persistedEndpointCanonicalKey` is resolved `(state-order, subject.kind, id:<subject.id>)` or unresolved `(state-order, intendedSubject.kind, id:<intendedSubject.id>, reasonCode)`. `patchEndpointCanonicalKey` is resolved `(state-order, subject.kind, objectRefToken(subject.ref))` or unresolved `(state-order, intendedSubject.kind, objectRefToken(intendedSubject.ref), reasonCode)`. The companion vector's `canonicalCollections` array is the machine-readable copy of this table.

| Exact document path | Kind | Sort key / retained meaning |
|---|---|---|
| `projectDesign.dependencies` | set | `lockId` |
| `projectDesign.dependencies[].supportedPlatformAbis` | set | `unicodeScalarValue` |
| `projectDesign.components` | set | `id` |
| `projectDesign.components[].extensions` | set | `ownerLockId,schema,version` |
| `projectDesign.interfaces` | set | `id` |
| `projectDesign.interfaces[].extensions` | set | `ownerLockId,schema,version` |
| `projectDesign.interfaces[].capabilities.*[]` | set | `canonicalJson` |
| `projectDesign.connections` | set | `id` |
| `projectDesign.topologies` | set | `id` |
| `projectDesign.topologies[].routers` | set | `id` |
| `projectDesign.topologies[].structuralLinks` | set | `id` |
| `projectDesign.topologies[].accessSlots` | set | `id` |
| `projectDesign.topologies[].accessSlots[].allowedContracts` | set | `contractLockId` |
| `projectDesign.topologies[].accessSlots[].allowedContracts[].roles` | set | `unicodeScalarValue` |
| `projectDesign.topologies[].accessSlots[].allowedContracts[].capabilityConstraints.*[]` | set | `canonicalJson` |
| `projectDesign.topologies[].attachments` | set | `id` |
| `projectDesign.topologies[].domains` | set | `id` |
| `projectDesign.topologies[].domainMemberships` | set | `id` |
| `projectDesign.topologies[].packageEntities` | set | `id` |
| `projectDesign.topologies[].packageEntities[].extensions` | set | `ownerLockId,schema,version` |
| `projectDesign.topologies[].packageRelations` | set | `id` |
| `projectDesign.topologies[].packageRelations[].sources` | set | `persistedEndpointCanonicalKey` |
| `projectDesign.topologies[].packageRelations[].targets` | set | `persistedEndpointCanonicalKey` |
| `projectDesign.topologies[].packageRelations[].extensions` | set | `ownerLockId,schema,version` |
| `projectDesign.topologies[].extensions` | set | `ownerLockId,schema,version` |
| `projectDesign.views` | set | `id` |
| `projectDesign.extensions` | set | `ownerLockId,schema,version` |
| `topologyIntent.packageEntities` | set | `id` |
| `topologyIntent.packageRelations` | set | `id` |
| `topologyIntent.packageRelations[].sources` | set | `persistedEndpointCanonicalKey` |
| `topologyIntent.packageRelations[].targets` | set | `persistedEndpointCanonicalKey` |
| `normalizedTopologyInput.mesh.slotTemplates` | set | `stableKey` |
| `normalizedTopologyInput.mesh.slotTemplates[].allowedContracts` | set | `contractId,version,bundleManifestDigest` |
| `normalizedTopologyInput.mesh.slotTemplates[].allowedContracts[].roles` | set | `unicodeScalarValue` |
| `normalizedTopologyInput.mesh.slotTemplates[].allowedContracts[].capabilityConstraints.*[]` | set | `canonicalJson` |
| `derivedState.routers` | set | `id` |
| `derivedState.structuralLinks` | set | `id` |
| `derivedState.accessSlots` | set | `id` |
| `derivedState.accessSlots[].allowedContracts` | set | `contractLockId` |
| `derivedState.accessSlots[].allowedContracts[].roles` | set | `unicodeScalarValue` |
| `derivedState.accessSlots[].allowedContracts[].capabilityConstraints.*[]` | set | `canonicalJson` |
| `derivedState.packageEntities` | set | `id` |
| `derivedState.packageRelations` | set | `id` |
| `derivedState.packageRelations[].sources` | set | `persistedEndpointCanonicalKey` |
| `derivedState.packageRelations[].targets` | set | `persistedEndpointCanonicalKey` |
| `patch.preconditions` | set | `canonicalJson` |
| `patch.operations` | ordered | operation sequence retained |
| `patchBody.operations` | ordered | operation sequence retained |
| `patchOperations[].unset` | set | `unicodeScalarValue` |
| `patchOperations[].value.allowedContracts` | set | `contractLockId` |
| `patchOperations[].value.allowedContracts[].roles` | set | `unicodeScalarValue` |
| `patchOperations[].value.allowedContracts[].capabilityConstraints.*[]` | set | `canonicalJson` |
| `patchOperations[].value.capabilities.*[]` | set | `canonicalJson` |
| `patchOperations[].value.sources` | set | `patchEndpointCanonicalKey` |
| `patchOperations[].value.targets` | set | `patchEndpointCanonicalKey` |
| `patchOperations[].value.extensions` | set | `ownerLockId,schema,version` |
| `candidateTransaction.authorityPatch.operations` | ordered | operation sequence retained |
| `candidateTransaction.applicationPatch.operations` | ordered | operation sequence retained |
| `candidateTransaction.tombstones` | set | `subjectKind,id` |
| `candidateTransaction.allocationOrder` | derived-ordered | `unicodeScalarValue` |
| `topologyImpactReport.impacts` | set | `code,subjectsCanonicalJson,detailsCanonicalJson,resolution` |
| `topologyImpactReport.impacts[].subjects` | set | `kind,id` |
| `pipelinePlan.steps` | ordered | declared pipeline order retained |
| `nocPackage.interfaceTemplates` | set | `key` |
| `nocPackage.domainTypes` | set | `key` |
| `nocPackage.packageEntityTypes` | set | `typeKey` |
| `nocPackage.packageRelationTypes` | set | `typeKey` |
| `nocPackage.extensions` | set | `ownerLockId,schema,version` |
| `nocPackage.configuration.global.fields` | set | `key` |
| `nocPackage.configuration.global.fields[].values` | set | `canonicalJson` |
| `nocPackage.topology.slotTemplates` | set | `stableKey` |
| `nocPackage.topology.slotTemplates[].allowedContracts` | set | `contractId,version,bundleManifestDigest` |
| `nocPackage.topology.slotTemplates[].allowedContracts[].roles` | set | `unicodeScalarValue` |
| `nocPackage.topology.slotTemplates[].allowedContracts[].capabilityConstraints.*[]` | set | `canonicalJson` |
| `nocPackage.interfaceTemplates[].nocConfig.fields` | set | `key` |
| `nocPackage.interfaceTemplates[].nocConfig.fields[].values` | set | `canonicalJson` |
| `nocPackage.interfaceTemplates[].capabilityDefaults.*[]` | set | `canonicalJson` |
| `nocPackage.domainTypes[].configuration.fields` | set | `key` |
| `nocPackage.domainTypes[].configuration.fields[].values` | set | `canonicalJson` |
| `nocPackage.packageRelationTypes[].sources.kinds` | set | `unicodeScalarValue` |
| `nocPackage.packageRelationTypes[].targets.kinds` | set | `unicodeScalarValue` |
| `nocPackage.tools.drc.command` | ordered | argv order retained |
| `nocPackage.tools.generate.command` | ordered | argv order retained |
| `interfaceContract.roles` | set | `key` |
| `interfaceContract.capabilities` | set | `key` |
| `interfaceContract.capabilities[].values` | set | `canonicalJson` |
| `interfaceContract.fields` | set | `key` |
| `interfaceContract.fields[].values` | set | `canonicalJson` |
| `engineBundle.migrationFromCompatibilityVersions` | set | `unicodeScalarValue` |
| `engineBundle.supportedPlatformAbis` | set | `unicodeScalarValue` |
| `providerManifest.command` | ordered | argv order retained |
| `providerManifest.capabilities` | set | `unicodeScalarValue` |
| `providerManifest.ownedEntityTypes` | set | `unicodeScalarValue` |
| `providerManifest.ownedRelationTypes` | set | `unicodeScalarValue` |
| `providerHello.requestedCapabilities` | set | `unicodeScalarValue` |
| `providerHelloResult.capabilities` | set | `unicodeScalarValue` |
| `reconcileRequest.dependencyLocks` | set | `lockId` |
| `reconcileRequest.capabilities` | set | `canonicalJson` |
| `providerResult.diagnostics` | ordered | producer emission order retained |
| `toolManifest.command` | ordered | argv order retained |
| `toolManifest.capabilities` | set | `unicodeScalarValue` |
| `toolInput.dependencies` | set | `lockId` |
| `bundleManifest.files` | set | `path` |
| `artifactManifest.artifacts` | set | `path` |
| `diagnosticReport.diagnostics` | ordered | producer emission order retained |
| `diagnosticReport.diagnostics[].subjects` | set | `kind,id` |
| `diagnosticReport.diagnostics[].properties` | set | `unicodeScalarValue` |
| `commandResult.diagnostics` | ordered | producer emission order retained |
| `pipelineResult.steps` | ordered | execution-plan order retained |
| `recovery.draftOverlay` | ordered | `sequence`/submission order retained |
| `recovery.draftUndo` | ordered | stack order retained |
| `recovery.draftRedo` | ordered | stack order retained |
| `recovery.draftOverlay[].diagnostics` | ordered | producer emission order retained |
| `recovery.draftUndo[].before` | ordered | Draft sequence retained |
| `recovery.draftUndo[].after` | ordered | Draft sequence retained |
| `recovery.draftRedo[].before` | ordered | Draft sequence retained |
| `recovery.draftRedo[].after` | ordered | Draft sequence retained |
| `recovery.pendingTopologyGroup.intentUndo` | ordered | stack order retained |
| `recovery.pendingTopologyGroup.intentRedo` | ordered | stack order retained |

Every set key is candidate/document-wide unique at its owning path. Literal sets also use `uniqueItems` where useful; composite-key uniqueness, canonical sorting, nested normalization, localRef graph integrity, allocation-order derivation, endpoint normalization, and digest equality are semantic-validator rules because standard JSON Schema cannot compare sibling array items or recompute digests. Schemas carry both `x-ipcraft-canonical` and `$comment` where this limitation matters; implementations MUST NOT claim JSON Schema alone enforces these rules.

Capability meanings remain path-specific: Package/Contract declaration arrays sort by declaration `key`; Interface/runtime capability-value arrays sort by `canonicalJson`; Provider/Tool declared capability string lists sort by `unicodeScalarValue`.

Undirected persisted/Core Structural Links store endpoint string Host IDs and normalize them as `id:` + ID. Candidate Patch Links use objectRef envelopes and normalize `endpointA`/`endpointB` by `objectRefToken`, so candidate endpoints may contain local refs. Candidate localRefs are unique across both sub-patches, Authority owns `authority:*`, Application owns `application:*`, and `allocationOrder` contains every create localRef exactly once. Literal Unicode ordering therefore places `application:000001` before `authority:router-0`.

## F11. Host Side-effect Contract V1

`ipcraft.noc-side-effects.v1` is stable for all V1 Hosts:

1. Process Authority operations in order and build the candidate-wide local-reference graph. Reject duplicate localRefs, wrong source prefixes, unknown local references, or references whose create is not visible in combined Authority-then-Application operation order.
2. For every created Router, create one Membership in each Domain type's Default Domain. Sort by `(domainTypeKey, objectRefToken(routerRef))` and assign `application:000001...` localRefs in that order.
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
