# IpCraft Architecture Review Checklist

Use this checklist for every implementation PR and every phase acceptance report. A PR may not be accepted only because tests pass; reviewers must inspect boundary discipline and generality.

## Required PR Evidence

- Implementation Notes identify exact spec requirements implemented.
- Public contracts changed are listed.
- New or changed schemas include parser, writer, roundtrip, negative, and golden tests.
- Boundaries preserved are explicitly stated:
  - `ProjectDesign` source of truth.
  - package capability boundary.
  - topology semantic/layout separation.
  - tool protocol boundary.
  - patch mutation boundary.
  - config resolution/provenance boundary.
- Temporary adapters are listed with removal condition and target phase.
- No "make tests pass" rationale appears as the implementation explanation.

## Source-Of-Truth Boundary

Check:

- Project load/save uses `ProjectDocumentV1` into `ProjectDesign`.
- Graph/node editor code is a projection or view adapter.
- Commands operate on `ProjectPatch` or services, not raw graph state.
- UI previews do not persistently mutate `ProjectDesign`.

Reject if:

- `Graph` is the project aggregate.
- `NodeEditorWidget` owns domain decisions.
- Project serialization walks UI scene items as the document truth.
- Domain services accept mutable UI graph pointers.

## Core Neutrality Boundary

Check:

- `ipcraft-core` has no Qt Widgets/UI includes.
- `ipcraft-core` has no NoC implementation package includes.
- Core schema contains no NoC-specific fields.
- Core topology graph is generic.
- Core diagnostics use stable refs, not UI object pointers.

Reject if core contains:

- `meshnoc`
- `vendor.meshnoc`
- `ipcraft.capability.noc`
- `mesh_router`
- `north`, `east`, `south`, `west` as NoC port semantics
- `router`/`endpoint` special cases outside generic topology kind handling
- package id comparisons for domain behavior

## Package Boundary

Check:

- `PackageRegistry` is injected into `ProjectSession` or application context.
- Multiple registry instances can exist.
- Package paths resolve relative to package root.
- Missing files produce deterministic diagnostics.
- Component/interface/connection rules are loaded from package capabilities.
- Ordinary packages do not require native plugins.

Reject if:

- A global mutable `ModuleRegistry` is used as the package source of truth.
- UI code hardcodes component compatibility.
- Package ids are special-cased in core/domain for ordinary behavior.
- Package command execution can read/write outside allowed roots without policy.

## Configuration Inspection Boundary

Check:

- Authored config, package defaults, resolved config, derived config, tool input config, and runtime/output config are separated.
- `ResolutionService` is read-only.
- Provenance includes value, type, layer, source ref, package/schema owner, default/explicit state, overrides, validation status, docs, unit, allowed range/enum, and consumers.
- Package parameter metadata renders without package-specific UI code.
- Semantic diff and layout diff are separable.

Reject if:

- Defaults are written into authored config without an explicit materialization patch.
- Resolved config mutates `ProjectDesign`.
- Layout-only changes alter resolved semantic config or tool input.
- UI needs package-specific code to show parameter descriptions, units, constraints, or enum labels.

## Topology Boundary

Check:

- Explicit graph topology supports arbitrary node/link counts, multi-links, directed/undirected links, non-grid layout, and attachments.
- Parametric mesh expands into generic `TopologyGraph`.
- AnyNet is modeled as `explicit_graph`.
- Topology semantic graph and view layout are separate.
- Tables and graph render the same topology IR.

Reject if:

- AnyNet is implemented as mesh with exceptions.
- Link waypoints are stored in topology link config.
- Topology provider changes semantic graph without returning a patch.
- Generator consumes layout coordinates.

## NoC Capability Boundary

Check:

- NoC schema and validation live in `ipcraft-capability-noc`.
- Payload IP, NIC/adapter, and NoC are normal components connected through semantic interfaces and attachment points.
- UI recommends adapters through services/rules.
- NoC-aware IP direct attachment is supported.

Reject if:

- Payload IP is forced to become an internal NoC endpoint module.
- package-specific NoC implementation or NoC capability fields are added to core.
- Core assumes NoC topology is mesh.
- UI hardcodes NoC component type strings.

## Example NoC Implementation Boundary

Check:

- the example NoC implementation package lives under `packages/vendor-meshnoc` or another ordinary package path.
- the example NoC implementation package declares components, interfaces, views, NoC capability requirement, tools, and examples.
- the example NoC implementation package tools consume `ipcraft.tool.input.v1`.
- Core and generic UI do not check package-specific NoC implementation package ids.

Reject if:

- a specific NoC package is treated as a built-in type.
- Core uses package-specific NoC schemas.
- Generator reads `.fpproj` directly.
- package-specific NoC implementation assumptions leak into generic topology.

## Tool Protocol Boundary

Check:

- Validators/generators consume `ipcraft.tool.input.v1`.
- Tool results use `ipcraft.tool.result.v1`.
- Artifacts paths are validated and confined.
- Diagnostics map to stable component/topology/config refs.
- Tool patches are suggestions validated by host.
- Tool input preview is inspectable and deterministic.

Reject if:

- Tool reads `.fpproj` as the assumed input.
- Tool consumes Qt graph export.
- Tool result can register arbitrary path artifacts.
- Host trusts tool patches without validation.

## Plugin Boundary

Check:

- Native plugin capabilities are advanced-only.
- Plugin receives `ProjectSnapshot`.
- Plugin returns `PluginResult` with diagnostics/artifacts/patches.
- Host validates plugin patches through `ProjectPatchCommand`.

Reject if:

- Plugin holds mutable `ProjectDesign`.
- Plugin depends on `NodeEditorWidget` internals.
- Plugin is required for ordinary component declaration.
- Plugin directly mutates layout or semantics.

## UI/UX Boundary

Check:

- Product wording and navigation center on creating, inspecting, validating, generating, and reviewing IP.
- The first surfaces include project overview, config inspector, block diagram, topology graph, interface table, diagnostics, and artifacts.
- View descriptors are semantic descriptors.
- Inspector edits config only; layout edits require layout section/view.
- Diagnostics select stable refs and highlight the relevant field/node/link/attachment.

Reject if:

- UI is framed primarily as a graph editor.
- View YAML is a drawing DSL.
- Inspector writes layout into config.
- Diagnostics target UI object pointers only.

## Test Visibility And Anti Fixture Coding

Check:

- Public tests explain contracts without exhausting hidden acceptance.
- Hidden tests are not present in implementation branch.
- Implementation does not reference hidden case ids, public golden filenames, fixture names, or exact sample dimensions in production code.
- Parsers validate schema generally instead of matching sample strings.
- Topology logic supports arbitrary graphs, not only public examples.
- Package discovery is manifest-driven, not known-package-driven.

Reject if:

- Production code contains public test fixture ids as behavior branches.
- Hidden test content was requested or copied.
- Tests were modified to fit implementation without spec update.
- Any implementation note says only "make tests pass".

## Acceptance Agent Report Template

Every phase report must include:

- Summary
- Public tests result
- Hidden tests result
- Architecture checks result
- Boundary violations
- Spec deviations
- UX/config-review impact
- Security/tool execution concerns
- Required fixes
- Final decision: `ACCEPTED`, `REJECTED`, or `CONDITIONALLY_ACCEPTED_WITH_REQUIRED_FIXES`
