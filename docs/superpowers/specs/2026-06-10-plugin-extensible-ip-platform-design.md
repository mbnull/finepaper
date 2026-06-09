# Plugin-Extensible IP Platform Architecture Design

## Summary

Finepaper will converge on a plugin-extensible commercial IP creation platform.
The product should no longer be organized as a NoC-specific graph editor.
Instead, Finepaper should provide a small application kernel, a plugin host for
internal capabilities, and V1-schema-based extension packages for concrete IP
integration.

NoC remains the first complete commercial workflow. The existing `finepaper-noc`,
`ravenoc`, and `opennoc` IPs are required acceptance anchors: each must load,
configure, validate, generate, collect artifacts, and survive project save/load
under the new architecture.

The architecture will be completed through governed phases. Each phase ends with
tests, architecture review, blocker/debt classification, and next-phase
selection. The final phase runs `qt-cpp-review` and writes a completion report.

## Goals

- Turn Finepaper into a small-kernel, plugin-extensible IP creation platform.
- Keep NoC as the first fully usable commercial IP creation workflow.
- Reuse the existing V1 public schema contracts instead of inventing a new
  project/package/tool/diagnostic model.
- Keep concrete IP behavior in extension packages, not in the kernel or
  workbench shell.
- Preserve the existing QtNodes/node-editor interaction implementation where it
  is useful, while replacing its durable data source and command boundary.
- Support multiple IP instances from the start.
- Make validate/generate a unified platform pipeline with emitted inputs,
  structured diagnostics, artifact collection, and run manifests.
- Provide a concise `finepaper-ip-onboarding` skill or prompt that lets agents
  adapt an existing IP codebase into Finepaper's extension/package flow.
- Add phased review gates so the architecture keeps progressing until the full
  target is complete.

## Non-Goals

- Do not build a full Eclipse/OSGi-style framework.
- Do not commit to a public C++ ABI in this design.
- Do not implement hot-plug native plugins or a marketplace.
- Do not rewrite the existing node-editor interaction layer from scratch.
- Do not redesign the existing V1 public schemas for projects, packages,
  emitted inputs, or diagnostics.
- Do not make every IP package ship native Qt/C++ code.

## Terminology

- **Kernel**: The small Finepaper platform core. It owns project state,
  commands, diagnostics, settings, file I/O, tool execution, and workbench
  services.
- **Plugin Host**: The internal mechanism that activates Finepaper capabilities
  and lets them register workbench contributions.
- **Internal Plugin**: A Finepaper code module such as Project, Package, NoC,
  Tool Pipeline, or Report. MVP plugins may be statically registered.
- **Extension**: A first-party or third-party IP capability package consumed by
  Finepaper through V1 package and capability descriptors.
- **IP Package**: A concrete extension package for one IP family or product.
- **Capability**: A package-declared domain section, such as NoC topology
  capability, interpreted by the matching internal plugin.
- **Projection**: A disposable editor view model derived from project state. A
  projection can support real-time UI interaction but is not the durable source
  of truth.

## Architecture Overview

The target architecture has five layers:

```text
Kernel
Plugin Host
Internal Plugins
Extensions / IP Packages
Phase Governance + IP Onboarding Skill
```

### Kernel

The kernel owns only platform services:

- `ProjectService`
- `PatchService` / command and undo-redo service
- `DiagnosticService`
- `SettingsService`
- `ToolExecutionService`
- `WorkbenchService`
- `ExtensionDiscoveryService`

The kernel must not embed IP-domain behavior. It must not contain NoC, AXI, CHI,
RaveNoC, OpenNoC, or finepaper-noc-specific rules.

### Plugin Host

The plugin host creates an `AppContext`, activates internal plugins, and collects
their workbench contributions. MVP registration can be static:

```cpp
registerProjectPlugin(ctx);
registerPackagePlugin(ctx);
registerNocPlugin(ctx);
registerToolPlugin(ctx);
registerReportPlugin(ctx);
```

The design intentionally avoids a broad native plugin ABI at this stage.

### Internal Plugins

Internal plugins are Finepaper implementation modules.

The initial plugin set is:

- **Project Plugin**: project read/write, current project state, patch/command
  application, undo/redo, and project lifecycle.
- **Package Plugin**: extension discovery, `ipcraft.package.v1` loading, IP
  catalog construction, common config/interface/rule/tool/artifact descriptors,
  and capability-section dispatch.
