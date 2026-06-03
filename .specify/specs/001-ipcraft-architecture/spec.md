# IpCraft Architecture Specification

## Summary

IpCraft will be refactored from a graph-centered editor into a general IP creation, configuration inspection, topology modeling, and tool-flow workbench. The new architecture centers on `ProjectDesign`, package-discoverable capabilities, topology IR, tool protocols, schema-driven configuration inspection, and patch-based mutation.

This is not a NoC-only specification chain. NoC remains a major product capability and a primary validation target, but NoC semantics are delivered through `ipcraft-capability-noc` and NoC implementation packages rather than through core schema or UI hardcode.

This is a hard cutover. The implementation must delete or replace old Graph-centric, NoC-specific, and `ipcraft.*` runtime assumptions instead of preserving compatibility.

## Product Users

### IP User

Creates projects, selects packages, instantiates IP, connects interfaces, configures parameters, inspects resolved configuration, validates designs, runs generators, reviews diagnostics, and collects artifacts.

### IP Developer

Builds packages by declaring components, interfaces, connection rules, topology providers, view descriptors, validators, generators, templates, examples, and optional native plugins. The default workflow uses YAML/JSON schemas and command-line tools, not Qt/C++ plugins.

## Product Goals

- Make `ProjectDesign` the project aggregate root and source of truth.
- Treat graph views as projections over semantic data.
- Support block diagrams, interface tables, arbitrary topology graphs, external endpoints, NIC/adapter components, imported vendor IP, blackbox Verilog packages, and domain capabilities such as NoC.
- Make configuration inspection, provenance, semantic diff, and tool input preview first-class.
- Allow third-party IP packages to be discoverable, validated, previewed, tested, and generated through public contracts.
- Keep NoC as a polished first-class capability implemented by `ipcraft-capability-noc`, not by core fields or package-id checks.
- Move any currently hardcoded NoC implementation into ordinary packages such as `packages/vendor-meshnoc`.
- Ensure validators/generators consume `ipcraft.tool.input.v1`, not `.fpproj`, Qt Graph, UI coordinates, or old NoC-specific command input.
- Enforce patch-based mutation for UI, tools, plugins, importers, templates, and wizards.
- Support multi-agent implementation and acceptance with hidden test isolation.

## Non-Goals

- No runtime compatibility with old project documents.
- No legacy `ipcraft.noc.project.v1` generator path.
- No Graph-centric aggregate.
- No package-specific special cases in core.
- No required Qt/C++ plugin for ordinary third-party IP.
- No view descriptor as arbitrary drawing language.
- No layout fields inside semantic config.
- No first implementation PR for UI rewrite or a specific NoC generator migration.

## User Stories

### Project Creation And Inspection

As an IP user, I can create or open a project and inspect packages, components, topologies, diagnostics, artifacts, authored config, resolved config, and tool input previews without manually editing anything.

As an IP integrator, I can tell whether each value is explicit, default, inherited, derived, generated, or overridden, and I can see provenance, documentation, constraints, unit, validation status, and consumers.

As a reviewer, I can diff two projects and separate semantic changes from layout-only changes.

### Package-Based IP Use

As an IP user, I can install or load a package and see its components, interfaces, connection rules, generators, validators, views, examples, templates, and diagnostics.

As an IP user, I can instantiate a blackbox Verilog component, connect it to another IP, inspect its ports and config, and generate a tool input projection.

### Connection Editing

As an IP user, I can connect component interfaces in a line-based block diagram or interface table. Compatibility is decided by `ConnectionCompatibilityService` using package-declared interface schemas and connection rules, not UI hardcode.

As an IP user, I receive adapter recommendations when a direct connection is invalid but a package-declared adapter can bridge the interfaces.

### AnyNet And Topology Editing

As a topology user, I can create an explicit arbitrary graph with routers, switches, adapters, bridges, endpoints, multi-links, directed or undirected links, attachments, and routing metadata.

As a topology user, I can inspect and edit graph-like systems that are not NoC, including vendor fabrics, arbitrary interconnects, and package-defined topologies.

