# Appendix C — NoC Package, Contract, Provider, Tool, and Diagnostic Contracts

**Normative status:** V1 Revision 4 baseline; Core schemas freeze at Gate 0 and Extension ABI freezes at Gate D.

## C1. Normative Schema IDs

```text
ipcraft.noc-package.v1
ipcraft.interface-contract.v1
ipcraft.engine-bundle.v1
ipcraft.engine-host.v1
ipcraft.noc-side-effects.v1
ipcraft.provider-protocol.v1
ipcraft.tool-input.v1
ipcraft.tool-result.v1
ipcraft.step-result.v1
ipcraft.pipeline-plan.v1
ipcraft.pipeline-result.v1
ipcraft.diagnostic-report.v1
ipcraft.artifact-manifest.v1
ipcraft.output-manifest.v1
ipcraft.bundle-manifest.v1
```

The legacy `ipcraft.package.v1` belongs only to the frozen old path. All schemas use UTF-8 JSON, reject duplicate keys, and apply RFC 8785 + SHA-256 for content digests.

## C2. NoC Package Root

```json
{
  "schema": "ipcraft.noc-package.v1",
  "id": "vendor.noc",
  "name": "Vendor NoC",
  "version": "1.0.0",
  "component": {},
  "defaultEngineRequirement": {
    "engineHostContractVersion": "ipcraft.engine-host.v1",
    "engineCompatibilityVersion": "1",
    "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1"
  },
  "structureAuthority": "default-engine",
  "configuration": {},
  "topology": {},
  "interfaceTemplates": [],
  "domainTypes": [],
  "packageEntityTypes": [],
  "packageRelationTypes": [],
  "extensionProvider": null,
  "tools": {},
  "extensions": []
}
```

Core root fields are closed. At design creation, `defaultEngineRequirement` filters installed exact Engine Bundles; the chosen Bundle is then pinned by digest and never replaced by this compatibility declaration. `structureAuthority` is exactly `default-engine` or `extension-provider`; selecting the latter requires an Extension Provider with `reconcile` capability. For `default-engine`, Authority identity/version/digest come from the exact Default Engine dependency lock. For `extension-provider`, they come from the locked Provider manifest/bundle, while the exact Default Engine lock remains part of Project replay context. Unknown business data belongs in `extensions`.

The Gate 0-frozen Package field is only a stable ABI reference:

```json
{
  "extensionProvider": {
    "protocol": "ipcraft.provider-protocol.v1",
    "providerLockId": "dep.provider",
    "manifestPath": "provider/provider.json"
  }
}
```

`manifestPath` is a normalized path inside the locked Provider sub-bundle. The referenced Provider manifest and protocol message schemas remain Extension ABI and freeze at Gate D; changing them before Gate D does not change the Core Package envelope.

### Component

```json
{
  "typeKey": "mesh-noc",
  "displayName": "Mesh NoC"
}
```

V1 declares exactly one NoC component type.

### Configuration

```json
{
  "global": { "fields": [] }
}
```

This is the only source of global fields. Interface `nocConfig` fields exist only in each Interface template; Contract fields exist only in the Interface Contract; Domain fields exist only in `domainTypes[].configuration`.

V1 field types:

```text
bool
int
double
string
enum
```

Field declaration:

```json
{
  "key": "rows",
  "type": "int",
  "label": "Rows",
  "description": "Mesh row count.",
  "default": 2,
  "required": true,
  "readOnly": false,
  "minimum": 1,
  "maximum": 32,
  "unit": null,
  "values": null,
  "visibleWhen": null,
  "enabledWhen": null,
  "topologyDriving": true
}
```

`visibleWhen` and `enabledWhen` support only:

```json
{ "field": "mode", "equals": "advanced" }
```

No nested expression operators exist in V1.

### Mesh topology

```json
{
  "kind": "mesh",
  "rowField": "rows",
  "columnField": "columns",
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
}
```

Slot template:

```json
{
  "stableKey": "local-0",
  "identityCompatibilityVersion": 1,
  "displayOrder": 0,
  "label": "Local 0",
  "allowedContracts": [
    {
      "contractId": "arm.chi",
      "version": "1.0.0",
      "bundleManifestDigest": "sha256:...",
      "roles": ["requester"],
      "capabilityConstraints": { "coherent": true }
    }
  ],
  "properties": {}
}
```

The Package may declare several Slot templates. Each materialized Router receives one Slot per applicable template. V1 has no custom Slot-capacity expression.

### Interface template

```json
{
  "key": "chi-requester",
  "label": "CHI Requester",
  "contractId": "arm.chi",
  "contractVersion": "1.0.0",
  "contractBundleManifestDigest": "sha256:...",
  "role": "requester",
  "capabilityDefaults": { "coherent": true },
  "contractConfigDefaults": {},
  "nocConfig": { "fields": [], "defaults": {} }
}
```

Contract fields and capabilities populate Interface `contractConfig` and `capabilities`; the NoC Package template populates `nocConfig`. These namespaces never merge. Conditions may reference only fields in their own namespace using local keys; cross-namespace conditional expressions are not V1.

