# IpCraft Architecture Deletion Map

Migration-only legacy schema handling. Not used by normal runtime loading.

This file is the current hard-cutover deletion and adapter boundary map. It
keeps the public terms required by architecture scan tests while documenting
the present implementation state.

## Cutover Policy

- Normal project architecture is `ipcraft.project.v1` + `ipcraft.package.v1`.
- Old project schemas are accepted only by explicit migration/import paths.
- Temporary adapters are adapter only: they cannot become source of truth,
  cannot add hidden public behavior, and cannot own durable domain state.
- Deletion is complete only when save/load, validation, generation, and UI
  mutation no longer depend on the legacy concept.

## Current Legacy Boundary Table

| Legacy concept | Classification | Replacement | Current status | Removal condition |
| --- | --- | --- | --- | --- |
| Graph / Module / Connection as project source of truth | replace | `ProjectDocument`, `ProjectDesign`, semantic `Connection`, `TopologyGraph`, `ViewDocument`, `LayoutDocument` | Qt still uses `Graph` for live canvas projection. It is no longer the architecture root. | Delete graph-rooted project ownership when UI mutations emit `ProjectPatch` or domain service intents and save/load/generation no longer depend on graph objects. |
| `NodeEditorWidget` domain mutation | adapter only | `ViewHost`, `BlockDiagramViewProvider`, `TopologyGraphViewProvider`, `DesignEditingService`, `ProjectPatch` | Node editor still drives canvas interaction and command creation. | Keep only as projection/rendering after it no longer owns compatibility checks, persistent state, or generation inputs. |
| `ModuleRegistry` global mutable singleton | replace | injected `PackageRegistry`, `ComponentTypeRegistry`, `InterfaceTypeRegistry`, `ConnectionRuleRegistry` | Current Qt bridge still uses registry-style module data from package manifests. | Remove global mutable access after package, component, interface, and connection types resolve through injected registries. |
| `ipcraft.noc.project.v1` normal generator input | delete | `ipcraft.tool.input.v1` or emitted package-specific tool input projected from ProjectDesign | Some first-party Ruby generators still keep compatibility normalization. | Delete all normal generator emission/reading of `ipcraft.noc.project.v1`; old schema remains only in explicit migration or comparison fixtures. |
| NoC implementation package ids in core | delete | ordinary package data; NoC rules live in package capability/extension data | Core direction is package-neutral. Some UI/generator adapters still know first-party package ids. | Delete core/UI checks for concrete NoC ids, router directions, endpoint module names, or implementation package details. |
| UI protocol compatibility hardcode | replace | package-owned `connection_rules` evaluated by domain compatibility services | Built-in validator and graph path still contain compatibility bridge logic. | Remove UI hardcodes after connection decisions come from package rules and domain services. |
| Raw graph commands for durable mutation | replace | `ProjectPatchCommand`, transaction groups, `PatchApplier`, `DesignEditingService` | Current command stack still wraps graph edits. | Delete durable graph-domain mutation after every persistent edit flows through design services or patch application. |
| Graph project serializer | adapter only | `ProjectDocumentV1` reader/writer and projection-only serializers | Still used to bridge Qt `Graph` with V1 project state. | Delete serializer writes for semantic project state; keep only explicit import or view projection if needed. |
| Old NoC exporter | delete | `ProjectionService`, `ToolInputBuilder`, package emitters, and package generator tools | First-party generators are moving toward ProjectDesign/tool-input projection. | Delete old exporter once all normal package generators consume emitted inputs only. |
| View XML/YAML drawing primitive descriptors | replace | `ipcraft.view.descriptor.v1` package descriptors | Existing views still include XML bridge data for Qt editor. | Delete old drawing primitive descriptor loading when view descriptors come from packages. |
| Explicit import/conversion of old projects | adapter only | named migration commands that emit canonical `ipcraft.project.v1` | Migration remains allowed as a side-effect-free conversion boundary. | Keep outside normal load/generation paths; normal loader rejects unsupported schemas. |

## Terms Required By The Architecture Gate

The following public terms are intentionally retained for audit and scan tests:

- Graph / Module / Connection
- NodeEditorWidget
- ModuleRegistry
- ipcraft.noc.project.v1
- ProjectPatchCommand
- ToolInputBuilder
- packages/vendor-meshnoc
- delete
- replace
- adapter only

## Current Direction

The architecture is not trying to preserve the legacy graph model. It is using
temporary adapters to keep the Qt editor and first-party generators operational
while the durable model converges on document, package, component, interface,
connection, topology, view, patch, and tool-input contracts.
