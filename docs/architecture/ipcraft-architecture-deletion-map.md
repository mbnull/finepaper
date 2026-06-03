# IpCraft Architecture Deletion Map

This map is the Phase 0 hard-cutover deletion record for
`.specify/specs/001-ipcraft-architecture`. It is sourced from the migration
strategy, implementation plan, and V1 core architecture contract.

## Cutover Policy

- Normal project load accepts `ipcraft.project.v1` only.
- Old schemas may exist only behind explicit import/conversion commands; the
  normal loader rejects old schemas with `project.unsupported_schema`.
- Temporary adapters are adapter only: they cannot be source of truth, cannot
  add public contract behavior, and cannot hold mutable domain state outside
  patch transactions.
- Phase 10 is incomplete until normal save/load, validation, generation, and UI
  mutation paths no longer depend on the legacy concepts below.

| Legacy concept | Classification | Replacement | Removal condition |
| --- | --- | --- | --- |
| Graph / Module / Connection as project source of truth | replace | `ProjectDesign`, semantic `Connection`, `TopologyGraph`, `ViewDocument`, and `LayoutDocument`; views are provided by `BlockDiagramViewProvider` and `TopologyGraphViewProvider`. | Delete legacy graph-rooted project ownership when UI emits `ProjectPatch` intents and no save/load/generation path depends on graph objects. |
| `NodeEditorWidget` domain mutation | adapter only | `ViewHost`, `BlockDiagramViewProvider`, `TopologyGraphViewProvider`, `DesignEditingService`, and `ProjectPatch`. | Remove or keep as projection-only rendering after it no longer owns domain logic, compatibility checks, save/load, or generation inputs. |
| `ModuleRegistry` global mutable singleton | replace | Injected `PackageRegistry`, `ComponentTypeRegistry`, `InterfaceTypeRegistry`, and `ConnectionRuleRegistry` with package capability data. | Delete global mutable registry access when no code path obtains package, component, interface, or connection types through `ModuleRegistry::instance()`. |
| `ipcraft.noc.project.v1` normal generator input | delete | `ipcraft.tool.input.v1` projected from `ProjectDesign` by `ProjectionService` and `ToolInputBuilder`. | Delete all normal generator emission or reading of `ipcraft.noc.project.v1`; old schema use may exist only in explicit import/conversion or isolated readiness comparison fixtures. |
| NoC implementation package ids in core | delete | Ordinary package data under `packages/vendor-meshnoc`; capability data under `ipcraft-capability-noc`. | Delete core/UI checks for concrete NoC package ids, `mesh_router`, router direction ports, endpoint module types, or other implementation package details. |
| UI protocol compatibility hardcode | replace | Package-owned connection rules loaded through `ConnectionRuleRegistry` and evaluated by domain compatibility services. | Remove UI hardcodes once connection decisions are backed by package connection rules and surfaced through domain services. |
| Raw graph commands for durable mutation | replace | `ProjectPatchCommand`, transaction groups, `PatchApplier`, and `DesignEditingService`. | Delete raw graph/domain command mutation once every durable UI mutation flows through `DesignEditingService` or `PatchApplier`. |
| Graph project serializer | adapter only | `ProjectDocumentV1` reader/writer for semantic project state; any graph serializer may only adapt view projections or explicit imports. | Delete serializer write paths for semantic project state; it cannot write `ProjectDesign`, generation inputs, or normal project files. |
| Old NoC exporter | delete | `ProjectionService`, `ToolInputBuilder`, `ipcraft.tool.input.v1`, and package generator tools. | Delete old exporter once package-specific NoC implementation generators and validators consume only `ipcraft.tool.input.v1`. |
| View XML/YAML drawing primitive descriptors | replace | `ipcraft.view.descriptor.v1` package descriptors loaded by package/view descriptor registries. | Delete old drawing primitive and internal module geometry descriptor loading from normal runtime after descriptors come from packages. |
| Explicit import/conversion of old projects | adapter only | Named conversion commands that read old documents, emit canonical `ipcraft.project.v1`, and report losses as diagnostics. | Keep outside normal load and generation paths; normal loader rejects old schemas with `project.unsupported_schema`. |
