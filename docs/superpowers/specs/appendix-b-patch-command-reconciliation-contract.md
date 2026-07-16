# Appendix B — Patch, Command, Ownership, and Reconciliation Contract

**Normative status:** V1 Revision 4 baseline; Core wire contract freezes at Gate 0.
**Normative schemas:** `ipcraft.patch.v1`, `ipcraft.command-result.v1`
**Rule:** UI uses typed commands. Generic Patch is internal and is also the only state-change payload accepted from an Extension Provider.

## B1. Ownership Matrix

| Subject/property | Owner | Allowed mutation source | Unresolved allowed | Persisted | Target deletion behavior |
|---|---|---|---|---|---|
| Root schema/profile/project ID | Application | create/migrate only | no | yes | not applicable |
| Dependency locks | Application | create/migrate only | no | yes | missing lock causes degraded mode |
| NoC Component identity/package/type | Application | create/migrate only | no | yes | Component cannot be deleted in V1 |
| NoC Component display name/global config | User | typed command | no | yes | not applicable |
| Router/Link/Slot core fields | selected structure Authority | Default Engine Patch or Provider Patch, never both | no | yes | explicit delete |
| Router/Link/Slot schema-declared `properties` | selected structure Authority | selected Authority Patch | no | yes | explicit update/delete; no opaque per-object extensions |
| Interface | User | typed command | no | yes | deleting Interface deletes its Attachment in same command |
| Attachment | User | typed command; Application may mark unresolved | yes | yes only when resolved/current | target deletion preserves unresolved Attachment |
| Domain identity/name/config | User/Application | wizard or typed Domain command | no | yes | merge/split rules apply |
| Domain Membership | Application-mediated user intent | typed Domain command; Application reconciliation side effect | no | yes | Router deletion deletes Membership |
| Package Entity/Relation declared `user` | User | generic typed Package command; Application may convert permitted endpoints unresolved | Package schema decides; V1 default no | yes | allowed unresolved endpoint converts in candidate; forbidden unresolved blocks candidate |
| Package Entity/Relation declared `engine` | selected structure Authority | selected Authority Patch | no unless Package schema explicitly opts in | yes | explicit Patch behavior |
| Opaque extension data | Owning tool/Package | no interactive Patch in V1 | envelope only | yes | preserved read-only |
| Derivation metadata | Application | reconciliation commit only | no | yes | replaced on accepted reconciliation |
| UI state/recovery/runs | Infrastructure | dedicated store/coordinator | not ProjectDesign references | no | separate lifecycle |

An Extension Provider cannot mutate user-owned Component config, Interface, Attachment, Domain, or user-owned Package Entity/Relation. It can mutate core Derived State only when the Package selects `structureAuthority: extension-provider`. Application-generated Domain, Attachment, and permitted unresolved Package-Relation side effects are appended after Authority Patch validation and committed with topology intent in the same transaction.

Package Entity and Package Relation `typeKey` is immutable after creation in V1. Creation and every final-state validation resolve the key through the selected Package declaration; an unknown key is a reference failure, and an update cannot cross or evade the declared user/engine owner. Package Relation validation also applies the frozen declaration's `unresolvedAllowed`, source/target kind, and cardinality constraints after the complete atomic transaction. Application may perform a resolved-to-unresolved conversion only for a declared user-owned relation type whose `unresolvedAllowed` is true and only when the same registered candidate Authority operations remove that exact target.

## B2. Patch Envelope

```json
{
  "schema": "ipcraft.patch.v1",
  "transactionId": "transaction-id",
  "patchId": "patch-id",
  "source": {
    "kind": "default-engine",
    "identity": "ipcraft.default-noc-engine",
    "version": "1",
    "bundleDigest": "sha256:..."
  },
  "causality": {
    "sessionRevision": 12
  },
  "applicability": {
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
      "version": "1",
      "bundleDigest": "sha256:..."
    },
    "packageBundleDigest": "sha256:...",
    "reconcileDependencySetDigest": "sha256:...",
    "defaultEngineLockId": "dep.default-engine",
    "defaultEngineBundleDigest": "sha256:...",
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1"
  },
  "preconditions": [],
  "operations": []
}
```