As a NoC user, I can create a mesh from a template, switch to topology view, inspect nodes/links/attachments in graph and table form, add routers and links in explicit graph mode when supported by the package, edit link width/latency/VC/protocol, and see invalid links or attachments on the graph.

### NoC Endpoint Attachment

As a system integrator, I can connect CPU -> NIC -> NoC attachment slot. Payload IP remains a normal component, the NIC is a normal component, and the NoC topology owns attachment points.

As a NoC-aware IP developer, I can declare an endpoint interface that connects directly to NoC attachment points without a NIC.

### Tool Execution

As a generator author, I receive deterministic `ipcraft.tool.input.v1` projections for my target. I return `ipcraft.tool.result.v1` with diagnostics, artifacts, optional patch suggestions, metrics, and logs.

As a user, I can inspect the exact tool input before running a validator or generator and after a flow completes.

### Package Authoring

As an IP developer, I can run:

- `ipcraft package init`
- `ipcraft package check`
- `ipcraft package preview`
- `ipcraft package test`
- `ipcraft package run-generator`
- `ipcraft package pack`

The CLI validates package manifests, schemas, examples, tools, command inputs/outputs, package-local paths, and preview projections.

## Functional Requirements

### ProjectDesign

`ProjectDesign` must contain:

- `id`
- `name`
- `packages`
- `components`
- resolved interface instances
- semantic connections
- topologies
- constraints
- views
- diagnostics
- artifacts
- extensions

Project load/save must use `ipcraft.project.v1`, which is human-readable, diff-friendly, schema-versioned, independent of Qt, independent of generator input, independent of graph implementation, and extensible through package-owned extension blocks.

### ComponentInstance

Each component instance must contain:

- stable `id`
- component `type`
- `packageRef`
- authored `config`
- `identity`
- declared/resolved `interfaces`
- `metadata`
- package-owned `extensionData`

Component config must contain semantic authored values only. Layout, node size, waypoints, collapsed state, zoom, pan, and view-local UI state are forbidden in component config.

### InterfaceInstance

Each interface instance must contain:

- stable `id`
- `ownerComponentId`
- interface `type`
- `role`
- `direction`
- `protocol`
- `clockRef`
- `resetRef`
- `config`
- `metadata`

### Connection

Connections are semantic links between component/interface endpoints. They model interface-level relationships such as CPU -> NIC or NIC -> memory. They do not own topology attachment state.

- stable `id`
- `from` endpoint ref
- `to` endpoint ref
- `kind`
- semantic `config`
- `constraints`
- `metadata`

Visual edge routing and waypoints live in view layout, not connection config.

`kind: attachment` is forbidden in `connections`. Payload IP -> NIC is a normal semantic connection. NIC/IP endpoint -> topology attachment point is represented only by `TopologyGraph.attachments`, because the topology owns attachment points.

### TopologyGraph

Topology IR must support:

- authored parametric topology requests through `ipcraft.topology.parametric.v1`
- provider-expanded semantic graphs through `ipcraft.topology.graph.v1`
- explicit graph topology
- arbitrary node/link counts
- arbitrary endpoint attachments
- multi-links
- directed or undirected links
- routing metadata
- layout-independent generator projections

AnyNet is explicit graph topology, not a mesh special case.

### Views

Project view documents describe target-bound views and layout references. Package view descriptors describe reusable provider templates without project layout. Built-in view surfaces:

- `ProjectOverviewView`
- `BlockDiagramViewProvider`
- `TopologyGraphViewProvider`
- `InterfaceTableView`
- `ConfigInspector`
- `InspectorPanel`
- `DiagnosticsPanel`
- `FlowArtifactsPanel`

Views emit UI intents that become `ProjectPatch` operations through `DesignEditingService` or `PatchApplier`.

### Configuration Resolution

IpCraft must implement `ResolutionService`:

- `resolveProject(ProjectDesign, PackageRegistry)`
- `resolveComponentConfig(componentId)`
- `resolveInterfaceConfig(interfaceRef)`
- `resolveTopology(topologyId)`
- `resolveToolInput(target)`
- `explainValue(path)`
- `diffResolvedConfig(a, b)`
- `produceConfigProvenanceTree()`