### Domain type

```json
{
  "key": "clock",
  "label": "Clock Domain",
  "defaultName": "clock-default",
  "visual": {
    "fill": "#35557d",
    "border": "#7fb3ff",
    "pattern": "solid"
  },
  "configuration": { "fields": [] }
}
```

All V1 Domain types implicitly use total coverage, same-type exclusive membership, undirected structural connectivity, and Default assignment. A Package cannot override those semantics.

### Package Entity/Relation declaration

```json
{
  "typeKey": "vendor.entity",
  "ownership": "user",
  "genericEditable": true,
  "topologyDriving": false,
  "schema": {}
}
```

Allowed ownership: `user`, `engine`. A Provider may modify `engine` content only when the Provider manifest declares that type key. Unsupported schema constructs make the content opaque rather than Package-fatal.

An ownership=`engine` declaration MUST set `topologyDriving: false`; derived data cannot drive its own reconciliation.

Relation declaration:

```json
{
  "typeKey": "vendor.relation",
  "ownership": "user",
  "topologyDriving": false,
  "sources": {
    "kinds": ["router"],
    "minimum": 1,
    "maximum": 1
  },
  "targets": {
    "kinds": ["package-entity"],
    "minimum": 1,
    "maximum": 1
  },
  "unresolvedAllowed": false,
  "schema": {}
}
```

V1 endpoint cardinalities are non-negative integers with `maximum >= minimum`. Relation instances use Appendix A resolved/unresolved endpoint envelopes. Unresolved endpoints are legal only when the declaration opts in.

Each declaration's `sources.kinds[]` and `targets.kinds[]` is a set sorted by literal Unicode scalar-value order; duplicates are invalid.

## C3. Unknown Capability Fallback

Processing order:

1. Known Default Engine capability and valid known schema → specialized path.
2. Supported generic config/entity/relation schema → generic form/Patch/persistence path.
3. Namespaced extension with unsupported semantics → opaque tool-managed path.

Opaque content:

- is preserved in `.nocproj`;
- is included in tool Snapshot/input;
- has no editable V1 UI;
- may have a read-only owner/schema/size summary;
- cannot be modified by generic Patch.

Reject only malformed core envelope, missing identity, duplicate IDs, unsafe executable declaration, wrong type for a known standard field, or a declaration claiming a known capability while violating that capability's schema.

## C4. Interface Contract Package

```json
{
  "schema": "ipcraft.interface-contract.v1",
  "id": "arm.chi",
  "name": "CHI",
  "version": "1.0.0",
  "roles": [],
  "capabilities": [],
  "fields": []
}
```

Role:

```json
{
  "key": "requester",
  "label": "Requester"
}
```

Capability:

```json
{
  "key": "coherent",
  "type": "bool",
  "default": false,
  "required": true,
  "editable": true
}
```

Fields use the same scalar declaration subset as NoC Package configuration. Matching is generic:

- exact Contract ID and version lock;
- selected role is listed by both Interface template and Slot template;
- required declared capability/value constraints match;
- scalar field schema validates.

Effective value precedence is Contract default, then Interface-template `capabilityDefaults`/`contractConfigDefaults`, then persisted instance value. Template overrides must validate against the Contract and cannot change `required`, `editable`, type, or allowed values. A required capability/field without an effective value is invalid. Constraint matching in V1 is exact canonical scalar equality; no range/expression negotiation is implied. An instance edit of a non-editable capability is rejected.

Core code contains no AXI5, ACE, or CHI ID branch. V1 Contract support does not validate address allocation, endpoint composition, transactions, or coherence correctness.

## C5. Default Engine Bundle and Host Contract

Default Engine is an independently installable, immutable Bundle resolved exclusively by Project dependency `bundleManifestDigest`:

```json
{
  "schema": "ipcraft.engine-bundle.v1",
  "id": "ipcraft.default-noc-engine",
  "version": "1.0.0",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "engineCompatibilityVersion": "1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "migrationFromCompatibilityVersions": ["1"],
  "supportedPlatformAbis": ["linux-x86_64-gnu-v1"],
  "entrypoint": "lib/libipcraft_noc_engine.so"
}
```

The Engine manifest is inside its content-addressed installable Bundle and therefore does not contain its own bundle digest. Its metadata type `id` is exactly `ipcraft.default-noc-engine`; `version` is display metadata. The ID constant identifies the artifact type and never authorizes replacement. `engineCompatibilityVersion` classifies whether explicit migration may be offered; it never authorizes replacement. Engine/side-effect version fields are structurally open non-empty IDs so unsupported installed Bundles still parse. A different digest is always a different exact Engine implementation.

