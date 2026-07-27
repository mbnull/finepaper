# Default NoC Gates A-C Headless Vertical Slice Implementation Plan — Revision 2

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use an isolated Git worktree created with `superpowers:using-git-worktrees`; never implement directly in a dirty primary workspace.

**Goal:** Deliver the isolated headless V1 path from strict ProjectDesign loading through deterministic Mesh materialization, formal save/reopen, Contract-level Interfaces, DRC/Generate, archived evidence, and transactional output promotion.

**Architecture:** Gate A builds a production contract runtime, typed ProjectDesign aggregate, atomic Patch engine, ordinary typed-command history, repository, and project lock without linking a legacy authority. Gate B moves Package/Contract and exact Bundle resolution ahead of the independently installable Default Engine, then adds Mesh reconciliation, the single Pending Topology Group, Draft Overlay, candidate confirmation, Host side effects, Attachments, Domains, validation modes, and recovery. Gate C migrates the first declarative Package and adds the isolated tool pipeline, reports, artifact verification, and output promotion.

**Tech Stack:** Qt 6 Core/Concurrent/Test, C++23, xmake, frozen Gate 0 Revision 5 schemas/vectors, RFC 8785 binary64 canonical JSON, pinned Unicode 17 path rules, `QSaveFile`, `QLockFile`, `QLibrary`, and `QProcess`.

**Normative inputs:**

- `docs/contracts/GATE-STATUS.md` — Gate 0 Revision 5 freeze and required regression commands.
- `docs/superpowers/specs/appendix-a-project-design-v1-contract.md` — persisted aggregate, identity, dependency locks, save and lock rules.
- `docs/superpowers/specs/appendix-b-patch-command-reconciliation-contract.md` — ownership, Patch, commands, history, Pending Group, validation modes, and recovery.
- `docs/superpowers/specs/appendix-c-package-contract-provider-tool-contract.md` — Package/Contract, exact Engine ABI, Bundle/runtime/tool contracts.
- `docs/superpowers/specs/appendix-e-gate-acceptance-matrix.md` — normative Gate A-C commands and scenarios.
- `docs/superpowers/specs/appendix-f-core-canonical-models.md` — canonical projections, sorting, side effects, Engine and migration vectors.

---

## Execution Rules

1. Do not modify a frozen Gate 0 schema, fixture, vector, error code, or normative spec to make implementation tests pass. Stop and request a formal unfreeze if a real contradiction is found.
2. Run the eight Gate 0 Qt checks and the required Python conformance checks at the end of every Gate. A Gate 0 regression blocks the Gate commit.
3. Preserve unrelated user changes. The primary workspace may be dirty; implementation happens in a dedicated worktree and merges only after review.
4. Use TDD. Each task begins with a focused failing test, implements the smallest closed behavior, runs its own test set, then commits.
5. No raw Patch is accepted from UI-facing/Application callers. Typed commands compile into trusted internal Patch transactions.
6. No new-path target may include or link `ProjectService`, `ProjectDocument`, legacy Graph, legacy `CommandManager`, or the legacy save/projection synchronization chain.
7. Stop for review after Gate A, Gate B, and Gate C. Do not start the next Gate merely because the previous task compiled.

## Target and File Boundaries

Create these seven production targets in `qt/xmake.lua` across Tasks 1 and 7:

```text
ipcraft_noc_contract_runtime   strict JSON, schema catalog/subset, RFC 8785, canonical collection rules
ipcraft_noc_domain             typed aggregate, IDs, Patch, validation, DRC; depends only on contract runtime + Qt Core
ipcraft_noc_application        DesignSession, commands, history, reconciliation and run ports; depends on domain
ipcraft_noc_infrastructure     repository, lock, Bundle/Engine/tool/process/filesystem adapters; depends on application
ipcraft_default_noc_engine     independently installable shared-library Bundle; no dependency on application/infrastructure
ipcraft_legacy_application     existing legacy implementation, excluding every `src/noc/v1/**` file
ipcraft_startup_launcher       current thin executable composition root; Gate A-C launches legacy only
```

The existing `qt` target may remain as a compatibility alias to `ipcraft_startup_launcher`, but it must not compile `src/**.cpp` after new-path sources exist. Replace the broad ownership of sources with an explicit legacy source set or an explicit `remove_files("src/noc/v1/**.cpp")` exclusion verified by the architecture test.

Create these directories:

```text
qt/inc/noc/v1/contract/          production strict JSON, schema and canonical runtime
qt/src/noc/v1/contract/
qt/inc/noc/v1/domain/            Qt-free-of-Widgets aggregate and mutation rules
qt/src/noc/v1/domain/
qt/inc/noc/v1/application/       typed use cases and ports
qt/src/noc/v1/application/
qt/inc/noc/v1/infrastructure/    filesystem/process/dynamic-library adapters
qt/src/noc/v1/infrastructure/
qt/default-engine/               immutable Engine Bundle source and manifest
qt/test/noc/                     Gate A-C tests
qt/test/support/noc/             builders, fakes and test-only process helpers
ipcores/finepaper-noc-v1/        first migrated declarative Package
```

Do not reuse `qt/test/support/noc_contract/*` directly from production. Promote shared algorithms into `ipcraft_noc_contract_runtime`, then make Gate 0 test support call the production implementation or byte-compare it against the frozen vectors.

---

## Gate A — Headless Core

### Task 1: Split the build graph and freeze the architecture boundary

**Files:**

- Modify: `qt/xmake.lua`
- Create: `qt/test/noc/noc_architecture_boundary_test.cpp`
- Create: `qt/test/support/noc/architecturepaths.h`

- [ ] **Step 1: Write the failing source/link boundary test**

Scan `qt/inc/noc/v1` and `qt/src/noc/v1` for forbidden includes/symbols and inspect generated target dependency metadata:

```cpp
const QStringList forbidden = {
    "project/projectdocument.h",
    "project/projectservice.h",
    "project/projectdesignserializer.h",
    "graph/graph.h",
    "commands/commandmanager.h",
    "project/designeditingservice.h",
    "project/projectstateservice.h",
    "project/projectipservice.h"
};
```

The test also fails if a legacy target source list contains `src/noc/v1/`, or if a new-path library depends on `ipcraft_legacy_application`/`ipcraft_startup_launcher`.

- [ ] **Step 2: Define the first six isolated targets**

Use separate source lists and dependency direction:

```lua
target("ipcraft_noc_contract_runtime")
    add_rules("qt.static")
    set_languages("c++23")
    add_frameworks("QtCore")
    add_files("src/noc/v1/contract/*.cpp")
    add_includedirs("inc", {public = true})

target("ipcraft_noc_domain")
    add_rules("qt.static")
    add_deps("ipcraft_noc_contract_runtime")
    add_files("src/noc/v1/domain/*.cpp")
    add_includedirs("inc", {public = true})

target("ipcraft_noc_application")
    add_rules("qt.static")
    add_deps("ipcraft_noc_domain")
    add_files("src/noc/v1/application/*.cpp")
    add_includedirs("inc", {public = true})

target("ipcraft_noc_infrastructure")
    add_rules("qt.static")
    add_deps("ipcraft_noc_application")
    add_files("src/noc/v1/infrastructure/*.cpp")
    add_includedirs("inc", {public = true})
```

Split the existing application source set into `ipcraft_legacy_application`; keep only `src/app/main.cpp` in the thin launcher. Explicitly exclude `src/noc/v1/**.cpp` from the legacy set.

Task 7 adds `ipcraft_default_noc_engine` when Engine sources exist.

- [ ] **Step 3: Add a reusable new-path test helper**

Add `add_noc_v1_test_target(name, deps)` so later tests declare only the libraries they require and do not inherit legacy sources.

- [ ] **Step 4: Inspect the empty new-path targets, then build the legacy launcher**

Before their source files exist, verify dependency metadata with `xmake show`; do not add meaningless placeholder translation units merely to archive an empty static library. Run:

```bash
xmake show -P qt -t ipcraft_noc_contract_runtime
xmake show -P qt -t ipcraft_noc_domain
xmake show -P qt -t ipcraft_noc_application
xmake show -P qt -t ipcraft_noc_infrastructure
xmake build -P qt qt
xmake run -P qt noc_architecture_boundary_test
```

Expected: all pass; the launcher still starts the legacy composition root only.

- [ ] **Step 5: Commit**

```bash
git add qt/xmake.lua qt/test/noc/noc_architecture_boundary_test.cpp qt/test/support/noc/architecturepaths.h
git commit -m "build: isolate NoC V1 architecture targets"
```

### Task 2: Promote Gate 0 strict JSON and canonicalization into production

**Files:**

- Create: `qt/inc/noc/v1/contract/strictjson.h`
- Create: `qt/src/noc/v1/contract/strictjson.cpp`
- Create: `qt/inc/noc/v1/contract/canonicaljson.h`
- Create: `qt/src/noc/v1/contract/canonicaljson.cpp`
- Create: `qt/inc/noc/v1/contract/schemacatalog.h`
- Create: `qt/src/noc/v1/contract/schemacatalog.cpp`
- Create: `qt/inc/noc/v1/contract/coreschemavalidator.h`
- Create: `qt/src/noc/v1/contract/coreschemavalidator.cpp`
- Modify: `qt/test/support/noc_contract/strictjson.cpp`
- Modify: `qt/test/support/noc_contract/canonicaljson.cpp`
- Create: `qt/test/noc/noc_contract_runtime_test.cpp`

- [ ] **Step 1: Write production-runtime conformance tests**

Load the frozen strict JSON, RFC 8785 and canonical collection vectors through the production API. Include duplicate keys, malformed UTF-8, lone surrogates, `1.0`, `-0.0`, `1e-7`, binary64 boundaries, UTF-16 key ordering, and randomized set-array permutations.

```cpp
const auto admitted = StrictJsonReader::parse(bytes);
QVERIFY(admitted.hasValue());
QCOMPARE(CanonicalJson::encode(admitted->value(), rules), expectedBytes);
QCOMPARE(CanonicalJson::sha256(admitted->value(), rules), expectedDigest);
```

- [ ] **Step 2: Implement byte-level admission before `QJsonDocument`**

Reject duplicate decoded property names, invalid UTF-8, lone surrogates, non-finite/out-of-domain numbers, and integers not exactly representable in the frozen binary64 domain before Qt parsing loses lexical evidence. Do not preserve an `int` versus `double` spelling distinction; semantic field declarations decide allowed values.

- [ ] **Step 3: Implement the frozen canonical runtime**

Use ECMAScript/RFC 8785 binary64 number serialization, UTF-16 code-unit object-key ordering, and the per-JSON-pointer set/ordered rules from the frozen catalog. Do not call `QJsonDocument::toJson()` for digest bytes.

- [ ] **Step 4: Implement normative schema catalog resolution**

Load `docs/contracts/schema-catalog.json` and resolve the frozen IDs through the catalog. Implement only the JSON Schema 2020-12 keywords used by the frozen Core schemas; fail closed on an unsupported keyword rather than silently ignoring it.

- [ ] **Step 5: Make Gate 0 Qt support exercise production code**

Turn existing test-support functions into thin adapters or byte-comparison oracles so Gate 0 and product implementations cannot drift into two canonicalization algorithms.

- [ ] **Step 6: Run conformance**

```bash
xmake run -P qt noc_contract_runtime_test
xmake run -P qt noc_strict_json_test
xmake run -P qt noc_canonical_digest_vectors_test
PYTHONDONTWRITEBYTECODE=1 python3 -B docs/contracts/tools/test_rfc8785.py
PYTHONDONTWRITEBYTECODE=1 python3 -B docs/contracts/tools/verify_canonical_vectors.py
```

- [ ] **Step 7: Commit**

```bash
git add qt/inc/noc/v1/contract qt/src/noc/v1/contract qt/test/support/noc_contract qt/test/noc/noc_contract_runtime_test.cpp qt/xmake.lua
git commit -m "feat: add production NoC contract runtime"
```

### Task 3: Implement ProjectDesign V1 and the three validation modes

**Files:**

- Create: `qt/inc/noc/v1/domain/projectdesign.h`
- Create: `qt/inc/noc/v1/domain/subjectref.h`
- Create: `qt/inc/noc/v1/domain/dependencylock.h`
- Create: `qt/inc/noc/v1/domain/diagnostic.h`
- Create: `qt/inc/noc/v1/domain/projectvalidator.h`
- Create: `qt/src/noc/v1/domain/projectdesign.cpp`
- Create: `qt/src/noc/v1/domain/projectvalidator.cpp`
- Create: `qt/test/noc/noc_project_design_contract_test.cpp`
- Create: `qt/test/support/noc/projectdesignbuilder.h`

- [ ] **Step 1: Write fixture-driven decode and validation tests**

Decode every relevant `ipcraft.project-design.v1` fixture into typed values and compare stable error codes. Add one working design with a disconnected Domain and matching `domain.disconnected` diagnostic:

```cpp
QVERIFY(ProjectValidator::validate(design, diagnostics,
    ValidationMode::WellFormed).accepted);
QVERIFY(ProjectValidator::validate(design, diagnostics,
    ValidationMode::CommitValid).accepted);
QVERIFY(!ProjectValidator::validate(design, diagnostics,
    ValidationMode::SaveEligible).accepted);
```

- [ ] **Step 2: Define the complete typed aggregate**

Use strongly typed opaque IDs and explicit structs for Project root, Component, Interface, Router, StructuralLink, AccessSlot, Attachment, Domain, DomainMembership, PackageEntity, PackageRelation, DependencyLock, View, configuration namespaces, and derivation metadata. IDs are project-global and never parsed for coordinates or semantics.

- [ ] **Step 3: Implement typed decode/encode through the contract runtime**

Unknown Package-owned data remains opaque JSON in its declared namespace. Core-known fields are closed and validated. Canonical encoding applies the catalog's exact collection rules.

- [ ] **Step 4: Implement explicit validation modes**

`WellFormed` admits readable working/recovery states and requires matching structural diagnostics. `CommitValid` accepts an atomic authoritative materialization, including a disconnected Domain only with the transaction's blocking diagnostic. `SaveEligible` additionally requires current Derived State, resolved Attachments, connected Domains, and no blocking diagnostic.

- [ ] **Step 5: Run tests**

```bash
xmake run -P qt noc_project_design_contract_test
xmake run -P qt noc_contract_fixture_catalog_test
```

- [ ] **Step 6: Commit**

```bash
git add qt/inc/noc/v1/domain qt/src/noc/v1/domain qt/test/noc/noc_project_design_contract_test.cpp qt/test/support/noc/projectdesignbuilder.h qt/xmake.lua
git commit -m "feat: add typed NoC ProjectDesign validation"
```

### Task 4: Implement the atomic Patch engine and Host ID allocation

**Files:**

- Create: `qt/inc/noc/v1/domain/patch.h`
- Create: `qt/inc/noc/v1/domain/patchengine.h`
- Create: `qt/inc/noc/v1/domain/hostidallocator.h`
- Create: `qt/src/noc/v1/domain/patch.cpp`
- Create: `qt/src/noc/v1/domain/patchengine.cpp`
- Create: `qt/src/noc/v1/domain/hostidallocator.cpp`
- Create: `qt/test/noc/noc_patch_engine_test.cpp`

- [ ] **Step 1: Write failing Patch algebra tests**

Cover create/update/delete entity and relation, project-root rename, merge/delete/null property semantics, ownership, preconditions, references, no implicit cascade, tombstones, inverse generation, and complete rollback:

```cpp
const QByteArray before = canonicalProjectDigest(design);
const auto result = engine.apply(design, invalidSecondOperationPatch, context);
QCOMPARE(result.error().code, QString("patch.ownership_violation"));
QCOMPARE(canonicalProjectDigest(design), before);
```

- [ ] **Step 2: Implement trusted Patch envelopes and source rules**

Represent `ordinary-command`, `topology-candidate`, `engine-migration`, and replay contexts explicitly. The Host constructs trusted source/provenance; an Engine later returns only a Patch body.

- [ ] **Step 3: Implement candidate-wide local references and allocation**

Validate the combined operation order, sort localRefs by the frozen canonical allocation order, allocate opaque Host IDs only inside atomic commit, rewrite all refs, and record the mapping. Rejected/discarded candidates publish and consume no visible IDs.

- [ ] **Step 4: Implement inverse/tombstone replay**

Deletion inverses carry complete tombstones. Formal replay restores recorded IDs and values without invoking an Engine or deriving from coordinates.

- [ ] **Step 5: Run tests and commit**

```bash
xmake run -P qt noc_patch_engine_test
git add qt/inc/noc/v1/domain qt/src/noc/v1/domain qt/test/noc/noc_patch_engine_test.cpp qt/xmake.lua
git commit -m "feat: add atomic NoC Patch transactions"
```

### Task 5: Add DesignSession, ordinary commands, formal history, save, and lock

**Files:**

- Create: `qt/inc/noc/v1/application/designsession.h`
- Create: `qt/inc/noc/v1/application/command.h`
- Create: `qt/inc/noc/v1/application/commanddispatcher.h`
- Create: `qt/inc/noc/v1/application/commandhistory.h`
- Create: `qt/inc/noc/v1/application/projectrepository.h`
- Create: `qt/src/noc/v1/application/designsession.cpp`
- Create: `qt/src/noc/v1/application/commanddispatcher.cpp`
- Create: `qt/src/noc/v1/application/commandhistory.cpp`
- Create: `qt/inc/noc/v1/infrastructure/jsonprojectrepository.h`
- Create: `qt/src/noc/v1/infrastructure/jsonprojectrepository.cpp`
- Create: `qt/inc/noc/v1/infrastructure/projectlock.h`
- Create: `qt/src/noc/v1/infrastructure/projectlock.cpp`
- Create: `qt/test/noc/noc_command_history_test.cpp`
- Extend: `qt/test/noc/noc_project_design_contract_test.cpp`

- [ ] **Step 1: Test only Gate A ordinary commands**

Cover `RenameDesign`, `RenameNoCComponent`, `CreateInterfaceFromTemplate` as an unattached Interface, `RenameInterface`, `ChangeInterfaceConfiguration`, and `DeleteInterface` when no Attachment exists. Attachment, Domain and topology commands remain Gate B tests.

- [ ] **Step 2: Define distinct formal history record variants**

```cpp
using FormalHistoryRecord = std::variant<
    OrdinaryCommandTransaction,
    MaterializedTopologyTransaction,
    EngineMigrationTransaction>;
```

Gate A implements ordinary records; Gate B fills topology/migration records. Every replay assigns new monotonic revisions. No replay calls an Engine.

- [ ] **Step 3: Compile typed commands into internal Patch**

The public dispatcher accepts typed command values and returns `ipcraft.command-result.v1`. Patch types remain private to Application/Domain collaboration.

- [ ] **Step 4: Implement strict repository load/save**

Load bytes through strict admission, schema/typed decoding, `WellFormed`, then dependency resolution later. Save requires `SaveEligible`, uses `QSaveFile`, and compares the current disk digest with `lastSavedDigest`; mismatch returns `project.concurrent_modification`.

- [ ] **Step 5: Implement the exact project lock**

Use `QLockFile` at `.workspace/project.lock`. A second process opens inspect-only and cannot write `.workspace`, recovery, reports, or output. Do not leave the primitive as “or equivalent”.

- [ ] **Step 6: Run Gate A acceptance**

```bash
xmake run -P qt noc_project_design_contract_test
xmake run -P qt noc_patch_engine_test
xmake run -P qt noc_command_history_test
xmake run -P qt noc_architecture_boundary_test
xmake build -P qt qt
```

Then run all eight Gate 0 Qt checks and the required Python checks listed in `docs/contracts/GATE-STATUS.md`.

- [ ] **Step 7: Record Gate A and stop for review**

Update only the Gate A section of `docs/contracts/GATE-STATUS.md` with implementation commit/toolchain/test evidence. Commit:

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/test/support/noc qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate A headless core"
```

Do not begin Gate B until the Gate A review accepts build isolation, validation modes, Patch atomicity, ordinary history, formal save, and two-process locking.

---

## Gate B — Mesh, Identity, Reconciliation, Domain

### Task 6: Implement Package/Contract resolution and exhaustive Bundle verification

**Files:**

- Create: `qt/inc/noc/v1/domain/nocpackage.h`
- Create: `qt/inc/noc/v1/domain/interfacecontract.h`
- Create: `qt/inc/noc/v1/application/packagecatalog.h`
- Create: `qt/src/noc/v1/application/packagecatalog.cpp`
- Create: `qt/inc/noc/v1/infrastructure/bundleverifier.h`
- Create: `qt/src/noc/v1/infrastructure/bundleverifier.cpp`
- Create: `qt/inc/noc/v1/infrastructure/contentaddressedbundlestore.h`
- Create: `qt/src/noc/v1/infrastructure/contentaddressedbundlestore.cpp`
- Create: `qt/test/noc/noc_bundle_manifest_test.cpp`
- Create: `qt/test/noc/noc_package_contract_test.cpp`
- Create: `qt/test/support/noc/bundles/minimal-mesh/` fixture Package, Contract, Engine and exhaustive manifests

- [ ] **Step 1: Write hostile Bundle tests**

Reject missing/unlisted files, extra files, duplicate normalized paths, Unicode/case-fold collisions, path escape, symlink, hard-link alias, special file, size/digest mismatch, non-exhaustive sub-bundle roots, and revoked digests.

- [ ] **Step 2: Implement pinned portable path validation**

Use the committed Unicode 17 NFC and simple-folding data, not host Unicode normalization/case folding. Build a read-only verified view containing exactly manifest-listed regular files.

- [ ] **Step 3: Implement exact dependency resolution**

Resolve Package, Contract, Default Engine, Runtime and Tool by `bundleManifestDigest`. IDs/versions are metadata only. Missing, corrupt, revoked, platform-incompatible or Host-contract-incompatible dependencies produce the specified degraded-inspect reason without fallback.

- [ ] **Step 4: Decode the V1 Package/Contract subset**

Support global/Interface/Domain scalar declarations, Mesh/Router/StructuralLink/Slot templates, exact Contract locks, capabilities, roles, and opaque fallback namespaces. Preserve unsupported generic content through the declared default/opaque path; do not add Core behavior for it.

Use the self-contained `minimal-mesh` test Bundle for Gate B. Do not make Gate B depend on the public `finepaper.noc` migration scheduled for Gate C.

- [ ] **Step 5: Run tests and commit**

```bash
xmake run -P qt noc_bundle_manifest_test
xmake run -P qt noc_package_contract_test
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua
git commit -m "feat: resolve exact NoC bundles and package contracts"
```

### Task 7: Build and load the exact Default Engine function-table ABI

**Files:**

- Create: `qt/default-engine/inc/ipcraft_engine_host_v1.h`
- Create: `qt/default-engine/src/engineapi.cpp`
- Create: `qt/default-engine/src/defaultmeshengine.cpp`
- Create: `qt/default-engine/engine.json`
- Create: `qt/inc/noc/v1/application/structureauthority.h`
- Create: `qt/inc/noc/v1/infrastructure/enginehost.h`
- Create: `qt/src/noc/v1/infrastructure/enginehost.cpp`
- Create: `qt/test/noc/noc_default_engine_host_test.cpp`

- [ ] **Step 1: Write ABI and exact-lock failure tests**

Test missing export, short/unknown function table, wrong platform ABI, wrong Engine Host contract, wrong Host side-effect contract, manifest/lock digest mismatch, revoked Bundle, allocator failure, malformed output, and absence of built-in fallback.

- [ ] **Step 2: Define the exact exported API**

The shared library exports only a getter for the frozen size-versioned table. `create`, rather than the getter, receives the Host callback table:

```c
IPCRAFT_ENGINE_EXPORT int ipcraft_engine_host_v1_get_api(
    uint32_t requested_abi_version,
    uint32_t caller_engine_api_size,
    ipcraft_engine_host_v1_engine_api *out_engine_api);

struct ipcraft_engine_host_v1_engine_api {
    uint32_t struct_size;
    uint32_t abi_version;
    int (*create)(const ipcraft_engine_host_v1_host_api *, void **out_handle);
    int (*reconcile)(void *handle,
                     ipcraft_engine_host_v1_reconcile_request request,
                     ipcraft_engine_host_v1_reconcile_response *response);
    void (*release_buffer)(void *handle, ipcraft_engine_host_v1_buffer buffer);
    void (*destroy)(void *handle);
};
```

The returned size-versioned table contains `create`, `reconcile`, `release_buffer`, and `destroy`. No Qt or C++ standard-library type crosses the ABI. `reconcile` receives canonical byte spans for `normalizedTopologyInput`, `currentDerivedState`, and `reconcileApplicability`, and returns canonical `ipcraft.patch-body.v1` plus optional structured diagnostics.

- [ ] **Step 3: Implement Host-owned provenance**

The Engine never receives project paths, Session revision, clock, randomness, environment, or Host IDs. The Host injects trusted source identity, transaction/session provenance and the complete Patch envelope after validating the returned Patch body.

- [ ] **Step 4: Package an immutable installable Engine Bundle**

Build the library and `ipcraft.engine-bundle.v1` manifest into a non-overlapping exhaustive Bundle root. Resolve and load only through its exact dependency lock digest.

- [ ] **Step 5: Run tests and commit**

```bash
xmake run -P qt noc_default_engine_host_test
git add qt/default-engine qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua
git commit -m "feat: load exact Default Engine ABI bundle"
```

### Task 8: Implement deterministic Mesh reconciliation and identity

**Files:**

- Extend: `qt/default-engine/src/defaultmeshengine.cpp`
- Create: `qt/inc/noc/v1/domain/topologycanonicalmodels.h`
- Create: `qt/src/noc/v1/domain/topologycanonicalmodels.cpp`
- Create: `qt/test/noc/noc_default_engine_mesh_test.cpp`

- [ ] **Step 1: Write frozen identity scenarios**

Add exact tests for 1×1, 2×2, 2×2→2×3 retention, 3×3→2×2→3×3 recreation with new IDs, shrink Undo tombstone restoration, Slot template compatibility, Router template incompatibility, and deterministic permutation-independent digests.

- [ ] **Step 2: Implement closed canonical projections**

Build `ipcraft.normalized-topology-input.v1`, `ipcraft.derived-state.v1`, and `ipcraft.reconcile-applicability.v1` exactly as frozen. Topology input excludes Attachments, Domains, Views, runs and non-driving Interface data.

- [ ] **Step 3: Implement Mesh Patch-body generation**

Use row/column coordinates and compatible template keys as logical matching inputs without encoding them in Host IDs. Persist Structural Links as undirected public structure; protocol/microarchitecture direction remains Engine-private and is not a “directional-link template” in Core.

- [ ] **Step 4: Verify Host allocation behavior**

Engine creates use transaction-local refs. Candidate digest is Host-ID independent. Only accepted atomic commit allocates opaque IDs in canonical order.

- [ ] **Step 5: Run tests and commit**

```bash
xmake run -P qt noc_default_engine_mesh_test
git add qt/default-engine qt/inc/noc/v1/domain qt/src/noc/v1/domain qt/test/noc/noc_default_engine_mesh_test.cpp
git commit -m "feat: reconcile deterministic Mesh identity"
```

### Task 9: Implement the single Pending Topology Group and Draft Overlay

**Files:**

- Create: `qt/inc/noc/v1/application/pendingtopologygroup.h`
- Create: `qt/inc/noc/v1/application/draftoverlay.h`
- Create: `qt/inc/noc/v1/application/topologycandidate.h`
- Create: `qt/inc/noc/v1/application/reconciliationcoordinator.h`
- Create: `qt/src/noc/v1/application/pendingtopologygroup.cpp`
- Create: `qt/src/noc/v1/application/draftoverlay.cpp`
- Create: `qt/src/noc/v1/application/topologycandidate.cpp`
- Create: `qt/src/noc/v1/application/reconciliationcoordinator.cpp`
- Create: `qt/test/noc/noc_reconciliation_contract_test.cpp`

- [ ] **Step 1: Write the C1/C2 coalescing test**

C1 changes rows to 3; C2 changes rows to 4 before response. Assert one open Group, one monotonic generation, generation-1 rejection, no accepted C1 command, and one formal topology transaction for the final materialized intent.

- [ ] **Step 2: Implement topology-intent local history**

Only one Group may be open. Edit/undo/retry increments `requestGeneration`; failure preserves the Group; discard cancels it and leaves authoritative state/history unchanged.

- [ ] **Step 3: Implement the closed applicability tuple**

Compare group/generation, topology input revision/digest, base Derived-State revision/digest, base authoritative-design digest, exact Authority/Package/Engine/dependency-set digests, Engine Host contract, and Host side-effect contract. `sessionRevision` is provenance only.

- [ ] **Step 4: Implement Draft Overlay as non-authoritative state**

During a Group, allowed ordinary edits create only Draft entries/local undo. They do not enter formal history and block formal Save. Recovery supports only the frozen safe subset; a draft-created Interface is one mutable self-contained entry with no cross-entry local refs.

- [ ] **Step 5: Implement immutable candidates**

Non-destructive valid candidates auto-commit. Destructive candidates become `ready-to-commit` and require `ConfirmPendingTopologyGroup(candidateDigest)`. Candidate digest covers structured impact codes/data, not localized text or final random Host IDs.

- [ ] **Step 6: Implement creation as a bootstrap materialization**

`CreateDesign` resolves exact dependencies and derives the initial Mesh/Default Domains before publishing the project directory. Successful creation establishes the non-undoable baseline; failure leaves no partially authoritative project and no formal history entry.

- [ ] **Step 7: Run tests and commit**

```bash
xmake run -P qt noc_reconciliation_contract_test
git add qt/inc/noc/v1/application qt/src/noc/v1/application qt/test/noc/noc_reconciliation_contract_test.cpp qt/xmake.lua
git commit -m "feat: add pending topology transaction workflow"
```

### Task 10: Add Host side effects, Attachments, Domains, formal topology history, and recovery

**Files:**

- Create: `qt/inc/noc/v1/application/hostsideeffects.h`
- Create: `qt/src/noc/v1/application/hostsideeffects.cpp`
- Create: `qt/inc/noc/v1/domain/corestructuraldrc.h`
- Create: `qt/src/noc/v1/domain/corestructuraldrc.cpp`
- Create: `qt/inc/noc/v1/application/recoverystore.h`
- Create: `qt/src/noc/v1/application/recoverystore.cpp`
- Create: `qt/test/noc/noc_interface_attachment_test.cpp`
- Create: `qt/test/noc/noc_domain_command_test.cpp`
- Create: `qt/test/noc/noc_recovery_contract_test.cpp`

- [ ] **Step 1: Write candidate-wide side-effect tests**

Cover Router-created Default Memberships, Router deletion, Slot deletion to unresolved Attachment, clear unresolved intent, Package Relation unresolved/blocking policy, empty non-Default Domain tombstone, and disconnection caused by Router or Link changes.

- [ ] **Step 2: Implement versioned `ipcraft.noc-side-effects.v1`**

Authority and Application sub-patches share one candidate-local namespace and validate/commit atomically. Unsupported persisted side-effect contract opens degraded inspect; the Host never silently reinterprets it.

- [ ] **Step 3: Add Attachment and Domain commands**

Implement Attach/Detach/Reattach and Move/Split/Merge as typed commands. Slot legality includes Contract, role, capability and capacity. A capability/config edit that invalidates a resolved Slot is rejected. Domain coverage is total/exclusive and connectivity is structural-undirected.

- [ ] **Step 4: Complete topology and migration history records**

Store forward/inverse transaction, tombstones, Host-ID mapping, exact dependency/provenance and revision effects. Undo/Redo assigns new monotonic Session/topology-input/Derived-State revisions and replays saved transactions without calling an Engine.

- [ ] **Step 5: Implement recovery without a second Derived State**

Persist `authoritativeDesign`, `authoritativeDiagnostics`, saved digest binding, pending topology intent, Draft Overlay and local undo/redo. Do not persist a separate “last successful Derived State” or a trusted candidate. Reopen validates `authoritativeDesign` as `WellFormed`, recomputes digests/diagnostics, resets a recovered Group to drafting, and re-derives.

- [ ] **Step 6: Add explicit Engine migration flow**

Migration always creates a confirmable candidate and atomically changes exact Engine lock, Derived State, Host side effects and provenance. Undo restores the saved inverse transaction and never executes either Engine; a missing restored Bundle leaves degraded inspect.

- [ ] **Step 7: Run Gate B acceptance**

```bash
xmake run -P qt noc_default_engine_host_test
xmake run -P qt noc_default_engine_mesh_test
xmake run -P qt noc_reconciliation_contract_test
xmake run -P qt noc_interface_attachment_test
xmake run -P qt noc_domain_command_test
xmake run -P qt noc_recovery_contract_test
```

Rerun all Gate A and Gate 0 checks.

- [ ] **Step 8: Record Gate B and stop for review**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/default-engine qt/test/noc qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate B Mesh workflow"
```

Do not begin Gate C until all 27 Gate B scenarios in Appendix E have a named test and pass.

---

## Gate C — First Package and Tool Vertical Slice

### Task 11: Migrate `finepaper.noc` and AXI5/ACE/CHI Contract-level support

**Files:**

- Create: `ipcores/finepaper-noc-v1/package.json`
- Create: `ipcores/finepaper-noc-v1/contracts/axi5.json`
- Create: `ipcores/finepaper-noc-v1/contracts/ace.json`
- Create: `ipcores/finepaper-noc-v1/contracts/chi.json`
- Create: `ipcores/finepaper-noc-v1/bundles/` exhaustive manifests
- Create: `qt/test/noc/noc_interface_contract_test.cpp`
- Create: `qt/test/noc/noc_finepaper_package_e2e_test.cpp`

- [ ] **Step 1: Write Contract-level support tests**

Validate identity, exact Contract lock, role, capabilities, scalar configuration, Slot compatibility, persistence and generator projection. Explicitly exclude address allocation, endpoint composition, transactions and coherence correctness.

- [ ] **Step 2: Author the declarative Package**

Use Default Engine Authority and no Provider. Declare global/Interface/Domain fields and Mesh/Router/StructuralLink/Slot templates. Unknown generic content goes to the frozen opaque/default path and receives no Core behavior.

- [ ] **Step 3: Implement effective Contract values**

Apply Contract default → Interface-template override → persisted instance value. Required values must resolve; template overrides cannot change declaration type/editability; exact canonical scalar matching decides Slot compatibility. Core code contains no AXI5/ACE/CHI ID branch.

- [ ] **Step 4: Run tests and commit**

```bash
xmake run -P qt noc_package_contract_test
xmake run -P qt noc_interface_contract_test
xmake run -P qt noc_finepaper_package_e2e_test
git add ipcores/finepaper-noc-v1 qt/test/noc qt/xmake.lua
git commit -m "feat: add first declarative NoC V1 package"
```

### Task 12: Implement RunCoordinator and isolated pipeline invocations

**Files:**

- Create: `qt/inc/noc/v1/application/runcoordinator.h`
- Create: `qt/inc/noc/v1/application/pipelinerun.h`
- Create: `qt/src/noc/v1/application/runcoordinator.cpp`
- Create: `qt/inc/noc/v1/infrastructure/toolprocessrunner.h`
- Create: `qt/src/noc/v1/infrastructure/toolprocessrunner.cpp`
- Create: `qt/test/noc/noc_tool_input_contract_test.cpp`
- Create: `qt/test/noc/noc_tool_result_contract_test.cpp`
- Create: `qt/test/noc/noc_pipeline_result_contract_test.cpp`
- Create: `qt/test/noc/noc_pipeline_invocation_isolation_test.cpp`

- [ ] **Step 1: Write pipeline gate tests**

Generate requires one current formally saved Snapshot and no Pending Group/Draft. Sequence: Core Structural DRC → optional semantic DRC invocation → generator invocation → artifact verification → promotion. Missing/stale/failed/blocking semantic DRC prevents generator launch.

- [ ] **Step 2: Implement parent/step/invocation identity**

Each external invocation gets `pipelineRunId`, `stepId`, `invocationId`, canonical Tool Input, optional raw Tool Result, Host-normalized Step Result, stdout NDJSON and stderr log. Crash/timeout/cancel synthesizes only the normalized Step Result.

- [ ] **Step 3: Build execution roots outside the project**

Pass only a fresh `executionRoot` with controlled relative paths and exact read-only Bundle/runtime views. Do not pass the project directory. Apply frozen environment, runtime closure, network policy, timeout, message and filesystem limits.

- [ ] **Step 4: Implement cancellation and result truth table**

Normal completion requires valid process exit plus valid required result/report manifests. Cancellation sends the declared graceful signal, waits the frozen grace interval, then kills. Timeout/crash never fabricates a tool-authored result.

- [ ] **Step 5: Run tests and commit**

```bash
xmake run -P qt noc_tool_input_contract_test
xmake run -P qt noc_tool_result_contract_test
xmake run -P qt noc_pipeline_result_contract_test
xmake run -P qt noc_pipeline_invocation_isolation_test
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua
git commit -m "feat: add isolated NoC pipeline execution"
```

### Task 13: Archive reports, verify artifacts, and promote output transactionally

**Files:**

- Create: `qt/inc/noc/v1/infrastructure/reportarchive.h`
- Create: `qt/src/noc/v1/infrastructure/reportarchive.cpp`
- Create: `qt/inc/noc/v1/infrastructure/artifactverifier.h`
- Create: `qt/src/noc/v1/infrastructure/artifactverifier.cpp`
- Create: `qt/inc/noc/v1/infrastructure/outputpromoter.h`
- Create: `qt/src/noc/v1/infrastructure/outputpromoter.cpp`
- Create: `qt/test/noc/noc_runtime_lock_test.cpp`
- Create: `qt/test/noc/noc_diagnostic_report_test.cpp`
- Create: `qt/test/noc/noc_artifact_manifest_test.cpp`
- Create: `qt/test/noc/noc_output_promotion_test.cpp`

- [ ] **Step 1: Write hostile artifact/output tests**

Reject unlisted/extra entries, duplicate normalized paths, path escape, Unicode/case-fold collision, symlink, hard link, special file, size/digest mismatch, invalid report pointers, stale pipeline provenance, non-current run, and cross-volume override.

- [ ] **Step 2: Verify complete artifact trees**

Require `ipcraft.artifact-manifest.v1` to enumerate the entire promoted tree. Copy only verified regular files into a clean Host-built sibling staging directory; never promote an arbitrary tool-written directory.

- [ ] **Step 3: Archive canonical evidence**

After termination, validate and copy canonical ProjectDesign Snapshot, pipeline plan/result, per-invocation inputs/results/logs, diagnostics and artifact manifest into `reports/runs/<pipelineRunId>/`. Shared writes require the project mutation lock.

- [ ] **Step 4: Implement rollback-safe promotion and freshness**

Promote only the latest eligible run for the exact current and saved ProjectDesign digests, dependency set, Default Engine digest, Engine Host contract and Host side-effect contract. Pending Group, Draft Overlay, accepted unsaved change or dependency mismatch makes prior output stale. Preserve the previous successful output if verification/promotion fails.

- [ ] **Step 5: Validate persisted machine artifacts**

Write and validate `pipeline.json` with `ipcraft.pipeline-result.v1` and `.ipcraft-output.json` with `ipcraft.output-manifest.v1`; include exact Engine/Host-side-effect provenance and `current-canonical` versus `last-successful-stale` state.

- [ ] **Step 6: Run tests and commit**

```bash
xmake run -P qt noc_runtime_lock_test
xmake run -P qt noc_diagnostic_report_test
xmake run -P qt noc_artifact_manifest_test
xmake run -P qt noc_output_promotion_test
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua
git commit -m "feat: verify and promote NoC generation output"
```

### Task 14: Complete the A-C end-to-end acceptance and delivery record

**Files:**

- Create: `qt/test/noc/noc_gates_a_c_e2e_test.cpp`
- Create: `qt/test/noc/noc_gate_acceptance_coverage_test.cpp`
- Modify: `docs/contracts/GATE-STATUS.md`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write one complete headless workflow test**

Create a project from `finepaper.noc`, materialize 2×2, create AXI5/ACE/CHI Interfaces, attach to explicit Slots, edit Domains, save, reopen, run Core/semantic DRC, generate, verify artifacts, promote output, modify the authoritative design, and confirm output becomes stale.

- [ ] **Step 2: Add an acceptance coverage test**

Map every Appendix E Gate A-C scenario to a named executable test case. Fail if a required scenario/command has no mapping. This prevents a capable implementation agent from satisfying only the happy path.

- [ ] **Step 3: Run the complete Gate C command set**

Run every command from Appendix E Gate C, then all Gate B, Gate A and Gate 0 commands separately. Also run:

```bash
xmake run -P qt noc_gates_a_c_e2e_test
xmake run -P qt noc_gate_acceptance_coverage_test
xmake build -P qt qt
git diff --check
```

Expected: all pass; legacy launcher still builds; no new-path library links a legacy authority; no frozen Gate 0 input changed.

- [ ] **Step 4: Record reproducible implementation evidence**

Update Gate A-C status with:

```text
contractCommit
implementationCommit
xmake version
Qt version
compiler and ABI
generated/frozen vector digests used
executed target list and results
```

Do not create or commit `docs.tar`; the repository contracts and commit identities are the implementation inputs.

- [ ] **Step 5: Commit and stop**

```bash
git add qt/test/noc/noc_gates_a_c_e2e_test.cpp qt/test/noc/noc_gate_acceptance_coverage_test.cpp qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "test: complete NoC Gates A-C acceptance"
```

Stop for architecture and code review. Gate D Extension ABI work and Gate E UI work are explicitly outside this plan.

---

## Gates A-C Exit Criteria

Do not declare this plan complete unless all are true:

- Gate 0 Revision 5 remains byte-for-byte unchanged or has a separately approved unfreeze/refreeze record.
- Production strict JSON, RFC 8785 canonicalization and schema-catalog behavior pass the frozen independent vectors.
- `ProjectDesignWellFormed`, `ProjectDesignCommitValid`, and `ProjectDesignSaveEligible` are separate production APIs with recovery/save tests.
- New-path libraries have one-way dependencies and no link/include path to a legacy authority.
- The exact Default Engine is a separately installable immutable Bundle loaded only through `ipcraft_engine_host_v1_get_api`; no fallback Engine exists.
- Package/Contract and Bundle verification exist before Mesh reconciliation uses templates or exact locks.
- 2×2→2×3 identity retention, recreation IDs, shrink Undo, candidate confirmation, Draft Overlay and monotonic revisions pass headlessly.
- Host side effects, topology intent and Authority Patch commit as one atomic formal transaction.
- Recovery persists one authoritative materialized design plus diagnostics and intent/drafts; it does not persist a second Derived State or trusted candidate.
- `finepaper.noc` completes create, reconcile, explicit Slot attachment, Domain edit, save/reopen, DRC, Generate, evidence archive and output promotion without a Provider.
- Exact Engine digest, Engine Host contract and Host side-effect contract appear in derivation, applicability, run, report and output freshness.
- The plan produces no Widget, Provider ABI, generic SoC composition, address allocation, protocol correctness or chiplet implementation.
