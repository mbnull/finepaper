# Ipcraft V1 Hard Cutover Core Architecture Design

## Summary

Finepaper will replace the current NoC/canvas-centered architecture with a new
`ipcraft.project.v1` and `ipcraft.package.v1` core. This is a hard cutover before
the product reaches a formal V1 release. The new architecture makes the project
document the source of truth for IP instances, generator configuration,
project-level composition, layout, flows, diagnostics, and artifacts.

The existing `Graph / Module / Connection / Port / Parameter` model remains only
as an implementation tool for graph-config editors and block-diagram views. It
is no longer the root project model and should not be extended into a universal
hardware semantic graph.

The work must be testable by third-party black-box audit agents. Public schema,
architecture documentation, CLI contracts, API contracts, and examples are part
of the product surface. Hidden tests must be able to verify behavior without
reading internal unit tests.

## Confirmed Direction

The chosen strategy is a hard cutover:

- Replace current `.fpproj` schema with `ipcraft.project.v1`.
- Replace current runtime manifest contract with `ipcraft.package.v1`.
- Replace `ipcraft.noc.project.v1` command input with generic emitted package
  inputs driven by package emitters and flows.
- Convert the in-repository `finepaper-noc`, `ravenoc`, and `opennoc` packages
  to the new contract in the same refactor.
- Do not keep a runtime compatibility layer for old project documents or old
  generator input schemas.
- Keep migration tooling only as an explicit import/conversion command for
  pre-cutover project files, not as a persistent runtime fallback.

The target schema names are:

- `ipcraft.project.v1`
- `ipcraft.package.v1`
- `ipcraft.diagnostics.v1`
- `ipcraft.graph-config.v1`

Any prior mention of `ipcraft.project.v2` is treated as a typo. Public commands
must use `ipcraft.project.v1` as the target.

## Goals

- Make `ProjectDocument` the project-level source of truth.
- Model project IP instances, generator configuration, IP-to-IP composition,
  layout, flows, diagnostics, and artifacts as first-class core data.
- Keep core semantics shallow and package-driven.
- Support complex IP generator frontends without embedding vendor/IP-specific
  hardware semantics into core.
- Make package runtime metadata declarative enough for ordinary IP packages to
  avoid writing Qt plugins.
- Keep first-party extensions separate from optional Qt/native plugins.
- Expose stable public CLI/API surfaces for black-box validation.
- Provide complete architecture documentation and contract examples that are
  sufficient for independent audit agents to write tests.
- Make structured diagnostics the universal error format, including CLI errors.
- Preserve unknown and native/vendor namespaces according to documented rules.
- Enforce package-local path and command security boundaries.

## Non-Goals

- Do not model all IP internal hardware semantics in core.
- Do not make core understand NoC routing, DDR timing, SerDes training, AI
  accelerator dataflow, DTC/DN/clock-domain insertion, or vendor resolved state.
- Do not preserve old `.fpproj` runtime behavior after the hard cutover.
- Do not keep `ipcraft.noc.project.v1` as a normal command input path.
- Do not make every IP package write a dynamic Qt plugin.
- Do not make `Graph` the universal semantic project graph.
- Do not hide contract behavior inside internal-only tests or fixtures.
- Do not special-case hidden test names, fixture names, or third-party package
  identifiers in implementation.

## Architecture Overview

The new architecture has six layers:

1. **Project core**: `ProjectDocument`, `IpInstanceState`, `ConfigBundle`,
   `CompositionModel`, `LayoutModel`, `DiagnosticStore`, `ArtifactIndex`, and
   native state preservation.
2. **Package contract**: `PackageSpec` describes configuration schema,
   interfaces, shallow connection rules, emitters, flows, artifacts,
   diagnostics, views, extensions, plugin metadata, and native schema.
3. **Emitter/build layer**: `PackageInputBuilder` and `ConfigEmitter` produce
   declared generator inputs from project state.