- **NoC Plugin**: NoC topology editor contribution, NoC property pages, topology
  presets, NoC editor projection, and algorithmic NoC checks that cannot be
  expressed as package data.
- **Tool Plugin**: validate/generate pipeline, emitted input construction, tool
  execution, diagnostic parsing, artifact collection, and run manifests.
- **Report Plugin**: diagnostics, validation/generation logs, artifact views,
  and report export.

### Extensions And IP Packages

Extensions are the public IP integration surface. A commercial IP package can
contain:

```text
ipcraft.json
views/
tools/
generator/
validator/
rtl/
vendor/
docs/
examples/
```

An extension package declares:

- identity and version
- configuration schema
- interfaces
- package-declared connection rules
- capability sections
- view descriptors
- validation and generation flows
- artifact declarations
- diagnostic mapping
- documentation and examples

Concrete IP behavior belongs here, not in the kernel. The NoC plugin may
interpret NoC capability sections, but the differences between `finepaper-noc`,
`ravenoc`, and `opennoc` must come from package data and package tools.

## V1 Schema Policy

The architecture reuses the existing V1 public contracts:

- `ipcraft.project.v1`
- `ipcraft.package.v1`
- `ipcraft.graph-config.v1`
- `ipcraft.emitted-inputs.v1`
- `ipcraft.diagnostics.v1`
- `ipcraft.diagnostic.v1`

Existing model work such as `ProjectDocument`, `PackageSpec`, `ConfigSchema`,
`ConfigBundle`, `CompositionModel`, `DiagnosticStore`, `PackageInputBuilder`,
`FlowRunner`, and `ArtifactCollector` should be reused or evolved where they
fit the new boundaries.

New descriptors should be added only where the plugin/workbench/capability model
needs a missing contract. They must not duplicate the existing project, package,
tool input, or diagnostic schemas.

## Project State And Editor Boundary

`ProjectDesign` / `ProjectDocument` is the durable project source of truth.
Editors can maintain fast in-memory projections, but those projections must be
rebuildable from project state.

The existing QtNodes/node-editor interaction implementation should be retained
where possible:

- scene and view interaction
- node and port geometry
- connection drawing
- drag/drop
- selection and hover behavior
- collapse/expand presentation
- existing view metadata bridges where they remain useful

The boundary changes are:

- `NodeEditorWidget` must no longer treat `Graph` as authoritative persistent
  state.
- UI gestures should become patch/command operations against `ProjectService`.
- Projection state should not be a save/generate/validate source.
- `MainWindow` should receive editor contributions from `WorkbenchService`
  instead of constructing domain-specific editor wiring itself.

## Workbench Contributions

`MainWindow` becomes a shell. It renders workbench contributions instead of
owning domain features directly.

Internal plugins may contribute:

- editors
- dock panels
- menus and actions
- property pages
- diagnostics overlays
- report views

`WorkbenchService` decides placement, enable/disable state, selection context,
and lifecycle. Plugins should contribute descriptors and factories rather than
directly mutating `MainWindow`.

## Extension Loading

Package loading follows this flow:

```text
extension root
  -> extension/package descriptor
  -> ipcraft.package.v1
  -> common package descriptors
  -> capability sections
  -> catalog entries and plugin-specific capability data
```

The Package Plugin parses common package data. Capability-specific sections are
handed to the internal plugin that owns that capability. Unknown optional
sections can be preserved with diagnostics. Unknown required sections are
errors.

Business UI must not directly parse capability JSON. JSON field access belongs
in the descriptor readers owned by the relevant plugin/package layer.

## Connection Checking

Fast connection checking is centered in a platform service, but concrete
connection semantics come from package data.

The flow is:

```text
endpoint metadata
  + package-declared connection rules
  + project context
  -> ConnectionCheckService
  -> allow / reject / warn / needs-selection
```

The platform performs generic structural checks and package-declared rule
matching. It does not hardcode protocol or connection type behavior.

Internal plugins may add algorithmic checks when data rules are insufficient.
For NoC, examples include topology slot capacity, reachability, and early
deadlock checks. Those checks must consume package-declared capability data and
project state rather than hardcoding concrete IP package behavior.

## Validate And Generate Pipeline

Validate and generate use a unified Tool Plugin pipeline:

```text
project state
  -> build emitted input from package declarations
  -> execute package-declared flow
  -> parse diagnostics
  -> collect artifacts
  -> write run/generation manifest
  -> update project run/artifact state
```