`ipcraft.engine-host.v1` is a C-compatible function-table ABI with no Qt or C++ standard-library types across the boundary. The shared library exports exactly `ipcraft_engine_host_v1_get_api`. It returns a size-versioned table containing `create`, `reconcile`, `release_buffer`, and `destroy`. `create` receives size-versioned Host allocator/log callbacks and returns an opaque Engine handle. `reconcile` is serialized per handle, accepts canonical UTF-8 JSON `normalizedTopologyInput`, `currentDerivedState`, and `reconcileApplicability` byte spans, and returns one canonical UTF-8 JSON `ipcraft.patch-body.v1` byte span plus optional structured diagnostics. Engine-owned output buffers remain valid until `release_buffer`; nonzero ABI status returns no Patch body. `destroy` releases the handle after outstanding calls finish. No callback supplies project paths, Qt objects, files, environment, clock, randomness, or network. The Host injects source identity, Session provenance, transaction ID, and Host IDs. Platform ABI or function-table size/version mismatch prevents loading.

The Host verifies exact Engine Bundle bytes before loading and records bundle digest and `engineHostContractVersion` in applicability, derivation, Pipeline provenance, and output freshness. A missing, revoked, corrupt, incompatible, or unsupported-platform Engine Bundle opens the project in degraded inspect mode; corruption uses `engine.bundle_mismatch`. A structurally valid Bundle unsupported by the current platform/Host ABI may be installed and retained in the content-addressed store but cannot resolve for execution. The Host MUST NOT fall back to another installed/built-in Engine with matching ID/version or compatibility class.

`upgrade-available` is an informational overlay on an exact normal resolution. It neither changes availability nor selects a digest. Engine Host or Host-side-effect freshness mismatch uses the existing output stale reason `dependency-changed`.

Host-owned invariant side effects use `hostSideEffectContractVersion: ipcraft.noc-side-effects.v1`. V1 fixes Router-created Default Membership, Router/Slot removal Attachment handling, Domain cleanup/connectivity evaluation, Package Relation unresolved/blocking handling, impact codes, and canonical side-effect localRef/order semantics in Appendix F. A Host that does not support the persisted side-effect contract opens degraded inspect mode; it cannot silently reinterpret an old project.

Engine migration is explicit and always produces a confirmable migration candidate. A target may be offered only when its manifest lists the persisted source `engineCompatibilityVersion`; this is discovery, never substitution. The target exact Engine reconciles current topology intent/Derived State under the target lock and does not execute the source Engine. One atomic transaction updates the `default-engine` dependency lock, Derived State, Host side effects, and derivation/provenance. It is never an automatic freshness repair. Undo uses the stored inverse transaction/localRef→Host-ID mapping and does not execute either Engine. Undo may leave the project in degraded inspect mode if the restored Engine Bundle is unavailable.

## C6. Package Bundle and Runtime Contract

Every resolved Package, Contract, Provider, and Tool sub-bundle has an `ipcraft.bundle-manifest.v1`:

```json
{
  "schema": "ipcraft.bundle-manifest.v1",
  "bundleId": "vendor.noc/provider",
  "bundleVersion": "1.0.0",
  "files": [
    {
      "path": "provider/main.py",
      "size": 1024,
      "digest": "sha256:...",
      "executable": false
    }
  ],
  "manifestDigest": "sha256:..."
}
```

Rules:

- V1 paths are portable normalized relative identifiers: UTF-8 NFC, `/`-separated, relative, case-sensitive, and composed of non-empty segments. They contain no C0 control (`U+0000`–`U+001F`), DEL (`U+007F`), colon, backslash, absolute or drive prefix, empty segment, `.` segment, or `..` segment.
- Semantic path validation rejects Windows reserved device names, segments ending in dot or space, duplicate normalized paths, Unicode simple-case-fold collisions, and any path escape. This portable contract intentionally rejects some filenames that are legal on a particular host so locked Bundles and execution views have one cross-platform identity.
- V1 NFC is pinned to Unicode 17.0.0 by [nfc-normalization-17.0.0.json](../../contracts/unicode/nfc-normalization-17.0.0.json): recursive canonical decomposition, canonical combining-class ordering, canonical composition excluding the pinned composition exclusions, and algorithmic Hangul decomposition/composition. [NormalizationTest-17.0.0.txt](../../contracts/unicode/NormalizationTest-17.0.0.txt) is the official committed conformance source. Normative validation never uses the host language's Unicode normalization database.
- V1 Unicode simple folding is pinned to Unicode 17.0.0 C/S mappings. The committed [simple-case-folding-17.0.0.json](../../contracts/unicode/simple-case-folding-17.0.0.json) table is the machine source: after pinned NFC validation, replace each code point only by its exact table target, leaving unlisted code points unchanged. Full/Turkic mappings, multi-code-point expansions, host language `casefold`/lowercase behavior, and silently newer host Unicode data are forbidden. Both Unicode artifacts pin official-source SHA-256 identities, counts, and full-array digests; Gate authoring regenerates and byte-compares them. The complete redistribution notice is [Unicode License V3](../../contracts/unicode/UNICODE-LICENSE.txt). A later Unicode version requires an explicit data/version migration and affected contract re-verification.
- Entries sort by normalized path before canonicalization. Regular files only; symlinks, hard-link aliases, devices, sockets, and other special files are rejected.
- `files[]` is exhaustive: it enumerates every regular file visible under that Bundle root except the Bundle Manifest document itself. Any unlisted entry, directory containing unlisted descendants, duplicate normalized path, Unicode/case-fold collision, link, special file, or path escape rejects installation/launch.
- Package, Contract, Engine, Provider, Runtime, and Tool sub-bundle roots are non-overlapping content-addressed roots. A process receives separate read-only execution views for only the exact locked roots declared in its dependency set; parent Bundle files are not implicitly visible to a child and child files are not included in a parent digest.
- `size` is the exact byte count; digest is over file bytes. `executable` records the bundle-declared executable bit independently of host filesystem representation.
- The manifest document is metadata and is not listed in its own `files`. `manifestDigest` is SHA-256 over RFC 8785 canonical JSON of the manifest with `manifestDigest` omitted.
- No file listed in a bundle may contain a normative field claiming that same bundle's manifest digest. Self-digest identity is supplied only by the parent dependency lock/launch context; a child file may reference different parent or dependency bundle digests.
- The NoC Package root, every Contract, Provider, DRC tool, and Generator tool use distinct sub-bundle manifests. Dependency locks reference the relevant manifest digest; Contract references include the expected Contract lock/digest rather than ID/version alone.
- The host verifies the selected manifest and every file before each process launch. A changed or missing file yields degraded mode or `dependency.bundle_mismatch`.