4. **Flow layer**: `FlowRunner` executes validate/generate/test/package flows
   from `FlowSpec` with structured diagnostics and artifact collection.
5. **Editor layer**: Qt UI edits project state only through commands. Graph and
   QtNodes views are adapters over project state, not authoritative storage.
6. **Public audit layer**: `ipcraft-cli`, public schema docs, examples, and
   machine-readable JSON output expose all important behavior to external tests.

## Source Of Truth

`ProjectDocument` becomes the source of truth. Persistent project mutations must
still go through commands, but commands now target project-level state rather
than only `Graph`.

Existing command discipline remains:

- UI does not directly mutate persistent state.
- Commands own undo/redo and dirty tracking.
- Loading, migration, and CLI inspection may construct documents directly.
- Runtime flow execution may create run-state and artifact records through
  documented project services or explicit CLI output directories.

## Graph Position

`Graph` becomes a bounded implementation model:

- Optional `GraphConfig` for packages that need graph-shaped instance
  configuration.
- Optional block-diagram or composition editor projection.
- Transitional UI adapter for QtNodes during the refactor.

`Graph` must not become:

- the project root model;
- the generic config store;
- the universal semantic graph;
- the only possible diagnostic location namespace.

Project-level IP-to-IP connections belong in `CompositionModel`, not in the old
port-level canvas graph. Single-IP internal topology can live in
`IpInstanceState.graphConfig` only when the package declares the
`ipcraft.graph_config` extension or equivalent view capability.

## Core Data Model

### ProjectDocument

`ProjectDocument` is the root object for `.fpproj`:

```json
{
  "schema": "ipcraft.project.v1",
  "project": {
    "id": "project_0",
    "name": "Example Project"
  },
  "instances": [],
  "composition": {
    "connections": [],
    "external_ports": [],
    "properties": {}
  },
  "layout": {
    "views": []
  },
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  },
  "artifacts": {
    "records": []
  },
  "migration": {},
  "native": {}
}
```

Required behavior:

- Unknown fields under documented extension/native namespaces are preserved.
- Unknown top-level fields outside allowed forward-compatible locations produce
  structured diagnostics.
- Duplicate IDs are errors.
- Project IDs and instance IDs are stable references, not display labels.
- `.fpproj` is JSON syntax with deterministic writing.

### IpInstanceState

Each project IP instance has:

```json
{
  "id": "ip0",
  "display_name": "IP 0",
  "package": {
    "id": "vendor.example.simple",
    "version": "1.0.0"
  },
  "config": {
    "parameters": {},
    "tables": {},
    "documents": {},
    "files": {},
    "preserved": {}
  },
  "graph_config": null,
  "native": {},
  "last_runs": {},
  "artifacts": {
    "records": []
  },
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  },
  "view": {}
}
```

`ConfigBundle` is the generator input state for a single IP. `GraphConfig` is
optional and only exists for graph-shaped package configuration. `NativeState`
is a namespace-preserving escape hatch for package/vendor data that core does
not interpret.

Clock domains, DTC/DN domains, DDR lanes, SerDes gearbox settings, and similar
deep IP semantics are configuration data by default, not core models.

### PackageSpec

`ipcraft.package.v1` describes package capabilities rather than IP internals:

```json
{
  "schema": "ipcraft.package.v1",
  "id": "vendor.example.simple",
  "version": "1.0.0",
  "name": "Simple Parameter IP",
  "extensions": [],
  "config_schema": {
    "parameters": [],
    "tables": [],
    "documents": [],
    "files": []
  },
  "interfaces": [],
  "connection_rules": {},
  "emitters": [],
  "flows": [],
  "artifacts": [],
  "diagnostics": {},
  "views": [],
  "plugin": null,
  "native_schema": {}
}
```

It answers:

- What configuration inputs does this package expose?
- What external interfaces can project composition connect?
- How are inputs emitted for validators and generators?
- What flows exist?
- What artifacts should be collected?
- How are external diagnostics mapped back to project objects?
- What optional dynamic plugin hooks exist?

