# Commercial NoC MVP Workflows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the MVP can create commercial-grade NoC IP outputs for `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc` through the Qt project model, V1 package schema, and real generation pipeline.

**Architecture:** Keep the production pipeline unchanged: package manifests are loaded through `loadIpcraftPackageManifests`, catalog entries are created by `IpCatalogService`, graphs are built from package module metadata and `TopologyPresetBuilder`, and generation runs through `ProjectGenerationRunner`. Vendor-backed packages are tested from temporary package workspaces with minimal vendor fixtures so the gate validates the Qt-to-generator contract without requiring checked-in third-party RTL.

**Tech Stack:** Qt 6 C++ tests, xmake test targets, V1 `ipcraft.json` package schema, `ipcraft-generate`, Ruby package generators.

---

### Task 1: Add Three-IP Commercial Workflow Gate

**Files:**
- Create: `qt/test/commercial_noc_mvp_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing test target**

Add `qt/test/commercial_noc_mvp_test.cpp` with helpers that:
- locate repo paths using `QCoreApplication::applicationDirPath()` and `QDir::currentPath()`;
- copy a package root into a `QTemporaryDir`;
- create minimal fake vendor files for `finepaper.ravenoc` and `finepaper.opennoc`;
- load each copied package through `loadIpcraftPackageManifests`;
- create a `ModuleRegistry`, `IpCatalogService`, graph, and `ProjectIpInstanceRecord`;
- apply the package mesh preset for `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`;
- run `ProjectGenerationRunner().generate(request)`;
- verify emitted input schema and generated artifacts for all three packages.

The test must assert all of these IDs are covered in one executable:

```cpp
QStringList requiredPackageIds() {
    return {
        QStringLiteral("finepaper.noc"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("finepaper.opennoc")
    };
}
```

- [ ] **Step 2: Register the target**

Add `commercial_noc_mvp_test` in `qt/xmake.lua` using `add_qt_test_target`. Include the same production sources used by `projectgenerationrunner_test`, plus `src/topology/topologypresetbuilder.cpp` and `src/connection/connectionruleservice.cpp`.

- [ ] **Step 3: Run the new test and observe the first failure**

Run:

```bash
xmake run -P qt commercial_noc_mvp_test
```

Expected before fixes: the target compiles or fails at the first real contract gap. Do not weaken assertions to make it pass.

- [ ] **Step 4: Fix only production or fixture gaps required by real workflows**

Allowed changes:
- test fixture setup for missing vendor files;
- package metadata corrections if a real package manifest omits a required V1 field;
- generator invocation compatibility if the Qt pipeline is not supplying the public emitted inputs contract.

Disallowed changes:
- bypassing `ProjectGenerationRunner`;
- directly shelling out to a generator from the test;
- adding a fake `GenerationFlowProvider` for this gate;
- broad rewrites of unrelated editor code.

- [ ] **Step 5: Verify the workflow gate**

Run:

```bash
xmake run -P qt commercial_noc_mvp_test
```

Expected: output contains `commercial_noc_mvp_test passed`.

- [ ] **Step 6: Commit the gate**

```bash
git add qt/test/commercial_noc_mvp_test.cpp qt/xmake.lua ipcores
git commit -m "test: add commercial NoC MVP workflow gate"
```

### Task 2: Add Phase 7 Architecture Scan

**Files:**
- Create: `qt/test/plugin_architecture_phase7_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write a scan test**

Create `plugin_architecture_phase7_scan_test.cpp` that reads `qt/test/commercial_noc_mvp_test.cpp` and `qt/xmake.lua`, then verifies:
- all three package IDs are named;
- the test uses `ProjectGenerationRunner`;
- the test uses `TopologyPresetBuilder`;
- the test uses `loadIpcraftPackageManifests`;
- the xmake target `commercial_noc_mvp_test` exists.

- [ ] **Step 2: Register the scan target**

Add `plugin_architecture_phase7_scan_test` in `qt/xmake.lua` with no extra production sources beyond the standard helper files.

- [ ] **Step 3: Verify the scan**

Run:

```bash
xmake run -P qt plugin_architecture_phase7_scan_test
```

Expected: output contains `plugin_architecture_phase7_scan_test passed`.

- [ ] **Step 4: Commit the scan**

```bash
git add qt/test/plugin_architecture_phase7_scan_test.cpp qt/xmake.lua
git commit -m "test: add commercial workflow phase seven scan"
```

### Task 3: Phase 7 Review Gate

**Files:**
- Read-only review of current branch.

- [ ] **Step 1: Run focused verification**

Run:

```bash
xmake run -P qt commercial_noc_mvp_test
xmake run -P qt plugin_architecture_phase7_scan_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt topology_preset_test
xmake build -P qt qt
```

Expected: all commands exit 0.

- [ ] **Step 2: Run automatic review**

Dispatch read-only reviewers:
- spec reviewer: confirm stage 7 proves package load, frontend graph construction, real generation, and artifacts for all three anchor packages;
- quality reviewer: check test fixture isolation, temp directory safety, and avoid hidden hardcoded bypasses.

- [ ] **Step 3: Fix high-confidence review findings**

If a reviewer finds a real issue, fix it, rerun the focused verification, and commit with a focused message.

- [ ] **Step 4: Mark Phase 7 complete**

Only after fresh verification and review closure, update the parent task plan to move Phase 8 to `in_progress`.

---

## Self-Review

- Spec coverage: The plan covers all three anchor IPs, real Qt project generation, V1 package manifests, artifact verification, vendor fixture isolation, scan enforcement, verification, review, and commit boundaries.
- Placeholder scan: No TODO/TBD/fill-in-later placeholders remain.
- Type consistency: All named production types and targets exist in the current Qt codebase.
