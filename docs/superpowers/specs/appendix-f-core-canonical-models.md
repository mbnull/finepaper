# Appendix F — Gate 0 Core Canonical Models and Projections

**Normative status:** V1 Revision 4 candidate; these Core models freeze at Gate 0.
**Rule:** Engine Host and Extension Provider envelopes may wrap these models but may not redefine their fields, identity, projection, or canonicalization.

Machine-readable companion artifacts:

- [Core canonical models schema](../../contracts/schemas/ipcraft.core-canonical-models.v1.schema.json)
- [Default Engine Bundle schema](../../contracts/schemas/ipcraft.engine-bundle.v1.schema.json)
- [Core projection/digest vectors](../../contracts/vectors/core-canonical-projection-v1.json)
- [Core collection permutation vectors](../../contracts/vectors/core-set-permutation-v1.json)
- [Candidate/local-reference vectors](../../contracts/vectors/candidate-local-ref-v1.json)

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

Impact code, severity, dataLoss, Subjects, details, and resolution participate in candidate digest. Impact entries sort by the total key `(code, severity, dataLoss, canonical subjects, canonical details, resolution)` in exactly that order. Localized title/message/help URLs do not exist in this object and are derived by the UI from `code`.

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

Freshness is computed against all exact fields plus current authoritative/saved design and Group/Draft state; it is not stored in this manifest. Engine Host or Host-side-effect contract mismatch uses the existing stale reason `dependency-changed`.

## F10. Canonical Collection Rules

Every reachable explicit array schema frozen at Gate 0 carries `x-ipcraft-canonical`. Its value has one consistent shape:

```json
{ "kind": "set", "sortKey": ["id"] }
{ "kind": "ordered" }
{ "kind": "derived-ordered", "sortKey": ["unicodeScalarValue"] }
```

Each machine rule has exact `{schemaId, schemaPointer, kind, sortKey}` addressing; ordered rules omit `sortKey`. `schemaPointer` is an RFC 6901 pointer to the physical array schema node in the catalogued document named by `schemaId`. Validators resolve local/external `$ref` edges and map every reachable reuse to that defining location exactly once; alias/display paths never create duplicate rules. The committed `docs/contracts/tools/verify_canonical_rules.py` authoring check proves this one-to-one graph coverage and exact metadata equality.

`sortKey` members name the comparison components below; `canonicalJson` and `*CanonicalJson` mean UTF-8 RFC 8785 canonical JSON bytes after nested normalization, `unicodeScalarValue` means literal string order, and `objectRefToken` means F2's token. `state-order` places resolved before unresolved. `persistedEndpointCanonicalKey` is resolved `(state-order, subject.kind, id:<subject.id>)` or unresolved `(state-order, intendedSubject.kind, id:<intendedSubject.id>, reasonCode)`. `patchEndpointCanonicalKey` is resolved `(state-order, subject.kind, objectRefToken(subject.ref))` or unresolved `(state-order, intendedSubject.kind, objectRefToken(intendedSubject.ref), reasonCode)`. The companion vector's `canonicalCollections` array is the machine-readable source mirrored by this table.

