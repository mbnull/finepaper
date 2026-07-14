# Appendix A — ProjectDesign V1 Contract

**Normative status:** V1 Revision 4 baseline; Core schema freezes at Gate 0.
**Root schema:** `ipcraft.project-design.v1`
**Required profile:** `ipcraft.profile.noc`, version `1`

## A1. JSON Rules

- Encoding MUST be UTF-8 JSON without duplicate object keys.
- Core envelopes use `additionalProperties: false` semantics.
- Package-private opaque content is allowed only inside an `extensions[]` block. Schema-declared Package Entity/Relation `data`, `config`, `contractConfig`, `nocConfig`, and `capabilities` are known typed fields rather than opaque extension storage.
- IDs are non-empty opaque strings and MUST be globally unique across every entity and relation in one ProjectDesign.
- References use IDs; array positions are never identity.
- Digests use `sha256:<64 lowercase hexadecimal characters>` over RFC 8785 canonical JSON unless a field explicitly says otherwise.
- Unknown enum values in core fields are errors. Unknown namespaced extension schemas are preserved as opaque content.
- Before digesting, every canonical collection follows the exhaustive per-path table in Appendix F. Undirected Link endpoints are stored/hash-normalized by Appendix F's object-reference comparison token. Only arrays explicitly declared ordered retain source order.

## A2. Root Object

Required fields:

```json
{
  "schema": "ipcraft.project-design.v1",
  "profile": {
    "id": "ipcraft.profile.noc",
    "version": "1"
  },
  "id": "project-id",
  "name": "My NoC",
  "dependencies": [],
  "components": [],
  "interfaces": [],
  "connections": [],
  "topologies": [],
  "views": [],
  "extensions": []
}
```

Rules:

- `components` contains exactly one `noc` Component.
- `connections` is empty in the V1 NoC Profile.
- `topologies` contains exactly one Mesh TopologyDocument owned by the NoC Component.
- `views` is empty in V1. UI state belongs to `.workspace/ui-state.json`; Package visual metadata belongs to the Package.
- Diagnostics, run history, reports, artifacts, stdout/stderr, and recovery state are not ProjectDesign fields.

## A3. Dependency Locks

`dependencies[]` is a tagged union. Every item contains:

```json
{
  "lockId": "dep.noc",
  "kind": "noc-package",
  "id": "vendor.noc",
  "version": "1.0.0",
  "bundleManifestDigest": "sha256:..."
}
```

Allowed `kind` values:

```text
noc-package
interface-contract
default-engine
extension-provider
drc-tool
generator-tool
runtime
```

Additional required fields:

| Kind | Additional fields |
|---|---|
| `default-engine` | `engineHostContractVersion`, `engineCompatibilityVersion`, `hostSideEffectContractVersion`, `supportedPlatformAbis` |
| `extension-provider` | `protocolVersion`, `runtimeLockId` |
| `drc-tool` | `toolProtocolVersion`, `runtimeLockId` |
| `generator-tool` | `toolProtocolVersion`, `runtimeLockId` |
| `runtime` | `runtimeClosure` |

Exact Default Engine lock:

```json
{
  "lockId": "dep.default-engine",
  "kind": "default-engine",
  "id": "ipcraft.default-noc-engine",
  "version": "1.0.0",
  "bundleManifestDigest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "engineCompatibilityVersion": "1",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "supportedPlatformAbis": ["linux-x86_64-gnu-v1"]
}
```

Rules:

- `lockId` is project-global and is the value used by all references.
- Exactly one `noc-package` lock exists.
- Exactly one `default-engine` lock exists. Its `bundleManifestDigest` is the sole exact Engine implementation identity; `id` and `version` are display metadata, and `engineCompatibilityVersion` is migration classification only.
- Resolved Engine Bundle manifest ID/version/Host contract/compatibility metadata must equal the lock metadata, but equality of those metadata never compensates for a digest mismatch. The Host supports only explicitly registered `engineHostContractVersion` and `hostSideEffectContractVersion` values.
- Engine Host and Host side-effect version fields are structurally non-empty IDs, not schema constants. Unsupported values parse through Core validation and resolve to degraded inspect rather than making the project schema-invalid.
- At least one Contract lock exists for every Contract referenced by an Interface template or instance.
- Provider and tool locks exist only when the selected Package declares those dependencies.
- `bundleManifestDigest` follows Appendix C's Package Bundle Contract rather than hashing an arbitrary directory directly.
- A Runtime dependency uses this closed nested object:

```json
{
  "lockId": "dep.runtime.python",
  "kind": "runtime",
  "id": "python-runtime",
  "version": "3.12.4",
  "bundleManifestDigest": "sha256:...",
  "runtimeClosure": {
    "closureKind": "host-managed",
    "runtimeId": "python",
    "runtimeVersion": "3.12.4",
    "runtimeDistributionBundleDigest": "sha256:...",
    "entrypoint": "bin/python3",
    "platformAbi": "linux-x86_64-gnu-v1",
    "invocationProfile": "ipcraft.python-isolated.v1",
    "moduleSearchPolicy": "runtime-and-tool-bundles-only",
    "environmentProfile": "ipcraft.empty-utf8-utc.v1",
    "networkPolicy": "prohibited"
  }
}
```

- `closureKind` is `host-managed` or `package-contained`. Common `bundleManifestDigest` and `runtimeClosure.runtimeDistributionBundleDigest` MUST be identical.
- Any exact dependency/runtime mismatch produces degraded inspect mode; the reader still parses core fields and preserves opaque extensions without fallback.
- A missing, revoked, corrupt, digest-mismatched, Host-ABI-incompatible, Host-side-effect-incompatible, or platform-incompatible Default Engine lock produces degraded inspect mode. Corruption and manifest/content disagreement use `engine.bundle_mismatch`. The Host MUST NOT substitute its current Engine implementation, even when ID/version or compatibility version match.
- A structurally valid unsupported-platform Bundle may remain installed in the content-addressed store but cannot resolve for execution. `upgrade-available` is only an informational overlay on an exact normal resolution and never selects a digest.

## A4. NoC Component

```json
{
  "id": "component.noc",
  "kind": "noc",
  "name": "noc0",
  "packageLockId": "dep.noc",
  "typeKey": "mesh-noc",
  "config": {},
  "extensions": []
}
```

Rules:

- `config` contains only V1 global configuration fields declared by the selected NoC Package. Opaque values belong in `extensions`.
- Topology-driving fields are identified by the Package contract, not inferred from names.
- Component ID, `kind`, Package lock, and `typeKey` are Application-owned after creation.
- User commands may change only declared configuration properties and the display name.

## A5. NoC Interface

```json
{
  "id": "interface.mem0",
  "ownerComponentId": "component.noc",
  "templateKey": "chi-requester",
  "name": "mem0",
  "contract": {
    "lockId": "dep.contract.chi",
    "role": "requester"
  },
  "capabilities": {},
  "contractConfig": {},
  "nocConfig": {},
  "extensions": []
}
```

Rules:

- Every Interface is owned by the sole NoC Component.
- Contract lock and role must be allowed by the Interface template.
- `capabilities` and `contractConfig` use only the selected Contract schema. `nocConfig` uses only the selected NoC Interface template schema. Keys never merge across these namespaces; opaque values belong in `extensions`.
- Effective Contract field/capability values use this precedence: Contract default, then schema-valid Interface-template default override, then persisted instance value. A required value missing after precedence is invalid. Capability editability comes from the Contract declaration and cannot be widened by a NoC Package.
- Attachment is not embedded in the Interface; it is a relation inside the TopologyDocument.

## A6. TopologyDocument

```json
{
  "id": "topology.main",
  "ownerComponentId": "component.noc",
  "kind": "mesh",
  "templateKey": "default-mesh",
  "derivation": {},
  "routers": [],
  "structuralLinks": [],
  "accessSlots": [],
  "attachments": [],
  "domains": [],
  "domainMemberships": [],
  "packageEntities": [],
  "packageRelations": [],
  "extensions": []
}
```

`derivation` fields:

```json
{
  "topologyInputRevision": 4,
  "topologyInputDigest": "sha256:...",
  "derivedStateRevision": 7,
  "derivedStateDigest": "sha256:...",
  "packageBundleDigest": "sha256:...",
  "reconcileDependencySetDigest": "sha256:...",
  "defaultEngineLockId": "dep.default-engine",
  "defaultEngineBundleDigest": "sha256:...",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "structureAuthority": {
    "kind": "default-engine",
    "lockId": "dep.default-engine",
    "identity": "ipcraft.default-noc-engine",
    "version": "1",
    "bundleDigest": "sha256:..."
  },
  "engineCompatibilityVersion": "1"
}
```