Tool commands and artifact declarations come from packages. UI callbacks and
domain plugins should not run generators or validators directly.

The target path is V1 package/tool pipeline. Existing generators brought into
the new architecture should be connected to that path. Legacy generator input
compatibility is outside this design scope.

## Diagnostics And Error Handling

All user-visible errors should flow through `DiagnosticService` and V1
diagnostics.

Diagnostic sources include:

- Project Plugin: project schema, save/load, patch, references
- Package Plugin: package schema, extension discovery, interfaces, rules,
  tools, artifacts
- NoC Plugin: NoC capability parsing, projection construction, topology checks
- ConnectionCheckService: fast connection decisions
- Tool Plugin: emitted input, flow execution, timeout, exit code, stdout/stderr
  parsing, artifacts
- Report Plugin: display and export only

Policy:

- Fatal package load errors omit that package from the catalog but keep the app
  running.
- Project load errors prevent entering editable state and produce diagnostics.
- Rejected connections do not submit patches.
- Tool failures still write run state with diagnostics and log summaries.
- Optional unknown extension data can be preserved with warnings; required
  unknown data is an error.

## Commercial NoC MVP Acceptance Anchors

The architecture is not complete unless these IPs work through the new flow:

- `finepaper-noc`
- `ravenoc`
- `opennoc`

Each anchor must prove:

- package loads
- catalog entry appears
- multiple instances can be created
- parameters/config can be edited
- topology/editor projection can load
- valid and invalid connections are checked
- project save/open roundtrips
- validation runs
- generation runs
- artifacts are collected
- run or generation manifest is written

These anchors are product acceptance tests, not only demo fixtures.

## IP Onboarding Skill / Prompt

The repository should provide a concise `finepaper-ip-onboarding` skill or
prompt for agents. It is for adapting an existing IP codebase into Finepaper's
Qt frontend and extension/package flow.

It should guide an agent through:

1. Inspecting the IP repository structure: RTL, generator, validator, docs,
   examples, and tests.
2. Extracting Finepaper frontend metadata: parameters, interfaces, connection
   rules, topology capability, tools, and artifacts.
3. Creating or updating extension/package descriptors.
4. Connecting the package to catalog, property, editor, validation, and
   generation surfaces.
5. Verifying validate/generate and artifact output.
6. Producing an onboarding review with remaining gaps.

The skill should stay lean. Detailed architecture belongs in this design and
follow-up plans; the skill should act as an operational checklist.

## Phase Governance

The architecture will be completed through governed phases. Every phase has:

- a narrow boundary
- acceptance criteria
- tests or scans
- three-IP anchor status
- blocker/debt classification
- a next-phase recommendation

The gate has two layers.

Critical blockers must be fixed before continuing:

- Kernel contains concrete IP/domain hardcoding.
- `MainWindow` keeps gaining direct business assembly.
- `Graph` remains the source of truth for new normal paths.
- UI directly parses package/capability JSON.
- Connection checking hardcodes connection type behavior in core or UI.
- Validate/generate bypasses the unified Tool Plugin pipeline.
- Any of the three IP anchors regresses on a relevant path.
- The phase has no verification evidence.

Non-critical debt may be recorded and carried forward:

- UI polish
- naming refinements
- temporary adapters with deletion conditions
- performance tuning
- non-critical test expansion
- documentation examples

Each phase review must output:

```text
phase status: pass / pass-with-debt / blocked
tests run
architecture scan status
three-IP anchor status
critical findings
debt findings
schema compatibility status
next phase recommendation
```

## Phase Plan

### Phase 1: Kernel And Plugin Host Foundation

Create `AppContext`, `PluginHost`, `WorkbenchService` contribution model, and
basic static internal plugin registration. `MainWindow` begins moving toward a
workbench shell.

Acceptance:

- Built-in plugins can register actions, panels, or editor descriptors.
- `MainWindow` can build at least part of the UI from `WorkbenchService`.
- No dynamic ABI is introduced.

### Phase 2: Project Plugin And V1 Source Of Truth

Make the Project Plugin own `ipcraft.project.v1` state and durable mutation
entry points.

Acceptance:

- Project create/open/save goes through Project Plugin services.
- New persistent mutations use patch/command boundaries.
- Editor projections can be rebuilt from project state.

### Phase 3: Package Plugin And Extension Loading