| Schema ID | RFC 6901 schema pointer | Kind | Sort key |
|---|---|---|---|
| `ipcraft.artifact-manifest.v1` | `/properties/artifacts` | set | `path` |
| `ipcraft.bundle-manifest.v1` | `/properties/files` | set | `path` |
| `ipcraft.command-result.v1` | `/properties/diagnostics` | ordered | `—` |
| `ipcraft.core-canonical-models.v1` | `/$defs/accessSlot/properties/allowedContracts` | set | `contractLockId` |
| `ipcraft.core-canonical-models.v1` | `/$defs/accessSlot/properties/allowedContracts/items/properties/roles` | set | `unicodeScalarValue` |
| `ipcraft.core-canonical-models.v1` | `/$defs/candidateTransaction/properties/allocationOrder` | derived-ordered | `unicodeScalarValue` |
| `ipcraft.core-canonical-models.v1` | `/$defs/candidateTransaction/properties/tombstones` | set | `subjectKind,id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/derivedState/properties/accessSlots` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/derivedState/properties/packageEntities` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/derivedState/properties/packageRelations` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/derivedState/properties/routers` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/derivedState/properties/structuralLinks` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/impact/properties/subjects` | set | `kind,id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/impactReport/properties/impacts` | set | `code,severity,dataLoss,subjectsCanonicalJson,detailsCanonicalJson,resolution` |
| `ipcraft.core-canonical-models.v1` | `/$defs/normalizedTopologyInput/properties/mesh/properties/slotTemplates` | set | `stableKey` |
| `ipcraft.core-canonical-models.v1` | `/$defs/packageRelation/properties/sources` | set | `persistedEndpointCanonicalKey` |
| `ipcraft.core-canonical-models.v1` | `/$defs/packageRelation/properties/targets` | set | `persistedEndpointCanonicalKey` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchComponentValue/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchInterfaceValue/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchOperations` | ordered | `—` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchPackageEntityValue/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchPackageRelationValue/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchPackageRelationValue/properties/sources` | set | `patchEndpointCanonicalKey` |
| `ipcraft.core-canonical-models.v1` | `/$defs/patchPackageRelationValue/properties/targets` | set | `patchEndpointCanonicalKey` |
| `ipcraft.core-canonical-models.v1` | `/$defs/pipelinePlan/properties/steps` | ordered | `—` |
| `ipcraft.core-canonical-models.v1` | `/$defs/slotAllowedContract/properties/roles` | set | `unicodeScalarValue` |
| `ipcraft.core-canonical-models.v1` | `/$defs/slotTemplate/properties/allowedContracts` | set | `contractId,version,bundleManifestDigest` |
| `ipcraft.core-canonical-models.v1` | `/$defs/topologyIntent/properties/packageEntities` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/topologyIntent/properties/packageRelations` | set | `id` |
| `ipcraft.core-canonical-models.v1` | `/$defs/updateEntityOperation/properties/unset` | set | `unicodeScalarValue` |
| `ipcraft.core-canonical-models.v1` | `/$defs/updateRelationOperation/properties/unset` | set | `unicodeScalarValue` |
| `ipcraft.diagnostic-report.v1` | `/$defs/diagnostic/properties/properties` | set | `unicodeScalarValue` |
| `ipcraft.diagnostic-report.v1` | `/$defs/diagnostic/properties/subjects` | set | `kind,id` |
| `ipcraft.diagnostic-report.v1` | `/properties/diagnostics` | ordered | `—` |
| `ipcraft.engine-bundle.v1` | `/properties/migrationFromCompatibilityVersions` | set | `unicodeScalarValue` |
| `ipcraft.engine-bundle.v1` | `/properties/supportedPlatformAbis` | set | `unicodeScalarValue` |
| `ipcraft.interface-contract.v1` | `/$defs/capability/properties/values/oneOf/0` | set | `canonicalJson` |
| `ipcraft.interface-contract.v1` | `/$defs/field/properties/values/oneOf/0` | set | `canonicalJson` |
| `ipcraft.interface-contract.v1` | `/properties/capabilities` | set | `key` |
| `ipcraft.interface-contract.v1` | `/properties/fields` | set | `key` |
| `ipcraft.interface-contract.v1` | `/properties/roles` | set | `key` |
| `ipcraft.noc-package.v1` | `/$defs/allowedContract/properties/roles` | set | `unicodeScalarValue` |
| `ipcraft.noc-package.v1` | `/$defs/configuration/properties/global/properties/fields` | set | `key` |
| `ipcraft.noc-package.v1` | `/$defs/domainType/properties/configuration/properties/fields` | set | `key` |
| `ipcraft.noc-package.v1` | `/$defs/endpointDeclaration/properties/kinds` | set | `unicodeScalarValue` |
| `ipcraft.noc-package.v1` | `/$defs/field/properties/values/oneOf/0` | set | `canonicalJson` |
| `ipcraft.noc-package.v1` | `/$defs/interfaceTemplate/properties/nocConfig/properties/fields` | set | `key` |
| `ipcraft.noc-package.v1` | `/$defs/slotTemplate/properties/allowedContracts` | set | `contractId,version,bundleManifestDigest` |
| `ipcraft.noc-package.v1` | `/$defs/tool/properties/command` | ordered | `—` |
| `ipcraft.noc-package.v1` | `/$defs/topology/properties/slotTemplates` | set | `stableKey` |
| `ipcraft.noc-package.v1` | `/properties/domainTypes` | set | `key` |
| `ipcraft.noc-package.v1` | `/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.noc-package.v1` | `/properties/interfaceTemplates` | set | `key` |
| `ipcraft.noc-package.v1` | `/properties/packageEntityTypes` | set | `typeKey` |
| `ipcraft.noc-package.v1` | `/properties/packageRelationTypes` | set | `typeKey` |
| `ipcraft.noc-side-effects.v1` | `/$defs/expected/properties/allocationOrder` | derived-ordered | `unicodeScalarValue` |
| `ipcraft.noc-side-effects.v1` | `/$defs/expected/properties/coreDiagnostics` | ordered | `—` |
| `ipcraft.noc-side-effects.v1` | `/$defs/expected/properties/tombstones` | set | `subjectKind,id` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/attachments` | set | `id` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/domainMemberships` | set | `id` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/domainTypes` | set | `key` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/domains` | set | `id` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/packageRelations` | set | `id` |
| `ipcraft.noc-side-effects.v1` | `/$defs/input/properties/relationDeclarations` | set | `typeKey` |
| `ipcraft.patch.v1` | `/properties/preconditions` | set | `canonicalJson` |
| `ipcraft.pipeline-result.v1` | `/properties/steps` | ordered | `—` |
| `ipcraft.project-design.v1` | `/$defs/component/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/$defs/defaultEngineDependencyLock/properties/supportedPlatformAbis` | set | `unicodeScalarValue` |
| `ipcraft.project-design.v1` | `/$defs/interface/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/$defs/packageEntity/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/$defs/packageRelation/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/$defs/packageRelation/properties/sources` | set | `persistedEndpointCanonicalKey` |
| `ipcraft.project-design.v1` | `/$defs/packageRelation/properties/targets` | set | `persistedEndpointCanonicalKey` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/accessSlots` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/attachments` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/domainMemberships` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/domains` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/packageEntities` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/packageRelations` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/routers` | set | `id` |
| `ipcraft.project-design.v1` | `/$defs/topology/properties/structuralLinks` | set | `id` |
| `ipcraft.project-design.v1` | `/properties/components` | set | `id` |
| `ipcraft.project-design.v1` | `/properties/connections` | set | `id` |
| `ipcraft.project-design.v1` | `/properties/dependencies` | set | `lockId` |
| `ipcraft.project-design.v1` | `/properties/extensions` | set | `ownerLockId,schema,version` |
| `ipcraft.project-design.v1` | `/properties/interfaces` | set | `id` |
| `ipcraft.project-design.v1` | `/properties/topologies` | set | `id` |
| `ipcraft.project-design.v1` | `/properties/views` | set | `id` |
| `ipcraft.recovery.v1` | `/$defs/draftEntry/properties/diagnostics` | ordered | `—` |
| `ipcraft.recovery.v1` | `/$defs/draftOverlayMutation/properties/after` | ordered | `—` |
| `ipcraft.recovery.v1` | `/$defs/draftOverlayMutation/properties/before` | ordered | `—` |
| `ipcraft.recovery.v1` | `/$defs/pendingTopologyGroup/properties/intentRedo` | ordered | `—` |
| `ipcraft.recovery.v1` | `/$defs/pendingTopologyGroup/properties/intentUndo` | ordered | `—` |
| `ipcraft.recovery.v1` | `/properties/draftOverlay` | ordered | `—` |
| `ipcraft.recovery.v1` | `/properties/draftRedo` | ordered | `—` |
| `ipcraft.recovery.v1` | `/properties/draftUndo` | ordered | `—` |
| `ipcraft.tool-input.v1` | `/properties/dependencies` | set | `lockId` |