Allowed `source.kind`:

```text
user-command
application-reconcile
application-migration
default-engine
extension-provider
recovery
undo-redo
```

Authority reconciliation Patches require the complete `applicability` object shown above; it is part of the Patch and candidate digest rather than an out-of-band request association. `sessionRevision` is provenance only for reconciliation and cannot reject an otherwise applicable response. Accepted ordinary user-command Patches omit `applicability` and require the current `sessionRevision`. Topology-driving intent remains pending until materialization and is never accepted as a standalone user Patch.

Patch source provenance is exact by source kind. `default-engine` and `extension-provider` sources MUST include `bundleDigest` equal to the selected Authority bundle digest. `user-command`, `application-reconcile`, `application-migration`, `recovery`, and `undo-redo` sources MUST omit it; Application/user provenance never invents an Authority digest.

The Patch validation context freezes the current `sessionRevision`. Every ordinary `user-command`, recovery Patch, and ordinary non-topology `undo-redo` Patch MUST carry that exact revision or fail with `patch.revision_conflict`. Reconciliation and migration sources use their closed applicability/history provenance; reconciliation `sessionRevision` remains provenance and is not an applicability member.

## B3. Preconditions

Supported V1 preconditions:

```json
{ "kind": "entity-exists", "entityKind": "router", "id": "..." }
{ "kind": "entity-absent", "entityKind": "router", "id": "..." }
{ "kind": "property-equals", "entityKind": "interface", "id": "...", "property": "name", "value": "mem0" }
{ "kind": "relation-exists", "relationKind": "attachment", "id": "..." }
{ "kind": "slot-unoccupied", "slotId": "..." }
```

All preconditions are evaluated before the first operation. Failure rejects the entire Patch.

## B4. Operation Formats

### Create entity

```json
{
  "op": "createEntity",
  "entityKind": "router",
  "localRef": "authority:r0",
  "value": {
    "templateKey": "mesh-router",
    "identityCompatibilityVersion": 1,
    "coordinate": { "row": 0, "column": 0 },
    "properties": {}
  }
}
```

`localRef` is unique within the containing transaction, not merely one source Patch. A normal single-command transaction contains one Patch; a Topology Candidate transaction contains an Authority Patch and an Application side-effect Patch sharing the namespace. Authority-created objects use `authority:<token>`; deterministic Host side-effect creates use `application:<zero-padded-sequence>`. References use either `{ "id": "host-id" }` or `{ "localRef": "authority:r0" }`.

### Update entity

```json
{
  "op": "updateEntity",
  "entityKind": "interface",
  "id": "interface-id",
  "set": { "name": "mem1", "contractConfig": {}, "nocConfig": {}, "capabilities": {} },
  "unset": ["optionalProperty"]
}
```

- `set` replaces complete top-level schema properties.
- `set` is a closed object selected by `entityKind` or `relationKind`; it may contain only fields declared for that subject kind. Each field is optional in the update set, and the final object must validate after applying `set` and `unset`.
- Nested `allowedContracts`, `roles`, Package Relation `sources`/`targets`, and `extensions` reuse the same typed canonical array schemas as create values. Only Package/Contract-defined `data`, `config`, `properties`, `contractConfig`, `nocConfig`, and `capabilities` value maps remain open for their owning semantic schema.
- Nested objects are replaced, never deep-merged.
- JSON `null` is a value only when the property schema permits it.
- Property removal uses `unset`; the same property cannot appear in both lists.
- `unset[]` is a set of literal property names, sorted by Unicode scalar-value order; duplicates are invalid.

### Delete entity

```json
{ "op": "deleteEntity", "entityKind": "router", "id": "router-id" }
```

Delete never cascades silently. Application invariant side effects are explicit additions to the candidate/accepted transaction: Router deletion deletes Domain Memberships, marks Attachments unresolved, and converts permitted Package Relation endpoints unresolved; a forbidden unresolved Package Relation blocks the candidate. Interface deletion removes its Attachment.

### Create/update/delete relation