### ConfigSchema And ConfigBundle

`ConfigSchema` supports:

- `ParameterDef`
- `TableDef`
- `ConfigDocumentDef`
- `FileInputDef`

`ParameterDef` supports `int`, `bool`, `double`, `string`, `enum`, `path`,
`object`, and `array`. It may declare defaults, enum values or enum providers,
ranges, required state, visibility/enabled expressions, label, group, and
description.

`TableDef` declares columns, row add/remove behavior, optional document/path
binding, and unknown-column preservation policy.

`ConfigDocumentDef` declares format (`yaml`, `json`, `tcl`, `xml`, `ini`,
`text`, `raw`), output path, optional template, editability, and unknown-field
preservation.

`FileInputDef` declares kind, allowed extensions, required state, and output
mapping.

`ConfigBundle` stores actual instance values:

```json
{
  "parameters": {
    "width": 64
  },
  "tables": {
    "regions": {
      "rows": []
    }
  },
  "documents": {
    "raw_cfg": {
      "format": "raw",
      "content": "..."
    }
  },
  "files": {
    "constraints": {
      "path": "constraints/top.xdc"
    }
  },
  "preserved": {}
}
```

### InterfaceSpec

Package interfaces are shallow composition endpoints:

```json
{
  "id": "m_axi",
  "kind": "bus",
  "protocol": "axi4",
  "role": "master",
  "direction": "out",
  "required": false,
  "fanout": "none",
  "properties": {}
}
```

Core validation checks only:

- instance exists;
- interface exists;
- kind/protocol tags match according to package rules;
- role/direction are compatible;
- required interfaces are connected;
- input endpoints are not multiply driven;
- clock/reset fanout has a single source.

Core does not implement AXI/CHI/APB/DDR/SerDes deep protocol semantics.

### CompositionModel

`CompositionModel` describes project-level IP-to-IP relationships:

```json
{
  "connections": [
    {
      "id": "clk_net0",
      "type": "clock",
      "endpoints": [
        {
          "instance": "pll0",
          "interface": "clk_out",
          "role": "source"
        },
        {
          "instance": "ip0",
          "interface": "aclk",
          "role": "sink"
        }
      ],
      "properties": {},
      "source": "user"
    }
  ],
  "external_ports": [],
  "groups": [],
  "properties": {}
}
```

Connections support n-ary endpoints for fanout. Interface/bus links may remain
binary by validation rule. Properties hold shallow generator metadata such as
address map references, wrapper signal names, protocol tags, or port indexes.

### LayoutModel

`LayoutModel` stores editor-only placement and view state:

```json
{
  "views": [
    {
      "id": "system",
      "kind": "composition",
      "nodes": {
        "ip0": {
          "x": 100,
          "y": 200
        }
      },
      "connections": {},
      "zoom": 1.0,
      "pan": {
        "x": 0,
        "y": 0
      },
      "state": {}
    }
  ]
}
```

Canvas coordinates must not be stored as generator parameters in the new model.
If a value affects generated hardware, it belongs in `ConfigBundle` or
`CompositionModel.properties`.

### EmitterSpec

Emitters describe how to create package inputs from project state:

- `emit_config_document`
- `emit_parameters`
- `emit_table`
- `template_emit`
- `yaml_emit`
- `json_emit`
- `xml_emit`
- `tcl_emit`
- `raw_emit`
- `copy_file`
- `emit_composition`
- `emit_graph_config`
- `plugin_hook`

`PackageInputBuilder` resolves package and project state, validates emitter
paths, writes declared inputs, and returns a structured manifest of emitted
files. It replaces the current graph-specific exporter.

### FlowSpec And FlowRunner

Flows support project-level and instance-level execution:

```json
{
  "id": "generate",
  "label": "Generate",
  "scope": "instance",
  "steps": [
    {
      "kind": "emit_inputs",
      "emitter": "generator_inputs"
    },
    {
      "kind": "exec",
      "command": {
        "executable": "tools/generate",
        "args": ["--input", "{inputs.manifest}", "--out", "{out}"]
      }
    },
    {
      "kind": "parse_diagnostics",
      "parser": "default_json"
    },
    {
      "kind": "collect_artifacts",
      "spec": "default"
    }
  ]
}
```