Kind-specific Patch update-set schemas reuse the create-value property schemas for `allowedContracts`/nested `roles`, Package Relation `sources`/`targets`, and `extensions`. Their create and update permutations therefore resolve to the same physical locations above and cannot diverge in canonicalization. The only open update values are the explicitly Package/Contract-defined `data`, `config`, `properties`, `contractConfig`, `nocConfig`, and `capabilities` maps.

### Deferred Extension Collections

These prose-only Provider/Tool capability paths freeze at Gate D. They are intentionally display-only, have no Gate 0 `schemaId`/`schemaPointer`, and are excluded from Core completeness checks.

The exact normative V1 set is `DEFERRED_EXTENSION_COLLECTIONS_V1` below; replacement, omission, or addition of a path is a contract change:

```text
DEFERRED_EXTENSION_COLLECTIONS_V1 = {
  providerManifest.command,
  providerManifest.capabilities,
  providerManifest.ownedEntityTypes,
  providerManifest.ownedRelationTypes,
  providerHello.requestedCapabilities,
  providerHelloResult.capabilities,
  reconcileRequest.dependencyLocks,
  reconcileRequest.capabilities,
  providerResult.diagnostics,
  toolManifest.capabilities
}
```

| Display path | Freeze gate |
|---|---|
| `providerManifest.command` | `extension` |
| `providerManifest.capabilities` | `extension` |
| `providerManifest.ownedEntityTypes` | `extension` |
| `providerManifest.ownedRelationTypes` | `extension` |
| `providerHello.requestedCapabilities` | `extension` |
| `providerHelloResult.capabilities` | `extension` |
| `reconcileRequest.dependencyLocks` | `extension` |
| `reconcileRequest.capabilities` | `extension` |
| `providerResult.diagnostics` | `extension` |
| `toolManifest.capabilities` | `extension` |

