# IpCraft Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace IpCraft's graph-rooted editor architecture with a ProjectDesign-centered IP creation workbench that supports package capabilities, arbitrary topology, NoC as a major capability, schema-driven configuration inspection, patch-based mutation, and tool protocol execution.

**Architecture:** `ipcraft-core` owns `ProjectDesign`, public document contracts, patching, diagnostics, artifacts, and layout-separated view documents. `ipcraft-package`, `ipcraft-domain`, `ipcraft-topology`, `ipcraft-ui`, `ipcraft-capability-noc`, and ordinary packages depend inward through public contracts and services. Generators, validators, native plugins, and UI views never mutate `ProjectDesign` directly; they return patches or diagnostics for host validation.

**Tech Stack:** C++/Qt for host and UI, JSON/YAML public schemas, package-local command tools, contract/golden tests, architecture scans, and hidden black-box acceptance harness.

---

## Repository And Build Layout

The current implementation is built from `qt/xmake.lua`; new architecture modules should first land under the existing Qt target tree unless a later PR explicitly creates standalone xmake targets.

Canonical path mapping for this plan:

- `ipcraft-core`: `qt/inc/ipcraft/core/` and `qt/src/ipcraft/core/`
- `ipcraft-package`: `qt/inc/ipcraft/package/` and `qt/src/ipcraft/package/`
- `ipcraft-domain`: `qt/inc/ipcraft/domain/` and `qt/src/ipcraft/domain/`
- `ipcraft-topology`: `qt/inc/ipcraft/topology/` and `qt/src/ipcraft/topology/`
- `ipcraft-ui`: `qt/inc/ipcraft/ui/` and `qt/src/ipcraft/ui/`, with adapters to existing `qt/inc/nodeeditor`, `qt/inc/widgets`, and `qt/inc/panels`
- `ipcraft-capability-noc`: `qt/inc/ipcraft/capability_noc/` and `qt/src/ipcraft/capability_noc/`
- package CLI: `qt/inc/cli/`, `qt/src/cli/`, or `qt/inc/ipcraft/cli/` and `qt/src/ipcraft/cli/` if split from the existing CLI facade
- tests: `qt/test/`, using contract test names such as `ipcraft_project_v1_contract_test.cpp`
- schemas: repository-root `schemas/`
- ordinary packages: repository-root `packages/`

Any new source directory must be wired into `qt/xmake.lua` in the same PR that introduces it. If a standalone target is added later, the PR must include an architecture note explaining dependency direction and tests.

---

## Target Module Boundaries

### ipcraft-core

Owns:

- `ProjectDesign`
- `ProjectDocumentV1` reader/writer
- `ComponentInstance`
- `InterfaceInstance`
- `Connection`
- `ConstraintSet`
- `TopologyGraph` references and neutral IR containers
- `ViewDocument`
- `LayoutDocument`
- `ExtensionBlock`
- `Diagnostic`
- `Artifact`
- `ProjectPatch`
- `PatchApplier`
- transaction/command boundary
- config provenance data structures

Dependencies:

- Standard library and Qt Core serialization helpers only if unavoidable.
- No Qt Widgets/UI headers.
- No NoC implementation package headers.
- No package-id special-case logic.

### ipcraft-package

Owns:

- `PackageManifest`
- `PackageRegistry`
- `CapabilityRegistry`
- `ComponentTypeRegistry`
- `InterfaceTypeRegistry`
- `ConnectionRuleRegistry`
- `ViewDescriptorRegistry`
- `ToolDescriptorRegistry`
- `PackageValidator`
- package authoring CLI support

Dependencies:

- `ipcraft-core`
- file/path/process abstraction
- no UI dependency
- no global mutable singleton

### ipcraft-domain

Owns:

- `ProjectSession`
- `DesignEditingService`
- `ResolutionService`
- `ConnectionCompatibilityService`
- `TopologyService`
- `ValidationPipeline`
- `GenerationPipeline`
- `ProjectionService`
- `ImportExportService`

Dependencies:

- `ipcraft-core`
- `ipcraft-package`
- `ipcraft-topology`

### ipcraft-topology

Owns:

- topology graph model helpers
- `ParametricTopologyProvider`
- `ExplicitGraphTopologyProvider`
- `LayoutProvider` interface
- built-in layout providers
- graph import/export helpers