Flow steps:

- `emit_inputs`
- `exec`
- `parse_diagnostics`
- `collect_artifacts`
- `plugin_hook`

Flow execution must record:

- flow run ID;
- project or instance scope;
- emitted input manifest;
- process command and arguments;
- stdout/stderr paths or captured content policy;
- exit status;
- diagnostics;
- artifacts.

### DiagnosticModel

All validation and runtime failures use `ipcraft.diagnostics.v1`:

```json
{
  "schema": "ipcraft.diagnostics.v1",
  "records": [
    {
      "severity": "error",
      "source": "core",
      "rule_id": "composition.unknown_interface",
      "message": "Interface 'm_axi' does not exist on instance 'ip0'.",
      "locations": [
        {
          "kind": "interface",
          "instance": "ip0",
          "interface": "m_axi"
        }
      ]
    }
  ]
}
```

Location kinds:

- `project`
- `ip_instance`
- `interface`
- `connection`
- `parameter`
- `table`
- `table_cell`
- `document_path`
- `file`
- `artifact`
- `graph_object`

External tools that cannot map precisely must at least map to instance, flow
step, emitted file, or file line.

### ArtifactSpec And ArtifactIndex

Artifacts are declared and collected explicitly:

```json
{
  "id": "rtl",
  "type": "rtl",
  "glob": "rtl/**/*.sv",
  "primary": true,
  "label": "RTL"
}
```

Collected artifact records include:

- path;
- type;
- size;
- modified time;
- checksum when enabled;
- source instance or project flow;
- flow run ID;
- originating spec ID.

Glob behavior must be documented and path-confined to the run directory or
declared output directory.

## Extension And Plugin Boundary

First-party extensions provide declarative capabilities:

- `ipcraft.config.params`
- `ipcraft.config.tables`
- `ipcraft.config.documents`
- `ipcraft.interfaces`
- `ipcraft.composition`
- `ipcraft.layout`
- `ipcraft.emitters`
- `ipcraft.flows`
- `ipcraft.artifacts`
- `ipcraft.diagnostics`
- `ipcraft.views`
- `ipcraft.graph_config`

Plugins are optional escape hatches:

- provide enum values;
- validate config;
- emit config;
- parse diagnostics;
- provide project views;
- migrate instance state;
- post-process artifacts;
- generate wrappers;
- implement custom layout.

The default path is declarative package spec plus templates and flow runner.
Plugins must not be required for ordinary parameter/table/document/flow-based
IP packages.

## Native Escape Hatch

Native/vendor data is allowed only under explicit namespace objects:

```json
{
  "native": {
    "vendor.example": {
      "opaque": true
    }
  }
}
```

Rules:

- namespaces must be stable reverse-DNS or package-owned IDs;
- core preserves data but does not interpret it;
- core may validate namespace shape and JSON size limits;
- native data must not bypass path security or command execution policy;
- hidden tests may mutate native data and expect round-trip preservation.

## Public CLI Contract

The implementation must provide `ipcraft-cli`. Every command supports
machine-readable JSON output. Human text may be printed only when explicitly
requested; default output must be parseable JSON.

All command failures return nonzero exit status and a JSON object containing:

```json
{
  "ok": false,
  "schema": "ipcraft.cli.result.v1",
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  }
}
```

Successful commands return:

```json
{
  "ok": true,
  "schema": "ipcraft.cli.result.v1",
  "result": {},
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  }
}
```

### inspect-project

```text
ipcraft-cli inspect-project <project>
```

Behavior:

- parses `ipcraft.project.v1`;
- validates JSON shape and duplicate IDs;
- does not run package validators or generators;
- reports project summary, instances, composition counts, layout views,
  diagnostics summary, artifact summary, and preserved native namespaces.