Every set key is candidate/document-wide unique at its owning path. Literal sets also use `uniqueItems` where useful; composite-key uniqueness, canonical sorting, nested normalization, localRef graph integrity, allocation-order derivation, endpoint normalization, and digest equality are semantic-validator rules because standard JSON Schema cannot compare sibling array items or recompute digests. Schemas carry both `x-ipcraft-canonical` and `$comment` where this limitation matters; implementations MUST NOT claim JSON Schema alone enforces these rules.

Capability meanings remain path-specific: Package/Contract declaration arrays sort by declaration `key`; Interface/runtime capability-value arrays sort by `canonicalJson`; Provider/Tool declared capability string lists sort by `unicodeScalarValue`.

Undirected persisted/Core Structural Links store endpoint string Host IDs and normalize them as `id:` + ID. Candidate Patch Links use objectRef envelopes and normalize `endpointA`/`endpointB` by `objectRefToken`, so candidate endpoints may contain local refs. Candidate localRefs are unique across both sub-patches, Authority owns `authority:*`, Application owns `application:*`, and `allocationOrder` contains every create localRef exactly once. Literal Unicode ordering therefore places `application:000001` before `authority:router-0`.

### Golden-vector envelope and coverage

Every focused canonical vector file uses this closed envelope:

```json
{
  "schema": "ipcraft.canonical-vector-catalog.v1",
  "kind": "collection-permutation",
  "canonicalization": "RFC8785-after-Appendix-F-set-projection",
  "cases": []
}
```

`kind` is exactly `collection-permutation` or `candidate-causality`. No other top-level members are permitted. A set or ordered collection case contains exactly `id`, `schemaId`, `schemaPointer`, `collectionKind`, `inputVariants`, `expectedRelation`, `expectedNormalized`, `expectedCanonicalJson`, and `expectedDigest`; a non-ordered case also contains the exact `sortKey`, and a derived-ordered case additionally contains `expectedErrorCode`. Derived expectations are two-element arrays aligned as valid then invalid: the valid slot has normalized/canonical/digest values and a null error, while the invalid slot has null normalized/canonical/digest values and one catalogued stable error.