Relations use the same structure with `relationKind` instead of `entityKind`. Relation `value` contains exact Host or transaction-local object references. A Host ref `{id:X}` compares as the UTF-8 string `id:` + X; a candidate-local ref `{localRef:X}` compares as `localRef:` + X. Tokens compare by Unicode scalar-value order wherever an object reference participates in a sort key. Operation order controls visibility, but a side-effect Patch may reference an Authority local object created earlier in the candidate transaction's canonical combined operation order.

Allowed V1 entity kinds:

```text
component
project
topology (update only)
interface
router
structural-link
access-slot
domain
package-entity
```

Allowed V1 relation kinds:

```text
attachment
domain-membership
package-relation
```

`project` is a singleton Patch subject with ID equal to ProjectDesign root `id`. Ordinary user Patch permits only `updateEntity(project)` of `name`; create/delete Project and mutation of schema/profile/dependencies are forbidden. `topology` is update-only with ID equal to the TopologyDocument ID and a closed set containing only complete `derivation` replacement; V1 has no create/delete topology operation. The schemas structurally enforce that only `application-migration` may replace exact `dependencies`, and only `application-reconcile` or `application-migration` may replace topology `derivation`. Authority, user, recovery, and ordinary `undo-redo` Patch envelopes reject those Application-owned mutations. An ordinary `undo-redo` Patch represents only the inverse or forward mutation of a non-topology formal command. Formal-history Undo/Redo of a materialized topology or Engine-migration transaction is instead a separate trusted Host replay of the exact stored transaction record; it is not a newly authored Patch envelope and never invokes an Engine or Provider. A confirmed Engine Migration candidate contains exactly one dependencies-only Project update and one derivation-only Topology update, both with empty `unset`, plus the migration confirmation impact in the same atomic transaction.

## B5. Operation Ordering and Atomicity

- Operations execute in array order after all Patch-level checks pass.
- Creates become visible to later combined-transaction operations only through local references.
- Any operation, ownership, schema, reference, or invariant failure rejects all operations and allocated IDs.
- Candidate digest covers the normalized local-reference graph and canonical Host-ID allocation order, never random final Host-ID strings. `allocationOrder` contains every create localRef across both candidate sub-patches exactly once, sorted by literal Unicode scalar-value order (`application:000001` sorts before `authority:router-0`). Atomic commit allocates Host IDs in that order, rewrites every local reference, and stores the complete `localRef → hostId` map in the formal history transaction. Rejected/discarded candidates publish and burn no Host IDs.
- Provider Patch response order is semantically significant and must be deterministic.

## B6. Stable Patch Error Codes

```text
patch.schema_invalid
patch.source_not_allowed
patch.revision_conflict
patch.topology_input_conflict
patch.group_conflict
patch.request_generation_conflict
patch.derived_state_base_conflict
patch.authority_conflict
patch.authoritative_design_base_conflict
patch.candidate_conflict
patch.precondition_failed
patch.ownership_violation
patch.unknown_entity_kind
patch.unknown_relation_kind
patch.duplicate_id
patch.unknown_reference
patch.schema_violation
patch.invariant_violation
patch.local_ref_invalid
patch.operation_invalid
command.attachment_would_be_illegal
command.pending_candidate_required
command.pending_candidate_blocked
command.pending_candidate_digest_mismatch
```

Each issue includes Patch ID, operation index when applicable, SubjectRef when known, and a stable JSON path.

## B7. Undo Tombstones

Undo history is Session-local and not part of `.nocproj`.

A user command record contains:

```text
immutable history-entry ID and complete-record digest
command type and parameters
forward and inverse transaction bodies with source attribution
full tombstones for deleted entities/relations
causal Authority reconciliation Patch when topology-driving
Application Domain/Attachment/Package-Relation side-effect Patch
complete localRef → Host-ID allocation map
before/after authoritative-design, topology-input, and Derived-State digests
committed revision values and the exact monotonic revision effects for replay
```

