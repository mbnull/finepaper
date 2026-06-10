# Tool Pipeline Plugin Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 6 of the plugin-extensible IP platform: route generation/tool execution through provider and service boundaries so package/tool behavior can be extended without editing `MainWindow` or hardcoding `FlowRunner` in the project generation coordinator.

**Architecture:** `ProjectGenerationRunner` remains the project-level coordinator for per-instance output directories, manifests, snapshots, and aggregate results. Actual package flow execution moves behind `GenerationFlowProvider`, with a built-in `PackageGenerationFlowProvider` that owns the existing `ipcraft::FlowRunner::runFlow()` call. `ToolPipelineService` becomes the application service consumed by `MainWindow` and exposed through `AppContext`; `ToolPipelinePlugin` verifies the tool pipeline is present as an internal plugin boundary.

**Tech Stack:** C++23, Qt Core/Widgets, existing `ProjectGenerationRunner`, `ipcraft::FlowRunner`, `PluginHost`, xmake Qt tests.

---

## Execution Rules

- Continue in worktree `/home/bnl/dev/finepaper/.worktrees/plugin-host-foundation` on branch `plugin-host-foundation`.
- Use subagents where useful. Recommended subagent model/effort: GPT-5.5 with `xhigh`; minimum effort is `high`.
- Commit after every task.
- Keep this phase scoped to generation/tool execution boundaries.
- Do not rewrite the public tool input schema or convert all generators in this phase.
- Preserve existing `ProjectGenerationRunner` behavior and generated artifact layout.

## Scope Check

This plan implements only Phase 6:

```text
Tool/generator pipeline plugin boundary
```

Follow-up phases own commercial NoC workflow completion, onboarding skill/prompt, hardening, and final architecture review.

## File Structure

Create:

- `qt/inc/app/generationflowprovider.h`: flow provider request/result interface and built-in package provider declaration.
- `qt/src/app/generationflowprovider.cpp`: built-in provider that calls `ipcraft::FlowRunner`.
- `qt/inc/app/toolpipelineservice.h`: application service for project generation and provider registration.
- `qt/src/app/toolpipelineservice.cpp`: service implementation over `ProjectGenerationRunner`.
- `qt/inc/app/toolpipelineplugin.h`: plugin factory for tool pipeline activation.
- `qt/src/app/toolpipelineplugin.cpp`: plugin implementation.
- `qt/test/toolpipelineplugin_test.cpp`: plugin activation tests.
- `qt/test/plugin_architecture_phase6_scan_test.cpp`: architecture scan gate.

Modify:

- `qt/inc/app/projectgenerationrunner.h`: provider registration API.
- `qt/src/app/projectgenerationrunner.cpp`: call providers instead of `ipcraft::FlowRunner::runFlow()` directly.
- `qt/test/projectgenerationrunner_test.cpp`: provider behavior tests.
- `qt/inc/app/appcontext.h`: expose `ToolPipelineService*`.
- `qt/inc/app/mainwindow.h`: own `ToolPipelineService`.
- `qt/src/app/mainwindow.cpp`: generate through `ToolPipelineService`.
- `qt/xmake.lua`: add provider/service/plugin sources and tests.

## Task 1: Generation Flow Provider Boundary

**Files:**
- Create: `qt/inc/app/generationflowprovider.h`
- Create: `qt/src/app/generationflowprovider.cpp`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/test/projectgenerationrunner_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `GenerationFlowRequest { ipcraft::FlowRunRequest flowRequest; QString outputDirectory; }`.
- [ ] Add `GenerationFlowProvider` with `canRun(const GenerationFlowRequest&)` and `run(const GenerationFlowRequest&)`.
- [ ] Add `PackageGenerationFlowProvider` that returns true for the package `generate` flow and calls `ipcraft::FlowRunner::runFlow()`.
- [ ] Add `ProjectGenerationRunner::addGenerationFlowProvider(std::unique_ptr<GenerationFlowProvider>)`.
- [ ] Install `PackageGenerationFlowProvider` by default in both constructors.
- [ ] In per-instance generation, build the existing `ipcraft::FlowRunRequest`, then call the last matching provider so later registered providers can override built-in execution.
- [ ] Return an instance error when no provider can run the flow.
- [ ] Add a fake provider test proving an injected provider can generate without invoking the default process flow.
- [ ] Add a fake warning/regression test proving the injected provider still receives the selected flow id, package id, instance id, run root, output root, config, graph config, and framework tool search paths.
- [ ] Run:

```bash
xmake run -P qt projectgenerationrunner_test
```

- [ ] Commit as `feat: add generation flow provider boundary`.

## Task 2: ToolPipelineService and Plugin

**Files:**
- Create: `qt/inc/app/toolpipelineservice.h`
- Create: `qt/src/app/toolpipelineservice.cpp`
- Create: `qt/inc/app/toolpipelineplugin.h`
- Create: `qt/src/app/toolpipelineplugin.cpp`
- Create: `qt/test/toolpipelineplugin_test.cpp`
- Modify: `qt/inc/app/appcontext.h`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add `ToolPipelineService::generateProject(const ProjectGenerationRequest&) const`.
- [ ] Add `ToolPipelineService::addGenerationFlowProvider(std::unique_ptr<GenerationFlowProvider>)`.
- [ ] Add `ToolPipelineService::frameworkToolSearchPaths()` and `setFrameworkToolSearchPaths(...)`.
- [ ] Add `ToolPipelineService* toolPipelineService` to `AppContext`.
- [ ] Add `ToolPipelinePlugin` with id `finepaper.tool-pipeline`.
- [ ] Make plugin activation fail if `ToolPipelineService` is missing.
- [ ] Add `std::unique_ptr<ToolPipelineService> m_toolPipelineService` to `MainWindow`.
- [ ] Initialize it before UI setup.
- [ ] Replace local `ProjectGenerationRunner runner; runner.generate(request);` in `MainWindow::generateVerilog()` with `m_toolPipelineService->generateProject(request)`.
- [ ] Add plugin activation tests mirroring project/package plugin tests.
- [ ] Run:

```bash
xmake run -P qt toolpipelineplugin_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt projectgenerationrunner_test
```

- [ ] Commit as `feat: add tool pipeline service plugin`.

## Task 3: Phase 6 Architecture Scan Gate

**Files:**
- Create: `qt/test/plugin_architecture_phase6_scan_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] Add a repo-root rundir scan test.
- [ ] Assert generation provider, tool pipeline service, and tool pipeline plugin files exist.
- [ ] Assert `generationflowprovider.cpp` owns the `ipcraft::FlowRunner::runFlow` call.
- [ ] Assert `projectgenerationrunner.cpp` calls `GenerationFlowProvider` and no longer contains `ipcraft::FlowRunner::runFlow`.
- [ ] Assert `MainWindow` owns `m_toolPipelineService` and calls `generateProject`.
- [ ] Assert `MainWindow` no longer constructs `ProjectGenerationRunner`.
- [ ] Assert `AppContext` exposes `toolPipelineService`.
- [ ] Run:

```bash
xmake run -P qt plugin_architecture_phase6_scan_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt toolpipelineplugin_test
xmake build -P qt qt
```

- [ ] Commit as `test: add tool pipeline phase six scan`.

## Review Gate

After Task 3, run a focused automatic review:

```bash
xmake run -P qt plugin_architecture_phase6_scan_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt toolpipelineplugin_test
xmake run -P qt ipcatalogpanel_test
xmake build -P qt qt
git status --short --branch
```

Review questions:

- Does package flow execution live behind a provider boundary?
- Can later registered providers override built-in flow execution without editing `ProjectGenerationRunner`?
- Does `MainWindow` consume a service instead of constructing the generation runner?
- Did generated artifact layout, manifests, snapshots, and existing package generation behavior remain intact?
