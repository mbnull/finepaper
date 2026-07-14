# Default NoC Gates A-C Headless Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the isolated, headless new architecture from persisted ProjectDesign through Mesh reconciliation, exact history, the first declarative Package, validation, generation, and transactional output promotion.

**Architecture:** New code lives in dedicated `ipcraft::noc::v1` domain/application namespaces and separate xmake libraries, never adapting through `ProjectService`, `ProjectDocument`, legacy Graph, or legacy CommandManager. Gate A establishes the aggregate, Patch engine, repository, lock, and formal history; Gate B adds the exact Default Engine Bundle, Pending Topology Group, candidates, side effects, identity, recovery, Domains and Attachments; Gate C adds `finepaper.noc`, Contract-level AXI5/ACE/CHI, RunCoordinator, external-tool staging, reports, and output promotion.

**Tech Stack:** Qt 6 Core/Concurrent/Test, C++23, xmake, JSON Schema contracts frozen at Gate 0, QSaveFile, QLockFile or equivalent project-level lock, QProcess for tools, SHA-256.

---

## File Structure

- Create `qt/inc/noc/v1/domain/` and `qt/src/noc/v1/domain/` for value types, ProjectDesign, Patch validation/application, canonical digest, DRC, and identity.
- Create `qt/inc/noc/v1/application/` and `qt/src/noc/v1/application/` for DesignSession, commands, history, repository ports, Pending Group, recovery, reconciliation, Package resolution, and RunCoordinator.
- Create `qt/inc/noc/v1/infrastructure/` and `qt/src/noc/v1/infrastructure/` for JSON repository, project lock, bundle resolver, Engine Host loader, process runner, report archive, and output promoter.
- Create `qt/default-engine/` as an independently built immutable bundle implementing `ipcraft.engine-host.v1`.
- Create `qt/test/noc/` for new-path tests and `qt/test/support/noc/` for builders/fakes.
- Create `ipcores/finepaper-noc-v1/` as the migrated `finepaper.noc` declarative Package; do not modify the legacy package in place before Gate G.
- Modify `qt/xmake.lua` to define the new libraries/binaries/tests with explicit file lists.

## Task 1: Split the build graph before adding domain code

**Files:**
- Modify: `qt/xmake.lua`
- Create: `qt/test/noc/noc_architecture_boundary_test.cpp`

- [ ] **Step 1: Write the failing architecture boundary scan**

The test reads new-path source/header files and fails on these includes or symbols:

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

It also inspects `xmake show -t ipcraft_noc_domain` and `ipcraft_noc_application` output captured by the test harness and rejects legacy target dependencies.

- [ ] **Step 2: Add explicit library targets**

Define:

```lua
target("ipcraft_noc_domain")
    set_kind("static")
    set_languages("c++23")
    add_includedirs("inc", {public = true})
    add_files("src/noc/v1/domain/*.cpp")

target("ipcraft_noc_application")
    set_kind("static")
    set_languages("c++23")
    add_deps("ipcraft_noc_domain")
    add_includedirs("inc", {public = true})
    add_files("src/noc/v1/application/*.cpp", "src/noc/v1/infrastructure/*.cpp")
```

Keep the existing `qt` target operational. Do not move legacy files during this task.

- [ ] **Step 3: Register and run the boundary test**

Run:

```bash
xmake build -P qt ipcraft_noc_domain ipcraft_noc_application
xmake run -P qt noc_architecture_boundary_test
xmake build -P qt qt
```

Expected: all pass; the legacy application still builds.

- [ ] **Step 4: Commit**

```bash
git add qt/xmake.lua qt/test/noc/noc_architecture_boundary_test.cpp
git commit -m "build: isolate new NoC architecture targets"
```

## Task 2: Implement ProjectDesign V1 value model and canonical JSON repository

