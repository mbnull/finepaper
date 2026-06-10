# Architecture Hardening And Deletion Gates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add end-to-end hardening evidence and deletion-gate scans so the plugin-extensible IP platform can be reviewed in Phase 10 without relying on informal memory.

**Architecture:** This phase does not introduce a new runtime subsystem. It records the current architecture state, classifies remaining legacy paths, and adds a scan gate that checks the main boundary risks: V1 schema reuse, extension/package terminology, unified generation, package-declared connection rules, three-IP anchors, and the onboarding skill.

**Tech Stack:** Markdown architecture docs, Qt 6 C++ scan test, xmake.

---

### Task 1: Add Architecture Hardening Report

**Files:**
- Create: `docs/architecture/plugin-architecture-hardening-report.md`
- Modify: `docs/architecture/README.md`

- [ ] **Step 1: Write the report**

Create a concise report with these sections:
- phase coverage matrix for Phases 2-8;
- boundary status for Project, Package, Editor, Connection, Tool, Commercial NoC, and Onboarding;
- three-IP anchor status for `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`;
- V1 schema reuse matrix for `ipcraft.project.v1`, `ipcraft.package.v1`, `ipcraft.graph-config.v1`, `ipcraft.emitted-inputs.v1`, and diagnostics;
- hardening/deletion gates for Graph source-of-truth, `MainWindow` assembly, UI JSON parsing, direct generator calls, connection hardcoding, and old compatibility paths;
- Phase 10 review inputs.

- [ ] **Step 2: Link the report**

Add `plugin-architecture-hardening-report.md` to `docs/architecture/README.md`.

- [ ] **Step 3: Commit the report**

```bash
git add docs/architecture/plugin-architecture-hardening-report.md docs/architecture/README.md
git commit -m "docs: add plugin architecture hardening report"
```

### Task 2: Add Phase 9 Architecture Scan

**Files:**
- Create: `qt/test/plugin_architecture_phase9_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the scan test**

Create a Qt console test that reads:
- `docs/architecture/plugin-architecture-hardening-report.md`;
- `docs/architecture/ip-package-authoring-flow.md`;
- `.agents/skills/finepaper-ip-onboarding/SKILL.md`;
- `qt/test/commercial_noc_mvp_test.cpp`;
- `qt/test/plugin_architecture_phase8_scan_test.cpp`;
- `qt/xmake.lua`.

The scan must verify:
- the hardening report names all phases from Phase 2 through Phase 10;
- the report names the three anchor package ids;
- the report names all required V1 schemas;
- the report contains deletion gates for Graph source-of-truth, `MainWindow`, UI JSON parsing, direct generator calls, and connection hardcoding;
- the authoring/onboarding docs use extension/package language;
- the commercial workflow test still uses `ProjectGenerationRunner` and verifies non-empty artifacts;
- the Phase 8 scan target and Phase 9 scan target are registered.

- [ ] **Step 2: Register the scan target**

Add `plugin_architecture_phase9_scan_test` next to the existing architecture scan targets in `qt/xmake.lua`, with `set_rundir("$(projectdir)/..")` and pass output `plugin_architecture_phase9_scan_test passed`.

- [ ] **Step 3: Verify the scan**

Run:

```bash
xmake run -P qt plugin_architecture_phase9_scan_test
```

Expected: output contains `plugin_architecture_phase9_scan_test passed`.

- [ ] **Step 4: Commit the scan**

```bash
git add qt/test/plugin_architecture_phase9_scan_test.cpp qt/xmake.lua
git commit -m "test: add architecture hardening phase scan"
```

### Task 3: Phase 9 Review Gate

**Files:**
- Read-only review of Phase 9 files.

- [ ] **Step 1: Run focused verification**

Run:

```bash
xmake run -P qt plugin_architecture_phase9_scan_test
xmake run -P qt plugin_architecture_phase8_scan_test
xmake run -P qt commercial_noc_mvp_test
xmake build -P qt qt
```

Expected: all commands exit 0.

- [ ] **Step 2: Run automatic review**

Dispatch read-only reviewers:
- spec reviewer: confirm the hardening report and scan cover Phase 9 acceptance and prepare Phase 10 inputs;
- quality reviewer: confirm the scan is deterministic, does not over-claim deletion, and does not hide runtime regressions.

- [ ] **Step 3: Fix high-confidence review findings**

If a reviewer finds a real issue, fix it, rerun focused verification, and commit with a focused message.

- [ ] **Step 4: Mark Phase 9 complete**

Only after fresh verification and review closure, update the parent task plan to move Phase 10 to `in_progress`.

---

## Self-Review

- Spec coverage: The plan covers hardening/deletion gates, architecture scans, V1 contract reuse, three-IP anchor status, onboarding continuity, review evidence, and Phase 10 inputs.
- Placeholder scan: No TODO/TBD/fill-in-later placeholders remain.
- Type consistency: All file paths, target names, package ids, schema ids, and commands are concrete and consistent.
