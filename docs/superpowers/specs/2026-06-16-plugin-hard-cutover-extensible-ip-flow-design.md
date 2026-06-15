# Plugin Hard Cutover Extensible IP Flow Design

## Problem Statement

Finepaper currently has package descriptors, plugin host primitives, V1 project
schemas, flow execution, and three first-party NoC anchor packages. That is not
enough to call the architecture complete.

The present system still proves the wrong thing: it proves that
`finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc` can be made to
work through a mixture of package data, Qt adapters, and historical Graph
paths. It does not prove that a new NoC IP can be onboarded without editing
product Qt/C++ code. It also does not prove that all package-declared
capabilities are visible, configurable, callable, and diagnosable.

The hard cutover target is to remove the hidden dependency on concrete IP
knowledge in the product runtime. Adding a new NoC IP package must be a package
authoring task, not a Qt application programming task.

## Goals

- Add an arbitrary `vendor-meshnoc` NoC package fixture without changing product
  Qt/C++ files.
- Make package-declared capabilities visible, configurable, callable, and
  diagnosable through registered plugin interaction points.
- Move normal editing, save, validate, and generate flows to
  `ProjectDesign` plus `DesignEditingService`.
- Stop using `Graph`, `Module`, or `Connection` as normal durable state or
  generation input.
- Move NoC-specific behavior into a static internal `NoCPlugin` that handles
  `noc.v1` by contract, not by concrete package or module names.
- Keep dynamic plugin loading out of scope. Static plugin registration is
  acceptable for this cutover.
- Replace the `go-with-debt` architecture gate with a hard pass gate that fails
  when product runtime paths still depend on concrete IP behavior or Graph
  source-of-truth adapters.

## Non-Goals

- No dynamic plugin ABI or external binary plugin loading in this phase.
- No generic visual scripting system for arbitrary UI behavior from packages.
- No second "capability view" or intermediate view-model layer.
- No package-provided native Qt code requirement for ordinary IP onboarding.
- No continued acceptance of Graph-centered normal paths as architecture debt.

## Hard Acceptance Criteria

1. `vendor-meshnoc` is added as a NoC IP package using only package source,
   descriptors, generator scripts, view descriptors, fixtures, and tests. No
   product Qt/C++ source file changes are needed for the package to appear,
   edit, validate, generate, and collect artifacts.
2. Product Qt/C++ runtime code does not branch on concrete package ids such as
   `finepaper.noc`, `finepaper.ravenoc`, `finepaper.opennoc`, or
   `vendor.meshnoc`.
3. Product Qt/C++ runtime code does not branch on concrete module ids such as
   `XP`, `Endpoint`, `RaveTile`, or `OpenNoCXP`.
4. `PackagePlugin` does not include, link against, or call `NoCPlugin`.
5. `NoCPlugin` does not include package-specific branches. It only handles
   `noc.v1` semantic descriptors.
6. `MainWindow` renders registered contributions and does not contain package,
   capability, topology, connection, generator, or artifact business logic.
7. Normal save, validate, and generate paths consume `ProjectDesign` state and
   package descriptors. They do not require `ProjectGenerationRequest::graph`
   or `GraphProjectSerializer::toProject`.
8. `GraphProjectSerializer` is removed from normal runtime paths. If it remains
   temporarily, it is restricted to explicit migration/import tests and must be
   blocked by runtime scan gates outside those allowlisted paths.
9. Every package-declared required capability has a registered handler or
   produces a blocking package activation diagnostic.
10. Every package-declared optional or unknown capability is visible in a
    package inspector with its raw descriptor, handler status, and diagnostics.
    It must not be silently dropped.
11. Every package-declared parameter, document input, tool flow, artifact,
    diagnostic rule, and capability section is either rendered through a
    registered contribution or surfaced as unsupported in the inspector.
12. Architecture completion reports must end with a hard pass or blocked
    verdict. `go-with-debt` is not an acceptable completion verdict for this
    cutover.

## Architecture Overview

The architecture is a static-plugin, registry-driven cutover:

```text
App startup
  -> StaticPluginCatalog creates internal plugin factories
  -> PluginHost activates plugins with a minimal PluginContext
  -> plugins register services, extension points, capability handlers, and contributions

PackagePlugin
  -> discovers and validates package descriptors
  -> publishes PackageCatalog service and package diagnostics
  -> does not interpret noc.v1

NoCPlugin
  -> registers noc.v1 handlers and contributions
  -> reads NoC semantic descriptors from packages
  -> registers editor tools, inspector sections, rule providers, and flow projectors
  -> never branches on concrete package ids or module ids

MainWindow
  -> renders registered contributions
  -> forwards user actions to registered commands/services
  -> owns no IP-specific behavior

User operation
  -> UI contribution emits a command intent
  -> handler calls DesignEditingService
  -> DesignEditingService mutates ProjectDesign through command/patch transactions
  -> UI refreshes from ProjectDesign projection
```