Runtime lock:

```json
{
  "kind": "host-managed",
  "runtimeId": "python",
  "version": "3.12.4",
  "runtimeDistributionBundleDigest": "sha256:...",
  "entrypoint": "bin/python3",
  "platformAbi": "linux-x86_64-gnu-v1",
  "invocationProfile": "ipcraft.python-isolated.v1",
  "moduleSearchPolicy": "runtime-and-tool-bundles-only",
  "environmentProfile": "ipcraft.empty-utf8-utc.v1",
  "networkPolicy": "prohibited"
}
```

V1 permits `package-contained` runtime closures and a small host-managed runtime registry. Bare PATH lookup is forbidden. `runtimeDistributionBundleDigest` identifies the complete verified runtime distribution, including interpreter/loader, standard library, startup files, and locked dynamic-library closure. For a host-managed runtime, `runtimeLockId` selects that distribution and `command` begins with the tool-bundle-relative script; the host constructs final argv without a shell. For a package-contained native runtime, the runtime bundle contains the executable and its dynamic dependency closure or references another exact runtime-distribution lock.

`platformAbi` is a Gate 0 catalog value covering operating-system ABI, CPU architecture, binary format, loader ABI, and C/C++ runtime family. `invocationProfile` fixes isolation flags such as Python `-I -S`, stdin behavior, cwd, locale, timezone, and startup-file suppression. `moduleSearchPolicy` permits imports/libraries only from the locked runtime and current tool bundles. `environmentProfile` identifies a canonical allowlist and fixed values; undeclared inherited environment is forbidden. V1 Provider/DRC/Generator manifests MUST declare `networkPolicy: prohibited` for reproducible operation. This is a behavioral contract and provenance assertion, not an OS security boundary without sandboxing.

Normative argv construction:

1. Resolve and verify the Runtime dependency and Tool/Provider Bundle by exact digest.
2. For `host-managed`, argv begins with the runtime-distribution `entrypoint`, followed by the exact fixed arguments of `invocationProfile`, followed by every literal `command[]` element after placeholder substitution.
3. For `package-contained`, argv begins with the runtime-closure `entrypoint`; manifest `command[]` contains only its following arguments. Its loader/library closure comes only from the locked runtime bundle.
4. Placeholder substitution operates on one argv element and never splits, quotes, expands environment variables, or invokes a shell.
5. cwd is `executionRoot/work`; stdin is closed unless the declared protocol uses it; environment is constructed solely from `environmentProfile`; module/library search variables are Host-generated only from verified runtime/tool execution-view roots.

V1 host policy defaults are: Provider handshake 5 seconds, reconcile 60 seconds, Tool run 30 minutes, and 64 MiB each for captured stdout/stderr. A Package may request longer reconcile/tool timeout up to host maxima of 10 minutes/4 hours; the effective values and any supported process-tree CPU/memory limits are recorded in run metadata. Exceeding a log limit terminates the process and reports a stable resource-limit failure.

The reproducibility claim is limited to verifiable replay with identical canonical ProjectDesign, bundle manifests/files, runtime locks, host compatibility class, and tool input. Cross-platform bit-for-bit artifact equality is not promised.

## C7. Extension Provider Manifest

```json
{
  "protocol": "ipcraft.provider-protocol.v1",
  "runtimeLockId": "runtime.python-3.12.4",
  "command": ["provider/main.py"],
  "capabilities": ["reconcile", "preview"],
  "ownedEntityTypes": ["vendor.engine-entity"],
  "ownedRelationTypes": [],
  "requestedTimeoutSeconds": 60,
  "environmentProfile": "ipcraft.empty-utf8-utc.v1",
  "networkPolicy": "prohibited"
}
```

Rules:

- Command elements are literal argv entries; shell interpolation is forbidden.
- `command[]` is ordered and retains argv order. `capabilities[]`, `ownedEntityTypes[]`, and `ownedRelationTypes[]` are literal-string sets sorted by Unicode scalar-value order with duplicates invalid. Provider capability strings are not Package/Contract declaration objects and are not runtime capability values.
- Relative executable/script paths resolve under the immutable Provider sub-bundle root.
- The Provider manifest MUST NOT contain its own Provider bundle digest. The external project dependency lock plus Package `providerLockId`/`manifestPath` binds it to the verified bundle; the host carries that digest in launch context, handshake checks, applicability, transaction provenance, and diagnostics.
- The host verifies the externally locked Provider bundle and runtime closure before every launch.
- One process exists per open DesignSession and Package.
- One request is active at a time.
- One NDJSON line is one message and is limited to 16 MiB including UTF-8 bytes and newline.
- stdout contains protocol messages only; stderr contains logs.
- Binary payloads are forbidden in V1.

## C8. Provider Handshake

Host request:

```json
{
  "type": "hello",
  "id": "hello-1",
  "protocol": "ipcraft.provider-protocol.v1",
  "hostVersion": "1",
  "package": {
    "id": "vendor.noc",
    "version": "1.0.0",
    "bundleManifestDigest": "sha256:..."
  },
  "provider": {
    "lockId": "dep.provider",
    "bundleManifestDigest": "sha256:...",
    "runtimeLockId": "runtime.python-3.12.4",
    "runtimeDistributionBundleDigest": "sha256:..."
  },
  "launchContextDigest": "sha256:...",
  "requestedCapabilities": ["reconcile", "preview"]
}
```

Provider response:

```json
{
  "type": "helloResult",
  "id": "hello-1",
  "protocol": "ipcraft.provider-protocol.v1",
  "providerIdentity": "vendor.provider",
  "providerVersion": "1.0.0",
  "launchContextDigest": "sha256:...",
  "capabilities": ["reconcile"]
}
```

`launchContextDigest` hashes the canonical Package lock, Provider lock, Runtime Closure lock/profile, protocol, and requested capabilities supplied by the host. The Provider echoes the received value; missing protocol/capability, identity mismatch, or echo mismatch terminates the process and produces `provider.handshake_failed`. The echo binds the live conversation without embedding the bundle digest in the Provider manifest.

## C9. Provider Request/Response

Request:

```json
{
  "type": "request",
  "id": "req-1",
  "method": "reconcile",
  "params": {
    "applicability": {
      "schema": "ipcraft.reconcile-applicability.v1",
      "groupId": "group-id",
      "requestGeneration": 3,
      "topologyInputRevision": 4,
      "topologyInputDigest": "sha256:...",
      "baseDerivedStateRevision": 7,
      "baseDerivedStateDigest": "sha256:...",
      "baseAuthoritativeDesignDigest": "sha256:...",
      "structureAuthority": {},
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
}
```

The abbreviated `applicability`, `normalizedTopologyInput`, and `currentDerivedState` objects above MUST validate against the exact Appendix F machine models; `{}` is editorial elision, not an open object. Provider ABI may wrap but cannot extend them.

`dependencyLocks[]` is a set sorted by `lockId`. `capabilities[]` contains runtime capability values and sorts by the UTF-8 bytes of each value's RFC 8785 canonical JSON after nested set normalization. This is distinct from Package/Contract capability declarations sorted by `key` and Provider/Tool declared capability string lists sorted literally.

Progress event:

```json
{
  "type": "progress",
  "requestId": "req-1",
  "fraction": 0.5,
  "message": "Deriving topology"
}
```

Success:

```json
{
  "type": "result",
  "id": "req-1",
  "result": {
    "patchBody": {
      "schema": "ipcraft.patch-body.v1",
      "applicability": {},
      "operations": []
    },
    "preview": null,
    "diagnostics": []
  }
}
```

For `reconcile`, `patchBody` is required even when `operations` is empty; JSON `null` is forbidden. For non-state methods such as `preview`, `patchBody` is JSON `null`. Provider returns no Patch ID, transaction ID, source identity, Host ID, or Session provenance. The Host verifies echoed applicability and constructs the complete trusted `ipcraft.patch.v1` envelope with source identity, `sessionRevision` provenance, candidate transaction ID, and normalized operations. Preview and diagnostics are non-state output. An unsupported `method` returns a structured error.

The reconcile payload schema is closed. It excludes complete ProjectDesign, Attachments, Domains, Draft Overlay, Views, diagnostics, runs, display-only metadata, and non-driving Interface data. A Provider returning a result for any non-matching applicability member is rejected before Patch validation. Provider creates use candidate-wide `authority:<token>` localRefs; the Host reserves `application:*`. When the Package selects `structureAuthority: extension-provider`, the Provider may mutate Router/Link/Slot and ownership=`engine` Package objects; otherwise such operations fail ownership validation.

Error:

```json
{
  "type": "error",
  "id": "req-1",
  "error": {
    "code": "provider.request_invalid",
    "message": "...",
    "data": {}
  }
}
```