Dependencies:

- `ipcraft-core`
- no NoC implementation package special cases

### ipcraft-ui

Owns:

- `ViewHost`
- `ProjectOverviewView`
- `BlockDiagramViewProvider`
- `TopologyGraphViewProvider`
- `InterfaceTableView`
- `ConfigInspector`
- `InspectorPanel`
- `PackageLibraryPanel`
- `DiagnosticsPanel`
- `FlowArtifactsPanel`
- UI intent adapters

Dependencies:

- `ipcraft-domain`
- `ipcraft-core`
- `ipcraft-package`
- optional native view plugin interface

Rule:

- UI emits intents and patches; no direct mutation of domain objects.

### ipcraft-capability-noc

Owns:

- NoC capability schemas
- NoC topology projection
- NoC validation rules
- NoC attachment model
- NoC topology view integration
- NoC-specific generator projection helpers

Dependencies:

- `ipcraft-core`
- `ipcraft-package`
- `ipcraft-domain`
- `ipcraft-topology`

Forbidden:

- core depending on this module.

### example package: packages/vendor-meshnoc

Owns:

- `package.yml`
- component schemas
- interface schemas
- connection rules
- NoC capability declaration
- view descriptors
- `tools/generate`
- `tools/validate`
- examples

Rule:

- This is one example NoC implementation package. Core code must not check this package id or any other implementation package id.

## Dependency Direction

Allowed:

```text
ipcraft-ui -> ipcraft-domain -> ipcraft-topology -> ipcraft-core
ipcraft-ui -> ipcraft-package -> ipcraft-core
ipcraft-domain -> ipcraft-package -> ipcraft-core
ipcraft-capability-noc -> ipcraft-domain/package/topology/core
ordinary packages -> public schemas/tool protocol
```

Forbidden:

```text
ipcraft-core -> ipcraft-ui
ipcraft-core -> ipcraft-capability-noc
ipcraft-core -> concrete packages
ipcraft-package -> ipcraft-ui
generators -> .fpproj
plugins -> mutable ProjectDesign
UI -> direct ProjectDesign mutation
```

## Data Flow

### Load And Inspect

1. `ProjectDocumentV1` parses `.fpproj` into `ProjectDesign`.
2. `PackageRegistry` loads package manifests and referenced schemas.
3. `ResolutionService` produces read-only resolved config with provenance.
4. `ProjectOverviewView` and `ConfigInspector` display authored, resolved, derived, and runtime data.
5. Diagnostics link to stable project refs, not UI object pointers.

### Edit

1. UI view creates an intent such as add component, set config, add topology link, or set layout.
2. `DesignEditingService` turns the intent into `ProjectPatch`.
3. `PatchApplier` validates patch operations against schemas, ids, capabilities, and invariants.
4. Transaction/command applies the patch and records undo/redo data.
5. Views refresh from `ProjectDesign` projections.

### Validate/Generate

1. User selects validator/generator target.
2. `ProjectionService` builds deterministic `ipcraft.tool.input.v1`.
3. UI can show Tool Input Preview before execution.
4. `ValidationPipeline` or `GenerationPipeline` executes package tool.
5. Tool returns `ipcraft.tool.result.v1`.
6. Host validates result, records diagnostics/artifacts, and optionally applies user-approved patches.

### Plugin

1. Host provides `ProjectSnapshot`.
2. Plugin returns `PluginResult`.
3. Host validates diagnostics, artifacts, and patches.
4. Patches apply only through `ProjectPatchCommand`.

## Phase Plan

### Phase 0: Architecture Audit And Deletion Map

Deliverables:

- Inventory of old Graph-centric areas.
- Inventory of NoC implementation package hardcoded paths.
- List of old schema files to delete or replace.
- List of temporary adapters with removal conditions.

No implementation beyond documentation and tests skeleton.

### Phase 1: Core IR

Deliverables:

- `ProjectDesign` value model.
- `ProjectDocumentV1` parser/writer.
- `ExtensionBlock`.
- `Diagnostic` and `Artifact`.
- `ProjectPatch` and `PatchApplier`.
- Public contract tests before implementation.

### Phase 2: Package Capability

Deliverables:

- `PackageManifest`.
- Injected `PackageRegistry`.
- Component/interface/connection rule registries.
- Package diagnostics.
- Package check tests.

### Phase 3: Topology IR

Deliverables:

- `TopologyGraph`.
- Explicit graph support.
- Parametric mesh expansion.
- Attachment model.
- Layout separation.
- AnyNet samples and tests.

Phase 3 owns topology attachment records. Semantic `connections` model component/interface relationships such as payload IP -> NIC, but `connections[].kind: attachment` is forbidden because topology attachment points are owned by `TopologyGraph.attachments`.

### Phase 4: Resolution And Tool Protocol

Deliverables:

- `ResolutionService` interface.
- `ConfigProvenance` model.
- `ToolInputBuilder`.
- `ToolResultParser`.
- `ProjectionService`.
- artifact registry and diagnostic mapper.
- dummy generator/validator tests.

### Phase 5: Domain Services

Deliverables:

- `DesignEditingService`.
- `ConnectionCompatibilityService`.
- `TopologyService`.
- `ValidationPipeline`.
- `GenerationPipeline`.
- documented replacement of direct UI mutation assumptions.

### Phase 6: UI View Host

Deliverables:

- `ViewProvider` interface.
- `ProjectOverviewView`.
- `BlockDiagramViewProvider` adapter.
- `TopologyGraphViewProvider` MVP.
- `InterfaceTableView` MVP.
- `ConfigInspector`.
- schema-driven `InspectorPanel`.
- UI intent to patch flow.

### Phase 7: NoC Capability

Deliverables:

- `ipcraft-capability-noc`.
- NoC extension schema.
- NoC attachment rules.
- NoC topology projection.
- NoC validation rules.
- NoC view integration.

### Phase 8: Example NoC Implementation Package

Deliverables:

- example NoC implementation manifest/components/interfaces/views/tools/examples.
- example NoC implementation generator consumes `ipcraft.tool.input.v1`.
- CPU -> NIC -> NoC package example validates.
- Architecture tests prove no core special cases.

### Phase 9: Package Authoring CLI

Deliverables:

- `ipcraft package init`
- `ipcraft package check`
- `ipcraft package preview`
- `ipcraft package test`
- `ipcraft package run-generator`
- `ipcraft package pack`

### Phase 10: Cleanup And Architecture Gates

Deliverables:

- Delete old Graph source-of-truth assumptions.
- Delete `ipcraft.noc.project.v1` generator path.
- Delete global `ModuleRegistry` singleton.
- Delete NoC hardcoded UI checks.
- Add architecture tests and hidden acceptance hooks.

## Testing Strategy By Phase

Every phase begins with public contract tests. Implementation begins only after failing tests exist for the public behavior of that phase.

Required test layers:

- contract tests
- unit tests
- integration tests
- golden file tests
- package tests
- UI smoke tests
- architecture boundary tests
- hidden acceptance tests maintained outside implementation branch

Public tests explain contracts. Hidden tests protect against fixture-specific implementation.

## First Implementation PR Recommendation

PR-001 should implement governance and core inspection foundation, not UI or any specific NoC generator migration.

Title:

`Introduce spec governance, ProjectDesign core IR, and config resolution contracts`

Scope:

- Add spec governance documents.
- Add public contract schemas for `ipcraft.project.v1`, `ipcraft.package.v1`, `ipcraft.component.v1`, `ipcraft.tool.input.v1`, and `ipcraft.patch.v1`.
- Add `ProjectDesign` value model.
- Add minimal `PackageManifest` model.
- Add `ResolutionService` interface.
- Add `ConfigProvenance` model.
- Add `ProjectPatch` interface.
- Add public contract tests.
- Add architecture test skeleton.
- Add examples: minimal UART, CPU -> NIC -> NoC, AnyNet explicit graph.

Do not migrate full UI. Do not implement a specific NoC generator.

Acceptance:

- Implementation Agent cannot access hidden tests.
- Acceptance Agent can run hidden tests externally.
- `ProjectDesign` roundtrips minimal examples.
- Resolved config shows package defaults with provenance.
- Layout is not part of semantic config.
- Dummy tool input projection can be generated and inspected.
- No NoC implementation package hardcode exists in core.