The key separation is between capability discovery and functionality. The
capability registry answers "which plugin handles this package-declared
capability and is it fully covered?" It is not a universal dispatcher for every
business operation. Actual behavior is registered through small extension
points.

## Runtime Source Of Truth

`ProjectDesign` is the runtime source of truth.

`ProjectDocument` is the persistence DTO used at file read/write boundaries.
Readers convert `ProjectDocument` into `ProjectDesign`. Writers convert
`ProjectDesign` back into `ProjectDocument`. Normal editing, validation, and
generation do not mutate or derive state from `ProjectDocument` directly after
load.

`Graph`, `Module`, and `Connection` are not durable domain models. If a QWidget
continues using graph-shaped drawing data during an intermediate migration, that
data is private render state. It cannot be the input to save, validation,
generation, connection decisions, package state, undo/redo, or tool flow
projection.

## Minimal Plugin Kernel

The kernel plugin contract stays small:

```text
PluginDescriptor
  id
  version
  dependencies

IAppPlugin
  descriptor()
  activate(PluginContext&)
```

The kernel contract does not mention NoC, packages, tools, panels, topology,
artifacts, or project editing APIs. Those are registered through services and
extension points.

`PluginContext` must not become a large service locator with one accessor per
feature. It exposes only stable registries:

```text
PluginContext
  services()
  extensionPoints()
  capabilities()
  diagnostics()
```

Core services such as project, package, tool pipeline, and design editing are
published through typed service keys in `ServiceRegistry`.

## Static Plugin Catalog

Dynamic loading is out of scope. The app uses a compile-time catalog at the
composition root:

```text
StaticPluginCatalog
  ProjectPlugin
  PackagePlugin
  ToolPipelinePlugin
  NoCPlugin
```

This is the only product runtime location allowed to name concrete internal
plugin classes. Feature code outside the composition root cannot include or
branch on specific internal plugin classes.

## Service Registry

`ServiceRegistry` stores small, typed services behind stable keys. Plugins use
it to publish and consume services without direct include dependencies between
plugins.

Required initial services:

- `ProjectService`: read/write project files and expose the current
  `ProjectDesign`.
- `DesignEditingService`: apply domain commands and project patches with
  transactions and undo/redo.
- `PackageCatalogService`: expose loaded package descriptors, activation
  status, and diagnostics.
- `ToolPipelineService`: execute package-declared flows through registered
  flow providers.
- `DiagnosticService`: collect project, package, capability, and tool
  diagnostics.

The registry blocks duplicate service keys unless a service explicitly declares
replacement policy. Missing required services produce plugin activation
diagnostics.

## Extension Point Registry

`ExtensionPointRegistry` is where behavior is registered. It prevents the
capability registry from becoming a hardcoded master switch.

Initial extension points:

- `ui.action`: commands shown in menus, toolbars, context menus, or command
  palettes.
- `ui.panel`: dockable or embedded panels created by plugin-owned factories.
- `ui.inspectorSection`: sections in package, project, instance, connection,
  tool, and artifact inspectors.
- `editor.tool`: interactive editor tools such as add node, connect, route, or
  apply topology.
- `design.command`: domain command handlers that call `DesignEditingService`.
- `connection.ruleProvider`: package-driven connection checks.
- `tool.flowInputProjector`: projection from `ProjectDesign` and package data
  into flow input manifests.
- `artifact.presenter`: artifact summary and navigation providers.
- `package.coverageReporter`: coverage reporting for package-declared features.

Extension point ids are strings with stable schemas. Adding a new extension
point is a platform change. Adding a new NoC behavior under an existing
extension point is a NoC plugin or package descriptor change.

## Capability Registry

`CapabilityRegistry` records capability handlers by namespace and version:

```text
capability id: noc.v1
handler plugin: finepaper.noc-plugin
required extension points:
  ui.inspectorSection
  editor.tool
  connection.ruleProvider
  tool.flowInputProjector
  artifact.presenter
coverage reporter: registered
```

Its responsibilities:

- Match package-declared capabilities to registered handlers.
- Distinguish required and optional capabilities.
- Produce activation diagnostics for missing required handlers.
- Produce inspector-visible unsupported records for optional or unknown
  capabilities.
- Report which declared package features are rendered, configurable, callable,
  or unsupported.

It does not directly run topology editing, validation, generation, or UI
rendering. Those operations go through extension points and services.

## Package Plugin Boundary

`PackagePlugin` owns generic package discovery and package descriptor
validation. It publishes a package catalog containing:

- package identity and version
- declared capabilities and required flags
- modules, interfaces, config schema, documents, flows, artifacts, diagnostics
- raw descriptor sections not handled by generic package parsing
- package activation and coverage diagnostics

`PackagePlugin` cannot interpret `noc.v1`. It cannot include or call
`NoCPlugin`. It cannot create NoC-specific UI, topology, connection, flow, or
artifact behavior.

## NoC Plugin Boundary

`NoCPlugin` handles `noc.v1`.

It may understand NoC semantic roles:

- router
- endpoint
- agent
- interface role
- interface protocol
- topology side
- coordinate model
- link class
- attachment slot
- topology preset

It may not understand concrete package ids, module ids, generator paths, output
filenames, or vendor-specific names. Those come from package descriptors.

`NoCPlugin` registers:

- package inspector sections for `noc.v1`
- instance parameter and topology controls driven by package config schema and
  NoC descriptors
- editor tools for NoC topology creation and connection editing
- package-declared connection rule providers
- flow input projectors for validate/generate flows
- artifact presenters driven by package artifact declarations
- coverage reporters for NoC-declared package features

If a NoC package needs behavior outside `noc.v1`, it must declare a new
versioned capability or optional package section. The product runtime cannot add
an ad hoc package-specific branch.

## Package Declared Feature Coverage

Package activation produces a coverage report. The report is shown in the
package inspector and is available to tests.

Coverage categories:

- `handled`: a registered handler or contribution owns this feature.
- `visible`: the feature is not executable by the app but is shown in the
  inspector with raw descriptor data.
- `unsupported`: the feature is declared but no handler exists.
- `blocking`: a required feature or required capability is unsupported.
- `invalid`: descriptor schema validation failed.

Coverage must include:

- capabilities
- modules
- interfaces
- parameters and config groups
- documents and file inputs
- topology descriptors
- connection rules
- flows
- artifacts
- diagnostics declarations
- native or metadata sections

The package loader cannot drop unknown descriptor data silently. Unknown data is
preserved in the catalog and surfaced through inspector coverage.

## Design Editing Flow

`DesignEditingService` is the only normal mutation entry point.

Accepted mutation inputs:

- platform domain commands registered through `design.command`
- capability domain commands registered by plugins
- validated `ProjectPatch` objects

The service owns:

- transaction grouping
- undo/redo
- command diagnostics
- mutation validation
- change notification
- conversion from commands/patches to `ProjectDesign` mutations

Normal UI editing flow:

```text
User event
  -> UI contribution or editor tool
  -> design command intent
  -> DesignEditingService
  -> ProjectDesign mutation transaction
  -> subscribers refresh rendered state
```

Node editor code cannot create durable Graph commands. Existing command manager
behavior must move behind `DesignEditingService` or be replaced by
DesignEditingService transactions.

## Save, Validate, And Generate Flow

Save:

```text
ProjectDesign
  -> ProjectDocument writer DTO
  -> .fpproj
```

Validate:

```text
ProjectDesign + PackageCatalog
  -> static project validation
  -> capability rule providers
  -> package validate flow input projector
  -> ToolPipelineService
  -> diagnostics
```

Generate:

```text
ProjectDesign + PackageCatalog
  -> package generate flow input projector
  -> ToolPipelineService
  -> FlowRunner
  -> artifact collection
  -> artifact presenters
```

No normal validate or generate path uses `GraphProjectSerializer::toProject`.
`ProjectGenerationRequest::graph` must be deleted or replaced by a request that
contains `ProjectDesign` and package catalog references.

## UI Contribution Rules

`MainWindow` owns shell layout, not business behavior.

It can:

- host dock areas and menus
- render registered panels and actions
- route command invocations to registered command services
- display diagnostics and inspector content

It cannot:

- branch on package ids or module ids
- inspect `noc.v1` payloads
- create package-specific topology controls
- call package generators or validators directly
- parse package capability JSON directly

IP packages cannot inject arbitrary native UI behavior. They provide
descriptors. Static internal plugins provide QWidget factories and handlers.

## Vendor MeshNoC Acceptance Package

The cutover fixture is `vendor-meshnoc`.

It must be different from the existing three anchors:

- different package id
- different module ids
- different artifact names
- different generator wrapper script
- different topology preset names
- at least one package-declared optional capability or metadata section that is
  visible but not handled as executable behavior

Required workflow:

1. Package discovery shows the package in the catalog.
2. Package inspector shows all declared capabilities and feature coverage.
3. User creates at least two instances.
4. User edits package parameters through schema-driven controls.
5. User applies a NoC topology preset through `NoCPlugin` tools.
6. User creates valid and invalid NoC connections through package-declared
   semantic descriptors and rules.