Rules:

- `derivation` is Application-owned.
- Router, Link, Slot, and ownership=`engine` Package objects are Authority-managed materialized Derived State.
- Attachment and Domain arrays are user/Application-owned design relations.
- `packageEntities` and `packageRelations` follow their Package-declared ownership.

## A7. Router

```json
{
  "id": "router-id",
  "templateKey": "mesh-router",
  "identityCompatibilityVersion": 1,
  "coordinate": { "row": 0, "column": 0 },
  "properties": {}
}
```

Rules:

- `row` and `column` are non-negative integers and unique as a pair.
- `properties` is read-only schema-declared Package template output. Router/Link/Slot have no opaque `extensions`; additional Authority data uses typed `properties` or ownership=`engine` Package Entities/Relations.
- Coordinate and template compatibility determine Default Engine retention, but are never parsed from `id`.

## A8. Structural Link

```json
{
  "id": "link-id",
  "templateKey": "mesh-link",
  "identityCompatibilityVersion": 1,
  "endpointA": "router-a",
  "endpointB": "router-b",
  "axis": "horizontal",
  "properties": {}
}
```

Rules:

- `axis` is `horizontal` or `vertical`.
- Endpoints exist, differ, and are orthogonally adjacent.
- Persisted/Core endpoints are string Host IDs and compare as tokens `id:` + ID. Candidate Patch link values use `{id:X}` or `{localRef:X}` object-reference envelopes and compare by Appendix F's object-reference token.
- Exactly one Link exists per unordered endpoint pair.
- Link directionality of private transport is not represented.

## A9. Access Slot

```json
{
  "id": "slot-id",
  "routerId": "router-id",
  "templateKey": "local-0",
  "identityCompatibilityVersion": 1,
  "displayOrder": 0,
  "label": "Local 0",
  "allowedContracts": [
    {
      "contractLockId": "dep.contract.chi",
      "roles": ["requester"],
      "capabilityConstraints": { "coherent": true }
    }
  ],
  "properties": {}
}
```

Rules:

- Slot capacity is exactly one Interface in V1.
- `(routerId, templateKey)` is unique.
- `displayOrder` is presentation metadata but remains persisted because Slot selection has stable user-visible position semantics.
- Legal attachment is determined by existence, vacancy, Contract/role membership, and capability constraints evaluated against the Interface instance's `capabilities`.

## A10. Attachment

Resolved form:

```json
{
  "id": "attachment-id",
  "interfaceId": "interface.mem0",
  "state": "resolved",
  "routerId": "router-id",
  "slotId": "slot-id"
}
```

Unresolved form:

```json
{
  "id": "attachment-id",
  "interfaceId": "interface.mem0",
  "state": "unresolved",
  "intendedTarget": {
    "routerId": "deleted-router-id",
    "slotId": "deleted-slot-id"
  },
  "reasonCode": "attachment.target_removed"
}
```

Rules:

- Each Interface has zero or one Attachment.
- A resolved Attachment references an existing Slot and that Slot's Router.
- No two resolved Attachments reference the same Slot.
- Unresolved intended IDs are historical opaque values and are exempt from reference-existence checks.
- Unresolved state blocks formal Save, Validate, and Generate.

## A11. Domain and Membership

Domain:

```json
{
  "id": "domain-id",
  "typeKey": "clock",
  "name": "clock-0",
  "isDefault": true,
  "config": {}
}
```

Membership:

```json
{
  "id": "membership-id",
  "domainId": "domain-id",
  "routerId": "router-id"
}
```

Rules:

- Exactly one Default Domain exists per Package-declared Domain type.
- For every live Router and every Domain type, exactly one Membership exists.
- A Membership is never unresolved.
- A non-empty Domain forms one connected component in the undirected Structural Link graph.
- Empty Default Domain is allowed; empty non-Default Domain is not persisted.
- If topology reconciliation removes the last member of a non-Default Domain, the Application deletes that Domain in the same materialization transaction, tombstones its configuration, and includes the deletion in impact preview/Undo.