**Files:**
- Create: `qt/inc/noc/v1/domain/projectdesign.h`
- Create: `qt/inc/noc/v1/domain/subjects.h`
- Create: `qt/inc/noc/v1/domain/dependencylock.h`
- Create: `qt/inc/noc/v1/domain/canonicaljson.h`
- Create: `qt/src/noc/v1/domain/projectdesign.cpp`
- Create: `qt/src/noc/v1/domain/canonicaljson.cpp`
- Create: `qt/inc/noc/v1/application/projectrepository.h`
- Create: `qt/inc/noc/v1/infrastructure/jsonprojectrepository.h`
- Create: `qt/src/noc/v1/infrastructure/jsonprojectrepository.cpp`
- Create: `qt/test/noc/noc_project_design_contract_test.cpp`

- [ ] **Step 1: Write failing fixture round-trip tests**

For each Appendix A valid fixture:

```cpp
const auto loaded = repository.load(fixturePath);
QVERIFY(loaded.hasValue());
const QByteArray first = canonicalJson(loaded.value());
repository.save(tempPath, loaded.value(), std::nullopt);
const auto reopened = repository.load(tempPath);
QCOMPARE(canonicalJson(reopened.value()), first);
```

For each invalid fixture, compare the exact stable code.

- [ ] **Step 2: Define aggregate-owned collections**

Use strongly typed IDs and explicit structs for Component, Interface, Router, StructuralLink, AccessSlot, Attachment, Domain, DomainMembership, PackageEntity, PackageRelation, DependencyLock, View, and derivation metadata. Store no Qt Widget or QObject type.

- [ ] **Step 3: Implement canonical read/write**

Reader rejects duplicate IDs, unknown root schema IDs, invalid references, and invalid dependency-lock variants. Writer emits the frozen canonical collection order and uses `QSaveFile`.

- [ ] **Step 4: Enforce optimistic save precondition**

`save(path, design, expectedLastSavedDigest)` compares the on-disk canonical digest before replacement and returns `project.save_conflict` on mismatch.

- [ ] **Step 5: Run the Gate A contract test**

Run: `xmake run -P qt noc_project_design_contract_test`

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc/noc_project_design_contract_test.cpp qt/xmake.lua
git commit -m "feat: add canonical NoC ProjectDesign repository"
```

## Task 3: Implement the atomic Patch engine and Host ID allocator

**Files:**
- Create: `qt/inc/noc/v1/domain/patch.h`
- Create: `qt/inc/noc/v1/domain/patchengine.h`
- Create: `qt/inc/noc/v1/domain/hostidallocator.h`
- Create: `qt/src/noc/v1/domain/patchengine.cpp`
- Create: `qt/src/noc/v1/domain/hostidallocator.cpp`
- Create: `qt/test/noc/noc_patch_engine_test.cpp`

- [ ] **Step 1: Write failing atomicity tests**

```cpp
const auto before = canonicalJson(design);
const Patch patch = patchWithValidCreateThenInvalidOwnershipUpdate();
const auto result = engine.apply(design, patch);
QCOMPARE(result.error().code, QString("patch.ownership_violation"));
QCOMPARE(canonicalJson(design), before);
```

Add create/update/delete entity/relation tests, property deletion/null semantics, preconditions, cascade prohibition, tombstone inverse, and project-root rename.

- [ ] **Step 2: Implement two-phase validation/application**

First validate source ownership, applicability/preconditions, localRef graph, references, and operation order against a copy. Only publish the resulting aggregate and inverse Patch if every operation succeeds.

- [ ] **Step 3: Implement canonical localRef allocation**

Sort candidate-local refs by the frozen allocation order, allocate opaque IDs, rewrite all refs, and return the mapping. Rejected candidates return an empty mapping and consume no visible IDs.

- [ ] **Step 4: Run Patch tests**

Run: `xmake run -P qt noc_patch_engine_test`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1/domain qt/src/noc/v1/domain qt/test/noc/noc_patch_engine_test.cpp qt/xmake.lua
git commit -m "feat: add atomic NoC Patch engine"
```

## Task 4: Add DesignSession, typed commands, and formal history

**Files:**
- Create: `qt/inc/noc/v1/application/designsession.h`
- Create: `qt/inc/noc/v1/application/command.h`
- Create: `qt/inc/noc/v1/application/commanddispatcher.h`
- Create: `qt/inc/noc/v1/application/commandhistory.h`
- Create: `qt/src/noc/v1/application/designsession.cpp`
- Create: `qt/src/noc/v1/application/commanddispatcher.cpp`
- Create: `qt/src/noc/v1/application/commandhistory.cpp`
- Create: `qt/test/noc/noc_command_history_test.cpp`