Every replay request is bound to the immutable history-entry ID, complete-record digest, direction (`undo` or `redo`), selected stored transaction body, full tombstones, complete localRef-to-Host-ID map, and expected current/result content-state digests. Undo is applicable whenever the current authoritative/topology/Derived content equals the record's after-state; Redo is applicable whenever it equals the before-state, regardless of how far the live monotonic counters have advanced. The request supplies the live current revision tuple, and each affected next revision MUST equal that current value plus the V1 recorded increment of one. This makes repeated Undo/Redo cycles and intervening Session events that leave the required content-state digests unchanged valid without permitting a counter to decrement or reuse an absolute historical revision. Any mismatch rejects replay without mutation. These values are supplied through an internal trusted Host history capability, never accepted as authority merely because an external Patch claims source `undo-redo`.

Undo/Redo is a new monotonic Session event: counters never decrement. Every formal Undo/Redo increments `sessionRevision`. Formal-history Undo/Redo of a materialized topology or Engine-migration transaction replays the exact stored inverse/forward record, restores the recorded content and Host IDs, allocates a new `topologyInputRevision`, recomputes `topologyInputDigest`, increments `derivedStateRevision`, and recomputes `derivedStateDigest`; it performs no ordinary reconciliation and never loads or executes the original Engine or Provider. Undo/Redo of a non-topology command uses an ordinary internally authored `undo-redo` Patch, changes only `sessionRevision` and affected authoritative content, and remains prohibited from replacing Project `dependencies` or Topology `derivation`. Clone/Migrate starts a new Session with empty formal and draft history.

Draft Overlay has a separate local undo/redo stack. Undo routing is deterministic: active widget gesture, then Draft Overlay, then edits inside the open Pending Topology Group, then formal history. Undoing the last topology edit discards the Group; changing an open Group increments `requestGeneration` and supersedes its active request.

## B8. Typed Command Catalog

| Command | Required parameters | Preconditions/result | Revision effects | Undo group |
|---|---|---|---|---|
| `CreateDesign` | project ID/name, dependency locks, Package type, initial global config | no existing Session; creates Default Domains and initial Mesh | initializes all revisions; initial Derived State materialization is part of creation | creation is not undoable after project directory is committed |
| `RenameDesign` | name | non-empty | session +1 | one command |
| `RenameNoCComponent` | component ID, display name | sole Component exists; non-empty | session +1 | one command |
| `ChangeGlobalConfiguration` | property replacements/unsets | fields declared global; draft schema-valid | non-driving: session +1 immediately; driving: opens/updates Group and changes no accepted revision until materialization | driving intent + Authority Patch + side effects is one causal group |
| `CreateInterfaceFromTemplate` | template key, name, initial config | template/Contract exists | session +1 | one command |
| `RenameInterface` | interface ID, name | Interface exists; name policy passes | session +1 | one command |
| `ChangeInterfaceConfiguration` | interface ID, capability/contractConfig/nocConfig replacements/unsets | schemas pass; any resolved Attachment remains Slot-compatible | session +1; V1 never topology-driving | one command; otherwise rejected with `command.attachment_would_be_illegal` |
| `DeleteInterface` | interface ID | Interface exists | session +1; deletes Attachment in same command | Interface + Attachment tombstone |
| `AttachInterface` | interface ID, Router ID, Slot ID | Interface unattached; Slot legal and free | session +1 | one command |
| `DetachInterface` | interface ID | resolved or unresolved Attachment exists | session +1 | deletes Attachment or clears unresolved intent in one command |
| `ReattachInterface` | interface ID, new Router/Slot | resolved/unresolved Attachment exists; target legal/free | session +1 | one command preserving previous state |
| `RenameDomain` | Domain ID, name | Domain exists | session +1 | one command |
| `ChangeDomainConfiguration` | Domain ID, replacements/unsets | Domain schema passes | session +1 | one command |
| `MoveRoutersBetweenDomains` | source ID, target ID, Router IDs | same type; post-state total/connected | session +1 | one atomic group; empty non-default source deleted |
| `SplitDomain` | source ID, selected Router IDs, new name/config | both resulting regions valid | session +1 | one atomic group; source keeps ID |
| `MergeDomains` | source ID, survivor ID | same type, adjacent; Default survives if involved | session +1 | one atomic group with source tombstone |
| `CreatePackageEntity` | type key, data | type declared `user` and generic-editable | non-driving: session +1; driving: Group only until materialization | one accepted command or one causal topology group |
| `UpdatePackageEntity` | ID, replacements/unsets | user-owned and schema-valid | same as type declaration; driving changes remain pending | one accepted command or one causal topology group |
| `DeletePackageEntity` | ID and explicit relation handling | no forbidden references | same as type declaration; driving changes remain pending | entity/relation tombstones in accepted command/group |
| `Create/Update/DeletePackageRelation` | type, endpoints, data | type user-owned and references valid | non-driving immediate; driving Group only until materialization | one accepted command or one causal topology group |

