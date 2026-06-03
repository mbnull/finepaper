# IpCraft UX Specification

IpCraft UX must center on making IP, inspecting IP configuration, validating integrations, and generating artifacts. It must not present the product primarily as a generic graph editor.

## Primary Surfaces

### Project Overview

Purpose:

- Summarize project state.
- Provide entry points to components, packages, topologies, flows, diagnostics, artifacts, and config inspection.

Required content:

- Project name/id.
- Package list with versions and diagnostics.
- Component count and categories.
- Topology list with kind and owner component.
- Flow status.
- Diagnostic summary.
- Artifact summary.

Required interactions:

- Open component in Inspector.
- Open topology in graph/table view.
- Open package in Package Library Panel.
- Open diagnostics.
- Open tool input preview for selected flow target.

### Block Diagram View

Purpose:

- Edit and inspect semantic component-to-component interface connections.

Required behavior:

- Components are rendered as IP blocks.
- Ports/interfaces are grouped from package schemas.
- Connections are semantic `Connection` records.
- Visual edges and waypoints are layout records.
- Add/connect actions call `ConnectionCompatibilityService`.
- Adapter recommendations are shown through package rules.
- Clock/reset auto-connect may be offered when rules allow.
- Diagnostics overlay highlights component/interface/connection refs.

Forbidden:

- UI hardcoded AXI/NoC/UART or package-specific connection rules.
- Graph object as persistent project aggregate.

### Topology Graph View

Purpose:

- Edit and inspect arbitrary topology graphs, including AnyNet and NoC topologies.

Required behavior:

- Render topology nodes, links, attachments, and routing metadata from `TopologyGraph`.
- Support arbitrary graph editing.
- Support routers, switches, bridges, adapters, endpoints, and custom nodes.
- Support semantic links with port-aware connection.
- Support manual layout, auto layout, pinned nodes, edge waypoints, and edge inspector.
- Support node table, link table, attachment table, and optional routing table synchronized with graph.
- Support diagnostics overlay.
- Support path highlight between endpoints.
- Support multi-links and directed/undirected links.
- Support non-grid layout.

Mutation rules:

- Add node/link/attachment emits semantic patch.
- Move node/set waypoint emits layout patch.
- Layout provider returns layout patch; it does not mutate topology.

### Interface Table View

Purpose:

- Inspect and edit interface-level connections without using a graph canvas.

Required columns:

- component
- interface
- interface type
- role
- direction
- protocol
- clock
- reset
- connected peer
- diagnostic status

Required behavior:

- Table-based connection editing through `DesignEditingService`.
- Compatibility checks through `ConnectionCompatibilityService`.
- Filter by package, component, protocol, connected/unconnected, diagnostic severity.

### Config Inspector / Configuration Browser

Config Inspector is a core product surface, not a debugging panel.

Required views:

1. Source View
2. Resolved View
3. Component Config View
4. Topology Config View
5. Tool Input Preview
6. Diff View
7. Diagnostics-linked View

#### Source View

Shows authored project document structure read-only by default. It can show YAML/JSON and highlights package-owned extension blocks.

#### Resolved View

Shows defaults and derived values. It distinguishes:

- explicit
- default
- inherited
- derived
- generated
- invalid

Each field must show provenance on selection.

#### Component Config View

Schema-driven form/table generated from component schema metadata.

Required metadata rendering:

- label
- description
- unit
- documentation link
- examples
- default explanation
- visibility
- advanced/basic grouping
- read-only/derived marker
- constraints
- enum labels
- warning thresholds
- deprecation message
- replacement suggestion
- generator relevance
- validation relevance

Editing goes through `ProjectPatch`.

#### Topology Config View

For NoC/AnyNet:

- nodes table
- links table
- attachments table
- routing table when present
- synchronized visual topology graph

It must show payload IP -> NIC -> NoC attachment mapping for NoC audits.

#### Tool Input Preview

Shows exact `ipcraft.tool.input.v1` generated for selected validator/generator. It is read-only and available before execution and after execution.

Uses:

- package generator debugging
- review of resolved config
- reproducibility
- user trust in generated artifacts

#### Diff View

Compares two `ProjectDesign` documents or two resolved configs.