- [ ] **Step 1: Write failing typed-command tests**

Cover `RenameDesign`, create/change/delete Interface, attach/detach/reattach, create/rename/change Domain, Move/Split/Merge Domains, and topology-driving config changes. Assert the UI-facing API accepts typed commands, never raw Patch.

- [ ] **Step 2: Define formal transaction entries**

```cpp
struct HistoryTransaction {
    QString commandType;
    Patch forward;
    Patch inverse;
    QHash<QString, QString> localRefToHostId;
    quint64 committedSessionRevision;
    quint64 committedDerivedStateRevision;
};
```

Undo/Redo applies stored inverse/forward Patch and assigns new monotonic revisions; it never invokes an Engine.

- [ ] **Step 3: Compile commands to Patch inside Application**

Command validators use the current aggregate and frozen contract. Patch remains internal and is not exposed from public UI/application ports.

- [ ] **Step 4: Run history tests**

Run: `xmake run -P qt noc_command_history_test`

Expected: PASS, including exact tombstoned ID restoration.

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1/application qt/src/noc/v1/application qt/test/noc/noc_command_history_test.cpp qt/xmake.lua
git commit -m "feat: add NoC typed commands and formal history"
```

## Task 5: Add project locking and Gate A completion

**Files:**
- Create: `qt/inc/noc/v1/infrastructure/projectlock.h`
- Create: `qt/src/noc/v1/infrastructure/projectlock.cpp`
- Extend: `qt/test/noc/noc_project_design_contract_test.cpp`
- Extend: `qt/test/noc/noc_architecture_boundary_test.cpp`

- [ ] **Step 1: Write two-process lock tests**

First process owns mutation lock. Second process may load inspect-only but cannot save, write shared recovery/UI state, archive reports, or promote output. A stale last-saved digest fails even if lock ownership changes.

- [ ] **Step 2: Implement one project-level lock**

Use a sibling lock file and expose `writable` versus `inspectOnly`. All later shared writers require the same capability object.

- [ ] **Step 3: Run all Gate A commands**

```bash
xmake build -P qt qt
xmake run -P qt noc_project_design_contract_test
xmake run -P qt noc_patch_engine_test
xmake run -P qt noc_command_history_test
xmake run -P qt noc_architecture_boundary_test
```

- [ ] **Step 4: Record Gate A status and commit**

```bash
git add qt/inc/noc/v1/infrastructure qt/src/noc/v1/infrastructure qt/test/noc docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate A foundation"
```

## Task 6: Package and load the exact Default Engine Bundle

**Files:**
- Create: `qt/default-engine/inc/ipcraft_engine_host_v1.h`
- Create: `qt/default-engine/src/defaultmeshengine.cpp`
- Create: `qt/default-engine/engine.json`
- Create: `qt/inc/noc/v1/infrastructure/enginebundleresolver.h`
- Create: `qt/inc/noc/v1/infrastructure/enginehost.h`
- Create: `qt/src/noc/v1/infrastructure/enginebundleresolver.cpp`
- Create: `qt/src/noc/v1/infrastructure/enginehost.cpp`
- Create: `qt/test/noc/noc_default_engine_mesh_test.cpp`

- [ ] **Step 1: Write resolver failure tests first**

Exact digest resolves; same ID/version with different digest, missing, revoked, wrong platform ABI, wrong `engineHostContractVersion`, or wrong Host side-effect contract returns degraded inspect. Assert no built-in fallback call exists.

- [ ] **Step 2: Define the C ABI entrypoint**

```c
typedef struct ipcraft_engine_host_v1_request ipcraft_engine_host_v1_request;
typedef struct ipcraft_engine_host_v1_response ipcraft_engine_host_v1_response;
int ipcraft_engine_reconcile_v1(const ipcraft_engine_host_v1_request*, ipcraft_engine_host_v1_response*);
```

All payloads are length-bounded UTF-8 canonical JSON described by Gate 0 schemas. The bundle owns no Host filesystem path.

- [ ] **Step 3: Implement minimal deterministic Mesh diff**

Support rows, columns, Router template, directional-link template, and stable Slot template keys. Match compatible entities by logical key/template compatibility, but keep Host IDs opaque and Host-allocated.

- [ ] **Step 4: Build an immutable installable bundle**

xmake produces the shared library, manifest, exhaustive file manifest, and digest under a versioned test install root. Tests resolve only by dependency lock digest.

- [ ] **Step 5: Run Mesh creation tests**

Run: `xmake run -P qt noc_default_engine_mesh_test`

Expected: 1×1 and 2×2 candidates are deterministic and contain no published IDs before commit.

- [ ] **Step 6: Commit**

```bash
git add qt/default-engine qt/inc/noc/v1/infrastructure qt/src/noc/v1/infrastructure qt/test/noc/noc_default_engine_mesh_test.cpp qt/xmake.lua
git commit -m "feat: add exact-lock Default Mesh Engine bundle"
```

## Task 7: Implement Pending Topology Group, Draft Overlay, and candidates

**Files:**
- Create: `qt/inc/noc/v1/application/pendingtopologygroup.h`
- Create: `qt/inc/noc/v1/application/draftoverlay.h`
- Create: `qt/inc/noc/v1/application/reconciliationcoordinator.h`
- Create: `qt/inc/noc/v1/application/topologycandidate.h`
- Create: `qt/src/noc/v1/application/pendingtopologygroup.cpp`
- Create: `qt/src/noc/v1/application/draftoverlay.cpp`
- Create: `qt/src/noc/v1/application/reconciliationcoordinator.cpp`
- Create: `qt/src/noc/v1/application/topologycandidate.cpp`
- Create: `qt/test/noc/noc_reconciliation_contract_test.cpp`

- [ ] **Step 1: Write the C1/C2 race test**

C1 changes rows to 3, C2 changes rows to 4 before response. Assert one open Group, incremented generation, generation-1 response rejected, one candidate for final intent, and exactly one formal transaction after commit.

- [ ] **Step 2: Write Draft Overlay tests**

While Group is open, ordinary config/Interface changes go only to draft-local history, block formal save, and never enter formal history. Attachment and Domain membership commands are disabled. After materialization, drafts revalidate and submit independently.

- [ ] **Step 3: Implement exact applicability**

Compare topology input revision/digest, base Derived-State revision/digest, base authoritative design digest, generation, Authority identity/digest, reconcile dependency set, exact Default Engine digest, Engine Host contract, and Host side-effect contract. Session revision is provenance only.

- [ ] **Step 4: Implement candidate states**

Non-destructive valid candidates auto-commit. Destructive candidates become `ready-to-commit` and require `ConfirmPendingTopologyGroup(candidateDigest)`. Illegal impacts become blocked. Any input/dependency/Authority/base change invalidates the candidate and increments generation.

- [ ] **Step 5: Run reconciliation tests**

Run: `xmake run -P qt noc_reconciliation_contract_test`

- [ ] **Step 6: Commit**

```bash
git add qt/inc/noc/v1/application qt/src/noc/v1/application qt/test/noc/noc_reconciliation_contract_test.cpp qt/xmake.lua
git commit -m "feat: add pending topology candidate workflow"
```

## Task 8: Add Host side effects, Attachments, Domains, DRC, and recovery

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

- [ ] **Step 1: Write atomic side-effect tests**

Cover new Router Default memberships, deleted Router membership removal, removed Slot unresolved Attachment, detachable unresolved intent, Package Relation unresolved/blocking policy, empty non-Default Domain tombstone, and Domain disconnection from Router or Link changes.

- [ ] **Step 2: Implement `ipcraft.noc-side-effects.v1` exactly**

Authority Patch plus Application side-effect Patch validate as one candidate-local graph and commit as one formal transaction. Failure in either sub-patch publishes nothing.

- [ ] **Step 3: Implement Domain typed commands**

Move/Split/Merge are atomic and enforce total, exclusive, structural-undirected connectivity. Define deterministic survivor IDs and config/name behavior from Appendix B.

- [ ] **Step 4: Implement disposable recovery**

Persist saved-design binding, topology intent, Group state, Draft Overlay, local undo, and last successful Derived State. Never persist/trust a candidate; reopen re-derives it. Recovery cannot become project authority.

- [ ] **Step 5: Run all Gate B commands**

```bash
xmake run -P qt noc_default_engine_mesh_test
xmake run -P qt noc_reconciliation_contract_test
xmake run -P qt noc_interface_attachment_test
xmake run -P qt noc_domain_command_test
xmake run -P qt noc_recovery_contract_test
```

- [ ] **Step 6: Record Gate B status and commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate B headless Mesh slice"
```

