# Ipcraft V1 Hard Cutover Core Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current graph-rooted NoC-oriented runtime with the hard-cutover `ipcraft.project.v1` / `ipcraft.package.v1` architecture, including public schemas, headless CLI, config/composition/flow/artifact/diagnostic contracts, contract examples, audit docs, package cutover, and Qt integration.

**Architecture:** `ProjectDocument` becomes the root source of truth. Existing `Graph / Module / Connection` code remains only as an editor implementation detail for graph-config or composition views, not as the project schema. Runtime package loading consumes normalized `ipcraft.package.v1` only; `ipcore.yml` remains authoring/specgen input outside runtime. The first audit surface is headless model + schemas + `ipcraft-cli` + examples, and Qt must conform to that model.

**Tech Stack:** C++23, Qt Core/Widgets JSON APIs, xmake targets under `qt/`, Ruby Minitest for `spec_generator` and generator packages, JSON Schema files under `schemas/`, public examples under `examples/contracts/`.

---

## Scope And Execution Rules

- This is a pre-1.0 hard cutover. Do not retain the old `ipcraft.noc.project.v1` as a normal runtime path.
- Migration is explicit through `ipcraft-cli migrate-project`; normal project loading rejects old schemas.
- `inspect-project` and default `validate-project` are static and side-effect free. They must not run external validators, generators, flow steps, or process-spawning plugins.
- Every command-line failure returns `ipcraft.cli.result.v1` JSON with `ipcraft.diagnostics.v1` records.
- Public behavior is the implementation target. Local test names, C++ type names, and helper function names in this plan are suggested implementation scaffolding unless a name appears in a public schema, CLI command, diagnostic `rule_id`, file path, or documented API contract.
- JSON schemas use strict top-level object validation. Forward compatibility is provided through documented `native`, `preserved`, `metadata`, or extension-owned namespaces, not blanket top-level `additionalProperties: true`.
- Use `xmake -P qt build <target>` and `xmake -P qt run <target>` for Qt targets.
- Use `ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb` for specgen tests.
- Commit after every task. Do not touch unrelated untracked files such as `.codex` or `image.png`.

## File Structure

Public schemas and docs:

- Create `schemas/ipcraft.project.v1.schema.json`: project root schema.
- Create `schemas/ipcraft.package.v1.schema.json`: normalized runtime package schema.
- Create `schemas/ipcraft.diagnostics.v1.schema.json`: diagnostics schema.
- Create `schemas/ipcraft.graph-config.v1.schema.json`: optional graph-config schema.
- Create `schemas/ipcraft.emitted-inputs.v1.schema.json`: emitted-inputs manifest schema.
- Create `schemas/ipcraft.cli.result.v1.schema.json`: CLI result envelope schema.
- Create `docs/architecture/v1-core-architecture.md`: public architecture contract.
- Create `docs/audit/black-box-audit-guide.md`: audit workflow.
- Create `docs/audit/coverage-matrix.md`: contract-to-test matrix.
- Create `docs/audit/failure-report-format.md`: failure summary format.
- Create `docs/audit/rule-id-catalog.md`: stable diagnostic `rule_id` catalog.

Qt/headless core:

- Create `qt/inc/ipcraft/schemaids.h`: canonical schema string constants.
- Create `qt/inc/ipcraft/jsonhelpers.h` and `qt/src/ipcraft/jsonhelpers.cpp`: deterministic JSON writer helpers, duplicate ID helpers, realpath-based path confinement helpers, diagnostic JSON helpers.
- Create `qt/inc/ipcraft/diagnostics.h` and `qt/src/ipcraft/diagnostics.cpp`: `Diagnostic`, `DiagnosticLocation`, `DiagnosticStore`, JSON round-trip.
- Create `qt/inc/ipcraft/value.h` and `qt/src/ipcraft/value.cpp`: value type checks, parameter type checks, condition expression evaluator.
- Replace `qt/inc/project/projectdocument.h` and `qt/src/project/projectreader.cpp` / `projectwriter.cpp` with `ipcraft.project.v1` load/write behavior.
- Replace `qt/inc/project/ipinstancestate.h` with `IpInstanceState`, `PackageRef`, `ConfigBundle`, `GraphConfig`, `NativeState`, `FlowRunState`, `InstanceViewState`.
- Create `qt/inc/ipcraft/configschema.h` and `qt/src/ipcraft/configschema.cpp`: config schema/bundle validation.
- Create `qt/inc/ipcraft/packagespec.h` and `qt/src/ipcraft/packagespec.cpp`: package spec model and parser.
- Create `qt/inc/ipcraft/compositionmodel.h` and `qt/src/ipcraft/compositionmodel.cpp`: composition model and shallow validation.
- Create `qt/inc/ipcraft/layoutmodel.h` and `qt/src/ipcraft/layoutmodel.cpp`: layout model JSON round-trip.
- Create `qt/inc/ipcraft/artifactmodel.h` and `qt/src/ipcraft/artifactmodel.cpp`: artifact spec/index and glob confinement.
- Create `qt/inc/ipcraft/emitter.h` and `qt/src/ipcraft/emitter.cpp`: `PackageInputBuilder` and emitted-inputs manifest.
- Create `qt/inc/ipcraft/flowrunner.h` and `qt/src/ipcraft/flowrunner.cpp`: flow runner, process policy, flow run state.

Runtime integration:

- Replace `qt/inc/ipcraft/ipcraftmanifest.h`, `qt/src/ipcraft/ipcraftmanifest.cpp`, `qt/inc/ipcraft/ipcraftmanifestreader.h`, and `qt/src/ipcraft/ipcraftmanifestreader.cpp` with `ipcraft.package.v1` data and parser.
- Modify `qt/inc/ipcraft/ipcraftregistry.h` and `qt/src/ipcraft/ipcraftregistry.cpp` to load package specs from package roots without `ipcore.yml`.
- Modify `qt/inc/modules/moduleregistry.h` and `qt/src/modules/moduleregistry.cpp` to become a graph-editor view adapter over `PackageSpec`, not the package source of truth.
- Modify `qt/inc/project/graphprojectserializer.h` and `qt/src/project/graphprojectserializer.cpp` into a graph-config/layout adapter. It must not write `ipcraft.project.v1` through legacy graph records.
- Modify `qt/inc/connection/connectionruleservice.h` and `qt/src/connection/connectionruleservice.cpp` to delegate shallow project composition checks to `CompositionValidator`.
- Modify `qt/inc/validation/projectvalidationrunner.h`, `qt/src/validation/projectvalidationrunner.cpp`, `qt/inc/validation/drcrunner.h`, and `qt/src/validation/drcrunner.cpp` so legacy external DRC invocation is removed from default validation and moved behind `FlowRunner`.
- Modify `qt/inc/app/projectgenerationrunner.h` and `qt/src/app/projectgenerationrunner.cpp` so generation goes through `FlowRunner`.

CLI:

- Create `qt/inc/cli/cliresult.h` and `qt/src/cli/cliresult.cpp`: `ipcraft.cli.result.v1` output helpers.
- Create `qt/src/cli/ipcraft_cli.cpp`: command dispatcher for `inspect-project`, `validate-project`, `emit-inputs`, `run-flow`, `migrate-project`, and `collect-artifacts`.
- Modify `qt/xmake.lua`: add `ipcraft-cli` target and new test targets.

Tests:

- Create `qt/test/ipcraft_diagnostics_test.cpp`.
- Create `qt/test/ipcraft_project_model_test.cpp`.
- Create `qt/test/ipcraft_package_spec_test.cpp`.
- Create `qt/test/ipcraft_config_validation_test.cpp`.
- Create `qt/test/ipcraft_composition_test.cpp`.
- Create `qt/test/ipcraft_emitter_test.cpp`.
- Create `qt/test/ipcraft_flowrunner_test.cpp`.
- Create `qt/test/ipcraft_artifact_test.cpp`.
- Create `qt/test/ipcraft_cli_contract_test.cpp`.
- Create `qt/test/ipcraft_migration_test.cpp`.
- Replace or retire graph-only expectations in existing tests after equivalent headless model coverage exists.

Examples:

- Create `examples/contracts/simple_parameter_ip/`.
- Create `examples/contracts/table_config_ip/`.
- Create `examples/contracts/raw_document_ip/`.
- Create `examples/contracts/composition_two_ip/`.
- Create `examples/contracts/clock_fanout_project/`.
- Create `examples/contracts/failing_validator_project/`.
- Create `examples/contracts/artifact_collection_project/`.
- Create `examples/contracts/noc_cutover_project/`.
- Create `examples/contracts/negative_malformed_package/`.
- Create `examples/contracts/negative_extension_required/`.
- Create `examples/contracts/negative_path_escape/`.
- Create `examples/contracts/negative_flow_missing_executable/`.

## Task 1: Public Schemas And Diagnostic Foundation

**Files:**
- Create: `schemas/ipcraft.diagnostics.v1.schema.json`
- Create: `schemas/ipcraft.project.v1.schema.json`
- Create: `schemas/ipcraft.package.v1.schema.json`
- Create: `schemas/ipcraft.graph-config.v1.schema.json`
- Create: `schemas/ipcraft.emitted-inputs.v1.schema.json`
- Create: `schemas/ipcraft.cli.result.v1.schema.json`
- Create: `qt/inc/ipcraft/schemaids.h`
- Create: `qt/inc/ipcraft/jsonhelpers.h`
- Create: `qt/src/ipcraft/jsonhelpers.cpp`
- Create: `qt/inc/ipcraft/diagnostics.h`
- Create: `qt/src/ipcraft/diagnostics.cpp`
- Create: `qt/test/ipcraft_diagnostics_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing diagnostic JSON round-trip test**

Create `qt/test/ipcraft_diagnostics_test.cpp` with a local smoke test covering this public behavior:

```cpp
#include "ipcraft/diagnostics.h"
#include "ipcraft/schemaids.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void testDiagnosticRoundTripKeepsStableFields() {
    Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("core");
    diagnostic.ruleId = QStringLiteral("composition.unknown_interface");
    diagnostic.category = QStringLiteral("composition");
    diagnostic.message = QStringLiteral("Human text may change");
    diagnostic.details.insert(QStringLiteral("interface"), QStringLiteral("m_axi"));

    DiagnosticLocation location;
    location.kind = QStringLiteral("interface");
    location.instanceId = QStringLiteral("ip0");
    location.interfaceId = QStringLiteral("m_axi");
    diagnostic.locations.append(location);

    DiagnosticStore store;
    store.records.append(diagnostic);

    const QJsonObject json = store.toJson();
    require(json.value(QStringLiteral("schema")).toString() == IpcraftSchemaIds::diagnosticsV1,
            "diagnostics schema must be ipcraft.diagnostics.v1");
    require(json.value(QStringLiteral("records")).toArray().size() == 1,
            "diagnostic record should serialize");

    const DiagnosticStore parsed = DiagnosticStore::fromJson(json);
    require(parsed.records.size() == 1, "diagnostic record should parse");
    require(parsed.records.first().ruleId == QStringLiteral("composition.unknown_interface"),
            "rule_id must round-trip");
    require(parsed.records.first().locations.first().kind == QStringLiteral("interface"),
            "location kind must round-trip");
}