### validate-project

```text
ipcraft-cli validate-project <project> --packages <package-root>
```

Behavior:

- loads package specs from the package root or package-root collection;
- validates project/package references;
- validates config bundle values against config schema;
- validates composition shallow rules;
- runs declared validation flows only when requested by package flow policy;
- returns diagnostics with stable locations.

### emit-inputs

```text
ipcraft-cli emit-inputs <project> --instance <id> --out <dir>
```

Behavior:

- resolves the target instance and package;
- emits declared inputs into `<dir>`;
- rejects path traversal and absolute output paths unless contract explicitly
  permits and confines them;
- returns emitted files and input manifest JSON.

### run-flow

```text
ipcraft-cli run-flow <project> --flow <flow-id> --out <dir>
```

Behavior:

- runs a project-level or instance-level flow according to `FlowSpec`;
- writes a run directory under `<dir>`;
- returns flow run state, diagnostics, and artifact index;
- handles step failure with structured diagnostics and partial run metadata.

### migrate-project

```text
ipcraft-cli migrate-project <project> --to ipcraft.project.v1
```

Behavior:

- imports pre-cutover project files only through explicit migration;
- never silently migrates during normal project load;
- preserves opaque old state in native/migration records when possible;
- reports unsupported legacy content as structured diagnostics.

### collect-artifacts

```text
ipcraft-cli collect-artifacts <run-dir> --spec <package-spec>
```

Behavior:

- loads artifact specs from `ipcraft.package.v1`;
- expands declared globs relative to the run directory or declared output root;
- rejects traversal outside allowed roots;
- returns `ArtifactIndex` JSON and diagnostics for missing required artifacts.

## Public API Contract

The CLI may be implemented over C++ or Ruby internals, but the public API should
be stable enough for tests:

- parse project;
- write project deterministically;
- parse package spec;
- validate project against package specs;
- emit inputs;
- run flow;
- parse diagnostics;
- collect artifacts.

The API must not expose Qt widget classes as required dependencies for headless
testing.

## Architecture Documentation Deliverable

Implementation must add:

```text
docs/architecture/v1-core-architecture.md
```

It must include:

- goals and non-goals;
- V1 hard cutover and legacy policy;
- ProjectDocument schema;
- PackageSpec schema;
- ConfigBundle model;
- CompositionModel model;
- LayoutModel model;
- FlowRunner model;
- DiagnosticModel model;
- ArtifactIndex model;
- extension vs plugin boundary;
- native escape hatch;
- migration strategy;
- security model;
- public CLI/API contract;
- testability contract.

The architecture doc is the primary source for third-party black-box audit
agents. It must not rely on internal test names or private fixtures.

## Contract Examples

Implementation must add `examples/contracts/` with at least:

- `simple_parameter_ip`
- `table_config_ip`
- `raw_document_ip`
- `composition_two_ip`
- `clock_fanout_project`
- `failing_validator_project`
- `artifact_collection_project`
- `v1_noc_compat_project`

Each example should contain a README, package spec, project file, expected CLI
commands, and expected high-level JSON result shape. Examples are public
contract material, not just internal test fixtures.

`v1_noc_compat_project` means a contract-level demonstration that the existing
in-repository NoC use case has been expressed in the new `ipcraft.project.v1`
and `ipcraft.package.v1` architecture. It does not mean retaining old
`ipcraft.noc.project.v1` runtime compatibility.

## Security Model

Required security rules:

- Package paths are package-local unless explicitly documented otherwise.
- Relative paths must be normalized and checked against allowed roots.
- `..` traversal outside allowed roots is rejected.
- Absolute paths in package specs are rejected by default.
- Emitters cannot overwrite files outside the requested output directory.
- Artifact globs cannot escape the run/output root.
- Flow `exec` commands are package-local executables or allowlisted framework
  tools.
- CLI diagnostics must not leak arbitrary host filesystem traversal results
  beyond necessary path diagnostics.