UI must not call a Patch operation directly. Wizard creation is one compound Application use case; after successful initial reconciliation and project directory creation, its result is the baseline and is not part of normal undo history.

The typed-command layer maps a proposed Interface change that would make its resolved Attachment illegal to `command.attachment_would_be_illegal`. The lower-level defensive Patch executor independently rechecks the final Interface contract lock, role, and capabilities against the selected Slot allowance and rejects an incompatible final transaction with `patch.invariant_violation`; callers must not expose that internal code as the command-level UX result.

When no Pending Topology Group exists, non-topology commands are accepted immediately. While a Group exists, topology-bound commands are disabled and other user-owned edits become Draft Overlay proposals rather than accepted invocations of this catalog. Topology-bound means Attach/Detach/Reattach, deleting an attached Interface, Move/Split/Merge Domain membership, or any Package command whose references include Derived State. Safe drafts include names, scalar non-driving configuration, creating an unattached Interface, and edits whose user-owned subject may be revalidated after materialization. A topology-driving command opens or updates the one Group and enters this catalog/history only as part of successful materialization.

Pending Group controls are typed Application operations but are not ordinary design commands:

| Control | Parameters | Effect |
|---|---|---|
| `RetryPendingTopologyGroup` | group ID | invalidates any candidate, increments generation, re-derives |
| `ConfirmPendingTopologyGroup` | group ID, candidate digest | atomically commits only the exact `ready-to-commit` candidate |
| `DiscardPendingTopologyGroup` | group ID | cancels work, drops Group/candidate, changes no formal revision/history |
| `DiscardDraftOverlay` | optional draft IDs | removes selected proposals through draft-local history only |

## B9. Pending Topology Group and Reconciliation State

Two independent fields:

```text
derivedStateFreshness: current | stale
jobState: idle | running | failed
```

`RunCoordinator` owns job state. `DesignSession` stores current derivation metadata and exposes freshness.

Exactly zero or one Group exists:

```json
{
  "groupId": "group-id",
  "kind": "topology-edit",
  "topologyInputRevision": 4,
  "requestGeneration": 3,
  "baseAuthoritativeDesignDigest": "sha256:...",
  "baseDerivedStateRevision": 7,
  "baseDerivedStateDigest": "sha256:...",
  "topologyIntent": {},
  "intentUndo": [],
  "intentRedo": [],
  "normalizedTopologyInput": {},
  "topologyInputDigest": "sha256:...",
  "status": "running",
  "candidate": null
}
```

Allowed Group status:

```text
drafting
running
candidate-validating
ready-to-commit
blocked
failed
```

The Group itself is not ProjectDesign and never enters formal history before materialization. `topologyInputRevision` is reserved when a Group opens; discarded reservations are never reused. On materialization it becomes the authoritative topology-input revision. Formal Undo/Redo of a topology transaction allocates another new revision.

Allowed Group `kind`: `topology-edit`, `default-engine-migration`. Engine migration uses the same single-open-Group exclusion, candidate digest, recovery re-derivation, localRef namespace, and atomic history rules; its migration impact requires confirmation unless a universal blocking impact makes the Group blocked and unconfirmable. It carries current/target Engine locks from Appendix F.

Request:

```json
{
  "requestId": "request-id",
  "applicability": {
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
      "version": "1",
      "bundleDigest": "sha256:..."
    },
    "packageBundleDigest": "sha256:...",
    "reconcileDependencySetDigest": "sha256:...",
    "defaultEngineLockId": "dep.default-engine",
    "defaultEngineBundleDigest": "sha256:...",
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1"
  },
  "normalizedTopologyInput": {},
  "currentDerivedState": {},
  "dependencyLocks": [],
  "capabilities": []
}
```