Move extension discovery, package loading, and catalog construction behind the
Package Plugin.

Acceptance:

- `finepaper-noc`, `ravenoc`, and `opennoc` load as packages.
- Catalog population does not depend on `MainWindow` hardcoding.
- Package parsing errors flow through diagnostics.

### Phase 4: Editor Shell Rebinding

Retain the existing node-editor interaction shell while replacing its durable
data source and command boundary.

Acceptance:

- Drag, selection, connection, and collapse interactions remain usable.
- Project state is the source of truth.
- Projection can be discarded and rebuilt.

### Phase 5: Connection Checking

Introduce data-driven fast connection checking.

Acceptance:

- Results include allow, reject, warn, and needs-selection.
- Core/UI does not hardcode concrete connection type behavior.
- Valid and invalid cases are covered for the anchor IPs.

### Phase 6: Tool Pipeline

Unify validate/generate through the Tool Plugin.

Acceptance:

- Package-declared tools run through one pipeline.
- Emitted inputs, diagnostics, artifacts, and run manifests are produced.
- The three anchor IPs can validate/generate through the pipeline.

### Phase 7: NoC Commercial Workflow Completion

Complete the NoC plugin workflow for commercial IP creation.

Acceptance:

- The three anchor IPs complete catalog/config/editor/validate/generate flows.
- Multiple IP instances are supported.
- Saved projects can be reopened and continue editing/generation.

### Phase 8: IP Onboarding Skill / Prompt

Create the `finepaper-ip-onboarding` skill or prompt.

Acceptance:

- It covers inspect, descriptor creation, frontend integration, tools, artifacts,
  verification, and onboarding review.
- It uses the three anchor IPs as examples or validation models.
- It is concise enough for routine agent use.

### Phase 9: Hardening And Deletion Gates

Delete or isolate old normal paths and strengthen architecture scans.

Acceptance:

- Legacy graph-centered normal paths are removed or isolated as explicit
  import/test paths.
- Architecture scans catch hardcoding and boundary regressions.
- Contract tests cover V1 schema reuse.

### Phase 10: Final Architecture Review And Report

Review the preceding phases and decide whether the architecture is complete.

Required actions:

- Run `qt-cpp-review` over the relevant Qt/C++ changes.
- Produce `docs/architecture/plugin-architecture-completion-report.md`.
- Summarize lint findings, deep analysis findings, accepted debt, and required
  fixes.
- Include phase completion matrix, three-IP anchor matrix, V1 schema reuse
  matrix, legacy path status, architecture scan status, and final go/no-go
  verdict.

Acceptance:

- `qt-cpp-review` has no unresolved high-confidence architecture-blocking
  findings.
- The three anchor IPs pass the complete workflow.
- Architecture scans pass.
- The report gives a clear go verdict.

## Testing Strategy

Testing is split into three groups.

### Architecture Tests

- Kernel/app scans for concrete IP/domain hardcoding.
- `MainWindow` does not directly assemble domain features.
- UI/panels do not directly parse package capability JSON.
- New normal paths do not use `Graph` as durable source of truth.
- Validate/generate goes through Tool Plugin pipeline.

### Contract Tests

- `ipcraft.project.v1` read/write roundtrip and negative cases.
- `ipcraft.package.v1` loading, capability dispatch, and negative cases.
- Generic package-declared connection rule matching.
- `ipcraft.emitted-inputs.v1` manifest output.
- V1 diagnostics stability.
- Artifact and run manifest output.

### Anchor Tests

For `finepaper-noc`, `ravenoc`, and `opennoc`:

- package load
- catalog entry
- instance create
- parameter/config edit
- editor projection load
- valid/invalid connection checks
- project save/open
- validate
- generate
- artifact collection
- run/generation manifest

## Completion Criteria

The total architecture is complete when:

- V1 schemas remain the foundation for project, package, graph-config, emitted
  input, and diagnostics.
- Kernel services are small and domain-neutral.
- Business capabilities are registered through internal plugins.
- IP behavior is supplied by extension packages.
- NoC commercial workflow is complete.
- `finepaper-noc`, `ravenoc`, and `opennoc` all pass the anchor workflow.
- `finepaper-ip-onboarding` exists and can guide agents through new IP
  adaptation.
- Old graph-centered normal paths are removed or explicitly isolated.
- Architecture scans prevent hardcoding and boundary regressions.
- Phase 10 review and `qt-cpp-review` produce a go verdict.