An equal/different candidate case contains exactly `id`, `inputVariants`, `expectedRelation`, `includedProjection`, `excludedProjection`, `expectedNormalized`, `expectedCanonicalJson`, and `expectedDigest`. The optional versioned metadata field `modelSchema` is permitted only for a complete non-candidate model case, currently `ipcraft.pipeline-plan.v1`. An invalid candidate case instead contains exactly `id`, `baselineId`, `mutation`, `inputVariants`, `expectedRelation`, `includedProjection`, `excludedProjection`, `expectedErrorCode`, and `violatedRule`. `mutation` is closed: `remove` has only `operation` and RFC 6901 `path`; `replace`, `add`, `append`, and `rename-local-ref` additionally require `value`. The verifier reconstructs the mutant from the named valid baseline and rejects any undeclared second change.

The collection catalog has exactly one case for every `{schemaId,schemaPointer}` in `canonicalCollections` and no case for a `deferredExtensionCollections` display path. Every set case contains at least three non-empty, nontrivial permutations of the same structurally conforming items (canonical, reverse, and fixed-seed shuffle) with one normalized array, canonical JSON string, and digest. Every ordered case contains at least two variants differing only in order and aligned normalized arrays, canonical JSON strings, and distinct digests. Every derived-ordered location contains its valid derived order and a noncanonical supplied order with a stable error code. Case IDs are unique across both focused catalogs, and every digest is `sha256:` followed by 64 lowercase hexadecimal characters.

For each multi-component comparator, the evidence includes a decisive pair for every component: all earlier components tie and that component differs. Persisted endpoint evidence separately covers resolved before unresolved, kind, Host ID, and unresolved reason ordering. Candidate Patch endpoint evidence additionally covers `id:` versus `localRef:` object-reference tokens. An implementation that compares only an early component cannot pass the independent verifier.

The two large focused JSON files are generated evidence. [generate_canonical_vectors.py](../../contracts/tools/generate_canonical_vectors.py) is a deterministic, stdlib-only, non-normative authoring generator; schemas, this Appendix, and the committed artifacts remain normative. [verify_canonical_vectors.py](../../contracts/tools/verify_canonical_vectors.py) is an independent stdlib-only recomputation and structural verifier and shares no implementation module with the generator. Authoring requires generation to a temporary directory followed by byte comparison with both committed artifacts.

Collection vectors exercise the physical array comparator independently of enclosing root cardinality. This matters for V1-reserved ProjectDesign arrays such as `connections` and `views`, whose root schemas currently require `maxItems: 0`; their collection-level items have no additional item constraints, while the root cardinality remains enforced separately.

## F11. Host Side-effect Contract V1

`ipcraft.noc-side-effects.v1` is stable for all V1 Hosts and fixes its behavioral `contractVersion` to that exact V1 ID. Persisted locks remain structurally open so unsupported versions degrade before this schema is selected:

1. Process Authority operations in order and build the candidate-wide local-reference graph. Reject duplicate localRefs, wrong source prefixes, unknown local references, or references whose create is not visible in combined Authority-then-Application operation order.
2. For every created Router, create one Membership in each Domain type's Default Domain. Sort by `(domainTypeKey, objectRefToken(routerRef))` and assign `application:000001...` localRefs in that order.
3. For every deleted Router, delete its Domain Memberships; for every deleted Router/Slot, convert affected Attachments to unresolved with `attachment.target_removed`.
4. For each user Package Relation endpoint targeting a deleted object: convert to unresolved and add `package_relation.endpoint_unresolved` when allowed; otherwise add `package_relation.endpoint_blocks_candidate` and mark candidate blocked.
5. After proposed Router/Link changes, recompute every Domain's structural-undirected connectivity. Disconnection adds blocking Core DRC/impact regardless of whether the cause was Router deletion, Link deletion/change, or membership placement.
6. Delete any empty non-Default Domain, tombstone its full value, and add data-loss impact `domain.non_default_deleted`. Empty Default Domain remains legal.
7. Attachment/Package-Relation conversions, Membership creates/deletes, Domain cleanup, impacts, and Application localRefs are deterministic functions of the frozen canonical inputs; Packages/Engines cannot override them in V1.
8. A different Host side-effect contract version is incompatible for replay. Migration must explicitly replace provenance through a confirmed candidate.