7. Project save/open roundtrips through `ProjectDesign` and `ProjectDocument`.
8. Default validation runs package-declared validate flows and surfaces DRC
   diagnostics.
9. Generation runs package-declared generate flows.
10. Artifacts are collected and presented using artifact declarations.

The test must fail if adding `vendor-meshnoc` requires modifying product Qt/C++
runtime source.

## Hardcoding Cutoff Gates

Scan gates must reject:

- concrete package ids in product runtime C++ except test fixtures, package
  fixtures, generated package descriptors, docs, and composition-root allowlists
- concrete NoC module ids in product runtime C++
- concrete generator executable names or artifact filenames in Qt product logic
- `MainWindow` references to package capability namespaces
- `PackagePlugin` references to `NoCPlugin` or `noc.v1` handling logic
- `NoCPlugin` references to concrete anchor package or module ids
- `GraphProjectSerializer` in normal save, validate, generate, connection, or
  editing paths
- `ProjectGenerationRequest::graph`
- `go-with-debt` as a completion verdict for this cutover

The composition root may mention internal plugin classes in
`StaticPluginCatalog`. Tests and migration code may mention legacy names when
their path is explicitly allowlisted.

## Error Handling

Package activation diagnostics use stable categories:

- `package.schema_invalid`
- `package.capability_missing_handler`
- `package.capability_unsupported`
- `package.feature_unhandled`
- `package.feature_visible_only`
- `design.command_rejected`
- `design.patch_invalid`
- `tool.flow_projection_failed`
- `tool.flow_failed`
- `artifact.declaration_unhandled`

Required capability failures block the package from normal editing and tool
execution, but the package remains visible in the catalog and inspector with
diagnostics. Optional unsupported capabilities do not block the package.

## Testing Strategy

### Contract Tests

- Plugin kernel activates static plugin catalog without business knowledge.
- Service registry rejects duplicate keys unless replacement policy is declared.
- Extension point registry registers and resolves typed contributions.
- Capability registry reports handled, visible, unsupported, blocking, and
  invalid feature coverage.
- Package plugin preserves unknown optional descriptor data.
- NoC plugin handles `noc.v1` semantic descriptors without concrete package ids.
- Design editing service applies commands and patches with undo/redo.

### Integration Tests

- `vendor-meshnoc` package loads without product Qt/C++ changes.
- Package inspector shows all declared feature coverage.
- NoC topology preset modifies `ProjectDesign`.
- Connection validation comes from package declarations and NoC semantic
  descriptors.
- Save/open roundtrip uses `ProjectDesign` and `ProjectDocument`, not Graph.
- Validation and generation consume `ProjectDesign` inputs.
- Artifacts are presented through artifact declarations.

### Scan Tests

- Product runtime C++ contains no concrete anchor package id branches.
- Product runtime C++ contains no concrete anchor module id branches.
- `PackagePlugin` does not include or reference NoC plugin implementation.
- `MainWindow` does not reference IP package or capability business terms.
- `GraphProjectSerializer` is absent from normal runtime paths.
- Completion report cannot contain `go-with-debt`.

## Migration And Phasing

This cutover is large enough to require phases, but the phases cannot end with
accepted architecture debt.

Recommended phase order:

1. Introduce minimal plugin context, service registry, extension point registry,
   and capability coverage reporting.
2. Move package activation and inspector coverage behind the registry flow.
3. Introduce `DesignEditingService` as the mutation and undo/redo owner.
4. Move NoC behavior into `NoCPlugin` registered handlers.
5. Move save, validate, and generate to `ProjectDesign` inputs.
6. Add `vendor-meshnoc` as the zero product C++ onboarding fixture.
7. Delete or isolate Graph normal paths and add hard cutoff scans.
8. Replace architecture reports with hard pass/blocked review.

Each phase must leave the repository in a passing state. A phase may carry
implementation work forward only if the remaining work is outside that phase's
acceptance criteria and is blocked by the next phase gate. The final cutover
does not accept carried architecture debt.

## Completion Criteria

The cutover is complete when:

- `vendor-meshnoc` passes the full package workflow without product Qt/C++
  changes.
- All package-declared features are handled, visible, unsupported with
  diagnostics, or blocked by required capability rules.
- Product runtime code is free of concrete IP package/module hardcoding.
- `PackagePlugin`, `NoCPlugin`, `MainWindow`, and tool pipeline boundaries are
  enforced by scan tests.
- Normal editing, save, validate, and generate flows use `ProjectDesign`.
- `GraphProjectSerializer` and `ProjectGenerationRequest::graph` are removed
  from normal runtime paths.
- Final architecture review returns hard pass.