int main() {
    testDiagnosticRoundTripKeepsStableFields();
    std::cout << "ipcraft_diagnostics_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target and verify it fails**

Add an `ipcraft_diagnostics_test` target to `qt/xmake.lua` using `qt.console`, `qt/test/ipcraft_diagnostics_test.cpp`, `src/ipcraft/diagnostics.cpp`, `src/ipcraft/jsonhelpers.cpp`, and the two new headers.

Run:

```bash
xmake -P qt build ipcraft_diagnostics_test
```

Expected: FAIL because the new headers and implementation do not exist.

- [ ] **Step 3: Implement schema constants, deterministic JSON helpers, and diagnostics**

Implement `qt/inc/ipcraft/schemaids.h`:

```cpp
#pragma once

#include <QString>

namespace IpcraftSchemaIds {
inline const QString projectV1 = QStringLiteral("ipcraft.project.v1");
inline const QString packageV1 = QStringLiteral("ipcraft.package.v1");
inline const QString diagnosticsV1 = QStringLiteral("ipcraft.diagnostics.v1");
inline const QString graphConfigV1 = QStringLiteral("ipcraft.graph-config.v1");
inline const QString emittedInputsV1 = QStringLiteral("ipcraft.emitted-inputs.v1");
inline const QString cliResultV1 = QStringLiteral("ipcraft.cli.result.v1");
}
```

Implement diagnostics model that serializes/deserializes the public schema fields. Internal structs may differ, but public JSON output must conform to `ipcraft.diagnostics.v1`.

Implement deterministic JSON writer helpers with these public-observable rules:

- object keys are emitted in sorted order for CLI and file writer output;
- schema-defined arrays preserve input/model order unless the contract says to sort;
- diagnostic records are sorted by `severity`, `source`, `rule_id`, then first location for deterministic CLI output;
- emitted-input manifest files are sorted by `kind`, `id`, then `path`;
- output uses UTF-8 and a stable newline policy;
- JSON semantic validation must not depend on object key order.

```cpp
struct DiagnosticLocation {
    QString kind;
    QString instanceId;
    QString interfaceId;
    QString connectionId;
    QString parameterId;
    QString tableId;
    int row = -1;
    QString column;
    QString documentId;
    QString path;
    QString file;
    int line = -1;
    int columnNumber = -1;
    QString artifactId;
    QString graphObjectId;
    QJsonObject details;

    QJsonObject toJson() const;
    static DiagnosticLocation fromJson(const QJsonObject& object);
};

struct Diagnostic {
    QString severity = QStringLiteral("error");
    QString source;
    QString ruleId;
    QString category;
    QString message;
    QJsonObject details;
    QVector<DiagnosticLocation> locations;

    QJsonObject toJson() const;
    static Diagnostic fromJson(const QJsonObject& object);
};

struct DiagnosticStore {
    QVector<Diagnostic> records;

    QJsonObject toJson() const;
    static DiagnosticStore fromJson(const QJsonObject& object);
};
```

- [ ] **Step 4: Add JSON Schema files with strict top-level policy**

Create each schema file with `$schema`, `$id`, `type: "object"`, required `schema`, and strict top-level fields. Do not use blanket top-level `additionalProperties: true`. Forward compatibility is handled only through documented `native`, `preserved`, `metadata`, `details`, extension-owned sections, and package/vendor namespaces.

Required schema policy:

- `ipcraft.project.v1` uses top-level `project`, `instances`, `composition`, `layout`, `diagnostics`, `artifacts`, `migration`, and `native`.
- `project.id` and `project.name` are the canonical project identity fields; top-level `id` and `name` are not valid V1 project identity fields.
- `ipcraft.package.v1` allows optional capability sections only when the matching extension is declared.
- `ipcraft.diagnostics.v1` requires `records[].severity`, `records[].source`, `records[].rule_id`, `records[].message`, and `records[].locations`.
- `ipcraft.cli.result.v1` requires `ok`, `schema`, `diagnostics`, and either `result` on success or structured diagnostics on failure.
- Unknown top-level fields produce schema diagnostics unless they are explicitly documented extension/native escape hatches.

- [ ] **Step 5: Verify diagnostics test passes**

Run:

```bash
xmake -P qt build ipcraft_diagnostics_test
xmake -P qt run ipcraft_diagnostics_test
```

Expected: build succeeds and run prints `ipcraft_diagnostics_test passed`.

- [ ] **Step 6: Commit**

```bash
git add schemas/ipcraft.diagnostics.v1.schema.json schemas/ipcraft.project.v1.schema.json schemas/ipcraft.package.v1.schema.json schemas/ipcraft.graph-config.v1.schema.json schemas/ipcraft.emitted-inputs.v1.schema.json schemas/ipcraft.cli.result.v1.schema.json qt/inc/ipcraft/schemaids.h qt/inc/ipcraft/jsonhelpers.h qt/src/ipcraft/jsonhelpers.cpp qt/inc/ipcraft/diagnostics.h qt/src/ipcraft/diagnostics.cpp qt/test/ipcraft_diagnostics_test.cpp qt/xmake.lua
git commit -m "feat: add ipcraft v1 schema and diagnostics foundation"
```

## Task 2: ProjectDocument V1 Root Model

**Files:**
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/inc/project/ipinstancestate.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Create: `qt/test/ipcraft_project_model_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing project round-trip tests**

Create `qt/test/ipcraft_project_model_test.cpp` with local smoke tests covering:

```cpp
void testProjectDocumentRoundTripsInstancesCompositionLayoutNative();
void testProjectReaderRejectsUnsupportedSchema();
void testProjectReaderRejectsDuplicateInstanceIds();
void testProjectWriterUsesDeterministicJson();
```

The first test must build a `QJsonObject` containing:

```json
{
  "schema": "ipcraft.project.v1",
  "project": {
    "id": "project_0",
    "name": "Contract Project"
  },
  "instances": [
    {
      "id": "ip0",
      "display_name": "IP 0",
      "package": { "id": "vendor.example.simple", "version": "1.0.0" },
      "config": { "parameters": { "width": 64 } },
      "native": { "vendor.example": { "opaque": true } }
    }
  ],
  "composition": { "connections": [], "external_ports": [] },
  "layout": { "views": [] },
  "native": { "vendor.example": { "project": true } }
}
```

Assert read/write preserves `project.id`, `project.name`, instance IDs, package refs, config parameters, composition, layout, and native objects.

- [ ] **Step 2: Add the test target and verify it fails**

Run:

```bash
xmake -P qt build ipcraft_project_model_test
```

Expected: FAIL because the current `ProjectDocument` still uses the old `.fpproj` graph fields and schema strings.

- [ ] **Step 3: Replace project DTOs with public V1 root behavior**

Internal DTO names and container types may differ. The public reader/writer must expose this JSON contract:

```json
{
  "schema": "ipcraft.project.v1",
  "project": {
    "id": "project_0",
    "name": "Contract Project"
  },
  "instances": [],
  "composition": {
    "connections": [],
    "external_ports": []
  },
  "layout": {
    "views": []
  },
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  },
  "artifacts": {},
  "migration": {},
  "native": {}
}
```

Public model behavior:

- `project.id` and `project.name` are required.
- top-level `id` and `name` are invalid for V1 project identity;
- `instances[].id` is unique within a project;
- `instances[].package.id` and `instances[].package.version` are required;
- each instance owns `config`, optional `graph_config`, `native`, `last_runs`, `artifacts`, `diagnostics`, and `view`;
- unknown vendor data is preserved only under `native` or documented `preserved` fields;
- `layout` stores editor state and canvas coordinates; config parameters do not store new `x/y` layout state.

- [ ] **Step 4: Implement strict project reader/writer behavior**

`ProjectReader` must:

- accept only `schema == "ipcraft.project.v1"` for normal load;
- return diagnostic `project.unsupported_schema` for other schema values;
- detect duplicate `instances[].id` and return diagnostic `project.duplicate_id`;
- preserve unknown fields under documented `native` namespaces;
- not silently migrate old project schemas.

`ProjectWriter` must:

- write `schema`, `project`, `instances`, `composition`, `layout`, `diagnostics`, `artifacts`, `migration`, and `native`;
- produce stable array ordering in existing in-memory order;
- use stable key ordering helper for generated JSON output.

- [ ] **Step 5: Verify project tests pass**

Run:

```bash
xmake -P qt build ipcraft_project_model_test
xmake -P qt run ipcraft_project_model_test
```

Expected: run prints `ipcraft_project_model_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/project/projectdocument.h qt/inc/project/ipinstancestate.h qt/src/project/projectreader.cpp qt/src/project/projectwriter.cpp qt/test/ipcraft_project_model_test.cpp qt/xmake.lua
git commit -m "feat: make project document the ipcraft v1 root model"
```

## Task 3: PackageSpec Parser And Extension Enforcement

**Files:**
- Replace: `qt/inc/ipcraft/ipcraftmanifest.h`
- Replace: `qt/src/ipcraft/ipcraftmanifest.cpp`
- Replace: `qt/inc/ipcraft/ipcraftmanifestreader.h`
- Replace: `qt/src/ipcraft/ipcraftmanifestreader.cpp`
- Create: `qt/inc/ipcraft/packagespec.h`
- Create: `qt/src/ipcraft/packagespec.cpp`
- Create: `qt/test/ipcraft_package_spec_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing package parser tests**

Create local smoke tests covering:

```cpp
void testRuntimeLoadsPackageV1WithoutIpcoreYml();
void testOptionalSectionRequiresExplicitExtension();
void testConnectionRulesParseAliasesAndCompatibility();
void testPluginMetadataIsSeparateFromExtensions();
void testPackageParserRejectsPathTraversal();
void testPackageResolverRequiresExactVersion();
void testPackageResolverRejectsAmbiguousPackageRoots();
```

`testOptionalSectionRequiresExplicitExtension` must parse a package with `config_schema.tables` and no `ipcraft.config.tables` extension, then assert diagnostic:

```json
{
  "severity": "error",
  "source": "package.parser",
  "rule_id": "package.extension_required",
  "locations": [{ "kind": "document_path", "path": "$.config_schema.tables" }]
}
```

- [ ] **Step 2: Verify package tests fail**

Run:

```bash
xmake -P qt build ipcraft_package_spec_test
```

Expected: FAIL because `ipcraft.package.v1` parsing and extension enforcement do not exist.

- [ ] **Step 3: Implement public `PackageSpec` behavior**

Internal structs may differ. Public parsing must expose these package sections:

```json
{
  "schema": "ipcraft.package.v1",
  "id": "vendor.example.simple",
  "version": "1.0.0",
  "name": "Simple IP",
  "extensions": [],
  "config_schema": {},
  "interfaces": [],
  "connection_rules": {},
  "emitters": [],
  "flows": [],
  "artifacts": [],
  "diagnostics": {},
  "views": {},
  "plugin": null,
  "native_schema": {}
}
```

Public parser behavior:

- `extensions` and `plugin` are separate concepts;
- interface specs expose `id`, `kind`, `protocol`, `role`, `direction`, `required`, `fanout`, and shallow properties;
- `connection_rules` exposes `protocol_aliases`, optional `kind_aliases`, and compatibility entries;
- emitters, flows, artifacts, diagnostics, and views are preserved as declared package capability contracts.

- [ ] **Step 4: Enforce runtime authoring boundary**

The loader must:

- read `ipcraft.json` as normalized `ipcraft.package.v1`;
- never require `ipcore.yml`;
- reject `schema != "ipcraft.package.v1"` with `package.unsupported_schema`;
- reject absolute package-local paths and `..` traversal with `package.path_escape`;
- resolve every package-local path through canonical realpath checks; symlinks that escape the package root are rejected with `package.path_escape`;
- keep `extensions` and `plugin` separate.

- [ ] **Step 5: Define package root and exact version resolution**

Package resolution rules:

- `--packages <package-root>` may point to one package root containing `ipcraft.json` or to a collection root containing package directories.
- Runtime discovery scans one directory level below a collection root for package-local `ipcraft.json`.
- A project instance reference `{ "package": { "id": "...", "version": "..." } }` resolves only to an exact `id` and exact `version` match.
- Multiple package specs with the same `{id, version}` in the same resolution set produce `package.duplicate_version`.
- Missing exact version produces `package.version_not_found`.
- Missing package ID produces `package.not_found`.
- Runtime resolution never falls back to latest, nearest, compatible, or authoring `ipcore.yml`.

- [ ] **Step 6: Enforce optional extension mapping**

Implement the exact section mapping from the spec:

```cpp
config_schema.parameters -> ipcraft.config.params
config_schema.tables -> ipcraft.config.tables
config_schema.documents -> ipcraft.config.documents
config_schema.files -> ipcraft.config.files
interfaces -> ipcraft.interfaces
connection_rules -> ipcraft.composition
emitters -> ipcraft.emitters
flows -> ipcraft.flows
artifacts -> ipcraft.artifacts
diagnostics -> ipcraft.diagnostics
views -> ipcraft.views
graph_config -> ipcraft.graph_config
```

Missing extension emits `package.extension_required` and rejects the package.

- [ ] **Step 7: Verify package parser tests pass**

Run:

```bash
xmake -P qt build ipcraft_package_spec_test
xmake -P qt run ipcraft_package_spec_test
```

Expected: run prints `ipcraft_package_spec_test passed`.

- [ ] **Step 8: Commit**

```bash
git add qt/inc/ipcraft/ipcraftmanifest.h qt/src/ipcraft/ipcraftmanifest.cpp qt/inc/ipcraft/ipcraftmanifestreader.h qt/src/ipcraft/ipcraftmanifestreader.cpp qt/inc/ipcraft/packagespec.h qt/src/ipcraft/packagespec.cpp qt/test/ipcraft_package_spec_test.cpp qt/xmake.lua
git commit -m "feat: parse ipcraft package v1 specs"
```

## Task 4: ConfigSchema, ConfigBundle, And Expression Validation

**Files:**
- Create: `qt/inc/ipcraft/value.h`
- Create: `qt/src/ipcraft/value.cpp`
- Create: `qt/inc/ipcraft/configschema.h`
- Create: `qt/src/ipcraft/configschema.cpp`
- Create: `qt/test/ipcraft_config_validation_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing config validation tests**

Create local smoke tests covering:

```cpp
void testParameterTypesMapToJsonValues();
void testEnumDeclarationMustChooseStringOrInt64();
void testSimpleExpressionsEvaluateWithoutScripts();
void testExpressionRejectsFileProcessNetworkAndEnvironmentAccess();
void testTableAndDocumentUnknownFieldsPreserveWhenDeclared();
void testPathParametersRejectTraversal();
```

The unsupported expression test must feed JSON AST objects using operations such as `exec`, `env`, `read_file`, and `python`, and assert diagnostic `config.expression_unsupported`.

- [ ] **Step 2: Verify config tests fail**

Run:

```bash
xmake -P qt build ipcraft_config_validation_test
```

Expected: FAIL because config schema validation is not implemented.

- [ ] **Step 3: Implement value and JSON AST expression evaluator**

V1 expression fields use JSON AST objects, not arbitrary script strings. Support only:

```json
{ "op": "eq", "left": { "param": "id" }, "right": { "literal": 1 } }
{ "op": "ne", "left": { "param": "id" }, "right": { "literal": "x" } }
{ "op": "exists", "param": "id" }
{ "op": "and", "args": [ { "literal": true }, { "op": "exists", "param": "id" } ] }
{ "op": "or", "args": [ { "literal": false }, { "op": "exists", "param": "id" } ] }
{ "op": "not", "arg": { "op": "exists", "param": "id" } }
{ "literal": true }
{ "literal": false }
```

Rejected forms:

```json
{ "op": "exec", "args": ["ls"] }
{ "op": "env", "name": "HOME" }
{ "op": "read_file", "path": "x" }
{ "op": "python", "code": "1+1" }
```

Implement rejected expression diagnostic:

```cpp
Diagnostic unsupportedExpression(QString path) {
    Diagnostic d;
    d.severity = QStringLiteral("error");
    d.source = QStringLiteral("core");
    d.ruleId = QStringLiteral("config.expression_unsupported");
    d.category = QStringLiteral("config");
    d.message = QStringLiteral("Expression form is not supported in V1");
    d.locations.append(DiagnosticLocation{.kind = QStringLiteral("document_path"), .path = path});
    return d;
}
```

- [ ] **Step 4: Implement schema/bundle validator**

Validation must produce these stable rule IDs:

```text
config.required_missing
config.type_mismatch
config.enum_invalid
config.range_invalid
config.path_escape
config.table_column_missing
config.document_format_invalid
config.file_extension_invalid
```

- [ ] **Step 5: Verify config tests pass**

Run:

```bash
xmake -P qt build ipcraft_config_validation_test
xmake -P qt run ipcraft_config_validation_test
```

Expected: run prints `ipcraft_config_validation_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/value.h qt/src/ipcraft/value.cpp qt/inc/ipcraft/configschema.h qt/src/ipcraft/configschema.cpp qt/test/ipcraft_config_validation_test.cpp qt/xmake.lua
git commit -m "feat: validate ipcraft config bundles"
```

## Task 5: CompositionModel, LayoutModel, And GraphConfig

**Files:**
- Create: `qt/inc/ipcraft/compositionmodel.h`
- Create: `qt/src/ipcraft/compositionmodel.cpp`
- Create: `qt/inc/ipcraft/layoutmodel.h`
- Create: `qt/src/ipcraft/layoutmodel.cpp`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Create: `qt/test/ipcraft_composition_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing composition tests**

Create local smoke tests covering:

```cpp
void testCompositionRejectsUnknownInstance();
void testCompositionRejectsUnknownInterface();
void testCompositionAllowsClockFanoutWithOneSource();
void testCompositionRejectsClockFanoutWithTwoSources();
void testProtocolAliasesNormalizeBeforeCompatibility();
void testLayoutStoresCanvasCoordinatesOutsideConfig();
void testGraphConfigUsesNaryRelationshipsNotPortRefs();
```

Expected stable diagnostics:

```text
composition.unknown_instance
composition.unknown_interface
composition.required_interface_unconnected
composition.multiply_driven_input
composition.clock_reset_source_count
composition.incompatible_endpoint
graph_config.duplicate_object
graph_config.unknown_endpoint_object
```

- [ ] **Step 2: Verify composition tests fail**

Run:

```bash
xmake -P qt build ipcraft_composition_test
```