`applicability`, `normalizedTopologyInput`, and `currentDerivedState` are the exact closed Appendix F machine models; abbreviated `{}` values are editorial only.

`dependencyLocks[]` is a set sorted by `lockId`. `capabilities[]` is a set sorted by the UTF-8 bytes of each value's RFC 8785 canonical JSON after recursively normalizing any nested sets. These runtime capability values are distinct from Package/Contract capability declarations, which sort by declaration `key`, and Provider/Tool declared capability strings, which sort as literal strings.

Applicability requires an exact match of `groupId`, `requestGeneration`, topology revision/digest, base Derived State revision/digest, base authoritative-design digest, selected Authority identity/version/bundle digest, Package bundle digest, reconcile dependency-set digest, Default Engine lock/digest, Engine Host contract, and Host side-effect contract. `sessionRevision` is not part of this comparison. `engineCompatibilityVersion` is excluded because it classifies explicit migration only. The dependency-set digest covers every locked bundle/runtime exposed to reconcile. The payload is closed; an Authority may not observe complete ProjectDesign or excluded user data.

An Authority response never commits directly. The Host validates its transaction-wide local-reference graph without allocating Host IDs, computes explicit Domain/Attachment/Package-Relation side effects in the same namespace, and produces an immutable candidate:

```json
{
  "transactionId": "transaction-id",
  "candidateDigest": "sha256:...",
  "applicability": {},
  "authorityPatchDigest": "sha256:...",
  "sideEffectPatchDigest": "sha256:...",
  "impactReport": [],
  "requiresConfirmation": true
}
```

`candidateDigest` hashes normalized applicability, transaction ID, Authority Patch body, Application side-effect/migration Patch body, candidate-wide local-reference graph, canonical allocation order, tombstones, and structured impact data. Random final Host IDs and localized impact messages are excluded. Impact entries contain stable code, severity, affected SubjectRefs, `dataLoss`, and structured proposed-resolution data; presentation text is derived after digesting.

- No destructive impact and no blocked reference: host automatically commits the candidate.
- Any impact with `dataLoss: true`: Group becomes `ready-to-commit`; only `ConfirmPendingTopologyGroup(candidateDigest)` may commit it.
- `engine_migration.dependency_replaced` always makes the migration Group `ready-to-commit` even though `dataLoss` is false.
- An impact that cannot be made legal automatically, including deletion of a target used by a user Package Relation with `unresolvedAllowed: false`, makes the Group `blocked`. It cannot be confirmed; the user discards the Group, repairs/deletes the relation, and retries.
- `domain.disconnected` is a legal auto-committed candidate impact and produces blocking Core DRC in the resulting current design; it does not make the Group blocked.
- Attachment targets removed by the candidate become unresolved. Package Relation endpoints with `unresolvedAllowed: true` become their unresolved envelope. These are explicit Application side effects included in the candidate.

Commit rechecks the entire applicability tuple and candidate digest, allocates/publishes Host IDs in canonical localRef order, rewrites all local references, and atomically applies final topology intent + Authority Patch + side effects/migration Patch + tombstones + derivation metadata. It stores the mapping in the formal history record, increments `sessionRevision` and `derivedStateRevision`, closes the Group, and adds one formal history record. `topologyInputRevision` is already the Group's reserved revision. Derived State may be current while Core Structural DRC is blocking after any Router/Link/Slot topology materialization, not only Router deletion.

Editing topology intent, local Group Undo/Redo, Retry, dependency/Authority change, or a base-authoritative-design mismatch discards the candidate and increments `requestGeneration`. Recovery never persists or trusts candidate content; it restores intent and re-derives.

## B10. Digest Canonicalization

- Canonical JSON: RFC 8785 over the strict JSON admission model in Appendix A.
- The Host performs byte-level strict JSON admission before Qt JSON parsing.
- Object property order is RFC 8785 UTF-16 code-unit order. Appendix F
  collection sort keys explicitly name whether they use RFC 8785 property order,
  Unicode scalar-value order, or UTF-8 byte order; implementations MUST NOT
  infer an order from a field name.
