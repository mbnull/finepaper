# Connection Provider Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 5 of the plugin-extensible IP platform: make real-time connection checking call package-declared rule providers instead of hardcoding IP connection class validation in `ConnectionRuleService`.

**Architecture:** `ConnectionRuleService` remains the fast editor-time coordinator for endpoint resolution, editor direction checks, topology checks, option sorting, and selection state. Package-declared IP interface class validation moves behind `ConnectionRuleProvider`, with a built-in `PackageConnectionRuleProvider` that wraps the existing V1 schema and `IpcraftConnectionValidator`. Ambiguous but legal package decisions surface as `ConnectionCheckStatus::Warning` while still producing a single auto-connectable option.

**Tech Stack:** C++23, Qt Core, existing `Graph`, `ModuleTypeMetadata`, V1 `IpcraftPackageManifest`, `IpcraftConnectionValidator`, xmake Qt tests.

---

## Execution Rules

- Continue in worktree `/home/bnl/dev/finepaper/.worktrees/plugin-host-foundation` on branch `plugin-host-foundation`.
- Use subagents where useful. Recommended subagent model/effort: GPT-5.5 with `xhigh`; minimum effort is `high`.
- Commit after every task.
- Keep this phase scoped to connection-checking provider boundaries.
- Do not rewrite schema loading, package discovery, `NodeEditorWidget`, command history, or project serialization in this phase.
- Preserve existing single-click/single-drag editor behavior for ambiguous package connections.

## Scope Check

This plan implements only Phase 5:

```text
Data-driven connection checking boundary
```

Follow-up phases own tool pipeline, commercial NoC workflows, onboarding skill/prompt, hardening, and final review.

## File Structure

Create:

- `qt/inc/connection/connectionruleprovider.h`: provider request/result API and built-in package provider declaration.
- `qt/src/connection/connectionruleprovider.cpp`: V1 package manifest provider implementation.
- `qt/test/plugin_architecture_phase5_scan_test.cpp`: Phase 5 architecture scan gate.

Modify:

- `qt/inc/connection/connectionruleservice.h`: provider ownership, warning status, provider registration API.
- `qt/src/connection/connectionruleservice.cpp`: call providers instead of `IpcraftConnectionValidator` directly.
- `qt/test/connectionruleservice_test.cpp`: assert ambiguous package checks return warning while remaining auto-connectable.
- `qt/xmake.lua`: compile provider source and add scan gate.

## Task 1: Provider Contract and Warning Status

**Files:**
- Create: `qt/inc/connection/connectionruleprovider.h`
- Create: `qt/src/connection/connectionruleprovider.cpp`
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `ConnectionCheckStatus::Warning`.
- [ ] Change `ConnectionCheckResult::hasSingleOption()` so `Allowed` and `Warning` with one option both return `true`.
- [ ] Add `ConnectionRuleProviderRequest`, `ConnectionRuleProviderStatus`, `ConnectionRuleProviderResult`, `ConnectionRuleProvider`, and `PackageConnectionRuleProvider`.
- [ ] Implement `PackageConnectionRuleProvider::canEvaluate()` as both endpoints having package accept rules.
- [ ] Implement `PackageConnectionRuleProvider::evaluate()` by converting `PortSemanticInfo` to `IpcraftConnectionParticipant` and calling `IpcraftConnectionValidator`.
- [ ] Map `IpcraftConnectionStatus::Valid` to provider `Allowed`, `Ambiguous` to provider `Warning`, and `Invalid` to provider `Rejected`.
- [ ] Preserve the old rejection reason codes: `interface_occupied` for already-used interfaces, otherwise `interface_class_mismatch`.
- [ ] Update the existing ambiguous service test to expect `ConnectionCheckStatus::Warning` and `hasSingleOption() == true`.
- [ ] Run:

```bash
xmake run -P qt connectionruleservice_test
```

- [ ] Commit as `feat: add connection rule provider boundary`.

## Task 2: Service Provider Aggregation

**Files:**
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/test/connectionruleservice_test.cpp`

- [ ] Add `ConnectionRuleService::addRuleProvider(std::unique_ptr<ConnectionRuleProvider>)`.
- [ ] Have both constructors install `PackageConnectionRuleProvider` by default.
- [ ] In `buildOptions()`, locate all providers whose `canEvaluate(source, target)` returns true.
- [ ] For provider-backed candidates, keep editor direction and class topology checks in `ConnectionRuleService`.
- [ ] Call provider `evaluate()` for package class validation; do not instantiate `IpcraftConnectionValidator` in `ConnectionRuleService`.
- [ ] For candidates with no matching provider, continue using existing declarative editor/IP-core rules.
- [ ] Set final `ConnectionCheckResult::status` to `Warning` when there is exactly one option and that option carries a non-`valid` connection status.
- [ ] Run:

```bash
xmake run -P qt connectionruleservice_test
xmake run -P qt graph_test
xmake run -P qt nodeeditor_geometry_test
```

- [ ] Commit as `feat: route connection checks through providers`.

## Task 3: Phase 5 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase5_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add a repo-root rundir scan test.
- [ ] Assert provider header/source files exist.
- [ ] Assert provider source owns `IpcraftConnectionValidator` usage.
- [ ] Assert `ConnectionRuleService` source owns provider iteration.
- [ ] Assert `ConnectionRuleService` source does not include `ipcraft/ipcraftconnectionvalidator.h` and does not instantiate `IpcraftConnectionValidator`.
- [ ] Assert `ConnectionCheckStatus::Warning` exists.
- [ ] Run:

```bash
xmake run -P qt plugin_architecture_phase5_scan_test
xmake run -P qt connectionruleservice_test
xmake build -P qt qt
```

- [ ] Commit as `test: add connection provider phase five scan`.

## Review Gate

After Task 3, run a focused automatic review:

```bash
xmake run -P qt plugin_architecture_phase5_scan_test
xmake run -P qt connectionruleservice_test
xmake run -P qt graph_test
xmake run -P qt nodeeditor_geometry_test
xmake build -P qt qt
git status --short --branch
```

Review questions:

- Does `ConnectionRuleService` still own fast editor orchestration but no longer directly validate package classes?
- Can packages declare connection classes through the existing V1 schema without new hardcoded service branches?
- Do ambiguous connections remain real-time connectable while surfacing warning status?
- Did the change avoid schema churn and avoid touching unrelated generator/UI code?
