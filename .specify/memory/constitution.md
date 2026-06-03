# IpCraft Architecture Constitution

## Status

This constitution governs the architecture refactor that turns IpCraft from a graph-centered editor into a general IP creation, configuration inspection, topology modeling, and tool-flow workbench. NoC remains a primary product capability and a major validation target, but it is not the specification chain, aggregate root, or core schema owner. The refactor is intentionally incompatible with the old NoC-specific document schema, old Graph-centric aggregate, old `ModuleRegistry` singleton, old view XML/YAML assumptions, and old `ipcraft.noc.project.v1` generator contract.

All implementation plans, code reviews, public tests, hidden acceptance tests, and final acceptance decisions must treat this file as the long-lived architecture source of truth.

## Core Principles

### 1. ProjectDesign Is The Aggregate Root

`ProjectDesign` is the source of truth for project semantics. It owns component instances, interface instances, semantic connections, topology IR, constraints, package references, extension blocks, diagnostics, artifacts, views, and layout documents.

`Graph` is only a projection, editing canvas, or view-local interaction model. A graph view may render and edit semantic data through `ProjectPatch`, but the graph object must never become the domain aggregate.

Forbidden:

- `Graph` as source of truth.
- `NodeEditorWidget` owning domain logic.
- Project save/load implemented as "serialize the canvas".
- Generators receiving Qt graph export as their input contract.

### 2. Core Is Domain-Neutral

`ipcraft-core` must not hard-code NoC package details, mesh, router endpoint behavior, north/east/south/west port semantics, or any other package-specific semantics. Core models general IP projects, package extension blocks, topology graphs, diagnostics, artifacts, configuration provenance, and patches.

NoC is a package-discoverable domain capability and one of IpCraft's primary product capabilities. Individual NoC implementations are packages or package families under that capability. Core must never branch on package id, component type, or module type to find NoC behavior.

IpCraft must make NoC creation, inspection, endpoint attachment, topology validation, and generator projection polished and testable through `ipcraft-capability-noc` and NoC packages, while keeping those semantics outside core.

Forbidden:

- Core field names such as `meshRows`, `routerX`, `endpointCount`, or package-specific config fields.
- package-specific behavior checks such as `if packageId == "vendor.meshnoc"` or `if packageId == "noc"` in core/package/domain/UI shared layers.
- `mesh_router`, `xp`, `endpoint`, or router direction string checks in core UI.

### 3. Semantic Data And Visual Layout Are Separate

Component config, identity, layout, and extension data are separate records. Layout data includes node coordinates, sizes, waypoints, collapse state, pinned state, zoom, pan, and view-local presentation settings. Layout never belongs in component config, topology link config, or tool input semantic config.

Generator and validator tools must consume deterministic `ipcraft.tool.input.v1` projections. They do not consume `.fpproj`, Qt objects, graph canvas coordinates, view layout, or UI state.

### 4. Public Contracts Come First

Public schemas, protocol examples, CLI behavior, diagnostics, and architecture boundaries are the product surface. Internal C++ APIs must implement those contracts rather than redefine them.

Required public contracts:

- `ipcraft.project.v1`
- `ipcraft.package.v1`
- `ipcraft.component.v1`
- `ipcraft.interface.v1`
- `ipcraft.connection_rules.v1`
- `ipcraft.topology.graph.v1`
- `ipcraft.topology.parametric.v1`
- `ipcraft.view.v1`
- `ipcraft.view.descriptor.v1`
- `ipcraft.tool.input.v1`
- `ipcraft.tool.result.v1`
- `ipcraft.diagnostic.v1`
- `ipcraft.artifact.v1`
- `ipcraft.patch.v1`

Each contract requires parser, writer, validation, roundtrip, negative, and golden tests before implementation is accepted.

### 5. Packages Are The Stable Third-Party Boundary

Ordinary third-party IP integration uses declarative package schema plus validator/generator/importer/exporter command tools. Package authors must not be required to write Qt/C++ plugins for basic IP declaration, validation, generation, views, or examples.

Native plugins are optional advanced integrations for custom view providers, wizards, analyzers, custom layout providers, and complex import/export interactions. Plugins receive snapshots and return results; they never hold mutable `ProjectDesign`, `Graph`, or `NodeEditorWidget` references.

Package registries must be injected into `ProjectSession` or application context. A global mutable registry is forbidden.

### 6. Mutation Uses ProjectPatch

All durable changes flow through `ProjectPatch`, `PatchApplier`, and transaction/command boundaries. UI intents, package tools, native plugins, importers, wizards, transforms, and templates return patches or patch suggestions. The host validates every patch before applying it.

Direct mutation of `ProjectDesign` is limited to construction inside core tests, parsers, and transaction internals. UI code and external tools must not bypass `DesignEditingService` or `PatchApplier`.

### 7. Configuration Inspection Is A Product Feature

IpCraft is an IP configuration authoring, inspection, audit, comparison, and generation tool. Users must be able to inspect source config, resolved config, derived config, tool input config, runtime output config, provenance, diagnostics, and semantic diffs without manual editing or generation.

`ResolutionService` must provide read-only resolved config with provenance. It must answer value, type, source layer, source reference, package/schema owner, default/explicit state, override chain, validation status, documentation, unit, allowed values, and consumers.

Resolved config must not mutate `ProjectDesign`.

### 8. AnyNet And Arbitrary Topology Are First-Class

