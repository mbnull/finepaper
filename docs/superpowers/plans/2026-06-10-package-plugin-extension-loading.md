# Package Plugin Extension Loading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 3 of the plugin-extensible IP platform: move package discovery, V1 package loading, module registry rebuild, and IP catalog construction behind an internal Package Plugin service.

**Architecture:** `PackageService` is the only application service that calls `loadIpcraftPackageManifestsWithDiagnostics()` and rebuilds `ModuleRegistry` from package manifests. `MainWindow` keeps the existing catalog UI refresh behavior, but consumes package service results instead of assembling package/catalog/module state inline.

**Tech Stack:** C++23, Qt Core/Widgets, existing `ipcraft.package.v1` loader, `ModuleRegistry`, `IpCatalogService`, xmake Qt test targets.

---

## Execution Rules

- Continue in worktree `/home/bnl/dev/finepaper/.worktrees/plugin-host-foundation` on branch `plugin-host-foundation`.
- Use subagent-driven development where useful. Recommended subagent model/effort: GPT-5.5 with `xhigh`; minimum effort is `high`.
- Commit after every task.
- Do not change project I/O, editor projection, generator behavior, connection checking, or NoC-specific behavior in this phase.
- Keep diagnostics behavior visible to the package roots dialog and log panel.
- The three anchor packages under `ipcores/` must be covered by tests: `finepaper-noc`, `ravenoc`, and `opennoc`.

## Scope Check

This plan implements only Phase 3:

```text
Package Plugin And Extension Loading
```

Follow-up phases own editor command rebinding, connection checking, tool pipeline, NoC workflow completion, onboarding skill, hardening, and final review.

## File Structure

Create:

- `qt/inc/package/packageservice.h`: package loading/catalog service API.
- `qt/src/package/packageservice.cpp`: manifest loading, registry rebuild, catalog construction.
- `qt/inc/package/packageplugin.h`: internal Package Plugin factory.
- `qt/src/package/packageplugin.cpp`: package plugin activation guard.
- `qt/test/packageservice_test.cpp`: service and anchor package tests.
- `qt/test/packageplugin_test.cpp`: plugin host activation tests.
- `qt/test/plugin_architecture_phase3_scan_test.cpp`: Phase 3 architecture scan gate.

Modify:

- `qt/inc/app/appcontext.h`: add `PackageService* packageService`.
- `qt/inc/app/mainwindow.h`: own `PackageService`.
- `qt/src/app/mainwindow.cpp`: route package diagnostics and catalog reload through `PackageService`.
- `qt/inc/ipcore/ipcatalogservice.h` and `qt/src/ipcore/ipcatalogservice.cpp`: add an empty default constructor so services can own an initially empty catalog.
- `qt/xmake.lua`: add tests and direct `mainwindow.cpp` test dependencies.

## Task 1: PackageService

