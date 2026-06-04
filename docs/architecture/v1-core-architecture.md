# Ipcraft V1 Core Architecture Contract

Migration-only legacy schema handling. Not used by normal runtime loading.

This document is the public V1 architecture contract for the current Finepaper
/ Ipcraft runtime. It is intentionally contract-oriented: schemas, CLI JSON
envelopes, diagnostics, examples, and this file define the externally auditable
behavior.

本文按当前版本重写。核心口径是：Finepaper 已从以 Qt `Graph` 为根的
NoC 编辑器，切换为以 `ipcraft.project.v1` 项目文档和
`ipcraft.package.v1` package 契约为根的 IP 制作与组合平台。

## Goals And Non-Goals

Goals:

- project-level IP composition
- declarative package capability contracts
- package-driven configuration, interfaces, connection rules, emitters, flows,
  artifacts, and views
- static validation by default
- explicit input emission and flow execution
- structured diagnostics and black-box audit through public contracts

Non-goals:

- embedding every IP domain rule in core
- hardcoding NoC, AXI, CHI, DDR, SerDes, CDC, or accelerator semantics in core
- requiring every package to ship a Qt/C++ plugin
- treating Qt canvas `Graph` as the durable project aggregate
- allowing generators or validators to read `.fpproj` directly as their normal
  input

Deep IP semantics belong in package validators, generator tools, first-party
extensions, or optional plugins.

## Runtime Source Of Truth

| Boundary | Current contract |
| --- | --- |
| Project file | `ipcraft.project.v1` |
| Package runtime file | `ipcraft.package.v1` in package-local `ipcraft.json` |
| CLI result envelope | `ipcraft.cli.result.v1` |
| Diagnostics | `ipcraft.diagnostics.v1` / `ipcraft.diagnostic.v1` |
| Generator emitted inputs | `ipcraft.emitted-inputs.v1` |
| Instance-local graph config | `ipcraft.graph-config.v1` |

`ProjectDocument` is the current in-memory project root used by the Qt/runtime
path. The newer foundation API under `ipcraft::core` exposes `ProjectDesign`
and `ProjectPatch` as the cleaner target IR. Both describe the same direction:
project semantics are document/component/interface/connection based, not
Qt-graph based.

## Project Model

`ipcraft.project.v1` is the public project schema. The foundation schema shape
uses the following root fields:

- `schema`
- `id`
- `name`
- `packages[]`
- `components[]`
- optional `interfaces[]`
- optional `connections[]`
- optional `topologies[]`
- optional `constraints`
- optional `views[]`
- optional `diagnostics[]`
- optional `artifacts[]`
- optional `extensions[]`
- optional `metadata`

Current Qt runtime also has `ProjectDocument` fields named `instances`,
`composition`, `layout`, `diagnostics`, `artifacts`, `migration`, and `native`.
Those fields represent the adapter/runtime project document used while the UI
continues to project through `Graph`. The architecture direction is that
semantic data lives in project/component/config/composition structures, while
visual state lives in `views[].layout` or runtime layout state.

Unknown top-level fields are rejected. Forward compatibility must use
`metadata`, package-owned `extensions[]`, `native`, or explicitly versioned
schema fields.

## Package Model

`ipcraft.package.v1` describes package capability, not resolved hardware state.
Runtime package loading consumes package-local `ipcraft.json` after
normalization. `ipcore.yml` may exist as authoring/specgen input, but it is not
the runtime loading contract.

`PackageSpec` contains:

- identity: `id`, `version`, `name`
- `extensions`
- `config_schema`
- `interfaces`
- `connection_rules`
- `emitters`
- `flows`
- `artifacts`
- `diagnostics`
- `views`
- optional `plugin`
- `native_schema`, `metadata`, `native`

Optional capability sections must be explicitly enabled by extension. Section
presence never implicitly enables a capability.

Known V1 extension IDs include:

- `ipcraft.config.params`
- `ipcraft.config.tables`
- `ipcraft.config.documents`
- `ipcraft.config.files`
- `ipcraft.interfaces`
- `ipcraft.composition`
- `ipcraft.layout`
- `ipcraft.emitters`
- `ipcraft.flows`
- `ipcraft.artifacts`
- `ipcraft.diagnostics`
- `ipcraft.views`
- `ipcraft.graph_config`
- `noc.v1`

Violations emit `package.extension_required`; unknown extensions emit
`package.unknown_extension`.

## Configuration Model

`ConfigSchema` declares what a package accepts. `ConfigBundle` stores instance
values.

`ConfigBundle` sections:

- `parameters`
- `tables`
- `documents`
- `files`
- `preserved`

Validation rejects undeclared parameters, documents, files, table columns, and
invalid file/path values with stable `config.*` diagnostics. Core validation is
structural and shallow. It does not implement protocol-specific correctness
such as AXI burst legality, CHI cache-state behavior, or NoC routing safety.

Value V1 supports null, bool, int64, double, string, array, and object.
Expression support is JSON AST only and limited to deterministic forms such as
`param`, `exists`, `eq`, `ne`, `and`, `or`, `not`, and literals. Expressions
cannot execute scripts, spawn processes, read files, access network state, or
mutate project data.

## Composition Model

`CompositionModel` describes project-level IP-to-IP connections.

Core records:

- `CompositionEndpointRef`: instance/interface/port/role reference
- `SystemConnection`: `id`, `type`, `endpoints[]`, `source`, `properties`,
  optional `native`