Expected: FAIL because composition and graph-config validators do not exist.

- [ ] **Step 3: Implement composition public JSON behavior**

Internal structs may differ. Public composition JSON must expose:

```json
{
  "connections": [
    {
      "id": "conn0",
      "type": "interface",
      "endpoints": [
        {
          "instance": "ip0",
          "interface": "m_axi",
          "role": "master"
        },
        {
          "instance": "ip1",
          "interface": "s_axi",
          "role": "slave"
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

- [ ] **Step 4: Implement shallow validator**

Validation must:

- check instance and interface existence;
- check required interfaces connected;
- apply package `protocol_aliases` and `kind_aliases`;
- apply compatibility rules with `binary` and `fanout` arity;
- reject multiply driven input/sink endpoints unless fanout allows;
- require clock/reset fanout to have exactly one source;
- avoid AXI/CHI/APB/DDR/SerDes deep protocol checks.

- [ ] **Step 5: Implement layout and graph-config round-trip**

Layout JSON stores:

```json
{
  "views": [
    {
      "id": "main",
      "canvas": {
        "nodes": { "ip0": { "x": 10, "y": 20 } },
        "connections": {},
        "zoom": 1.0,
        "pan": { "x": 0, "y": 0 }
      }
    }
  ]
}
```

Graph-config JSON stores objects and relationships exactly as `ipcraft.graph-config.v1`; it must not use old `source PortRef -> target PortRef` as schema.

- [ ] **Step 6: Verify composition tests pass**

Run:

```bash
xmake -P qt build ipcraft_composition_test
xmake -P qt run ipcraft_composition_test
```

Expected: run prints `ipcraft_composition_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/ipcraft/compositionmodel.h qt/src/ipcraft/compositionmodel.cpp qt/inc/ipcraft/layoutmodel.h qt/src/ipcraft/layoutmodel.cpp qt/src/connection/connectionruleservice.cpp qt/src/project/graphprojectserializer.cpp qt/test/ipcraft_composition_test.cpp qt/xmake.lua
git commit -m "feat: add project composition and layout models"
```

## Task 6: PackageInputBuilder And Emitted Inputs Manifest

**Files:**
- Create: `qt/inc/ipcraft/emitter.h`
- Create: `qt/src/ipcraft/emitter.cpp`
- Create: `qt/test/ipcraft_emitter_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing emitter tests**

Create local smoke tests covering:

```cpp
void testEmitParametersWritesDeterministicJson();
void testEmitConfigDocumentRejectsAbsoluteOutputPath();
void testEmitConfigDocumentRejectsTraversal();
void testEmitCompositionWritesManifestEntry();
void testManifestFilesAreRelativeAndDeterministic();
void testPartialEmitFailureStillReturnsDiagnosticsManifest();
```

Path security diagnostics:

```text
emitter.path_absolute
emitter.path_escape
emitter.write_failed
```

- [ ] **Step 2: Verify emitter tests fail**

Run:

```bash
xmake -P qt build ipcraft_emitter_test
```

Expected: FAIL because `PackageInputBuilder` does not exist.

- [ ] **Step 3: Implement emitted inputs manifest**

`PackageInputBuilder::emitInputs` must return public JSON matching:

```json
{
  "schema": "ipcraft.emitted-inputs.v1",
  "project": "project_0",
  "instance": "ip0",
  "package": {
    "id": "vendor.example.simple",
    "version": "1.0.0"
  },
  "run_id": "run0",
  "files": [],
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  }
}
```

Manifest file paths are relative only. File records are deterministic by `kind`, `id`, then `path`.

- [ ] **Step 4: Implement emitter actions**

Support V1 emitter kinds:

```text
emit_config_document
emit_parameters
emit_table
template
copy_file
emit_composition
emit_graph_config
plugin_hook
```

For `plugin_hook`, return diagnostic `emitter.plugin_unavailable` until plugin hooks are implemented through the declared plugin boundary.

- [ ] **Step 5: Verify emitter tests pass**

Run:

```bash
xmake -P qt build ipcraft_emitter_test
xmake -P qt run ipcraft_emitter_test
```

Expected: run prints `ipcraft_emitter_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/emitter.h qt/src/ipcraft/emitter.cpp qt/test/ipcraft_emitter_test.cpp qt/xmake.lua
git commit -m "feat: emit package inputs with manifest"
```

## Task 7: FlowRunner, Process Security, And Artifact Collection

**Files:**
- Create: `qt/inc/ipcraft/flowrunner.h`
- Create: `qt/src/ipcraft/flowrunner.cpp`
- Create: `qt/inc/ipcraft/artifactmodel.h`
- Create: `qt/src/ipcraft/artifactmodel.cpp`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/src/validation/projectvalidationrunner.cpp`
- Create: `qt/test/ipcraft_flowrunner_test.cpp`
- Create: `qt/test/ipcraft_artifact_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing flow tests**

Create local smoke tests covering:

```cpp
void testExecMissingExecutableReturnsDiagnostic();
void testExecNonzeroExitCapturesStdoutStderr();
void testExecTimeoutReturnsDiagnosticAndAttemptsCleanup();
void testExecRejectsNativeCommandPolicyOverride();
void testRunFlowUsesRunDirectoryCwdByDefault();
void testValidateProjectDoesNotRunFlow();
void testStdoutStderrCaptureTruncationReturnsDiagnostic();
```

Expected diagnostics:

```text
flow.executable_missing
flow.exec_failed
flow.timeout
flow.command_policy_violation
flow.output_truncated
```

- [ ] **Step 2: Write failing artifact tests**

Create local smoke tests covering:

```cpp
void testArtifactGlobCollectsInsideRunRoot();
void testArtifactGlobRejectsTraversal();
void testArtifactGlobRejectsSymlinkEscape();
void testRequiredArtifactMissingReturnsDiagnostic();
void testArtifactIndexRecordsTypeSizeModifiedTime();
```

Expected diagnostics:

```text
artifact.glob_escape
artifact.required_missing
```

- [ ] **Step 3: Verify flow and artifact tests fail**

Run:

```bash
xmake -P qt build ipcraft_flowrunner_test
xmake -P qt build ipcraft_artifact_test
```

Expected: FAIL because flow runner and artifact collection do not exist.

- [ ] **Step 4: Implement `FlowRunner`**

Implement process defaults:

- `cwd == run_dir` when omitted;
- package-local executable paths resolve relative to package root;
- package-local executable, cwd, stdout/stderr capture files, emitted inputs, and artifact paths are confined by canonical realpath checks after symlink resolution;
- framework tools come only from application policy allowlist;
- environment starts from sanitized base plus declared allowlist;
- `timeout_ms` uses declared value or fixed default `60000`;
- stdout/stderr capture paths are under run directory;
- capture limit defaults to `1048576` bytes;
- stdout/stderr exceeding the capture limit is truncated deterministically and produces `flow.output_truncated` with stream name, limit, and capture file location;
- nonzero exit, timeout, and missing executable produce structured diagnostics;
- parallel flow execution is disabled.

- [ ] **Step 5: Implement artifact collector**

Artifact collector must:

- expand globs under run directory or declared output root only;
- reject `..`, absolute glob escape, and symlink escape after canonical realpath resolution;
- sort records by `type`, `id`, then relative path;
- include `path`, `type`, `size`, `modified_time`, `source_instance`, and `flow_run_id`.

- [ ] **Step 6: Verify flow and artifact tests pass**

Run:

```bash
xmake -P qt build ipcraft_flowrunner_test
xmake -P qt run ipcraft_flowrunner_test
xmake -P qt build ipcraft_artifact_test
xmake -P qt run ipcraft_artifact_test
```

Expected: both runs print their `passed` lines.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/ipcraft/flowrunner.h qt/src/ipcraft/flowrunner.cpp qt/inc/ipcraft/artifactmodel.h qt/src/ipcraft/artifactmodel.cpp qt/src/app/projectgenerationrunner.cpp qt/src/validation/projectvalidationrunner.cpp qt/test/ipcraft_flowrunner_test.cpp qt/test/ipcraft_artifact_test.cpp qt/xmake.lua
git commit -m "feat: run ipcraft flows and collect artifacts"
```

## Task 8: Public CLI Contract

**Files:**
- Create: `qt/inc/cli/cliresult.h`
- Create: `qt/src/cli/cliresult.cpp`
- Create: `qt/src/cli/ipcraft_cli.cpp`
- Create: `qt/test/ipcraft_cli_contract_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing CLI contract tests**

Create tests that spawn the built CLI and assert:

```cpp
void testInspectProjectReturnsJsonResult();
void testValidateProjectIsStaticAndDoesNotCreateRunFiles();
void testEmitInputsReturnsManifest();
void testRunFlowReportsMissingExecutable();
void testRunFlowRequiresInstanceOrAllInstancesForInstanceScopedFlow();
void testCollectArtifactsReturnsArtifactIndex();
void testMigrateProjectRequiresExplicitTarget();
void testMigrateProjectReturnsProjectUnderResultProject();
```

Every failure test must assert:

```json
{
  "ok": false,
  "schema": "ipcraft.cli.result.v1",
  "diagnostics": { "schema": "ipcraft.diagnostics.v1", "records": [] }
}
```

- [ ] **Step 2: Add CLI target and verify tests fail**

Run:

```bash
xmake -P qt build ipcraft-cli
xmake -P qt build ipcraft_cli_contract_test
```

Expected: FAIL because the CLI target and dispatcher do not exist.

- [ ] **Step 3: Implement CLI result helper**

`CliResult` must write compact or indented JSON to stdout and no human text by default. Internal C++ shape may differ, but the public JSON envelope is:

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

- [ ] **Step 4: Implement command dispatcher**

Support:

```text
ipcraft-cli inspect-project <project>
ipcraft-cli validate-project <project> --packages <package-root>
ipcraft-cli emit-inputs <project> --instance <id> --out <dir> --packages <package-root>
ipcraft-cli run-flow <project> --flow <flow-id> --instance <id> --out <dir> --packages <package-root>
ipcraft-cli run-flow <project> --flow <flow-id> --all-instances --out <dir> --packages <package-root>
ipcraft-cli migrate-project <project> --to ipcraft.project.v1
ipcraft-cli collect-artifacts <run-dir> --spec <package-spec>
```

Unknown commands return `cli.unknown_command`. Missing required arguments return `cli.missing_argument`. Invalid paths return the underlying `project.*`, `package.*`, `emitter.*`, `flow.*`, or `artifact.*` diagnostic.

Run-flow instance targeting rules:

- instance-scoped flows require exactly one of `--instance <id>` or `--all-instances`;
- project-scoped flows reject `--instance` and `--all-instances`;
- providing both `--instance` and `--all-instances` returns `cli.argument_conflict`;
- omitting both for an instance-scoped flow returns `cli.instance_scope_required`;
- `--all-instances` runs instances in deterministic order by instance ID and writes deterministic run directory names.

Migration output rule:

- `migrate-project` returns migrated project JSON under `result.project`;
- diagnostics remain under top-level `diagnostics`;
- failure results must not emit a partial migrated project except under diagnostic `details` when redacted and explicitly documented.

- [ ] **Step 5: Verify CLI tests pass**

Run:

```bash
xmake -P qt build ipcraft-cli
xmake -P qt build ipcraft_cli_contract_test
xmake -P qt run ipcraft_cli_contract_test
```

Expected: run prints `ipcraft_cli_contract_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/cli/cliresult.h qt/src/cli/cliresult.cpp qt/src/cli/ipcraft_cli.cpp qt/test/ipcraft_cli_contract_test.cpp qt/xmake.lua
git commit -m "feat: expose ipcraft cli contract"
```

## Task 9: Explicit Migration And Old Schema Rejection

**Files:**
- Create: `qt/inc/ipcraft/migration.h`
- Create: `qt/src/ipcraft/migration.cpp`
- Create: `qt/test/ipcraft_migration_test.cpp`
- Modify: `qt/src/cli/ipcraft_cli.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing migration tests**

Create local smoke tests covering:

```cpp
void testNormalProjectLoadRejectsOldFinepaperSchema();
void testMigrateProjectRequiresToProjectV1();
void testMigratePreservesOldIpcoreStateUnderMigrationNative();
void testMigrateMovesXYCollapsedIntoLayout();
void testUnsupportedLegacyContentReportsDiagnostic();
```

Expected diagnostics:

```text
project.unsupported_schema
migration.target_required
migration.unsupported_legacy_content
```

- [ ] **Step 2: Verify migration tests fail**

Run:

```bash
xmake -P qt build ipcraft_migration_test
```

Expected: FAIL because migration helper does not exist.

- [ ] **Step 3: Implement explicit migrator**

Internal migration APIs may differ. Public migration behavior must return the
standard CLI envelope and place the migrated document under `result.project` on
success:

```json
{
  "ok": true,
  "schema": "ipcraft.cli.result.v1",
  "result": {
    "project": {
      "schema": "ipcraft.project.v1"
    }
  },
  "diagnostics": {
    "schema": "ipcraft.diagnostics.v1",
    "records": []
  }
}
```

Rules:

- do not call migrator from normal `ProjectReader`;
- move old module `x`, `y`, and `collapsed` parameters into `layout.views[0].canvas.nodes`;
- move old module non-layout parameters into `ConfigBundle.parameters` when a package/instance mapping is clear;
- preserve unmapped old `ipcore_state` under `migration.preserved_legacy_state`;
- emit `migration.unsupported_legacy_content` for content that cannot be represented.

- [ ] **Step 4: Wire CLI migration command**

`ipcraft-cli migrate-project old.fpproj --to ipcraft.project.v1` writes the
standard CLI result JSON to stdout. On success, the migrated project appears at
`result.project`. This task does not add an output-file flag.

- [ ] **Step 5: Verify migration tests pass**

Run:

```bash
xmake -P qt build ipcraft_migration_test
xmake -P qt run ipcraft_migration_test
```

Expected: run prints `ipcraft_migration_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/migration.h qt/src/ipcraft/migration.cpp qt/test/ipcraft_migration_test.cpp qt/src/cli/ipcraft_cli.cpp qt/xmake.lua
git commit -m "feat: add explicit project migration to ipcraft v1"
```

## Task 10: Public Architecture, Audit Docs, And Contract Examples

**Files:**
- Create: `docs/architecture/v1-core-architecture.md`
- Create: `docs/audit/black-box-audit-guide.md`
- Create: `docs/audit/coverage-matrix.md`
- Create: `docs/audit/failure-report-format.md`
- Create: `docs/audit/rule-id-catalog.md`
- Create: `examples/contracts/simple_parameter_ip/README.md`
- Create: `examples/contracts/simple_parameter_ip/package/ipcraft.json`
- Create: `examples/contracts/simple_parameter_ip/project.fpproj`
- Create: equivalent README/package/project files for all required examples
- Create: `qt/test/ipcraft_contract_examples_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing example smoke test**