Configuration layers:

- authored config
- package defaults
- resolved config
- derived config
- tool input config
- runtime/output config

Resolved config is read-only and must not mutate `ProjectDesign`.

### Package Capability

`ipcraft.package.v1` packages may provide:

- components
- interfaces
- connection rules
- topologies
- views
- layout providers
- validators
- generators
- importers
- exporters
- templates
- transforms
- optional native plugins

Package registry requirements:

- no global mutable singleton
- injected into `ProjectSession` or application context
- deterministic load diagnostics
- package-local path resolution
- package check validates all referenced files
- future multi-version package support
- local tool execution sandboxing policy

### Tool Protocol

Generator and validator tools consume `ipcraft.tool.input.v1` and return `ipcraft.tool.result.v1`.

Tool input contains project metadata, package metadata, target, deterministic `projection`, output directory, and environment. Resolved components, resolved interfaces, resolved connections, relevant topologies, constraints, package extension data, and provenance live only under `projection`.

Tool result contains status, diagnostics, artifacts, optional patch suggestions, optional metrics, and optional logs.

Tools may return patch suggestions. Host validates and applies patches only through normal command/transaction flow.

### Plugin Boundary

Native plugins are optional. They may provide custom views, wizards, analyzers, layout providers, or complex import/export interactions.

Plugins receive `ProjectSnapshot` and return `PluginResult` containing diagnostics, artifacts, and patch suggestions. They do not receive mutable `ProjectDesign`, mutable `Graph`, or `NodeEditorWidget` internals.

### Command And Transaction

The command model operates on `ProjectPatch`, not raw graph operations. Required commands include:

- `ProjectPatchCommand`
- transaction groups
- undo/redo
- validation before apply
- semantic data patches
- layout data patches

Transient UI previews do not mutate `ProjectDesign`.

## Non-Functional Requirements

- Deterministic serialization of public documents.
- Deterministic projection generation.
- Stable ids and stable refs for diagnostics and artifacts.
- Human-readable and diff-friendly project files.
- Public examples sufficient for third-party package authors.
- Architecture tests for dependency direction and forbidden patterns.
- Hidden acceptance tests derived from spec, not implementation.
- Reviewable implementation notes per PR.
- No public contract behavior hidden only in internal C++ tests.

## Success Criteria

- A minimal UART project roundtrips as `ipcraft.project.v1`.
- A CPU -> NIC -> NoC project validates through generic package and NoC capability paths.
- A mesh NoC project loads a package-specific NoC implementation as an ordinary package; no core special cases exist.
- An AnyNet explicit graph project roundtrips and validates arbitrary topology data.
- A blackbox Verilog package loads, exposes ports/interfaces, and projects tool input.
- Layout edits do not change semantic or tool input projections.
- Package defaults appear in resolved config with provenance but are not written to authored config unless materialized by patch.
- Generator receives `ipcraft.tool.input.v1`, not `.fpproj` or Qt graph.
- Invalid patches, invalid package manifests, invalid connections, invalid attachments, and invalid artifact paths are rejected with diagnostics.
- Acceptance Agent can run public tests, hidden tests, architecture scans, forbidden pattern scans, and manual checklist review.

## Anti-Requirements

The implementation must reject:

- old document compatibility as a runtime requirement
- Graph as source of truth
- UI-domain coupling
- global singleton package/module registry
- NoC implementation package hardcoded into core
- NoC topology limited to mesh
- endpoint as hardcoded NoC internal module
- generator reading `.fpproj`
- layout in semantic config
- view descriptors as Qt drawing scripts
- plugin direct mutation of project state
- interface compatibility rules in UI
- package authoring that requires native plugin for basic IP

## Required Examples

The spec package and future repo examples must include:

- minimal UART project
- CPU -> NIC -> NoC block diagram project
- mesh NoC package project
- AnyNet explicit topology project
- blackbox Verilog component project

Examples must demonstrate package version locks, generic components, generic topologies, NoC extension data under `ipcraft.capability.noc`, implementation-owned NoC extension data such as `vendor.meshnoc`, layout under view/layout documents, semantic connections, and visual waypoints separated from semantic connections.