## Task 9: Migrate `finepaper.noc` and Contract-level AXI5/ACE/CHI

**Files:**
- Create: `ipcores/finepaper-noc-v1/package.json`
- Create: `ipcores/finepaper-noc-v1/contracts/axi5.json`
- Create: `ipcores/finepaper-noc-v1/contracts/ace.json`
- Create: `ipcores/finepaper-noc-v1/contracts/chi.json`
- Create: `ipcores/finepaper-noc-v1/tools/` bundle manifests and entrypoints
- Create: `qt/inc/noc/v1/application/packagecatalog.h`
- Create: `qt/src/noc/v1/application/packagecatalog.cpp`
- Create: `qt/test/noc/noc_package_contract_test.cpp`
- Create: `qt/test/noc/noc_interface_contract_test.cpp`
- Create: `qt/test/noc/noc_finepaper_package_e2e_test.cpp`

- [ ] **Step 1: Write Package/Contract fixture tests**

Assert `finepaper.noc` uses Default Engine Authority and no Provider. Validate identity, role, declared config, capability values/constraints, Slot compatibility, persistence, and generator projection. Do not test address allocation or protocol transaction correctness.

- [ ] **Step 2: Implement schema-driven Package loading**

Support V1 global/Interface/Domain scalar fields, Mesh/Router/Link/Slot templates, Contract refs, Package entities/relations, tools, and opaque fallback namespaces. Unsupported generic fields are preserved through the declared default/opaque path rather than rejected or interpreted by Core.