IpCraft must support explicit arbitrary topology graphs as first-class topology IR, not as mesh exceptions. AnyNet means arbitrary routers, switches, bridges, adapters, endpoints, multi-links, directed/undirected links, non-grid layout, attachment points, optional routing metadata, and graph/table synchronized inspection.

Parametric mesh, ring, torus, tree, fat tree, and package-defined topologies are providers that expand into semantic topology graphs. Explicit graph topology is not a degraded mode.

### 9. NoC Endpoint Modeling Uses Payload IP, NIC/Adapter, And Attachment Points

Payload IP remains a normal component. NICs/adapters are normal components. NoC topologies expose semantic attachment points. A common system path is payload IP -> NIC/adapter -> NoC attachment slot.

NoC-aware payload IP may directly implement a NoC endpoint interface. UI may recommend adapters through package connection rules, but must not force endpoint modules into core or special-case any NoC implementation package.

### 10. Views Are Descriptors, Not Drawing DSLs

View documents describe view kind, provider id, source refs, semantic templates, port grouping, labels, badges, property groups, layout preferences, interaction affordances, diagnostics overlays, and icon references.

View documents must not encode arbitrary Qt painting commands, business logic, topology generation logic, validation logic, generator logic, or raw drawing primitives as the primary mechanism.

## Multi-Agent Governance

The project uses isolated agent roles. One agent must not own specification, implementation, test authoring, and final acceptance at the same time.

### Spec Agent

Responsibilities:

- Creates and maintains constitution, spec, plan, tasks, contracts, and review checklist.
- Reads project code and product goals.
- Does not implement production code.
- Does not author hidden acceptance expected outputs.

### Implementation Agent

Responsibilities:

- Implements code according to public spec and task files.
- Reads source code, public spec, public contract examples, public smoke tests, and public schema examples.

Forbidden:

- Reading hidden acceptance tests, hidden expected outputs, private acceptance harness files, or Acceptance Agent private rules.
- Modifying tests to fit implementation.
- Hard-coding test fixture ids, hidden case ids, golden filenames, sample topology sizes, known package ids, or public example shapes.
- Writing "make tests pass" as implementation rationale.

Each PR must include Implementation Notes covering spec requirements, public contracts changed, preserved boundaries, temporary adapters, and adapter removal conditions.

### Test/QA Agent

Responsibilities:

- Derives public contract tests and hidden acceptance tests from the spec.
- Keeps hidden tests outside Implementation Agent accessible repo paths.
- Designs property, metamorphic, golden, negative, architecture, and UX/data-flow tests.

Hidden tests must be black-box contract tests and must not depend on implementation details.

### Acceptance Agent

Responsibilities:

- Provides final acceptance with veto power.
- Reads the full spec independently from the implementation notes.
- Runs public tests, hidden acceptance tests, architecture checks, source scans, and manual semantic review.
- Checks `review-checklist.md` item by item.

Allowed final decisions:

- `ACCEPTED`
- `REJECTED`
- `CONDITIONALLY_ACCEPTED_WITH_REQUIRED_FIXES`

Implementation Agent self-test success is not final acceptance.

## Test Visibility Rules

### Public Contract Tests

Visible to Implementation Agent. They explain contracts but do not exhaust acceptance. They live in the repo and cover schema roundtrip, package load, minimal generator protocol, topology examples, and smoke flows.

### Hidden Acceptance Tests

Invisible to Implementation Agent. They live outside implementation branches and cover arbitrary topology, AnyNet, payload IP -> NIC -> NoC attachment, invalid diagnostics, invalid patch rejection, tool protocol isolation, layout/semantic separation, capability discovery, forbidden patterns, specific NoC package ids not hardcoded, and NoC not hardcoded into core.

### Architecture Boundary Tests

May be partly public and partly hidden. They verify dependency direction, include/import bans, no global mutable registry, no hardcoded package ids, no generator `.fpproj` reads, no direct plugin mutation, and no layout in component config.

## Required Review Gates

Every implementation phase must pass:

1. Contract tests written before implementation.
2. Public parser/writer/roundtrip tests for each public schema touched.
3. Negative tests for boundary violations.
4. Architecture scan for forbidden dependency and string patterns.
5. Implementation Notes review.
6. Acceptance Agent report.

## Forbidden Patterns

- Graph is source of truth.
- `NodeEditorWidget` owns domain logic.
- UI checks module type strings like `mesh_router`.
- Core checks package ids such as `noc`, `vendor.meshnoc`, or any known NoC implementation id.
- Component config contains `x`, `y`, `collapsed`, `node_width`, `node_height`, `waypoints`, `zoom`, or `pan`.
- Generator reads `.fpproj` directly.
- Generator receives Qt-specific graph export.
- Package author must write C++ plugin for basic IP.
- Native plugin mutates `ProjectDesign` directly.
- Registry is a global mutable singleton.
- View YAML draws primitives as the primary mechanism.
- NoC topology equals mesh only.
- Endpoint equals hardcoded NoC internal module.
- A specific NoC implementation package is treated as a built-in core type.
- Using `ipcraft.noc.project.v1` as the new generator contract.
- Project document mixes file DTO, working model, and projection buffer.
- Interface compatibility rules live in UI code.
- Layout provider changes semantic graph without patch.

## Amendment Process

Changes to this constitution require:

1. A spec change that explains the requirement and affected boundaries.
2. Updates to contracts, review checklist, and test strategy.
3. Acceptance Agent approval.
4. New architecture tests when a boundary changes.