- Hash: SHA-256.
- String: `sha256:` plus lowercase hexadecimal.
- Objects use canonical key ordering.
- Arrays declared as ordered sequences retain order. Every set and sequence uses the exact Appendix F per-path rule; no implementation may infer a key from a field name.
- Composite canonical values are compared by the UTF-8 bytes of RFC 8785 canonical JSON after their nested set-valued arrays have first been normalized.
- An undirected persisted/Core Link stores string Host IDs and compares each endpoint as the token `id:` + ID. A candidate Patch Link uses object-reference envelopes: `{id:X}` becomes `id:` + X and `{localRef:X}` becomes `localRef:` + X. Tokens compare by Unicode scalar-value order.
- Topology input excludes Attachments, Domains, Views, diagnostics, runs, and non-driving Interface data.
- Derived State digest includes Router/Link/Slot IDs and properties, ownership=`engine` Package Entities/Relations, and every Authority-owned property affecting generation or later reconciliation.
- User-owned intent, Draft Overlay, Attachments, Domains, Views, diagnostics, and runs are excluded from Derived State digest.
- Random permutation of any set-valued persisted array MUST produce the same normalized digest.

## B13. Validation Modes

The same authoritative ProjectDesign is evaluated under three explicit modes:

- `ProjectDesignWellFormed` admits a readable working/recovery state, including
  a disconnected non-empty Domain, and requires matching structural diagnostics
  when such a state is present.
- `ProjectDesignCommitValid` admits an atomic materialization into the
  authoritative working design. A disconnected Domain is legal only with the
  stable blocking `domain.disconnected` diagnostic produced by that transaction.
- `ProjectDesignSaveEligible` is required for formal `.nocproj` Save, Validate,
  and Generate. It rejects disconnected Domains, unresolved Attachments, stale
  Derived State, and blocking diagnostics.

Recovery stores the WellFormed working design and its provenance. On reopen the
Host re-runs the Core checks and does not treat persisted diagnostics as a
second design authority.

## B11. Scheduling

- Only one Pending Topology Group and one active generation exist.
- One editing gesture or debounce window updates that Group at most once and schedules at most one generation.
- Updating, locally undoing, or retrying the Group increments `requestGeneration`; a superseded response cannot commit.
- V1 Provider host serializes requests; cancellation terminates and restarts the Provider if it does not exit promptly.
- Failure preserves the uncommitted Group for Retry or Discard and never changes authoritative state/history.
- Group Discard cancels work and restores the last materialized projection; Draft Overlay remains until independently submitted or discarded.
- On reopen, persisted and recomputed input/Derived-State/dependency fingerprints must all match before freshness becomes current.

## B12. Recovery Contract

Recovery schema: `ipcraft.recovery.v1`.

```json
{
  "schema": "ipcraft.recovery.v1",
  "projectId": "project-id",
  "savedProjectDigest": "sha256:...",
  "updatedAt": "2026-07-13T00:00:00Z",
  "authoritativeDesign": {},
  "sessionRevision": 12,
  "derivedStateRevision": 7,
  "pendingTopologyGroup": null,
  "draftOverlay": [],
  "draftUndo": [],
  "draftRedo": []
}
```

Draft Overlay entry:

```json
{
  "draftId": "draft-id",
  "sequence": 1,
  "commandType": "RenameInterface",
  "parameters": {},
  "validationStatus": "unvalidated",
  "diagnostics": []
}
```

Draft sequences are strictly increasing and preserve proposed submission order. Allowed validation status: `unvalidated`, `valid`, `invalid`. `draftUndo`/`draftRedo` contain complete before/after overlay mutations, not formal Design Patches. Group `intentUndo`/`intentRedo` similarly affect only proposed topology intent and are persisted inside the Group for interaction recovery.