- [ ] **Step 3: Implement exact Contract precedence**

Apply Contract default, then Interface-template override, then instance value. Reject key collisions across namespaces and reject a capability edit that invalidates the current Slot.

- [ ] **Step 4: Run Package tests**

```bash
xmake run -P qt noc_package_contract_test
xmake run -P qt noc_interface_contract_test
xmake run -P qt noc_finepaper_package_e2e_test
```

- [ ] **Step 5: Commit**

```bash
git add ipcores/finepaper-noc-v1 qt/inc/noc/v1/application qt/src/noc/v1/application qt/test/noc qt/xmake.lua
git commit -m "feat: migrate finepaper NoC package to V1 contracts"
```

## Task 10: Implement RunCoordinator and isolated external invocations

**Files:**
- Create: `qt/inc/noc/v1/application/runcoordinator.h`
- Create: `qt/inc/noc/v1/application/pipelinerun.h`
- Create: `qt/inc/noc/v1/infrastructure/toolprocessrunner.h`
- Create: `qt/src/noc/v1/application/runcoordinator.cpp`
- Create: `qt/src/noc/v1/infrastructure/toolprocessrunner.cpp`
- Create: `qt/test/noc/noc_tool_input_contract_test.cpp`
- Create: `qt/test/noc/noc_tool_result_contract_test.cpp`
- Create: `qt/test/noc/noc_pipeline_result_contract_test.cpp`
- Create: `qt/test/noc/noc_pipeline_invocation_isolation_test.cpp`

- [ ] **Step 1: Write pipeline sequencing tests**

Generate uses one immutable formally saved Snapshot and ordered steps: structural DRC, optional semantic DRC invocation, generator invocation, artifact verify, promotion. Missing/stale/failed/blocking semantic DRC prevents generator launch.

- [ ] **Step 2: Implement execution roots outside the project**

Each external invocation receives only its own `executionRoot`, normalized relative input/output paths, fixed runtime closure, environment profile, network policy, timeout, and size limits. It never receives the project path.