## A12. Package Entity and Relation

Entity:

```json
{
  "id": "package-entity-id",
  "typeKey": "vendor.type",
  "data": {},
  "extensions": []
}
```

Relation:

```json
{
  "id": "package-relation-id",
  "typeKey": "vendor.relation",
  "sources": [{ "state": "resolved", "subject": { "kind": "router", "id": "router-id" } }],
  "targets": [{ "state": "resolved", "subject": { "kind": "package-entity", "id": "package-entity-id" } }],
  "data": {},
  "extensions": []
}
```

Allowed `SubjectRef.kind` values:

```text
project
component
interface
router
structural-link
access-slot
attachment
domain
domain-membership
package-entity
package-relation
```

When a Package Relation declaration explicitly permits unresolved endpoints, an endpoint uses:

```json
{
  "state": "unresolved",
  "intendedSubject": { "kind": "router", "id": "deleted-router-id" },
  "reasonCode": "relation.target_removed"
}
```

Resolved endpoints use `{ "state": "resolved", "subject": { "kind": "router", "id": "router-id" } }`. Core Attachment fields do not reuse this Package Relation envelope.

Ownership and unresolved behavior are defined in Appendix B. Unknown types are editable only when their Package schema fits the supported generic path; otherwise their data is opaque and tool-managed.

When topology deletion targets a user-owned Package Relation endpoint, `unresolvedAllowed: true` converts that endpoint to the unresolved envelope through an explicit Application side effect in the candidate. `unresolvedAllowed: false` blocks the candidate; the Authority never deletes or mutates the user relation itself.

## A13. Extension Block

```json
{
  "ownerLockId": "dep.noc",
  "schema": "vendor.extension.v1",
  "version": "1",
  "data": {}
}
```

Rules:

- The core validates only the envelope when the owning dependency/schema is unavailable.
- Opaque data is preserved semantically and is not exposed to generic Patch editing.
- Degraded mode is read-only, so unavailable extension schemas never need to be rewritten.

## A14. Storage Boundary

Stored in `.nocproj`:

- every field defined above;
- only the last formally saved, current, structurally valid design.

Stored outside `.nocproj`:

| Data | Location |
|---|---|
| Window/panel/zoom/selection | `.workspace/ui-state.json` |
| Unsaved working revision and command recovery | `.workspace/recovery.json` |
| Exclusive mutation lock | `.workspace/project.lock` |
| DRC/generation pipeline reports and raw output | `reports/runs/<pipelineRunId>/` |
| Current promoted generation artifacts | `output/` |
| Promoted Snapshot/manifest digest and freshness | `output/.ipcraft-output.json` |
| Historical or staging artifacts | run-specific staging managed by RunCoordinator |

The project directory is protected by one host-level exclusive mutation lock at `.workspace/project.lock`, covering formal Save, recovery, shared UI state, reports, and output promotion. Lock metadata records project ID, process ID, process-start token, and host identity, but ownership is decided by the platform locking primitive rather than PID text alone. A second process may open read-only and keeps UI state in memory only; it cannot write `.workspace`, `reports`, or `output`. A crash-stale file may be replaced only after the platform primitive confirms no owner. Save compares the current on-disk canonical digest with the Session's last-saved digest and fails with `project.concurrent_modification` on mismatch.

## A15. Required Fixtures

Gate A must provide at least these valid fixtures:

1. `minimal-1x1.nocproj`: one Router, one Default Domain, no Interfaces.
2. `mesh-2x2-attached.nocproj`: one resolved Interface Attachment.
3. `mesh-2x2-package-extension.nocproj`: one schema-declared Package Entity/Relation and one opaque extension.

Required invalid fixtures:

- wrong/legacy schema ID;
- missing NoC profile;
- zero or two top-level Components;
- non-empty system `connections`;
- duplicate project-global ID;
- Slot referencing missing Router;
- resolved Attachment referencing missing/occupied Slot;
- resolved Attachment whose Contract/role/capabilities violate Slot constraints;
- Router missing one Domain type Membership;
- disconnected non-empty Domain;
- known core field with wrong JSON type;
- unknown opaque extension outside the extension envelope.
- runtime lock missing distribution/ABI/profile/closure fields.
