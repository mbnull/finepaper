# Final Architecture Review And Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Phase 10 by reviewing the preceding architecture phases, running `qt-cpp-review`, and producing the final plugin-extensible IP platform architecture report.

**Architecture:** Phase 10 is a review/report gate. It should not introduce new runtime architecture unless a reviewer finds a critical blocker that must be fixed. The output is a completion report plus a scan target that confirms the report contains the required evidence.

**Tech Stack:** Markdown architecture report, project-local `qt-cpp-review` skill, Qt 6 C++ scan test, xmake.

---

### Task 1: Run Final Verification And Qt/C++ Review

**Files:**
- Read-only review and command output.

- [ ] **Step 1: Run final focused verification**

Run:

```bash
ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb
xmake run -P qt commercial_noc_mvp_test
xmake run -P qt plugin_architecture_phase9_scan_test
xmake run -P qt plugin_architecture_phase8_scan_test
xmake run -P qt plugin_architecture_phase7_scan_test
xmake build -P qt qt
```

Expected: all commands exit 0.

- [ ] **Step 2: Invoke `qt-cpp-review`**

Use the project-local `qt-cpp-review` skill against the relevant Qt/C++ changes in this branch. Record the deterministic linter summary, deep-review summary, high-confidence blockers, accepted debt, and any commands run.

- [ ] **Step 3: Fix critical review blockers**

If `qt-cpp-review` or final verification finds a high-confidence architecture blocker, fix it, rerun the affected verification, and commit with a focused message.

### Task 2: Add Completion Report

**Files:**
- Create: `docs/architecture/plugin-architecture-completion-report.md`
- Modify: `docs/architecture/README.md`

- [ ] **Step 1: Write the report**

The report must contain:
- final verdict: go, go-with-debt, or no-go;
- phase completion matrix for Phases 2-10;
- three-IP anchor matrix for `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`;
- V1 schema reuse matrix for project, package, graph-config, emitted inputs, diagnostics, and diagnostic records;
- internal plugin vs extension/package terminology status;
- legacy path and deletion-gate status;
- architecture scan status;
- verification command evidence;
- `qt-cpp-review` summary;
- accepted debt and required follow-up work.

- [ ] **Step 2: Link the report**

Add `plugin-architecture-completion-report.md` to `docs/architecture/README.md`.

- [ ] **Step 3: Commit the report**

```bash
git add docs/architecture/plugin-architecture-completion-report.md docs/architecture/README.md
git commit -m "docs: add plugin architecture completion report"
```

### Task 3: Add Phase 10 Completion Scan

**Files:**
- Create: `qt/test/plugin_architecture_phase10_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the scan test**

Create a Qt console test that reads the completion report, hardening report, architecture README, and `qt/xmake.lua`. It must verify:
- the completion report names Phases 2-10;
- the report contains a final verdict;
- the report names all three anchor package ids;
- the report names all V1 schemas;
- the report contains `qt-cpp-review`;
- the report contains architecture scan status and legacy path/deletion-gate status;
- the README links the completion report;
- `plugin_architecture_phase10_scan_test` is registered.

- [ ] **Step 2: Register the scan target**

Add `plugin_architecture_phase10_scan_test` next to the existing architecture scan targets in `qt/xmake.lua`, with `set_rundir("$(projectdir)/..")` and pass output `plugin_architecture_phase10_scan_test passed`.

- [ ] **Step 3: Verify the scan**

Run:

```bash
xmake run -P qt plugin_architecture_phase10_scan_test
```

Expected: output contains `plugin_architecture_phase10_scan_test passed`.

- [ ] **Step 4: Commit the scan**

```bash
git add qt/test/plugin_architecture_phase10_scan_test.cpp qt/xmake.lua
git commit -m "test: add final architecture review scan"
```

### Task 4: Final Review Gate

**Files:**
- Read-only review of current branch.

- [ ] **Step 1: Run final verification**

Run:

```bash
ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb
xmake run -P qt commercial_noc_mvp_test
xmake run -P qt plugin_architecture_phase10_scan_test
xmake run -P qt plugin_architecture_phase9_scan_test
xmake build -P qt qt
```

Expected: all commands exit 0.

- [ ] **Step 2: Dispatch final automatic reviewers**

Dispatch:
- final architecture reviewer: confirm Phases 2-10 acceptance and report completeness;
- final code-quality reviewer: confirm no high-confidence blocker remains after `qt-cpp-review`.

- [ ] **Step 3: Fix critical final findings**

If a final reviewer finds a real blocker, fix it, rerun verification, and commit with a focused message.

- [ ] **Step 4: Mark the goal complete**

Only after verification and final review closure, update the goal status to complete and report final evidence.

---

## Self-Review

- Spec coverage: The plan includes `qt-cpp-review`, architecture report, phase matrix, three-IP matrix, V1 schema matrix, legacy path status, scan target, verification, final review, and completion conditions.
- Placeholder scan: No TODO/TBD/fill-in-later placeholders remain.
- Type consistency: All target names, file paths, package ids, schema ids, and commands are concrete and consistent.
