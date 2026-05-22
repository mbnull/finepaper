# Ipcraft V1 Core Architecture Contract

This document is the public contract for the V1 hard cutover. Runtime behavior is defined by schemas, CLI JSON envelopes, public examples, and this document.

## Goals / Non-Goals

Goals:

- project-level IP composition
- IP generator frontend and input emission
- declarative package capability contracts
- static validation, flow orchestration, artifacts, and diagnostics
- stable black-box audit through CLI and public examples

Non-goals:

- modeling every IP's internal hardware semantics in core
- embedding NoC, DDR, SerDes, clock-domain, or accelerator-specific correctness rules in core
- requiring every package to ship a Qt plugin

Vendor or IP-specific semantics belong in package validators, generators, first-party extensions, or optional plugins.

## V1 Hard Cutover And Legacy Policy

`ipcraft.project.v1` is the project runtime schema. Old `schema: "v1"` Finepaper projects and legacy NoC instance-state documents are not normal runtime formats. Normal project loading rejects old schemas with `project.unsupported_schema`.

Legacy content is accepted only through:

```bash
ipcraft-cli migrate-project old.fpproj --to ipcraft.project.v1
```

Successful migration returns the migrated document under `result.project`. Opaque legacy state is preserved under `migration.preserved.legacy_state`.

## Authoring/Runtime Package Boundary

Package runtime files must be self-contained after normalization. Runtime code must load `ipcraft.package.v1` directly and must not depend on `ipcore.yml`. `ipcore.yml` may exist as an authoring/specgen source, but it is outside the runtime loading path.

Qt editor, headless API, and `ipcraft-cli` consume normalized `ipcraft.package.v1` only.

## ProjectDocument Schema

`ipcraft.project.v1` uses:

- `schema`
- `project.id`
- `project.name`
- `instances[]`
- `composition`
- `layout`
- `diagnostics`
- `artifacts`
- `migration`
- `native`

Unknown top-level fields are rejected. Forward compatibility uses `metadata`, `native`, `preserved`, or extension-owned sections.

## PackageSpec Schema

`ipcraft.package.v1` describes package capabilities, not internal resolved hardware state. It includes package identity, extensions, config schema, interfaces, connection rules, emitters, flows, artifacts, diagnostics mapping, views, plugin metadata, native schema, metadata, and native data.

The runtime loader resolves exact `{id, version}` matches. If a package id exists but the requested version does not, resolution emits `package.version_not_found`. If the id is unknown, it emits `package.not_found`.

## ConfigBundle Model

`ConfigSchema` declares parameters, tables, documents, and files. `ConfigBundle` stores instance values:

- `parameters`
- `tables`
- `documents`
- `files`
- `preserved`

Clock domains, DTC domains, DDR lanes, SerDes tuning, and IP-specific dataflow rules are config or native data unless an extension/plugin explicitly promotes them.

## Value Type System And Expression Boundary

Value V1 supports null, bool, int64, double, string, array, and object.

Parameter mappings:

- `int` -> int64
- `bool` -> bool
- `double` -> double
- `string` -> string
- `enum` -> string or int64, declared by `value_type`
- `path` -> string with path confinement validation
- `object` -> object
- `array` -> array

Expressions are JSON AST only. Allowed forms are `param`, `exists`, `eq`, `ne`, `and`, `or`, `not`, and literal booleans. Arbitrary script, process execution, network access, file reads, environment access, and state mutation are forbidden.

Allowed expression sites are `visible_when`, `enabled_when`, `required_when`, and `default_when` only when explicitly supported.

## CompositionModel Model

`CompositionModel` describes project-level IP-to-IP connections. A connection has `id`, `type`, n-ary `endpoints`, `properties`, `source`, and optional native data. Core performs shallow validation: instance existence, interface existence, required interface connection, fanout/source-count rules, role/protocol/kind compatibility, and multiply driven inputs.

Core does not implement deep AXI, CHI, APB, NoC, DDR, SerDes, or CDC semantics.

## LayoutModel Model

Layout is editor-owned state. Canvas node positions, zoom, pan, collapsed state, and view state belong in `layout`, not in generator parameters. Values that affect generated hardware, such as lane index or channel index, belong in `ConfigBundle` or `CompositionModel.properties`.

## Graph-Config Schema

`ipcraft.graph-config.v1` is package instance configuration, not the project root. It contains `objects[]`, n-ary `relationships[]`, `properties`, and `native`. It must not expose old source/target PortRef-only assumptions. Canvas x/y remains in `LayoutModel`.

## Emitted Inputs Manifest Schema

`ipcraft.emitted-inputs.v1` is returned by `emit-inputs` and flow `emit_inputs` steps. Paths are relative to the emit root, deterministic, and confined. The manifest records project id, instance id, package id/version, optional run id, files, hashes/sizes when available, source mapping, and diagnostics.

## FlowRunner Model

Flows are declared in `PackageSpec.flows`. Step kinds are:

- `emit_inputs`
- `exec`
- `parse_diagnostics`
- `collect_artifacts`
- `plugin_hook`

Default `validate-project` is static and does not run flows or external processes. External validation, generation, tests, and packaging run only through `run-flow`.

Instance-scoped flows require `--instance <id>` or `--all-instances`. Project-scoped flows do not accept instance targeting.

## FlowRunner Process Security

Defaults:

- cwd is the run directory unless declared otherwise
- package-local executables resolve relative to package root
- framework tools come from application policy, not package native data
- environment is sanitized plus explicitly allowed variables
- timeout defaults to a fixed value and is bounded
- stdout/stderr are captured under the run directory with byte limits
- nonzero exit, timeout, missing executable, policy violation, and truncation produce structured diagnostics
- process-tree cleanup is attempted on timeout
- parallel flow execution is disabled in V1 unless explicitly implemented with deterministic run directories

## DiagnosticModel Model

Diagnostics use `ipcraft.diagnostics.v1`. A diagnostic has stable `severity`, `source`, `rule_id`, optional `category`, human-readable `message`, `details`, and ordered `locations`.

Location kinds include project, ip instance, interface, connection, parameter, table cell, document path, file, artifact, and graph object.

## Diagnostic Stability Rules

Black-box tests match `rule_id`, `severity`, `source`, and locations. Messages are human-readable and not stable. Most specific locations appear first; fallback locations may follow.

## ArtifactIndex Model

`ArtifactSpec` declares expected outputs by id, type, glob, primary flag, and metadata. `ArtifactIndex` records collected artifacts by flow run, path, type, size, modified time, source instance, and spec id. Globs are confined to the run/output root after realpath resolution.

## Extension Vs Plugin Boundary

First-party extensions are declarative capability descriptors. Plugins are optional dynamic escape hatches.

Default path: first-party extensions + `ipcraft.package.v1` + templates + FlowRunner.

Plugins are used only when declaration is insufficient, for enum providers, custom validation, custom emission, diagnostic parsing, custom project views, instance migration, artifact post-processing, wrapper generation, or custom layout.

## Explicit Extension Enablement Rules

Optional capability sections must declare their extension. Section presence never implicitly enables a capability.

- `config_schema.parameters` requires `ipcraft.config.params`
- `config_schema.tables` requires `ipcraft.config.tables`
- `config_schema.documents` requires `ipcraft.config.documents`
- `config_schema.files` requires `ipcraft.config.files`
- `interfaces` requires `ipcraft.interfaces`
- `connection_rules` requires `ipcraft.composition`
- `emitters` requires `ipcraft.emitters`
- `flows` requires `ipcraft.flows`
- `artifacts` requires `ipcraft.artifacts`
- `diagnostics` requires `ipcraft.diagnostics`
- `views` requires `ipcraft.views`
- `graph_config` requires `ipcraft.graph_config`

Violations emit `package.extension_required`.

## Native Escape Hatch

`native` and `preserved` store namespaced opaque data. Core must preserve these fields but must not derive hidden hardware semantics from them. Native data cannot override process security policy.

## Migration Strategy

Migration is explicit and side-effect free. Old project data is read by migrator code, not by the normal project reader. Migrated output is canonical `ipcraft.project.v1`.

Migration maps old IP state to instances, old layout parameters to LayoutModel, unambiguous non-layout parameters to ConfigBundle, same-instance internal graph data to graph-config, and unsupported legacy content to `migration.unsupported_legacy_content`.

## Security Model

Runtime rejects unsupported schemas, duplicate ids, unknown top-level fields, path traversal, absolute package-local paths, symlink escapes, missing executables, flow policy violations, and artifact escapes. Validation commands are static by default.
CLI commands that derive output subdirectories from project data must reject path separators, `.` and `..` segments before creating run directories.

## Public CLI/API Contract

All commands emit `ipcraft.cli.result.v1` JSON to stdout:

- `inspect-project <project>`
- `validate-project <project> --packages <package-root>`
- `emit-inputs <project> --instance <id> --out <dir> --packages <package-root>`
- `run-flow <project> --flow <flow-id> --out <dir> --packages <package-root> --instance <id>`
- `run-flow <project> --flow <flow-id> --out <dir> --packages <package-root> --all-instances`
- `migrate-project <project> --to ipcraft.project.v1`
- `collect-artifacts <run-dir> --spec <package-spec>`

Errors also use the CLI result envelope with structured diagnostics.

Current command details:

- `inspect-project` reads only the project file and returns `result.project.{id,name}` plus `result.instances`.
- `validate-project` requires `--packages`, performs static schema/config/composition validation, and does not spawn external processes.
- `emit-inputs` requires `--instance`, `--out`, and `--packages`.
- instance-scoped `run-flow` requires exactly one of `--instance` or `--all-instances`; project-scoped flows omit both.
- `run-flow` creates per-instance run directories under `--out`; unsafe instance ids that would become path segments are rejected with `cli.path_escape`.
- `collect-artifacts --spec` takes a package spec file path, not a package root.

## Testability Contract

Third-party audit agents use only public docs, schemas, examples, CLI, and public APIs. They do not need internal unit tests. The implementation must not branch on hidden test names, fixture names, package ids, or example names.

## Black-Box Audit Protocol

Workflow:

1. Implementation publishes docs, schemas, examples, and `ipcraft-cli`.
2. Audit agent reads only public materials.
3. Audit agent writes independent hidden tests.
4. Codex cannot read hidden tests.
5. Codex receives only failure summaries.
6. Failure summaries include contract section, expected behavior, actual behavior, and minimal redacted input.
7. Ambiguous contracts are fixed publicly before changing tests or implementation.
8. Implementation never branches on hidden test names, example names, or package ids.

## Diagnostic Rule-ID Catalog

The authoritative rule-id catalog is `docs/audit/rule-id-catalog.md`.