- [ ] **Step 3: Implement parent/child results**

Each external step has `pipelineRunId`, `stepId`, `invocationId`, Tool Input, optional raw Tool Result, Host-normalized Step Result, stdout NDJSON, and stderr log. Crash/timeout/cancel synthesizes only the normalized Step Result.

- [ ] **Step 4: Run tool protocol tests**

```bash
xmake run -P qt noc_tool_input_contract_test
xmake run -P qt noc_tool_result_contract_test
xmake run -P qt noc_pipeline_result_contract_test
xmake run -P qt noc_pipeline_invocation_isolation_test
```

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua
git commit -m "feat: add isolated NoC tool pipeline runs"
```

## Task 11: Verify bundles, reports, artifacts, and transactional promotion

**Files:**
- Create: `qt/inc/noc/v1/infrastructure/bundleverifier.h`
- Create: `qt/inc/noc/v1/infrastructure/reportarchive.h`
- Create: `qt/inc/noc/v1/infrastructure/outputpromoter.h`
- Create: `qt/src/noc/v1/infrastructure/bundleverifier.cpp`
- Create: `qt/src/noc/v1/infrastructure/reportarchive.cpp`
- Create: `qt/src/noc/v1/infrastructure/outputpromoter.cpp`
- Create: `qt/test/noc/noc_bundle_manifest_test.cpp`
- Create: `qt/test/noc/noc_runtime_lock_test.cpp`
- Create: `qt/test/noc/noc_diagnostic_report_test.cpp`
- Create: `qt/test/noc/noc_artifact_manifest_test.cpp`
- Create: `qt/test/noc/noc_output_promotion_test.cpp`

- [ ] **Step 1: Write hostile filesystem tests**

Reject unlisted files, duplicate normalized paths, path escape, Unicode/case-fold collision, symlink, hard link, special file, digest/size mismatch, extra artifact, and cross-volume output override.

- [ ] **Step 2: Implement exhaustive verification**

Bundle manifests enumerate the complete readable bundle view except the manifest itself. Artifact manifests enumerate the complete promoted tree. Promotion copies verified files into a clean Host-built sibling staging tree.

- [ ] **Step 3: Implement report archive copy**

After process termination, validate then copy canonical inputs/results/logs from external execution roots to `reports/runs/{pipelineRunId}/`. Shared archive writes require the project mutation lock.

- [ ] **Step 4: Implement rollback-safe promotion and freshness**

Promote only the latest eligible run for the exact current/saved Project digest, dependency set, Default Engine digest, Engine Host contract, and Host side-effect contract with no Group/Draft. Preserve prior output on failure and mark it `last-successful-stale` when the authoritative working design changes.

- [ ] **Step 5: Run all Gate C commands**

```bash
xmake run -P qt noc_package_contract_test
xmake run -P qt noc_interface_contract_test
xmake run -P qt noc_finepaper_package_e2e_test
xmake run -P qt noc_tool_input_contract_test
xmake run -P qt noc_tool_result_contract_test
xmake run -P qt noc_pipeline_result_contract_test
xmake run -P qt noc_pipeline_invocation_isolation_test
xmake run -P qt noc_bundle_manifest_test
xmake run -P qt noc_runtime_lock_test
xmake run -P qt noc_diagnostic_report_test
xmake run -P qt noc_artifact_manifest_test
xmake run -P qt noc_output_promotion_test
xmake build -P qt qt
```

Then rerun every Gate A/B command. Expected: all pass.

- [ ] **Step 6: Record Gate C and commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate C tool vertical slice"
```

## Gates A-C Exit Check

Do not begin Extension ABI or UI work unless:

- Gate 0 remains unchanged or has a formally approved unfreeze/refreeze;
- all Gate A-C commands pass together;
- `finepaper.noc` completes create, reconcile, attach, domain edit, formal save, reopen, DRC, Generate, verify, and promote without Provider;
- no new-path library links a legacy authority;
- exact Engine, Host ABI, and Host side-effect provenance appears in design, run, report, and output freshness;
- Pending Group/Draft behavior is covered headlessly before any Widget is built.