V1 cancellation terminates and restarts the Provider process after a short host grace period; there is no cancellation message contract. Requests may be replayed only by incrementing the current Group's generation and rebuilding its closed payload after a successful handshake. Provider-local caches cannot be shared across DesignSessions.

## C10. Tool Manifest

Package tool declaration:

```json
{
  "tools": {
    "drc": {
      "id": "vendor.drc",
      "version": "1.0.0",
      "protocol": "ipcraft.tool-input.v1",
      "runtimeLockId": "runtime.python-3.12.4",
      "bundleManifestDigest": "sha256:...",
      "requestedTimeoutSeconds": 1800,
      "environmentProfile": "ipcraft.empty-utf8-utc.v1",
      "networkPolicy": "prohibited",
      "command": ["tools/drc.py", "{inputManifest}"]
    },
    "generate": {
      "id": "vendor.generator",
      "version": "1.0.0",
      "protocol": "ipcraft.tool-input.v1",
      "runtimeLockId": "runtime.python-3.12.4",
      "bundleManifestDigest": "sha256:...",
      "requestedTimeoutSeconds": 1800,
      "environmentProfile": "ipcraft.empty-utf8-utc.v1",
      "networkPolicy": "prohibited",
      "command": ["tools/generate.py", "{inputManifest}"]
    }
  }
}
```

These declarations live in the parent NoC Package manifest, outside each locked Tool sub-bundle, so their `bundleManifestDigest` fields do not hash a file that contains the same digest. Only documented literal placeholders are substituted. Shell execution and arbitrary environment expansion are forbidden. Manifest runtime/environment/network declarations must equal the referenced Runtime Lock/profile; a mismatch rejects launch.

Each Tool `command[]` is an ordered argv sequence. Any Tool manifest capability declaration string list is a set sorted by literal Unicode scalar-value order; it does not use the Package/Contract declaration-key or runtime capability-value comparator.

## C11. Tool Input Manifest

```json
{
  "schema": "ipcraft.tool-input.v1",
  "pipelineRunId": "pipeline-run-id",
  "stepId": "semantic-drc",
  "invocationId": "invocation-id",
  "toolLockId": "dep.tool.drc",
  "kind": "semantic-drc",
  "snapshotSessionRevision": 12,
  "snapshotDigest": "sha256:...",
  "formallySavedProjectDigest": "sha256:...",
  "dependencies": [],
  "projectDesignFile": "inputs/project-design.json",
  "resultFile": "reports/tool-result.json",
  "reportDirectory": "reports"
}
```

Every external invocation receives a unique Host-created execution root outside the project tree:

```text
executionRoot/
  inputs/project-design.json       read-only
  tool-input.json                  read-only
  work/                            cwd, tool-private scratch
  reports/                         tool result/diagnostics/manifests
  artifacts/                       Generator only
```

Every Tool Input path is a normalized relative path whose base is that invocation's `executionRoot`. The Host creates `inputs/project-design.json` and `tool-input.json`; tools may read but not replace them. Tools may write only beneath `work/`, `reports/`, and for Generator `artifacts/`. Generator input additionally requires `"outputDirectory": "artifacts"`; semantic DRC input omits it. `formallySavedProjectDigest` may be JSON `null` for standalone DRC; Generate requires it and requires equality with `snapshotDigest`. The external process receives no project path or report-archive path.

## C12. Tool Events and Result

stdout is UTF-8 NDJSON protocol only; stderr is raw log. Allowed live events:

```json
{ "type": "progress", "pipelineRunId": "pipeline-run-id", "stepId": "generator", "invocationId": "invocation-id", "sequence": 1, "fraction": 0.5, "message": "Generating" }
{ "type": "diagnostic", "pipelineRunId": "pipeline-run-id", "stepId": "generator", "invocationId": "invocation-id", "sequence": 2, "diagnostic": {} }
```

`sequence` strictly increases. Invalid stdout protocol marks the run failed but raw bytes remain in support logs. The final result file is:

```json
{
  "schema": "ipcraft.tool-result.v1",
  "pipelineRunId": "pipeline-run-id",
  "stepId": "generator",
  "invocationId": "invocation-id",
  "snapshotDigest": "sha256:...",
  "dependencySetDigest": "sha256:...",
  "status": "succeeded",
  "diagnosticReport": "reports/diagnostics.json",
  "artifactManifest": "reports/artifact-manifest.json",
  "metrics": {},
  "failure": null
}
```

Allowed tool-authored final status: `succeeded`, `failed`. Semantic DRC omits `artifactManifest`; Generator requires it on success. On normal process completion the tool MUST write the declared Tool Result. Process exit code zero plus a valid matching `succeeded` result is invocation success. Timeout, cancellation, crash, forced termination, nonzero exit, or invalid/missing result is non-success and may lack a raw Tool Result. Cancellation sends graceful termination, waits the recorded grace period, then force-terminates the process tree where supported.

`dependencySetDigest` is the Appendix B canonical digest of the resolved dependency locks sorted by `lockId`, including bundle manifest and runtime lock fields.

The host always writes one normalized `ipcraft.step-result.v1`, even when the process cannot write a Tool Result:

```json
{
  "schema": "ipcraft.step-result.v1",
  "pipelineRunId": "pipeline-run-id",
  "stepId": "generator",
  "stepKind": "external-tool",
  "invocationId": "invocation-id",
  "status": "timed-out",
  "toolLockId": "dep.tool.generator",
  "toolResult": null,
  "failure": {
    "code": "tool.timed_out",
    "source": "host"
  }
}
```

Allowed normalized step status: `succeeded`, `failed`, `cancelled`, `timed-out`, `skipped`. Host steps omit `invocationId` and use `stepKind: host`. A missing raw Tool Result remains missing; the host does not fabricate a tool-authored document.

## C13. Diagnostic Report

```json
{
  "schema": "ipcraft.diagnostic-report.v1",
  "pipelineRunId": "pipeline-run-id",
  "stepId": "semantic-drc",
  "invocationId": "invocation-id",
  "snapshotDigest": "sha256:...",
  "diagnostics": [
    {
      "ruleId": "address.overlap",
      "severity": "error",
      "message": "...",
      "blocking": true,
      "subjects": [{ "kind": "interface", "id": "interface-id" }],
      "properties": ["contractConfig.address"]
    }
  ]
}
```

Allowed severity: `info`, `warning`, `error`. `blocking: true` is authoritative for gating and is legal only with `severity: error`; `info`/`warning` MUST use `blocking: false`. Host structural-DRC reports use `invocationId: null`; external reports require the matching invocation ID. Any blocking diagnostic prevents Generate. Unknown Subject IDs remain visible but are marked unlocatable. Reports with the wrong pipeline/step/invocation/Snapshot digest are rejected.

## C14. Artifact Manifest

```json
{
  "schema": "ipcraft.artifact-manifest.v1",
  "pipelineRunId": "pipeline-run-id",
  "stepId": "generator",
  "invocationId": "invocation-id",
  "snapshotDigest": "sha256:...",
  "artifacts": [
    {
      "path": "rtl/top.sv",
      "kind": "rtl",
      "mediaType": "text/plain",
      "size": 1024,
      "digest": "sha256:..."
    }
  ]
}
```

Rules:

- Artifact paths are portable normalized relative identifiers under the C6 path contract: UTF-8 NFC, `/`-separated, with no C0/DEL controls, colon, backslash, absolute or drive prefix, empty segment, `.` segment, or `..` segment.
- Semantic validation rejects Windows reserved device names, trailing dot/space segments, Unicode simple-case-fold collisions, and any filesystem containment escape. This intentionally excludes some host-native legal filenames to preserve one cross-platform artifact and report identity.
- Artifact files must be regular files under staging; symlinks and special files are rejected.
- `artifacts[]` is exhaustive for the promoted output: every entry under execution `artifacts/` must appear exactly once. Unlisted files/directories, hard links, duplicate normalized paths, Unicode/case-fold collisions, symlinks, special files, and escapes reject verification.
- Host policy defines maximum entry count, per-file size, and total size; exceeding policy rejects promotion with stable diagnostics.
- Every manifest size/digest is verified before promotion.
- The Host builds a new clean promotion tree by copying only verified manifest entries in canonical path order. It never renames or promotes the arbitrary tool-written `artifacts/` directory itself.
- `diagnosticReport`, `artifactManifest`, Tool Input paths, Tool Result pointer paths, and archive-relative paths use the same C6 portable normalized-relative-path and containment checks, relative to their declared execution or report root.

## C15. Run, Pipeline, and Output Ownership

Every Validate/Generate operation is a parent pipeline. Generate has these ordered steps:

```text
structural-drc   host
semantic-drc     optional external invocation
generator        external invocation
artifact-verify  host
promotion        host
```

Standalone Validate omits Generator/artifact/promotion. Every step writes a normalized Step Result. The parent stores:

```text
reports/runs/<pipelineRunId>/pipeline.json
reports/runs/<pipelineRunId>/pipeline-result.json
reports/runs/<pipelineRunId>/project-design.json
reports/runs/<pipelineRunId>/steps/<stepId>/step-result.json
reports/runs/<pipelineRunId>/steps/<stepId>/invocations/<invocationId>/tool-input.json
reports/runs/<pipelineRunId>/steps/<stepId>/invocations/<invocationId>/tool-result.json (only if tool wrote one)
reports/runs/<pipelineRunId>/steps/<stepId>/invocations/<invocationId>/stdout.log
reports/runs/<pipelineRunId>/steps/<stepId>/invocations/<invocationId>/stderr.log
reports/runs/<pipelineRunId>/steps/semantic-drc/invocations/<invocationId>/diagnostics.json
reports/runs/<pipelineRunId>/steps/generator/invocations/<invocationId>/artifact-manifest.json
```

These paths are archive paths whose base is the project report root and are never passed to external processes. After each process terminates, the Host validates the complete execution root, normalizes raw evidence, and copies canonical inputs/results/manifests/logs into a newly created archive subtree while holding the project mutation lock. A raw Tool Result is archived only when present. Archive failure fails the step/pipeline but never grants the tool project access. The execution root is then removed according to crash-recovery policy.