Required filters:

- semantic config changes
- topology changes
- package version changes
- layout-only changes
- generated artifact changes
- package/component/topology/connection/extension/artifact scope

#### Diagnostics-linked View

Selecting a diagnostic highlights:

- config field
- component
- interface
- connection
- topology node
- topology link
- attachment
- artifact

Diagnostics must target stable refs, not UI pointers.

### InspectorPanel

Purpose:

- Show and edit selected component, connection, topology node/link, attachment, view layout object, artifact, or diagnostic.

Rules:

- Component selection edits component config only.
- Layout selection edits layout only.
- Topology link selection edits semantic link config only unless layout sub-section is selected.
- All edits become patches.

### PackageLibraryPanel

Purpose:

- Discover package capabilities and examples.

Required content:

- package id/name/version
- components
- interfaces
- connection rules
- topologies
- view descriptors
- generators
- validators
- templates
- examples
- diagnostics

### DiagnosticsPanel

Purpose:

- Show structured diagnostics from core, packages, validators, generators, plugins, and importers.

Required behavior:

- Filter by severity, source, package, target type.
- Navigate to stable target refs.
- Show suggested patches.
- Let user apply suggested patch only through host validation.

### FlowArtifactsPanel

Purpose:

- Run validation/generation/export flows and inspect outputs.

Required behavior:

- Validate.
- Generate.
- Export.
- Show logs.
- Show artifacts.
- Show diagnostics.
- Show tool input preview before execution.
- Show tool result after execution.

## NoC UX Requirements

The NoC user experience must support both template-generated and explicit graph workflows.

Required flows:

- Create mesh from template.
- Switch mesh to topology view.
- Attach external IP through NIC.
- Directly attach NoC-aware IP.
- Add router/link in explicit graph mode.
- Edit link width, latency, VC, and protocol.
- Use nodes, links, attachments, and routing tables.
- Highlight path between endpoints.
- Show invalid attachment/link directly on graph and table.
- Audit payload IP -> NIC -> NoC attachment mapping.

NoC-specific UX lives in `ipcraft-capability-noc` view integration and package descriptors. Generic UI surfaces remain package/capability-driven.

## AnyNet UX Requirements

AnyNet is not mesh mode. It must support:

- arbitrary node placement
- arbitrary links
- multi-links
- non-grid layout
- directed/undirected links
- explicit endpoint attachments
- topology inspection without manual editing
- table and graph synchronized selection
- generator projection preview

## View Descriptor Rules

Allowed descriptor content:

- view kind
- provider id
- source ref
- node template
- edge template
- port grouping
- labels
- badges
- property groups
- layout algorithm preference
- interaction affordances
- diagnostics overlay options
- icon/SVG references

Forbidden descriptor content:

- raw drawing primitives as main mechanism
- arbitrary Qt painting commands
- business logic
- topology generation logic
- generator logic
- validation logic

## Inspection-First Workflows

### Open And Inspect

1. User opens project.
2. Project Overview shows packages/components/topologies/diagnostics/artifacts.
3. Config Inspector shows authored and resolved config.
4. No generation is required.

### Template-Generated Project Inspection

1. User creates project from template.
2. Template returns `ProjectPatch`.
3. User inspects generated components/topology/config.
4. User accepts, modifies, or discards through transaction history.

### NoC Endpoint Audit

1. User opens NoC topology.
2. UI shows payload IP -> NIC -> NoC attachment mapping.
3. User selects attachment and sees derived generator mapping and provenance.

### Generator Debug

1. User selects Generate RTL.
2. IpCraft shows `ipcraft.tool.input.v1` preview.
3. User runs generator.
4. Artifacts and diagnostics link back to config refs.

### Configuration Review

1. User compares two revisions.
2. UI separates semantic config, topology, package version, layout-only, and generated artifact changes.
3. User can export or share review results.

## Accessibility And Robustness

- Important actions must be available from both graph and table surfaces.
- Text labels must fit in panels and controls across supported view sizes.
- Diagnostics must have text equivalents for graph overlays.
- Keyboard navigation should work in tables and inspector.
- Layout changes must be undoable independently from semantic changes.
