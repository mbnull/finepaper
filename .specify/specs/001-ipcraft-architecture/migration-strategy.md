# Migration And Deletion Strategy

This refactor does not preserve old runtime compatibility. The migration strategy is a deletion and replacement plan, not an adapter-first compatibility plan.

## Policy

- Normal project load accepts `ipcraft.project.v1` only.
- Old schemas may be supported only by explicit import/conversion commands if needed for manual migration.
- Old Graph-centric code may remain temporarily only as a view adapter or import adapter with a removal phase.
- No old NoC generator input path remains as normal generation contract.
- Every temporary adapter must have an owner, target replacement, and deletion condition.

## Old Modules To Delete

Delete or fully retire these concepts from normal runtime paths:

- old document schemas that model the project as graph/canvas state
- old `ipcraft.noc.project.v1` generator input
- old NoC-specific exporter as generator contract
- old direct Graph -> generator path
- old `ModuleRegistry` global mutable singleton
- old view XML/YAML assumptions that describe drawing primitives or internal module geometry
- old UI checks for `mesh_router`, router direction ports, hardcoded endpoint module types, or package-specific NoC implementation package ids
- old command types that mutate raw graph/domain state without `ProjectPatch`

## Old Modules To Replace

### Graph / Module / Connection

Replacement:

- `ProjectDesign` for domain semantics.
- `TopologyGraph` for semantic topology.
- `ViewDocument` and `LayoutDocument` for visual state.
- `BlockDiagramViewProvider` and `TopologyGraphViewProvider` for interactive graph editing.

Temporary adapter:

- Existing graph rendering code may be wrapped behind view providers during Phase 6.

Removal condition:

- UI emits `ProjectPatch` intents and no save/load/generation path depends on graph objects.

### Command Layer

Replacement:

- `ProjectPatchCommand`
- transaction groups
- semantic/layout patch ops
- `DesignEditingService`

Temporary adapter:

- Existing add/remove module/connection commands may delegate to patch commands while UI migration is incomplete.

Removal condition:

- All durable UI mutations flow through `DesignEditingService` or `PatchApplier`.

### ModuleRegistry Singleton

Replacement:

- Injected `PackageRegistry`
- `CapabilityRegistry`
- `ComponentTypeRegistry`
- `InterfaceTypeRegistry`
- `ConnectionRuleRegistry`

Temporary adapter:

- A read-only adapter may expose package registry data to old UI controls during Phase 2-6.

Removal condition:

- No code path obtains package/component types through a global mutable registry.

### IP Catalog / ipcraft Package Runtime

Replacement:

- `ipcraft.package.v1`
- package capability registries
- package authoring CLI
- package validator

Temporary adapter:

- Existing package discovery may be used only to locate roots, then normalized into `ipcraft.package.v1`.

Removal condition:

- Package loading consumes only `ipcraft.package.v1` manifests and referenced capability files.

### NoC Exporter

Replacement:

- `ProjectionService`
- `ipcraft.tool.input.v1`
- `ipcraft-capability-noc` projection
- package-specific NoC implementation generator

Temporary adapter:

- None for normal generation. A one-off comparison tool may exist in tests/readiness only if it is not part of runtime.

Removal condition:

- package-specific NoC implementation generator and validator consume only `ipcraft.tool.input.v1`.

### NodeEditorWidget

Replacement:

- `ViewHost`
- `BlockDiagramViewProvider`
- `TopologyGraphViewProvider`
- UI intents -> `DesignEditingService` -> `ProjectPatch`

Temporary adapter:

- Existing widget may render a projection during Phase 6.

Removal condition:

- Widget no longer owns domain logic, compatibility checks, save/load, or generation inputs.

## Old Modules To Keep As Adapters Only

These may remain temporarily if isolated:

- graph painter and geometry helpers for rendering view projections
- old node editor canvas for temporary block diagram display
- import/conversion helpers for old project files, behind explicit command
- old generator comparison harnesses under test/readiness paths only

Adapter rules:

- Adapter cannot be source of truth.
- Adapter cannot introduce new public contract behavior.
- Adapter cannot hold mutable domain state outside patch transactions.
- Adapter must be removed or converted by Phase 10.

## Replacement Path By Phase

### Phase 0

Create deletion map and architecture scans. No production migration yet.

### Phase 1

Add `ProjectDesign`, document v1 reader/writer, diagnostics, artifacts, extension blocks, and patches. Old project loader remains only until new tests pass.

### Phase 2

Introduce package registry and package contract. Old registry becomes an adapter over new package data or is disabled for new projects.

### Phase 3

Move semantic topology out of graph editor objects. Graph views render `TopologyGraph`.

### Phase 4

Replace generator inputs with tool protocol. Delete normal `ipcraft.noc.project.v1` emission.

### Phase 5

Domain services become the only mutation, validation, and generation entry points. Resolution and projection interfaces already exist from Phase 4.

### Phase 6

UI view host replaces graph-as-domain interaction. Node editor becomes projection-only or is removed.

### Phase 7

NoC logic moves into capability module.

### Phase 8

Concrete NoC implementation packages move into package trees and tools.

### Phase 9

Package authoring CLI replaces ad hoc package/runtime authoring paths.

### Phase 10

Delete old code paths, enforce architecture gates, and remove temporary adapters whose replacement exists.

## Normal Loader Behavior

Accepted:

```yaml
schema: ipcraft.project.v1
```

Rejected:

```yaml
schema: ipcraft.noc.project.v1
```

Diagnostic:

```yaml
schema: ipcraft.diagnostic.v1
id: diag_unsupported_schema
severity: error
code: project.unsupported_schema
message: Project schema ipcraft.noc.project.v1 is not supported by the normal IpCraft loader.
targetRef: document:/
source: { component: ipcraft-core, validator: project_document_v1 }
```

## Import Command Behavior

If explicit import is retained, it must be named as conversion, not compatibility:

```bash
ipcraft import legacy-project old.fpproj --output migrated.fpproj
```

Rules:

- It reads old documents.
- It emits `ipcraft.project.v1`.
- It reports losses as diagnostics.
- It does not become part of normal project load.
- It is not used by generators.

## Deletion Acceptance

Phase 10 is incomplete until scans prove:

- No normal generator path emits or reads `ipcraft.noc.project.v1`.
- No core/domain source depends on old Graph as aggregate.
- No global mutable `ModuleRegistry` is reachable by package loading.
- No core/UI hardcoded package-specific NoC implementation package-id checks remain.
- No component config layout fields remain in examples, tests, or serializers.