`pipeline.json` is `ipcraft.pipeline-plan.v1` and contains pipeline identity/kind, Snapshot revision/digest, exact dependency bundle/runtime/Default-Engine locks, ordered step plan, resource policy, start time, and stale flag. `ipcraft.pipeline-result.v1` aggregates normalized steps:

```json
{
  "schema": "ipcraft.pipeline-result.v1",
  "pipelineRunId": "pipeline-run-id",
  "kind": "generate",
  "snapshotDigest": "sha256:...",
  "dependencySetDigest": "sha256:...",
  "status": "failed",
  "failedStepId": "semantic-drc",
  "steps": [
    { "stepId": "structural-drc", "result": "steps/structural-drc/step-result.json" },
    { "stepId": "semantic-drc", "result": "steps/semantic-drc/step-result.json" },
    { "stepId": "generator", "result": "steps/generator/step-result.json" },
    { "stepId": "artifact-verify", "result": "steps/artifact-verify/step-result.json" },
    { "stepId": "promotion", "result": "steps/promotion/step-result.json" }
  ],
  "startedAt": "2026-07-14T00:00:00Z",
  "completedAt": "2026-07-14T00:00:01Z"
}
```

Allowed pipeline status: `succeeded`, `failed`, `cancelled`, `timed-out`. Steps not started after failure are recorded `skipped`. Cancellation targets the active invocation, then marks all remaining steps skipped. Pipeline identity, step identity, tool lock, timeout, and failure source are therefore never inferred from a shared log.

Generate always executes Core Structural DRC, then optional semantic DRC, then Generator, then artifact verification, all for the same Snapshot/dependency set. Missing, stale, failed, or blocking semantic DRC prevents Generator launch. The pipeline never reuses a report from another Snapshot or dependency set and never lets one invocation overwrite another invocation's files.

- Old-Snapshot results are historical and never replace current Problems without a stale badge.
- Only the latest eligible successful Generate for the current formally saved Snapshot may promote canonical output, while holding the project mutation lock.
- `output/.ipcraft-output.json` conforms to `ipcraft.output-manifest.v1` and records promoted pipeline ID, Snapshot digest, dependency-set digest, Default Engine bundle digest, Engine Host contract, Host side-effect contract, artifact-manifest digest, and completion time. Freshness rules are defined below.
- Run history is removed only through explicit Clear Run History; the current promoted manifest and logs required for its provenance are retained.
- Default project `output/` uses same-filesystem sibling staging and rollback-safe promotion.
- An overridden output path is supported only when the host can create and rename sibling staging on the same filesystem. Otherwise Generate fails with `tool.output_filesystem_unsupported`; cross-volume copy promotion is not a V1 feature.

Output state is computed, not manually stored:

- `current-canonical` only when promoted Snapshot digest equals both the current authoritative ProjectDesign digest and the formally saved project digest, dependency-set and exact Default Engine digests still match, Engine Host/Host side-effect contract versions still match, and no Pending Topology Group or Draft Overlay exists;
- `last-successful-stale` after any accepted unsaved edit, dependency change, Pending Group, Draft Overlay, or mismatch with the formally saved digest;
- `none` when no successful promotion exists.

The UI lists stale reasons separately (`authoritative-design-changed`, `not-formally-saved`, `dependency-changed`, `pending-topology`, `draft-overlay`). Pending/Draft state does not alter the promoted manifest; it changes the computed presentation state.

## C16. Package Set for Gates

Shipped V1 Packages:

```text
finepaper.noc          directory: ipcores/finepaper-noc
finepaper.ravenoc      directory: ipcores/ravenoc
finepaper.opennoc      directory: ipcores/opennoc
```

Fixture only:

```text
vendor.meshnoc         directory: ipcores/vendor-meshnoc
```

`vendor.meshnoc` is retained as an onboarding/architecture fixture and is not counted as a shipped selectable Package in Gate F. Existing `1.0` strings in pre-release manifests do not constitute the product's stable compatibility baseline; migrated V1 manifests are reissued under the new schema before stable release.

## C17. Minimum Stable Runtime Error Codes

```text
dependency.bundle_mismatch
dependency.runtime_mismatch
engine.bundle_missing
engine.bundle_revoked
engine.bundle_mismatch
engine.host_contract_unsupported
engine.platform_unsupported
host.side_effect_contract_unsupported
provider.handshake_failed
provider.message_too_large
provider.protocol_invalid
provider.timeout
provider.crashed
tool.result_missing
tool.result_invalid
tool.result_mismatch
tool.exit_nonzero
tool.cancelled
tool.timed_out
tool.stdout_limit_exceeded
tool.stderr_limit_exceeded
tool.artifact_invalid
tool.output_filesystem_unsupported
pipeline.step_failed
pipeline.cancelled
pipeline.timed_out
pipeline.result_invalid
project.locked
project.concurrent_modification
```

Gate 0's machine-readable error catalog may add codes but cannot rename or overload these meanings after Core freeze. Extension-specific Provider errors may be added until Extension ABI freeze at Gate D.
