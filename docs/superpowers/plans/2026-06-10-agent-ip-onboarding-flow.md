# Agent IP Onboarding Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a project-local skill and package-authoring guide that lets agents adapt an existing IP codebase into Finepaper's Qt frontend, V1 package, and generation flow.

**Architecture:** Keep this phase as documentation and governance assets: a `.agents/skills/finepaper-ip-onboarding` skill describes the agent workflow, `docs/architecture/ip-package-authoring-flow.md` documents the package authoring boundary, and a Qt scan test enforces the terms and contracts. No runtime plugin ABI or package loader changes are introduced in this phase.

**Tech Stack:** Markdown skills, Finepaper V1 package schemas, Qt 6 C++ scan test, xmake.

---

### Task 1: Add Project-Local IP Onboarding Skill

**Files:**
- Create: `.agents/skills/finepaper-ip-onboarding/SKILL.md`

- [ ] **Step 1: Create the skill directory and file**

Create `.agents/skills/finepaper-ip-onboarding/SKILL.md` with frontmatter that names the skill `finepaper-ip-onboarding` and describes its trigger as adapting an existing IP codebase into Finepaper's extension/package flow.

- [ ] **Step 2: Write the onboarding workflow**

The skill must instruct agents to:
- inspect the source IP's RTL, parameters, bus interfaces, scripts, examples, tests, and license boundaries;
- create or update an `ipcraft.package.v1` `ipcraft.json`;
- map modules, interfaces, connection rules, config schema, NoC capabilities, views, emitters, flows, artifacts, diagnostics, docs, and examples into package-owned declarations;
- keep external/public deliverables named extensions or packages, not plugins;
- use `loadIpcraftPackageManifests`, `IpCatalogService`, `ProjectGenerationRunner`, and `FlowRunner` as the Qt frontend path;
- use `ipcraft.emitted-inputs.v1` and `ipcraft.graph-config.v1` instead of legacy generator inputs;
- verify against `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc` as anchor examples.

- [ ] **Step 3: Commit the skill**

```bash
git add .agents/skills/finepaper-ip-onboarding/SKILL.md
git commit -m "docs: add finepaper IP onboarding skill"
```

### Task 2: Add Package Authoring Flow Documentation

**Files:**
- Create: `docs/architecture/ip-package-authoring-flow.md`
- Modify: `docs/architecture/README.md`

- [ ] **Step 1: Document the package authoring flow**

Create `docs/architecture/ip-package-authoring-flow.md` with:
- the distinction between internal Finepaper plugins and public extensions/packages;
- the minimal package anatomy around `ipcraft.json`, `tools/`, `generator/`, `rtl/`, `vendor/`, `docs/`, and `examples/`;
- the Qt frontend path from package discovery through catalog, editor projection, connection checking, generation, artifact collection, and reports;
- the V1 schema contracts: `ipcraft.package.v1`, `ipcraft.project.v1`, `ipcraft.graph-config.v1`, `ipcraft.emitted-inputs.v1`, and diagnostics;
- the authoring checklist for adapting existing IP codebases without depending on a legacy generator compatibility path.

- [ ] **Step 2: Link the guide from the architecture README**

Add `ip-package-authoring-flow.md` to the architecture README reading-order table with a short description.

- [ ] **Step 3: Commit the docs**

```bash
git add docs/architecture/ip-package-authoring-flow.md docs/architecture/README.md
git commit -m "docs: describe IP package authoring flow"
```

### Task 3: Add Phase 8 Architecture Scan

**Files:**
- Create: `qt/test/plugin_architecture_phase8_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the scan test**

Create a Qt console test that reads the new skill, the new architecture doc, and `qt/xmake.lua`. It must verify:
- the skill file exists and names `finepaper-ip-onboarding`;
- the skill mentions `ipcraft.package.v1`, `ipcraft.emitted-inputs.v1`, `ipcraft.graph-config.v1`, `loadIpcraftPackageManifests`, `IpCatalogService`, `ProjectGenerationRunner`, and `FlowRunner`;
- the skill mentions all three anchor packages: `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`;
- the architecture doc states internal plugins are separate from extensions/packages;
- the architecture doc describes package-declared connection rules and artifact declarations;
- the xmake target `plugin_architecture_phase8_scan_test` is registered.

- [ ] **Step 2: Register the scan target**

Add a `plugin_architecture_phase8_scan_test` target next to the Phase 5/6/7 scan targets in `qt/xmake.lua`, with `set_rundir("$(projectdir)/..")` and pass output `plugin_architecture_phase8_scan_test passed`.

- [ ] **Step 3: Verify the scan**

Run:

```bash
xmake run -P qt plugin_architecture_phase8_scan_test
```

Expected: output contains `plugin_architecture_phase8_scan_test passed`.

- [ ] **Step 4: Commit the scan**

```bash
git add qt/test/plugin_architecture_phase8_scan_test.cpp qt/xmake.lua
git commit -m "test: add IP onboarding phase scan"
```

### Task 4: Phase 8 Review Gate

**Files:**
- Read-only review of Phase 8 files.

- [ ] **Step 1: Run focused verification**

Run:

```bash
xmake run -P qt plugin_architecture_phase8_scan_test
xmake run -P qt plugin_architecture_phase7_scan_test
xmake build -P qt qt
```

Expected: all commands exit 0.

- [ ] **Step 2: Run automatic review**

Dispatch read-only reviewers:
- spec reviewer: confirm the skill/prompt can guide an agent from an existing IP codebase into Finepaper's Qt frontend and V1 package flow;
- quality reviewer: confirm the docs do not rename external packages as plugins and do not introduce a legacy generator dependency.

- [ ] **Step 3: Fix high-confidence review findings**

If a reviewer finds a real issue, fix it, rerun focused verification, and commit with a focused message.

- [ ] **Step 4: Mark Phase 8 complete**

Only after fresh verification and review closure, update the parent task plan to move Phase 9 to `in_progress`.

---

## Self-Review

- Spec coverage: The plan covers the requested skill/prompt, the correction from "migration" to onboarding existing IP into the Qt frontend flow, V1 schema reuse, extension/package terminology, three anchor IP examples, verification, review, and commit boundaries.
- Placeholder scan: No TBD/TODO/fill-in-later placeholders remain.
- Type consistency: All commands, target names, and file paths are defined in this plan.