Create `qt/test/ipcraft_contract_examples_test.cpp` that enumerates:

```cpp
QStringList requiredExamples = {
    "simple_parameter_ip",
    "table_config_ip",
    "raw_document_ip",
    "composition_two_ip",
    "clock_fanout_project",
    "failing_validator_project",
    "artifact_collection_project",
    "noc_cutover_project",
    "negative_malformed_package",
    "negative_extension_required",
    "negative_path_escape",
    "negative_flow_missing_executable"
};
```

For each example, assert `README.md`, at least one `ipcraft.json`, and one `project.fpproj` exist and parse as public schemas.

- [ ] **Step 2: Verify example test fails**

Run:

```bash
xmake -P qt build ipcraft_contract_examples_test
```

Expected: FAIL because examples and docs do not exist.

- [ ] **Step 3: Write architecture doc**

`docs/architecture/v1-core-architecture.md` must include sections:

```text
goals / non-goals
V1 hard cutover and legacy policy
authoring/runtime package boundary
ProjectDocument schema
PackageSpec schema
ConfigBundle model
Value type system and expression boundary
CompositionModel model
LayoutModel model
graph-config schema
emitted inputs manifest schema
FlowRunner model
FlowRunner process security
DiagnosticModel model
diagnostic stability rules
ArtifactIndex model
extension vs plugin boundary
explicit extension enablement rules
native escape hatch
migration strategy
security model
public CLI/API contract
testability contract
black-box audit protocol
diagnostic rule-id catalog
```

- [ ] **Step 4: Write audit docs**

`coverage-matrix.md` must include rows for:

```text
CLI JSON shape
exit code behavior
schema validation
deterministic writing
duplicate IDs
extension enforcement
native preservation
config validation
table validation
document validation
composition validation
graph-config validation
path security
flow security
diagnostics stability
artifact collection
migration behavior
old schema rejection
package cutover
```

`failure-report-format.md` must define fields:

```json
{
  "contract_section": "string",
  "expected_behavior": "string",
  "actual_behavior": "string",
  "minimal_redacted_input": {},
  "stable_diagnostics_observed": []
}
```

`rule-id-catalog.md` must list every stable rule ID used by public diagnostics,
including severity, source, category, stable location kinds, and the contract
section that owns the rule. Hidden tests match `rule_id`, `severity`, `source`,
and locations; they do not match full message text.

Minimum rule IDs to catalog:

```text
project.unsupported_schema
project.duplicate_id
package.unsupported_schema
package.extension_required
package.path_escape
package.duplicate_version
package.version_not_found
package.not_found
config.required_missing
config.type_mismatch
config.enum_invalid
config.range_invalid
config.path_escape
config.expression_unsupported
composition.unknown_instance
composition.unknown_interface
composition.required_interface_unconnected
composition.multiply_driven_input
composition.clock_reset_source_count
composition.incompatible_endpoint
graph_config.duplicate_object
graph_config.unknown_endpoint_object
emitter.path_absolute
emitter.path_escape
emitter.write_failed
flow.executable_missing
flow.exec_failed
flow.timeout
flow.command_policy_violation
flow.output_truncated
artifact.glob_escape
artifact.required_missing
cli.unknown_command
cli.missing_argument
cli.argument_conflict
cli.instance_scope_required
migration.target_required
migration.unsupported_legacy_content
```

- [ ] **Step 5: Create contract examples**

Each example README must include exact CLI commands and expected high-level JSON result. Each package must be `ipcraft.package.v1`; each project must be `ipcraft.project.v1`. `failing_validator_project` must use `run-flow` to fail, not default `validate-project`.

Negative examples must be public audit fixtures with expected stable diagnostics:

- `negative_malformed_package` demonstrates malformed package JSON and `package.unsupported_schema` or parser diagnostics.
- `negative_extension_required` demonstrates `package.extension_required`.
- `negative_path_escape` demonstrates package/emitter path confinement.
- `negative_flow_missing_executable` demonstrates `flow.executable_missing` through `run-flow`.

- [ ] **Step 6: Verify docs/examples test passes**

Run:

```bash
xmake -P qt build ipcraft_contract_examples_test
xmake -P qt run ipcraft_contract_examples_test
```

Expected: run prints `ipcraft_contract_examples_test passed`.

- [ ] **Step 7: Commit**

```bash
git add docs/architecture/v1-core-architecture.md docs/audit examples/contracts qt/test/ipcraft_contract_examples_test.cpp qt/xmake.lua
git commit -m "docs: publish ipcraft v1 audit contract examples"
```

## Task 11: Specgen And In-Repository Package Cutover

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `spec_generator/README.md`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/finepaper-noc/ipcraft.json`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcraft.json`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcraft.json`
- Modify: `ipcores/finepaper-noc/generator/src/ruby/parser/json_parser.rb`
- Modify: `ipcores/finepaper-noc/generator/src/ruby/drc/drc_runner.rb`
- Modify: `ipcores/opennoc/generator/src/ruby/opennoc_generator.rb`
- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`

- [ ] **Step 1: Write failing specgen tests for package v1**

In `spec_generator/test/spec_generator_test.rb`, add tests:

```ruby
def test_build_emits_ipcraft_package_v1
def test_runtime_manifest_is_self_contained_without_ipcore_yml
def test_optional_sections_require_extensions
def test_specgen_does_not_emit_ipcraft_manifest_v1
```

The last test asserts no generated runtime file uses `ipcraft.manifest.v1`.

- [ ] **Step 2: Verify specgen tests fail**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because current specgen/runtime names still reflect older manifest vocabulary.

- [ ] **Step 3: Update specgen output**

Specgen must:

- treat `ipcore.yml` as authoring input only;
- emit package-local `ipcraft.json` with `schema: "ipcraft.package.v1"`;
- include explicit `extensions`;
- include `config_schema`, `interfaces`, `connection_rules`, `emitters`, `flows`, `artifacts`, `diagnostics`, `views`, `plugin`, and `native_schema` only when declared;
- never require runtime loaders to read `ipcore.yml`.

- [ ] **Step 4: Convert bundled package specs**

Convert `ipcores/finepaper-noc`, `ipcores/ravenoc`, and `ipcores/opennoc` so each runtime `ipcraft.json` is a self-contained `ipcraft.package.v1` package. Do not encode NoC/mesh/DTC/DN concepts in core schema fields; use config tables/documents/native or graph-config where needed.

- [ ] **Step 5: Verify package specs with CLI**

Run:

```bash
xmake -P qt build ipcraft-cli
xmake -P qt run ipcraft-cli validate-project examples/contracts/noc_cutover_project/project.fpproj --packages ipcores
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: CLI returns JSON with `"ok": true`; specgen tests pass.

- [ ] **Step 6: Commit**

```bash
git add spec_generator ipcores examples/contracts/noc_cutover_project
git commit -m "feat: cut bundled packages to ipcraft package v1"
```

## Task 12: Qt Editor Integration Over ProjectDocument

**Files:**
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorwidget.h`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/inc/project/projectipservice.h`
- Modify: `qt/src/project/projectipservice.cpp`
- Modify: `qt/inc/commands/*.h`
- Modify: `qt/src/commands/*.cpp`
- Modify: existing Qt UI tests affected by project schema

- [ ] **Step 1: Write failing editor model integration tests**

Update existing tests or add focused tests named:

```cpp
void testUiCommandsMutateProjectDocumentThroughCommandManager();
void testNodeEditorUsesGraphAsViewAdapterOnly();
void testCanvasPositionWritesLayoutModelNotConfigParameters();
void testActiveWorkspaceRemainsProjectScoped();
```

The layout test must assert that moving a canvas node changes `ProjectDocument.layout` and does not create `config.parameters.x` or `config.parameters.y`.

- [ ] **Step 2: Verify editor integration tests fail**

Run:

```bash
xmake -P qt build nodeeditor_geometry_test
xmake -P qt build projectipservice_test
```

Expected: FAIL where old Graph-root assumptions remain.

- [ ] **Step 3: Introduce project-root editing services**

`ProjectIpService` should create/remove/select `IpInstanceState` entries. Persistent edits must be represented as commands. The graph editor may maintain an in-memory `Graph` projection for a selected composition or graph-config view, but save/load must use `ProjectDocument`.

- [ ] **Step 4: Move layout writes**

Any new canvas position/collapsed/editor state write goes to `LayoutModel`. Existing legacy parameter reads may be handled only by explicit migration code, not new save paths.

- [ ] **Step 5: Verify UI-adjacent tests pass**

Run:

```bash
xmake -P qt build nodeeditor_geometry_test
xmake -P qt run nodeeditor_geometry_test
xmake -P qt build projectipservice_test
xmake -P qt run projectipservice_test
```

Expected: both runs print their `passed` lines.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/inc/nodeeditor/nodeeditorwidget.h qt/src/nodeeditor/nodeeditorwidget.cpp qt/inc/modules/moduleregistry.h qt/src/modules/moduleregistry.cpp qt/inc/project/projectipservice.h qt/src/project/projectipservice.cpp qt/inc/commands qt/src/commands qt/test
git commit -m "feat: integrate qt editor with ipcraft project document"
```

## Task 13: Remove Old Normal Runtime Paths And Run Hard Cutover Gate

**Files:**
- Modify: `qt/inc/ipcore/ipcoregraphexporter.h`
- Modify: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/inc/validation/projectvalidationrunner.h`
- Modify: `qt/src/validation/projectvalidationrunner.cpp`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`
- Modify: `docs/superpowers/specs/2026-05-22-ipcraft-v1-hard-cutover-core-architecture-design.md` only if implementation reveals a public contract ambiguity
- Modify: `qt/test/v1architecturegate_test.cpp`

- [ ] **Step 1: Write or update hard cutover gate test**

`qt/test/v1architecturegate_test.cpp` must assert:

```cpp
void testNoNormalRuntimePathUsesNocProjectV1();
void testPackageRuntimeLoadDoesNotRequireIpcoreYml();
void testDefaultValidateProjectIsSideEffectFree();
void testCliCommandsReturnMachineReadableJson();
void testAuditDocsAndSchemasExist();
void testRuleIdCatalogContainsEveryPublicDiagnostic();
void testNegativeContractExamplesExist();
```

The NoC schema test may allow old schema names only inside migration tests, docs explaining migration, or archived fixtures explicitly under migration directories.

- [ ] **Step 2: Verify gate initially fails if old paths remain**

Run:

```bash
xmake -P qt build v1architecturegate_test
xmake -P qt run v1architecturegate_test
```

Expected: FAIL until all normal runtime references to `ipcraft.noc.project.v1` and `ipcraft.manifest.v1` are removed or confined to migration/history docs.

- [ ] **Step 3: Remove or quarantine old exporters/runners**

Replace normal uses of:

```text
IpCoreGraphExporter
DRCRunner direct process path
ProjectGenerationRunner direct package command path
ipcraft.noc.project.v1 command input
ipcraft.manifest.v1 runtime package schema
```

with:

```text
PackageInputBuilder
FlowRunner
ArtifactCollector
ipcraft.project.v1
ipcraft.package.v1
ipcraft.emitted-inputs.v1
```

If a file remains for migration-only reasons, add a test-recognizable comment:

```cpp
// Migration-only legacy schema handling. Not used by normal runtime loading.
```

- [ ] **Step 4: Update Qt docs**

Update `qt/doc/README.md` and `qt/doc/architecture.md` so they describe:

- ProjectDocument as root source of truth;
- Graph as editor projection / optional graph-config;
- `ipcraft.package.v1` runtime boundary;
- default static validation behavior;
- `run-flow` for external execution.

- [ ] **Step 5: Run full verification**

Run:

```bash
xmake -P qt build ipcraft_diagnostics_test
xmake -P qt run ipcraft_diagnostics_test
xmake -P qt build ipcraft_project_model_test
xmake -P qt run ipcraft_project_model_test
xmake -P qt build ipcraft_package_spec_test
xmake -P qt run ipcraft_package_spec_test
xmake -P qt build ipcraft_config_validation_test
xmake -P qt run ipcraft_config_validation_test
xmake -P qt build ipcraft_composition_test
xmake -P qt run ipcraft_composition_test
xmake -P qt build ipcraft_emitter_test
xmake -P qt run ipcraft_emitter_test
xmake -P qt build ipcraft_flowrunner_test
xmake -P qt run ipcraft_flowrunner_test
xmake -P qt build ipcraft_artifact_test
xmake -P qt run ipcraft_artifact_test
xmake -P qt build ipcraft-cli
xmake -P qt build ipcraft_cli_contract_test
xmake -P qt run ipcraft_cli_contract_test
xmake -P qt build ipcraft_migration_test
xmake -P qt run ipcraft_migration_test
xmake -P qt build ipcraft_contract_examples_test
xmake -P qt run ipcraft_contract_examples_test
xmake -P qt build v1architecturegate_test
xmake -P qt run v1architecturegate_test
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: every command exits 0 and every test prints its `passed` line or Ruby reports zero failures/errors.

- [ ] **Step 6: Commit**

```bash
git add qt docs spec_generator ipcores examples schemas
git commit -m "chore: complete ipcraft v1 hard cutover gate"
```

## Self-Review Checklist

- [ ] Every public schema in the spec has a schema file.
- [ ] `ipcraft.cli.result.v1` has a public schema file.
- [ ] Schemas are strict at top level and use only documented native/preserved escape hatches.
- [ ] Project identity is `project.id` and `project.name`.
- [ ] Every required CLI command has a task and a test.
- [ ] Instance-scoped `run-flow` requires `--instance` or `--all-instances`.
- [ ] `migrate-project` returns migrated output under `result.project`.
- [ ] Package resolution requires exact `{id, version}`.
- [ ] Deterministic JSON writing is specified and tested.
- [ ] Public diagnostics are listed in `docs/audit/rule-id-catalog.md`.
- [ ] Contract examples include negative fixtures.
- [ ] The NoC contract example is named `noc_cutover_project`.
- [ ] Expressions use JSON AST, not arbitrary scripts.
- [ ] Path confinement uses canonical realpath checks after symlink resolution.
- [ ] Flow stdout/stderr truncation produces stable diagnostics.
- [ ] Default `validate-project` is static and side-effect free.
- [ ] Extension enforcement is parser-level and diagnostic-driven.
- [ ] Flow execution security is implemented only in `FlowRunner`.
- [ ] Artifact glob confinement is tested.
- [ ] Native namespace preservation is tested.
- [ ] Old schemas are rejected during normal load and allowed only through explicit migration.
- [ ] Contract examples are public fixtures, not hidden unit-test dependencies.
- [ ] Audit docs exist and map contract sections to public test surfaces.
- [ ] Qt editor changes follow the headless model instead of defining a separate runtime contract.