Recovery V1 Draft Overlay is a closed safe subset: `RenameDesign`, `RenameInterface`, `RenameDomain`, and one self-contained `CreateInterfaceFromTemplate` entry with its final unattached Interface values. Each command has a closed parameter object in `ipcraft.recovery.v1`. Attach/Detach/Reattach, deleting an Interface, Domain membership Move/Split/Merge, topology intent, Package configuration, arbitrary configuration edits, and unknown command types are forbidden. Context-dependent topology-driving classification is never accepted from recovery data supplied by the file itself.

V1 has no cross-entry Draft local-reference system. `CreateInterfaceFromTemplate` may exist as one mutable Draft entry containing its final name/capability/contractConfig/nocConfig values; later gestures edit that same entry by `draftId`. No other Draft entry may target the not-yet-created Interface. After materialization, the Create draft must be accepted and receive a Host ID before any separate command can reference it.

Rules:

- `authoritativeDesign` is the last materialized ProjectDesign; pending topology intent is stored only in `pendingTopologyGroup`.
- Recovery serializes `pendingTopologyGroup.candidate` as JSON `null`; validated candidates, impact confirmations, provisional allocation plans, and candidate digests are never trusted across process lifetime.
- A recovered `default-engine-migration` Group stores the shared complete Appendix F `migration` context with exact current and target locks; no parallel partial digest/version fields exist. A `topology-edit` Group forbids `migration`.
- Recovery persists Draft Overlay and its local undo/redo because drafts are not formal commands. It does not persist formal undo/redo history, Provider process state, active request/process IDs, DRC/Generate jobs, or staging paths.
- The host writes recovery with atomic single-file replacement after a one-second idle debounce following user commands or draft changes and immediately before a deliberate close that keeps recovery.
- Recovery is applied only when project ID and the digest of the currently saved `.nocproj` equal `savedProjectDigest`.
- Mismatch, malformed JSON, or unsupported recovery schema is quarantined/ignored with a user diagnostic; it never modifies the project.
- Restore creates a new Session with empty formal history, recomputes the authoritative-design/input/Derived-State/dependency digests, rejects a Group whose `baseAuthoritativeDesignDigest` no longer matches, restores at most one remaining Group as `drafting` with no candidate, preserves both migration locks canonically byte-for-byte, increments its generation, and schedules a fresh request only after dependency validation.
- Successful formal Save, explicit Discard Recovery, or successful Clone/Migrate clears recovery.
- Recovery is disposable and is never a dependency for reproducibility or generation.

## B13. Degraded Parsing Layers

Opening a project performs:

1. Core JSON/schema parsing without Package code.
2. Core reference and NoC Profile validation.
3. Dependency lock resolution/digest verification.
4. Package/Contract/extension semantic validation only when exact dependencies are available.

If steps 1–2 pass but dependency/Engine/runtime/Host-contract resolution fails, the project opens degraded inspect mode. Core-known data is displayed, Package-owned schema content is preserved as raw read-only JSON, and opaque extensions remain uninterpreted. Save, normal reconciliation, DRC, Generate, and generic editing are disabled. The only allowed mutation is an explicitly offered Default Engine migration whose target manifest declares the source compatibility class and whose candidate can be derived without executing the missing source Engine. There is no fallback substitution. Failure of steps 1–2 means the project cannot open as a design.

Persisted and recovery Engine Host/Host-side-effect version values are structurally non-empty IDs. Unknown values survive parsing and cause degraded inspect during resolution; only the selected behavioral schema `ipcraft.noc-side-effects.v1` fixes its own `contractVersion` to V1.

## B14. Command Result

Every accepted, drafted, pending, or rejected typed-command attempt returns `ipcraft.command-result.v1`:

```json
{
  "schema": "ipcraft.command-result.v1",
  "commandId": "command-id",
  "disposition": "accepted",
  "sessionRevision": 13,
  "pendingGroupId": null,
  "draftId": null,
  "diagnostics": []
}
```

Allowed disposition: `accepted`, `pending-topology`, `drafted`, `rejected`. `accepted` alone means authoritative ProjectDesign/formal history changed. `pending-topology` identifies the single open Group; `drafted` identifies an Overlay entry. Rejections use stable command/Patch error codes and do not mutate any revision or history.