Exact impact dispositions and candidate effects:

| Code | Severity | `dataLoss` | Resolution | Candidate effect |
|---|---|---:|---|---|
| `attachment.target_removed` | warning | false | `reattach-or-detach` | legal auto-commit |
| `domain.non_default_deleted` | warning | true | `confirm-or-discard` | ready-to-commit confirmation |
| `domain.disconnected` | error | false | `repair-domain` | legal auto-commit; resulting design has blocking Core DRC, Group is not blocked |
| `package_relation.endpoint_unresolved` | warning | false | `reattach-or-delete-relation` | legal auto-commit |
| `package_relation.endpoint_blocks_candidate` | error | false | `discard-and-repair` | blocked and unconfirmable |
| `engine_migration.dependency_replaced` | warning | false | `confirm-or-discard` | ready-to-commit confirmation regardless of data loss |

Disposition priority is exact: `package_relation.endpoint_blocks_candidate` wins and blocks; otherwise either confirmation impact requires confirmation; otherwise the candidate auto-commits. `domain.disconnected` therefore auto-commits while a matching blocking `domain.disconnected` Core diagnostic blocks Save, Validate, and Generate in the resulting current design.

## F12. Default Engine Migration Candidate

Engine migration uses `candidate-transaction.kind: default-engine-migration`. Its migration impact normally requires confirmation; universal blocking-impact priority still applies, so `package_relation.endpoint_blocks_candidate` makes the migration blocked and unconfirmable. Normal exact reconcile applicability remains unchanged and continues to describe the current base. Separate `candidateTransaction.migration` provenance contains both complete exact locks:

```json
{
  "currentDefaultEngineLock": {
    "lockId": "dep.default-engine",
    "kind": "default-engine",
    "id": "ipcraft.default-noc-engine",
    "version": "1.0.0",
    "bundleManifestDigest": "sha256:...",
    "engineCompatibilityVersion": "1",
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
    "supportedPlatformAbis": ["linux-x86_64-gnu-v1"]
  },
  "targetDefaultEngineLock": {
    "lockId": "dep.default-engine",
    "kind": "default-engine",
    "id": "ipcraft.default-noc-engine",
    "version": "2.0.0",
    "bundleManifestDigest": "sha256:...",
    "engineCompatibilityVersion": "2",
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
    "supportedPlatformAbis": ["linux-x86_64-gnu-v1"]
  }
}
```

The dependency `lockId` remains stable while its exact Engine metadata/digest is replaced. Machine schema requires exactly one of each Application-owned update and exactly one `engine_migration.dependency_replaced`; semantic validation binds that impact to the dependency-update Project ID and exact `{lockId,currentBundleDigest,targetBundleDigest}` details. It also binds the current lock exactly to applicability, requires equal lock IDs and different digests, binds target Authority source/derivation to the target lock, preserves every non-Engine dependency canonically unchanged with unique lock IDs, and requires the target manifest to declare the source compatibility version. The Project update contains only `dependencies`, the Topology update only `derivation`, and both `unset` arrays are empty. Recovery stores this same complete two-lock `migration` context losslessly, stores no candidate, and re-derives after reopen. Commit is atomic after confirmation, unless a universal blocking impact makes the candidate unconfirmable. Formal Undo/Redo uses stored forward/inverse Patches and localRef→Host-ID mappings without loading either Engine.

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