- Native state cannot grant command execution or path exceptions.
- JSON input size limits and recursion/depth limits must be documented and
  enforced.

## Testability Contract

Third-party audit agents may test only through:

- architecture docs;
- JSON schema examples;
- package specs;
- project files;
- `ipcraft-cli`;
- stable headless API if published.

The implementation must therefore make these behaviors public and deterministic:

- malformed package spec diagnostics;
- malformed project diagnostics;
- duplicate IDs;
- unknown field preservation or rejection according to namespace;
- explicit migration behavior;
- path traversal rejection;
- missing command diagnostics;
- flow failure diagnostics;
- diagnostic mapping;
- artifact glob behavior;
- composition validation;
- schema round-trip;
- native namespace preservation.

Internal tests may cover smoke and round-trip behavior, but they are not the
source of truth for correctness. Implementation must not hard-code behavior
based on hidden test fixture names, internal test names, or example directory
names.

## Migration Strategy

This is not a runtime compatibility layer. Migration is an explicit command:

```text
ipcraft-cli migrate-project old.fpproj --to ipcraft.project.v1
```

Migration must:

- parse supported pre-cutover project files;
- construct new `ProjectDocument` instances where possible;
- move old graph module parameter state into `ConfigBundle`, `GraphConfig`, or
  `LayoutModel` based on documented mapping;
- move old `x/y/collapsed` editor state to layout/view state;
- preserve old `ipcore_state` under migration/native records when it cannot be
  structurally mapped;
- report unsupported cases as diagnostics.

Normal project loading rejects old schemas and tells users to run migration.

## Existing Package Cutover

The in-repository packages must be converted:

- `ipcores/finepaper-noc`
- `ipcores/ravenoc`
- `ipcores/opennoc`

The cutover must:

- update package specs to `ipcraft.package.v1`;
- replace NoC-specific command input with declared emitters/flows;
- express current NoC graph editing as `graph_config` or configuration views;
- provide new examples under `examples/contracts/`;
- make validate/generate flows pass through `FlowRunner`;
- remove old `ipcraft.noc.project.v1` assumptions from core validators and
  generators.

## Implementation Shape

The implementation plan should be decomposed, but all tasks serve one hard
cutover. Expected major work packages:

1. Public schema and data model.
2. Project reader/writer hard cutover.
3. Package spec parser hard cutover.
4. Composition and layout model.
5. Config emitter and input builder.
6. Flow runner and structured diagnostics.
7. Artifact collection.
8. `ipcraft-cli`.
9. Contract examples.
10. Qt editor command/model integration.
11. Existing package conversion.
12. Architecture documentation.
13. Black-box audit readiness checks.

Each work package must compile and test when landed, but the branch is allowed
to be breaking until the full cutover closes.

## Acceptance Criteria

- `.fpproj` files use `ipcraft.project.v1`.
- Package runtime specs use `ipcraft.package.v1`.
- `ProjectDocument` is the root source of truth.
- `Graph` is no longer required for non-graph IP configuration.
- Existing package use cases are represented in the new architecture.
- `ipcraft-cli` exposes every required command with JSON output.
- Error paths return structured diagnostics.
- `docs/architecture/v1-core-architecture.md` documents the public contract.
- `examples/contracts/` includes all required examples.
- Hidden black-box tests can validate behavior without reading internal tests.
- No implementation branch relies on old `ipcraft.noc.project.v1` as a normal
  runtime path.

## Open Design Decisions For Implementation Plan

- Whether `ipcraft-cli` is implemented in C++ beside Qt core libraries or as a
  Ruby wrapper over shared libraries/scripts.
- Whether JSON Schema files are generated from C++ structs or maintained by
  hand under a `schemas/` directory.
- Whether initial plugin hooks are stubbed as declared-but-unavailable or fully
  implemented in the first cutover.
- Whether the Qt editor is switched in one large integration task or after the
  headless CLI/model path is complete.

These decisions should be resolved in the implementation plan, not by changing
the public architecture contract above.