**Files:**
- Create: `qt/inc/package/packageservice.h`
- Create: `qt/src/package/packageservice.cpp`
- Create: `qt/test/packageservice_test.cpp`
- Modify: `qt/inc/ipcore/ipcatalogservice.h`
- Modify: `qt/src/ipcore/ipcatalogservice.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add an empty `IpCatalogService()` constructor that leaves `entries()` empty.
- [ ] Add `PackageServiceLoadResult` with `success`, `packageRootCount`, `packageCount`, and `QVector<IpcraftDiagnostic> diagnostics`.
- [ ] Implement `PackageService::reloadPackageRoots(const QStringList& rootPaths)`:
  - call `loadIpcraftPackageManifestsWithDiagnostics(rootPaths)`;
  - reset the supplied `ModuleRegistry` to `ModuleRegistry(ModuleRegistry::LoadMode::Empty)`;
  - load manifests into that registry with `loadIpcraftPackages`;
  - rebuild the owned `IpCatalogService`;
  - store root paths, manifests, diagnostics, and catalog state.
- [ ] Implement read-only accessors for package roots, manifests, diagnostics, catalog, and module registry.
- [ ] Test empty catalog default construction.
- [ ] Test loading `ipcores/` discovers `finepaper-noc`, `ravenoc`, and `opennoc`, exposes catalog entries for all three, and registers module types for each loaded package.
- [ ] Test invalid package roots keep diagnostics in the service and result without crashing.
- [ ] Run:

```bash
xmake run -P qt packageservice_test
xmake run -P qt ipcatalogservice_test
```

- [ ] Commit as `feat: add package service source of truth`.

## Task 2: PackagePlugin And AppContext

**Files:**
- Create: `qt/inc/package/packageplugin.h`
- Create: `qt/src/package/packageplugin.cpp`
- Create: `qt/test/packageplugin_test.cpp`
- Modify: `qt/inc/app/appcontext.h`
- Modify: `qt/xmake.lua`

- [ ] Add `PackageService* packageService = nullptr` to `AppContext`.
- [ ] Implement `createPackagePlugin()` returning an internal plugin with id `finepaper.package`.
- [ ] Package plugin activation must fail with a clear error if `AppContext::packageService` is missing.
- [ ] Test activation success with `PackageService` and failure without it.
- [ ] Run:

```bash
xmake run -P qt packageplugin_test
xmake run -P qt pluginhost_foundation_test
```

- [ ] Commit as `feat: add package plugin activation`.

## Task 3: MainWindow Package Service Rebinding

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `std::unique_ptr<PackageService> m_packageService`.
- [ ] Initialize `PackageService` with `ModuleRegistry::instance()`.
- [ ] Change `manageIpcorePackageRoots()` to get diagnostics through `PackageService` instead of direct manifest loading.
- [ ] Change `reloadIpcoreCatalog()` to call `m_packageService->reloadPackageRoots(rootPaths)`.
- [ ] Preserve existing log messages, package count, diagnostic lines, property panel refresh, catalog panel search refresh, selected IP restoration, topology menu rebuild, and status bar behavior.
- [ ] Ensure `MainWindow` no longer directly calls `loadIpcraftPackageManifestsWithDiagnostics()` or `ModuleRegistry::loadIpcraftPackages()`.
- [ ] Run:

```bash
xmake run -P qt ipcatalogpanel_test
xmake run -P qt packageservice_test
xmake build -P qt qt
```

- [ ] Commit as `feat: route package catalog reload through package service`.

## Task 4: Phase 3 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase3_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add a scan test with repo-root rundir.
- [ ] Assert PackageService and PackagePlugin files exist.
- [ ] Assert PackageService owns `loadIpcraftPackageManifestsWithDiagnostics`, `loadIpcraftPackages`, and `IpCatalogService` construction.
- [ ] Assert `MainWindow` contains `m_packageService->reloadPackageRoots` and no direct `loadIpcraftPackageManifestsWithDiagnostics` / `loadIpcraftPackages(loadResult.manifests)` package loading path.
- [ ] Assert `packageservice_test.cpp` names all three anchor packages.
- [ ] Run:

```bash
xmake run -P qt plugin_architecture_phase3_scan_test
xmake run -P qt packageservice_test
xmake run -P qt packageplugin_test
xmake run -P qt ipcatalogpanel_test
xmake build -P qt qt
```

- [ ] Commit as `test: add package plugin phase three scan`.

## Phase 3 Review Gate

Run fresh verification:

```bash
xmake run -P qt packageservice_test
xmake run -P qt packageplugin_test
xmake run -P qt plugin_architecture_phase3_scan_test
xmake run -P qt ipcatalogservice_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt projectservice_test
xmake build -P qt qt
git status --short --branch
```

Then run automatic spec and quality review. Classify the phase:

- `pass`: all acceptance criteria met.
- `pass-with-debt`: criteria met but known hardening remains for later phases.
- `blocked`: package loading/catalog boundaries cannot be validated.

Expected debt if Phase 3 passes:

- `IpCatalogService` still contains kind-specific `noc` instance-limit fallback until Phase 7 or Phase 9 hardening.
- `ModuleRegistry` remains a singleton integration point until a later kernel service cleanup.