- `ExternalPort`: project-facing exposed interface
- `CompositionModel`: connections, external ports, groups, properties, native

Validation order:

1. referenced instance exists
2. referenced interface exists
3. duplicate/multiply driven inputs are rejected
4. clock/reset source-count rules are checked
5. connection type is resolved through package connection rules
6. endpoint role/protocol/kind/direction compatibility is checked

If a connection type has no declared compatibility rule after alias
normalization, validation emits `composition.unknown_connection_class` and stops
deeper compatibility evaluation for that connection.

## GraphConfig And Layout

`ipcraft.graph-config.v1` is instance-local internal graph configuration. It is
not the project root. It contains:

- `objects[]`
- n-ary `relationships[]`
- `properties`
- `native`

Visual editor state is separate. Canvas coordinates, node size, collapsed
state, zoom, pan, edge waypoints, and presentation state belong to layout/view
state, not generator parameters.

Qt still uses `Graph` as the live canvas projection. That projection is an
adapter and UI implementation detail; it cannot redefine the V1 project
contract.

## Emitters And Tool Inputs

Package emitters are deterministic and ordered by declaration order.

V1 emitter kinds:

- `emit_parameters`
- `emit_config_document`
- `emit_table`
- `copy_file`
- `emit_composition`
- `emit_graph_config`

Emitter output paths are relative to the emit root. Absolute paths, `..`,
symlink escapes, duplicate output paths, and missing declared source objects
produce structured `emitter.*` diagnostics.

`ipcraft.emitted-inputs.v1` records project id, instance id, package id/version,
run id, emitted files, hashes/sizes when available, source mapping, and
diagnostics.

## FlowRunner Model

Flows are declared in `PackageSpec.flows`.

Step kinds:

- `emit_inputs`
- `exec`
- `parse_diagnostics`
- `collect_artifacts`
- `plugin_hook`

Current runtime supports static validation separately from flow execution.
`validate-project` does not run external processes. External generation,
validation, tests, and packaging run through `run-flow` or the Qt Generate
path.

Instance-scoped flows require exactly one of `--instance <id>` or
`--all-instances`. Project-scoped flows reject instance targeting.

## Process And Artifact Security

Runtime must reject:

- unsupported schemas
- duplicate ids
- unknown top-level fields
- package-local path traversal
- absolute package-local paths
- symlink escapes
- missing executables
- flow policy violations
- unsafe output subdirectories
- artifact glob escapes

Flow process defaults:

- cwd is a confined run directory unless policy declares otherwise
- package-local executables resolve relative to package root
- framework tools come from host policy
- environment is sanitized and allowlisted
- timeout and stdout/stderr capture have upper bounds
- process-tree cleanup is attempted on timeout

## Diagnostics

Diagnostics are structured and stable for audit.

Stable fields:

- `severity`
- `source`
- `rule_id`
- ordered `locations[]`

Messages are human-readable and not a stable test API. The authoritative
rule-id catalog is `docs/audit/rule-id-catalog.md`.

Location kinds include project, instance, interface, connection, parameter,
table cell, document path, file, artifact, and graph object.

## Migration Policy

Normal project loading accepts the current V1 project contract. Old schemas may
exist only through explicit migration/import commands.

`migrate-project <project> --to ipcraft.project.v1` is side-effect free. It
returns migrated output under `result.project`; unsupported or opaque legacy
state is preserved under migration-specific preserved data and diagnostics.

Old NoC project inputs such as `ipcraft.noc.project.v1` are not the normal
runtime project format. They may remain only in explicit migration paths,
readiness comparison fixtures, or isolated generator compatibility adapters.

## Public CLI Contract

All public commands emit `ipcraft.cli.result.v1` JSON to stdout.

Required commands:

- `inspect-project <project>`
- `validate-project <project> --packages <package-root>`
- `emit-inputs <project> --instance <id> --out <dir> --packages <package-root>`
- `run-flow <project> --flow <flow-id> --out <dir> --packages <package-root> --instance <id>`
- `run-flow <project> --flow <flow-id> --out <dir> --packages <package-root> --all-instances`
- `migrate-project <project> --to ipcraft.project.v1`
- `collect-artifacts <run-dir> --spec <package-spec>`

Important behavior:

- `validate-project` is static and does not spawn external processes.
- `emit-inputs` requires explicit instance and output root.
- `run-flow` creates confined run directories.
- unsafe instance ids used as path segments are rejected with `cli.path_escape`.
- errors use the same CLI result envelope with structured diagnostics.

## Black-Box Audit Protocol

Audit agents use only public docs, schemas, examples, CLI, and public APIs.
They do not need internal unit tests.

Rules:

1. Public contract ambiguities are fixed publicly before implementation changes.
2. Hidden tests must match stable schema, CLI, and diagnostic fields.
3. Implementation must not branch on hidden test names, fixture names, or
   example names.
4. Implementation must not hardcode test package ids or example project ids.

## Current Transition State

Current runtime is in a V1 cutover transition:

- `ProjectDocument` / `PackageSpec` / `ConfigSchema` / `CompositionModel` are
  the runtime contract direction.
- Qt editor still uses `Graph` for live interaction and projects it back to V1
  state during save/generation.
- `ProjectDesign` / `ProjectPatch` foundation APIs exist and represent the
  cleaner long-term core IR.
- first-party generators are being moved toward standard projected tool inputs;
  legacy graph-shaped adapters are temporary.
