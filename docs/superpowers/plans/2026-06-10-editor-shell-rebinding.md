# Editor Shell Rebinding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 4 of the plugin-extensible IP platform: keep the existing QtNodes/node-editor interaction shell while moving the durable editor projection bridge out of `MainWindow` and behind a tested service boundary.

**Architecture:** `EditorProjectionService` becomes the only application service that calls `GraphProjectSerializer` for load/save projection bridging. `MainWindow` still owns UI suppression, command history, and window state, but no longer converts between `Graph` and `ProjectDocument` directly. Existing editor gestures continue to mutate `Graph` through `CommandManager`; this phase does not rewrite QtNodes or command classes.

**Tech Stack:** C++23, Qt Core/Widgets, existing `Graph`, `ProjectService`, `ProjectStateService`, `ProjectIpService`, `GraphProjectSerializer`, xmake Qt test targets.

---

## Execution Rules

- Continue in worktree `/home/bnl/dev/finepaper/.worktrees/plugin-host-foundation` on branch `plugin-host-foundation`.
- Use subagent-driven development where useful. Recommended subagent model/effort: GPT-5.5 with `xhigh`; minimum effort is `high`.
- Commit after every task.
- Do not rewrite `NodeEditorWidget`, QtNodes interaction handling, command classes, connection checking, package loading, or generator behavior in this phase.
- Existing real-time editor interactions must remain covered by current UI/editor tests.
- `GraphProjectSerializer` may remain as the temporary projection adapter, but `MainWindow` must stop calling it directly.

## Scope Check

This plan implements only Phase 4:

```text
Editor Shell Rebinding
```

Follow-up phases own data-driven connection checking, tool pipeline, NoC commercial workflow completion, onboarding skill, hardening, and final review.

## File Structure

Create:

- `qt/inc/project/editorprojectionservice.h`: projection bridge service API.
- `qt/src/project/editorprojectionservice.cpp`: rebuild projection, sync projection, clear projection.
- `qt/test/editorprojectionservice_test.cpp`: service boundary tests.
- `qt/test/plugin_architecture_phase4_scan_test.cpp`: Phase 4 architecture scan gate.

Modify:

- `qt/inc/app/mainwindow.h`: own `EditorProjectionService`.
- `qt/src/app/mainwindow.cpp`: route load/save/clear projection bridge through `EditorProjectionService`.
- `qt/xmake.lua`: add tests and direct `mainwindow.cpp` dependencies.

## Task 1: EditorProjectionService

**Files:**
- Create: `qt/inc/project/editorprojectionservice.h`
- Create: `qt/src/project/editorprojectionservice.cpp`
- Create: `qt/test/editorprojectionservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `EditorProjectionResult { bool success; QString error; }`.
- [ ] Add `EditorProjectionService(Graph*, ProjectStateService*, ProjectIpService*, ProjectService*)`.
- [ ] Add `rebuildProjectionFromDocument(const ProjectDocument& document, const QString& loadedPath)`:
  - call `GraphProjectSerializer::loadProject(document, *graph)`;
  - call `ProjectIpService::loadFromDocument(document)`;
  - call `ProjectService::replaceDocumentFromLoadedFile(document, loadedPath)`;
  - return failure without mutating `ProjectService` when graph projection load fails.
- [ ] Add `syncProjectFromProjection(const QString& projectName)`:
  - call `GraphProjectSerializer::toProject(*graph, projectName)`;
  - call `ProjectStateService::writeToDocument(document)`;
  - call `ProjectService::replaceDocumentFromProjection(std::move(document))`.
- [ ] Add `clearProjection()`:
  - clear `Graph`, `ProjectIpService`, and `ProjectService`.
- [ ] Test rebuild from a V1 document with one registered module and one IP instance.
- [ ] Test sync from graph writes current module and IP instance state into `ProjectService`.
- [ ] Test clear empties graph, project IP state, and durable service.
- [ ] Run:

```bash
xmake run -P qt editorprojectionservice_test
xmake run -P qt projectdocument_test
xmake run -P qt projectservice_test
```

- [ ] Commit as `feat: add editor projection service`.

## Task 2: MainWindow Projection Rebinding

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `std::unique_ptr<EditorProjectionService> m_editorProjectionService`.
- [ ] Initialize it after `ProjectService`, `ProjectStateService`, and `ProjectIpService`.
- [ ] Replace `GraphProjectSerializer::loadProject(...)` in `loadDocument()` with `m_editorProjectionService->rebuildProjectionFromDocument(...)`.
- [ ] Replace `GraphProjectSerializer::toProject(...)` and `ProjectStateService::writeToDocument(...)` in `saveDocument()` with `m_editorProjectionService->syncProjectFromProjection(...)`.
- [ ] Replace direct graph/project-IP/project-service clear logic in `clearDocument()` with `m_editorProjectionService->clearProjection()`.
- [ ] Preserve document tracking suppression, command history baseline, property selection reset, dirty-state updates, recent project updates, and user-facing messages.
- [ ] Ensure `MainWindow` no longer contains `GraphProjectSerializer::loadProject` or `GraphProjectSerializer::toProject`.
- [ ] Run:

```bash
xmake run -P qt ipcatalogpanel_test
xmake run -P qt nodeeditor_geometry_test
xmake run -P qt editorprojectionservice_test
xmake build -P qt qt
```

- [ ] Commit as `feat: route editor projection through service`.

## Task 3: Phase 4 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase4_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add a repo-root rundir scan test.
- [ ] Assert `EditorProjectionService` files exist.
- [ ] Assert `EditorProjectionService` source owns `GraphProjectSerializer::loadProject`, `GraphProjectSerializer::toProject`, `replaceDocumentFromLoadedFile`, and `replaceDocumentFromProjection`.
- [ ] Assert `MainWindow` owns `m_editorProjectionService`.
- [ ] Assert `MainWindow` contains `rebuildProjectionFromDocument`, `syncProjectFromProjection`, and `clearProjection`.
- [ ] Assert `MainWindow` does not contain direct `GraphProjectSerializer::loadProject` or `GraphProjectSerializer::toProject`.
- [ ] Assert existing node-editor interaction test target remains present.
- [ ] Run:

```bash
xmake run -P qt plugin_architecture_phase4_scan_test
xmake run -P qt editorprojectionservice_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt nodeeditor_geometry_test
xmake build -P qt qt
```

- [ ] Commit as `test: add editor shell phase four scan`.

## Phase 4 Review Gate

Run fresh verification:

```bash
xmake run -P qt editorprojectionservice_test
xmake run -P qt plugin_architecture_phase4_scan_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt nodeeditor_geometry_test
xmake run -P qt projectdocument_test
xmake run -P qt projectservice_test
xmake run -P qt packageservice_test
xmake build -P qt qt
git status --short --branch
```

Then run automatic spec and quality review. Classify the phase:

- `pass`: all acceptance criteria met.
- `pass-with-debt`: criteria met but projection still uses `GraphProjectSerializer` as an adapter.
- `blocked`: editor interactions or project load/save regress.

Expected debt if Phase 4 passes:

- `Graph` remains the live real-time editor projection.
- `GraphProjectSerializer` remains the temporary projection adapter until later projection model replacement.
- Individual editor commands still mutate `Graph`; Phase 4 only centralizes durable projection synchronization.
