# Ipcraft NoC Package V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the generated `ipcore-runtime.json/modules.xml` flow with the Ipcraft NoC package V1 contract, including constrained YAML authoring, `ipcraft.json` runtime manifests, interface-based connection classes, Qt built-in validation, optional IP-XACT connection checks, and schema-versioned package commands.

**Architecture:** `specgen` becomes the IP developer CLI that reads constrained `ipcore.yml`, loads schema extensions, validates IP-XACT mappability, and emits package-local `ipcraft.json`. Qt consumes `ipcraft.json` and XML views directly, builds strict runtime indexes, validates all editor-owned invariants, runs fast connection checks during editing, and invokes package validators/generators only after built-in validation. Existing runtime classes are migrated in place first, then renamed or wrapped only where needed to avoid a risky one-shot rewrite.

**Tech Stack:** C++23, Qt Widgets, Qt JSON/XML APIs, xmake test targets, Ruby Minitest for `spec_generator`, package-local XML views, optional IP-XACT XML parsing, external validate/generate commands using `ipcraft.noc.project.v1` JSON.

**Execution Model:** Use GPT-5.5 for implementation workers. Default reasoning effort is `xhigh`; minimum accepted reasoning effort is `high`. Execute one task per worker or one task per inline checkpoint. Commit after every task.

---

## File Structure

Create these focused Qt runtime units:

- `qt/inc/ipcraft/ipcraftmanifest.h` and `qt/src/ipcraft/ipcraftmanifest.cpp`: value types for package, modules, interfaces, connection classes, commands, extensions, plugin metadata, and optional IP-XACT metadata.
- `qt/inc/ipcraft/ipcraftmanifestreader.h` and `qt/src/ipcraft/ipcraftmanifestreader.cpp`: strict JSON manifest parser and package-root path resolver.
- `qt/inc/ipcraft/ipcraftregistry.h` and `qt/src/ipcraft/ipcraftregistry.cpp`: package discovery, all-or-nothing package loading, runtime indexes, diagnostics.
- `qt/inc/ipcraft/ipcraftconnectionvalidator.h` and `qt/src/ipcraft/ipcraftconnectionvalidator.cpp`: frontend fast connection checks, ambiguous class resolution, participant normalization.
- `qt/inc/ipcraft/ipcraftbuiltinvalidator.h` and `qt/src/ipcraft/ipcraftbuiltinvalidator.cpp`: project/package/view/topology/command/connection built-in validation before validate/generate.
- `qt/inc/ipcraft/ipxactconnectionchecker.h` and `qt/src/ipcraft/ipxactconnectionchecker.cpp`: optional IP-XACT connection-only strict sub-pass.

Modify these Qt integration files:

- `qt/inc/ipcore/ipcoreruntimedescriptor.h`, `qt/src/ipcore/ipcoreruntimeregistry.cpp`, and related tests during the transition from runtime bundles to package manifests.
- `qt/inc/modules/moduletypemetadata.h`, `qt/inc/modules/moduleprovider.h`, `qt/src/modules/moduleprovider.cpp`, and `qt/src/modules/moduleregistry.cpp` to populate modules/interfaces/views from `ipcraft.json` plus source XML views.
- `qt/inc/connection/connectionruleservice.h` and `qt/src/connection/connectionruleservice.cpp` to validate by interface/class/role instead of port-name heuristics.
- `qt/inc/graph/connection.h`, `qt/src/graph/connection.cpp`, `qt/inc/project/projectdocument.h`, `qt/src/project/projectreader.cpp`, `qt/src/project/projectwriter.cpp`, and `qt/src/project/graphprojectserializer.cpp` to persist `class`, unordered `interfaces`, `status`, and `alternatives`.
- `qt/inc/validation/projectvalidationrunner.h`, `qt/src/validation/projectvalidationrunner.cpp`, `qt/inc/validation/validationresult.h`, `qt/src/validation/validationresult.cpp`, `qt/src/validation/validationmanager.cpp`, and `qt/src/validation/drcrunner.cpp` to run built-in validation before package validators.
- `qt/inc/app/projectgenerationrunner.h`, `qt/src/app/projectgenerationrunner.cpp`, and `qt/src/ipcore/ipcoregraphexporter.cpp` to export `ipcraft.noc.project.v1`.
- `qt/src/panels/propertypanel.cpp` and `qt/src/panels/logpanel.cpp` to expose ambiguous connection class selection and diagnostics.
- `qt/src/topology/topologypresetbuilder.cpp` and `qt/src/nodeeditor/graphnodegeometry.cpp` to consume manifest graph roles, attachment zones, and interface anchors instead of hardcoded NoC names.

Modify these spec generator units:

- `spec_generator/lib/spec_generator.rb`: constrained YAML validation, extension expansion, IP-XACT mappability validation, manifest emitter.
- `spec_generator/bin/spec-gen`: CLI options for `check`, `build`, and package roots instead of hardcoded repository outputs.
- `spec_generator/test/spec_generator_test.rb`: Minitest coverage for constrained YAML, manifest emission, extension mapping, command schema validation, and removal of committed runtime bundle expectations.

Package source updates:

- `ipcores/finepaper-noc/ipcore.yml`, `ipcores/ravenoc/ipcore.yml`, and `ipcores/opennoc/ipcore.yml`: move to `ipcraft.package.v1` source schema.
- `ipcores/*/views/*.xml`: remain source view XML, updated only where needed to reference manifest modules/interfaces.
- Remove long-term dependence on `generated/ipcores/**` after replacement tests are green.

## Task 1: Specgen Constrained YAML Contract

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add failing tests for rejected YAML features**

Add Minitest cases named:

```ruby
def test_rejects_yaml_anchors_aliases_and_merge_keys
def test_rejects_duplicate_yaml_keys
def test_rejects_multi_document_yaml
def test_rejects_implicit_timestamp_fields
```

Each test writes a minimal `ipcore.yml` using the forbidden construct and calls the parser entry point. Expected error messages:

```text
YAML anchors and aliases are not allowed
Duplicate YAML key
YAML multi-document streams are not allowed
Implicit timestamp values are not allowed
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because the current parser accepts at least one forbidden YAML feature or raises a generic Psych error instead of the required schema error.

- [ ] **Step 3: Implement constrained YAML loader**

In `spec_generator/lib/spec_generator.rb`, add `ConstrainedYamlLoader` that:

- scans raw YAML text for anchors, aliases, merge keys, custom tags, and document separators before `Psych.safe_load`;
- uses a `Psych::Handler` to reject duplicate mapping keys;
- calls `Psych.safe_load` with aliases disabled;
- recursively rejects `Date`, `Time`, and `DateTime` values;
- treats version fields as strings.

Expose it through the existing parser path so all source `ipcore.yml` parsing uses it.

- [ ] **Step 4: Run tests and verify pass**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: PASS for the new constrained YAML tests, with existing tests failing only where they still expect generated runtime bundles.

- [ ] **Step 5: Commit**

```bash
git add spec_generator/lib/spec_generator.rb spec_generator/bin/spec-gen spec_generator/test/spec_generator_test.rb
git commit -m "feat: enforce constrained ipcraft YAML"
```

## Task 2: Specgen Ipcraft Manifest Model

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add failing manifest emission tests**

Add tests named:

```ruby
def test_builds_ipcraft_manifest_with_interfaces_and_connection_classes
def test_requires_command_input_schema
def test_rejects_interface_mode_without_ipxact_mapping
def test_expands_noc_chi_extension_modes
def test_does_not_write_generated_ipcore_runtime_bundle
```

Fixtures should assert the manifest file is `ipcraft.json` and contains:

```json
{
  "schema": "ipcraft.manifest.v1",
  "id": "org.example.opennoc",
  "extensions": { "noc.v1": { "enabled": true } },
  "connection_classes": [
    { "id": "chi_node_interface", "roles": ["node", "interconnect"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "xp",
      "interfaces": [
        {
          "id": "rnf0",
          "modes": ["chi_interconnect"],
          "accepts": [{ "class": "chi_node_interface", "role": "interconnect" }]
        }
      ]
    }
  ],
  "commands": {
    "validate": { "executable": "tools/validate", "input_schema": "ipcraft.noc.project.v1" },
    "generate": { "executable": "tools/generate", "input_schema": "ipcraft.noc.project.v1" }
  }
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because `specgen` still emits `ipcore-runtime.json`, `modules.xml`, and `graphics/`.

- [ ] **Step 3: Implement normalized manifest emitter**

Replace the runtime bundle emitter path with an `IpcraftManifestEmitter` that:

- writes package-local `ipcraft.json`;
- normalizes `plugin`, `extensions`, `ipxact`, `parameters`, `connection_classes`, `modules`, `views`, `topologies`, and `commands`;
- materializes `noc.v1` CHI mode mappings;
- rejects missing `commands.*.input_schema`;
- validates every mode is IP-XACT-native or extension-defined;
- validates every interface and connection class has an IP-XACT connection mapping source.

Keep old emitter code behind tests only until all package fixtures migrate, then remove it in Task 12.

- [ ] **Step 4: Add CLI commands**

Update `spec_generator/bin/spec-gen` to support:

```bash
ruby spec_generator/bin/spec-gen check --ipcore ipcores/opennoc/ipcore.yml
ruby spec_generator/bin/spec-gen build --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
```

Expected CLI output:

```text
Checked ipcraft package source: ipcores/opennoc/ipcore.yml
Built ipcraft manifest: ipcores/opennoc/ipcraft.json
```

- [ ] **Step 5: Run tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: PASS for manifest model tests.

- [ ] **Step 6: Commit**

```bash
git add spec_generator/lib/spec_generator.rb spec_generator/bin/spec-gen spec_generator/test/spec_generator_test.rb
git commit -m "feat: emit ipcraft package manifests"
```

## Task 3: Convert Bundled NoC Packages To Ipcraft Source

**Files:**
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcore.yml`
- Create: `ipcores/finepaper-noc/ipcraft.json`
- Create: `ipcores/ravenoc/ipcraft.json`
- Create: `ipcores/opennoc/ipcraft.json`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add repository package smoke tests**

Add tests:

```ruby
def test_repository_finepaper_noc_builds_ipcraft_manifest
def test_repository_ravenoc_builds_ipcraft_manifest
def test_repository_opennoc_builds_ipcraft_manifest
```

Expected assertions:

- manifest schema is `ipcraft.manifest.v1`;
- every command has `input_schema`;
- every view references an existing module;
- every interface `accepts.class` exists in `connection_classes`;
- OpenNoC CHI modes expand through `noc.v1`.

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because existing package YAML still uses the old source schema.

- [ ] **Step 3: Update package YAML**

For each bundled package:

- set `schema: ipcraft.package.v1`;
- define `extensions.noc.v1` where package uses NoC graph roles or CHI mappings;
- move module ports into `interfaces` with `modes`, `accepts`, and `multi_connection`;
- define `connection_classes`;
- define `views` using existing XML view paths;
- define `commands.validate.input_schema` and `commands.generate.input_schema` as `ipcraft.noc.project.v1`.

RaveNoC AXI interfaces use IP-XACT-native modes or explicit mapping metadata. OpenNoC CHI interfaces use `noc.v1` CHI extension modes.

- [ ] **Step 4: Build manifests**

Run:

```bash
ruby spec_generator/bin/spec-gen build --ipcore ipcores/finepaper-noc/ipcore.yml --package-root ipcores/finepaper-noc
ruby spec_generator/bin/spec-gen build --ipcore ipcores/ravenoc/ipcore.yml --package-root ipcores/ravenoc
ruby spec_generator/bin/spec-gen build --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
```

Expected: each command prints `Built ipcraft manifest`.

- [ ] **Step 5: Run tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
```

Expected: PASS for repository package smoke tests.

- [ ] **Step 6: Commit**

```bash
git add ipcores/finepaper-noc ipcores/ravenoc ipcores/opennoc spec_generator/test/spec_generator_test.rb
git commit -m "feat: convert bundled NoC packages to ipcraft"
```

## Task 4: Qt Strict Manifest Reader And Registry

**Files:**
- Create: `qt/inc/ipcraft/ipcraftmanifest.h`
- Create: `qt/src/ipcraft/ipcraftmanifest.cpp`
- Create: `qt/inc/ipcraft/ipcraftmanifestreader.h`
- Create: `qt/src/ipcraft/ipcraftmanifestreader.cpp`
- Create: `qt/inc/ipcraft/ipcraftregistry.h`
- Create: `qt/src/ipcraft/ipcraftregistry.cpp`
- Create: `qt/test/ipcraftmanifest_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing manifest reader tests**

Create `qt/test/ipcraftmanifest_test.cpp` with tests:

```cpp
void testLoadsMinimalPackageManifest();
void testRejectsDuplicateJsonKeys();
void testRejectsMissingCommandInputSchema();
void testRejectsUnknownRequiredShapeFields();
void testRejectsPartialPackageRegistration();
void testPluginAndExtensionsAreDistinct();
```

Use temporary package roots with `ipcraft.json` and minimal `views/Module.xml` files.

- [ ] **Step 2: Add xmake target and run failing test**

Add `ipcraftmanifest_test` to `qt/xmake.lua`.

Run:

```bash
xmake run -P qt ipcraftmanifest_test
```

Expected: build FAIL because the new reader and registry classes do not exist.

- [ ] **Step 3: Implement manifest value types**

Define value structs for:

```cpp
IpcraftPackageManifest
IpcraftPluginDescriptor
IpcraftExtensionDescriptor
IpcraftConnectionClass
IpcraftInterfaceAcceptRule
IpcraftInterfaceDescriptor
IpcraftModuleDescriptor
IpcraftViewDescriptor
IpcraftCommandDescriptor
IpcraftIpxactDescriptor
IpcraftDiagnostic
```

Include package-root-resolved paths for views and commands.

- [ ] **Step 4: Implement strict JSON parsing**

`IpcraftManifestReader` must:

- reject duplicate JSON keys by scanning object keys before converting to `QJsonDocument`;
- require `schema == "ipcraft.manifest.v1"`;
- reject missing `id`, `name`, `version`;
- reject commands without `input_schema`;
- reject references to missing modules, interfaces, connection classes, and views;
- keep `plugin` and `extensions` in separate fields.

- [ ] **Step 5: Implement registry all-or-nothing loading**

`IpcraftRegistry` must:

- discover package roots from explicit paths;
- load `ipcraft.json`;
- validate view XML references at package load time;
- register nothing for a failed package;
- expose diagnostics for log panel integration.

- [ ] **Step 6: Run test**

Run:

```bash
xmake run -P qt ipcraftmanifest_test
```

Expected: `ipcraftmanifest_test passed`.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/ipcraft qt/src/ipcraft qt/test/ipcraftmanifest_test.cpp qt/xmake.lua
git commit -m "feat: load ipcraft package manifests in Qt"
```

## Task 5: Qt Catalog And Module Registry Migration

**Files:**
- Modify: `qt/inc/ipcore/ipcatalogservice.h`
- Modify: `qt/src/ipcore/ipcatalogservice.cpp`
- Modify: `qt/inc/modules/moduletypemetadata.h`
- Modify: `qt/inc/modules/moduleprovider.h`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/test/ipcatalogservice_test.cpp`
- Modify: `qt/test/ipcoreruntime_test.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Add failing catalog tests for package roots**

Add tests asserting:

- package discovery reads `ipcores/ravenoc/ipcraft.json`;
- catalog entries expose package ID, modules, interfaces, connection classes, views, commands;
- no default discovery depends on `generated/ipcores`.

Run:

```bash
xmake run -P qt ipcatalogservice_test
xmake run -P qt ipcoreruntime_test
```

Expected: FAIL because catalog still discovers runtime bundles.

- [ ] **Step 2: Bridge catalog entries to Ipcraft manifests**

Update `IpCatalogEntry` to hold package manifest data while keeping temporary compatibility fields used by existing UI. Mark old runtime-only fields for removal in Task 12.

- [ ] **Step 3: Populate module types from manifest modules**

Update `ModuleRegistry` and `ModuleProvider` so module metadata comes from `IpcraftModuleDescriptor`. Each module type must carry:

- package ID;
- module ID;
- graph role;
- interfaces;
- view file path;
- attachment metadata.

- [ ] **Step 4: Load XML views directly from package `views`**

Update `XmlModuleGraphicsOverlay` or replace it with a package view loader that reads source view XML paths referenced by `ipcraft.json`, not generated `graphics/*.xml`.

- [ ] **Step 5: Run tests**

```bash
xmake run -P qt ipcatalogservice_test
xmake run -P qt ipcoreruntime_test
xmake run -P qt graph_test
```

Expected: all pass. `ipcoreruntime_test` should either be renamed in a later task or assert compatibility wrappers only.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcore qt/src/ipcore qt/inc/modules qt/src/modules qt/test/ipcatalogservice_test.cpp qt/test/ipcoreruntime_test.cpp qt/test/graph_test.cpp
git commit -m "feat: populate catalog from ipcraft packages"
```

## Task 6: Project Connection Schema With Interfaces

**Files:**
- Modify: `qt/inc/graph/connection.h`
- Modify: `qt/src/graph/connection.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Add failing project connection tests**

Add tests for:

```cpp
void testProjectWritesInterfaceConnectionsWithoutFromTo();
void testProjectReadsAmbiguousConnectionAlternatives();
void testReverseInterfaceConnectionNormalizesForSymmetricClass();
```

Expected project JSON:

```json
{
  "connections": [
    {
      "id": "conn_0",
      "class": "chi_node_interface",
      "interfaces": [
        { "instance": "rnf_0", "interface": "chi" },
        { "instance": "xp_0", "interface": "rnf0" }
      ],
      "status": "valid"
    }
  ]
}
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt projectdocument_test
xmake run -P qt graph_test
```

Expected: FAIL because connections still serialize port-style endpoints.

- [ ] **Step 3: Extend connection model**

Add fields:

```cpp
QString connectionClassId;
QVector<ProjectConnectionInterfaceRef> interfaces;
QString status;
QStringList alternatives;
```

Keep legacy port endpoint fields only for a one-task migration path. Do not save legacy `from`/`to` in new project writes.

- [ ] **Step 4: Update reader/writer/serializer**

Reader accepts new interface connection format and rejects malformed interface participants. Writer emits only new format for new documents. Serializer maps graph connection endpoints through module instance and interface metadata.

- [ ] **Step 5: Run tests**

```bash
xmake run -P qt projectdocument_test
xmake run -P qt graph_test
```

Expected: both pass.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/graph qt/src/graph qt/inc/project qt/src/project qt/test/projectdocument_test.cpp qt/test/graph_test.cpp
git commit -m "feat: store project connections by interfaces"
```

## Task 7: Fast Connection Validation And Ambiguity UI

**Files:**
- Create: `qt/inc/ipcraft/ipcraftconnectionvalidator.h`
- Create: `qt/src/ipcraft/ipcraftconnectionvalidator.cpp`
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/src/panels/logpanel.cpp`
- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/test/logpanel_test.cpp`

- [ ] **Step 1: Add failing validator tests**

Add tests:

```cpp
void testAcceptsMatchingClassAndRoles();
void testRejectsRoleMismatch();
void testRejectsUsedSingleConnectionInterface();
void testSymmetricClassNormalizesReverseDrag();
void testAmbiguousClassCreatesWarningResult();
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt connectionruleservice_test
```

Expected: FAIL because connection rules still use port semantics and hardcoded endpoint names.

- [ ] **Step 3: Implement `IpcraftConnectionValidator`**

The validator takes package manifest metadata, current graph/project connections, and attempted participants. It returns:

```cpp
enum class IpcraftConnectionStatus { Valid, Ambiguous, Invalid };
struct IpcraftConnectionDecision {
    IpcraftConnectionStatus status;
    QString selectedClassId;
    QStringList alternatives;
    QString message;
    QVector<ProjectConnectionInterfaceRef> normalizedInterfaces;
};
```

- [ ] **Step 4: Integrate with `ConnectionRuleService`**

Route editor drag checks through interface/class validation. Preserve current structural rejection diagnostics but remove reliance on hardcoded `ep`, `local`, `east`, `west`, `north`, and `south` for connection legality.

- [ ] **Step 5: Add property/log behavior**

Property panel shows `Connection class` for ambiguous connections. Log panel receives one warning line:

```text
Connection conn_1 has multiple valid classes: chi_node_interface, monitor_tap
```

- [ ] **Step 6: Run tests**

```bash
xmake run -P qt connectionruleservice_test
xmake run -P qt propertypanel_test
xmake run -P qt logpanel_test
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/ipcraft/ipcraftconnectionvalidator.h qt/src/ipcraft/ipcraftconnectionvalidator.cpp qt/inc/connection qt/src/connection qt/src/nodeeditor qt/src/panels qt/test/connectionruleservice_test.cpp qt/test/propertypanel_test.cpp qt/test/logpanel_test.cpp
git commit -m "feat: validate interface connection classes"
```

## Task 8: Qt Built-In Validation Runner

**Files:**
- Create: `qt/inc/ipcraft/ipcraftbuiltinvalidator.h`
- Create: `qt/src/ipcraft/ipcraftbuiltinvalidator.cpp`
- Modify: `qt/inc/validation/projectvalidationrunner.h`
- Modify: `qt/src/validation/projectvalidationrunner.cpp`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Add failing built-in validation tests**

Add tests:

```cpp
void testBuiltInValidationRunsBeforePackageValidate();
void testBuiltInValidationReportsMissingPackage();
void testBuiltInValidationReportsMissingInterface();
void testBuiltInValidationReportsViewReferenceError();
void testGenerateStopsOnBuiltInValidationError();
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt validation_test
```

Expected: FAIL because validation currently focuses on structural checks and DRC runner order.

- [ ] **Step 3: Implement built-in validator**

`IpcraftBuiltInValidator` checks:

- package was fully loaded;
- project instances reference existing package modules;
- graph module package and instance ownership is valid;
- view XML references existing modules/interfaces/attachment zones;
- topology presets reference existing modules/interfaces/classes;
- commands declare executable and input schema;
- project connections pass fast validation;
- package metadata remains mappable to IP-XACT connection semantics.

- [ ] **Step 4: Integrate validation ordering**

`ValidationManager::runValidation()` and `ProjectValidationRunner` must:

1. run built-in validation;
2. show built-in diagnostics;
3. run package validate commands only for executable instances;
4. merge diagnostics in log order.

- [ ] **Step 5: Run tests**

```bash
xmake run -P qt validation_test
```

Expected: `validation_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/ipcraftbuiltinvalidator.h qt/src/ipcraft/ipcraftbuiltinvalidator.cpp qt/inc/validation qt/src/validation qt/test/validation_test.cpp
git commit -m "feat: add Qt built-in package validation"
```

## Task 9: Optional IP-XACT Connection Strict Checker

**Files:**
- Create: `qt/inc/ipcraft/ipxactconnectionchecker.h`
- Create: `qt/src/ipcraft/ipxactconnectionchecker.cpp`
- Modify: `qt/src/ipcraft/ipcraftbuiltinvalidator.cpp`
- Create: `qt/test/ipxactconnectionchecker_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing IP-XACT checker tests**

Create tests:

```cpp
void testSkipsWhenNoIpxactRoot();
void testReportsMissingBusInterface();
void testAcceptsCompatibleActiveInterfaces();
void testRejectsIncompatibleInterfaceModes();
```

Use small XML fixtures inside the test temp directory. The minimal fixture must include component instances, bus interface names, and mode tags needed by the checker.

- [ ] **Step 2: Run failing test**

```bash
xmake run -P qt ipxactconnectionchecker_test
```

Expected: build FAIL because checker does not exist.

- [ ] **Step 3: Implement connection-only checker**

Use Qt XML streaming APIs. The checker only extracts:

- component instance names;
- component references when present;
- bus interface names;
- bus interface modes;
- enough bus/abstraction identity to compare declared project connection participants.

It does not validate register maps, filesets, generators, view data, or RTL.

- [ ] **Step 4: Wire into built-in validation**

If manifest has `ipxact.root`, built-in validation calls `IpxactConnectionChecker` and attaches findings to project connections.

- [ ] **Step 5: Run tests**

```bash
xmake run -P qt ipxactconnectionchecker_test
xmake run -P qt validation_test
```

Expected: both pass.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/ipcraft/ipxactconnectionchecker.h qt/src/ipcraft/ipxactconnectionchecker.cpp qt/test/ipxactconnectionchecker_test.cpp qt/xmake.lua qt/src/ipcraft/ipcraftbuiltinvalidator.cpp
git commit -m "feat: add optional IP-XACT connection checks"
```

## Task 10: Schema-Versioned Project Export And Commands

**Files:**
- Modify: `qt/inc/ipcore/ipcoregraphexporter.h`
- Modify: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify: `qt/inc/ipcore/ipcorecommandrunner.h`
- Modify: `qt/src/ipcore/ipcorecommandrunner.cpp`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/test/ipcoregraphexporter_test.cpp`
- Modify: `qt/test/projectgenerationrunner_test.cpp`
- Modify: `qt/test/validation_test.cpp`

- [ ] **Step 1: Add failing export tests**

Add tests:

```cpp
void testExportsIpcraftNocProjectV1Schema();
void testExportsInterfaceConnections();
void testCommandRunnerRejectsSchemaMismatch();
void testGenerateRunsBuiltInValidationBeforeCommand();
```

Expected export root:

```json
{
  "schema": "ipcraft.noc.project.v1",
  "package": "org.example.ravenoc",
  "instances": [],
  "connections": []
}
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt ipcoregraphexporter_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt validation_test
```

Expected: FAIL because exporter still emits older graph schema names and command runner does not enforce `input_schema`.

- [ ] **Step 3: Update exporter**

Exporter writes `ipcraft.noc.project.v1`, includes project instance parameters, module instances, interface connections, graph state, and package ID. It does not write `finepaper` schema names.

- [ ] **Step 4: Update command runner**

Command runner reads `input_schema` from manifest command descriptors and rejects command invocation when the exported project schema differs.

- [ ] **Step 5: Enforce built-in validation before commands**

Generation and validation runners must stop command execution on built-in validation errors. Validation can continue to package commands for instances without blocking errors.

- [ ] **Step 6: Run tests**

```bash
xmake run -P qt ipcoregraphexporter_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt validation_test
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/ipcore qt/src/ipcore qt/inc/validation qt/src/validation qt/inc/app qt/src/app qt/test/ipcoregraphexporter_test.cpp qt/test/projectgenerationrunner_test.cpp qt/test/validation_test.cpp
git commit -m "feat: export ipcraft project command input"
```

## Task 11: Topology And View Runtime Semantics

**Files:**
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify: `qt/inc/topology/topologypresetbuilder.h`
- Modify: `qt/src/nodeeditor/graphnodegeometry.cpp`
- Modify: `qt/inc/nodeeditor/graphnodegeometry.h`
- Modify: `qt/inc/common/portlayout.h`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `qt/test/topology_preset_test.cpp`
- Modify: `qt/test/nodeeditor_geometry_test.cpp`

- [ ] **Step 1: Add failing tests for no hardcoded NoC names**

Add tests:

```cpp
void testMeshPresetUsesManifestConnectionClassesNotEastWestNames();
void testCollapsedAttachedNodeMirrorsIntoHostZone();
void testOpenNocMultipleInterfacesRenderFromViewAnchors();
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt topology_preset_test
xmake run -P qt nodeeditor_geometry_test
```

Expected: FAIL where code still depends on `east`, `west`, `ep`, `local`, `mesh_router`, or `endpoint` as behavior rules.

- [ ] **Step 3: Move behavior to manifest metadata**

Topology builder consumes:

- module graph roles;
- attachment zones;
- topology preset definitions;
- interface IDs;
- connection classes.

Geometry consumes:

- view XML states;
- interface anchors;
- attachment zone anchors;
- mirror behavior for attached nodes.

Hardcoded layout strings may remain as visual style names only, not as behavior semantics.

- [ ] **Step 4: Run tests**

```bash
xmake run -P qt topology_preset_test
xmake run -P qt nodeeditor_geometry_test
```

Expected: both pass.

- [ ] **Step 5: Commit**

```bash
git add qt/inc/topology qt/src/topology qt/inc/nodeeditor qt/src/nodeeditor qt/inc/common/portlayout.h qt/src/modules/moduleprovider.cpp qt/test/topology_preset_test.cpp qt/test/nodeeditor_geometry_test.cpp
git commit -m "feat: drive NoC topology and views from manifest"
```

## Task 12: Remove Generated Runtime Bundle Dependency

**Files:**
- Delete or stop using: `generated/ipcores/**`
- Modify: `spec_generator/README.md`
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `qt/inc/ipcore/ipcoreruntimedescriptor.h`
- Modify: `qt/inc/ipcore/ipcoreruntimeregistry.h`
- Modify: `qt/src/ipcore/ipcoreruntimeregistry.cpp`
- Modify: `qt/test/ipcoreruntime_test.cpp`
- Modify: `qt/test/v1architecturegate_test.cpp`

- [ ] **Step 1: Add failing architecture gate**

Update `v1architecturegate_test` to fail if maintained runtime code or docs require:

```text
generated/ipcores
ipcore-runtime.json
modules.xml as package runtime bundle
FINEPAPER_IPCORE_PATH as generated runtime path
```

Run:

```bash
xmake run -P qt v1architecturegate_test
```

Expected: FAIL before cleanup.

- [ ] **Step 2: Remove generated runtime discovery**

Default package discovery reads package roots from application settings and explicit test paths. It must not scan repository-local `generated/ipcores`.

- [ ] **Step 3: Retire old emitter and docs**

Remove old runtime bundle emitter paths from `spec_generator`. Update README to describe:

```bash
ruby spec_generator/bin/spec-gen check --ipcore ipcores/opennoc/ipcore.yml
ruby spec_generator/bin/spec-gen build --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
```

- [ ] **Step 4: Remove or quarantine generated artifacts**

Delete committed generated runtime bundle files after all tests consume package-local `ipcraft.json`. Do not delete OpenNoC vendor submodule or vendor sources.

- [ ] **Step 5: Run gates**

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
xmake run -P qt v1architecturegate_test
xmake run -P qt ipcraftmanifest_test
xmake run -P qt ipcatalogservice_test
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -u generated spec_generator qt/inc/ipcore qt/src/ipcore qt/test/ipcoreruntime_test.cpp qt/test/v1architecturegate_test.cpp
git add spec_generator/README.md
git commit -m "chore: remove generated runtime bundle path"
```

## Task 13: End-To-End Package Flow

**Files:**
- Modify: `qt/test/projectgenerationrunner_test.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/panels/ipcatalogpanel.cpp`

- [ ] **Step 1: Add end-to-end tests**

Add tests:

```cpp
void testNewProjectAddsIpcraftPackageInstanceFromCatalog();
void testValidateRunsBuiltInThenPackageValidate();
void testGenerateExportsIpcraftProjectForEveryInstance();
void testAmbiguousConnectionAppearsInPropertyPanelAndLog();
```

- [ ] **Step 2: Run tests and verify failure**

```bash
xmake run -P qt ipcatalogpanel_test
xmake run -P qt propertypanel_test
xmake run -P qt validation_test
xmake run -P qt projectgenerationrunner_test
```

Expected: FAIL where UI orchestration still expects old runtime descriptors or active-only generate/validate behavior.

- [ ] **Step 3: Update main window orchestration**

Ensure:

- catalog adds package instances from `IpcraftRegistry`;
- workspace tools are active-instance editing helpers only;
- Validate iterates project IP instances and runs built-in validation first;
- Generate iterates project IP instances and exports `ipcraft.noc.project.v1`;
- log panel receives package load, validation, ambiguity, and command diagnostics.

- [ ] **Step 4: Run end-to-end tests**

```bash
xmake run -P qt ipcatalogpanel_test
xmake run -P qt propertypanel_test
xmake run -P qt validation_test
xmake run -P qt projectgenerationrunner_test
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add qt/src/app/mainwindow.cpp qt/src/panels/ipcatalogpanel.cpp qt/test/ipcatalogpanel_test.cpp qt/test/propertypanel_test.cpp qt/test/validation_test.cpp qt/test/projectgenerationrunner_test.cpp
git commit -m "feat: wire ipcraft package flow through UI"
```

## Task 14: Documentation, Gates, And Full Verification

**Files:**
- Modify: `spec_generator/README.md`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md` if present
- Modify: `docs/superpowers/specs/2026-05-14-ipcraft-noc-package-v1-design.md` only if implementation reveals a confirmed spec correction
- Modify: `qt/test/v1architecturegate_test.cpp`

- [ ] **Step 1: Update docs**

Docs must describe:

- `ipcore.yml` as constrained authoring YAML;
- `ipcraft.json` as runtime manifest;
- Qt does not parse authoring YAML;
- package roots come from app settings or explicit paths;
- `plugin` means Qt dynamic plugin;
- `extensions` means schema/specgen extension;
- IP-XACT file is optional;
- all package semantics must be mappable to IP-XACT connection semantics;
- Validate/Generate run Qt built-in validation before package commands.

- [ ] **Step 2: Add final architecture gate assertions**

`v1architecturegate_test` asserts:

- no public schema name contains `finepaper`;
- no default Qt package discovery depends on `generated/ipcores`;
- new project connection schema uses `interfaces`, not `from`/`to`;
- commands declare `input_schema`;
- Qt manifest loader has tests for duplicate JSON keys and partial package rejection.

- [ ] **Step 3: Run full verification**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
xmake run -P qt ipcraftmanifest_test
xmake run -P qt ipxactconnectionchecker_test
xmake run -P qt ipcatalogservice_test
xmake run -P qt projectdocument_test
xmake run -P qt connectionruleservice_test
xmake run -P qt validation_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt topology_preset_test
xmake run -P qt nodeeditor_geometry_test
xmake run -P qt ipcatalogpanel_test
xmake run -P qt propertypanel_test
xmake run -P qt v1architecturegate_test
xmake build -P qt
git diff --check
```

Expected:

```text
spec_generator_test: all tests pass
each xmake run target prints "<target> passed"
xmake build -P qt succeeds
git diff --check exits with no output
```

- [ ] **Step 4: Commit**

```bash
git add spec_generator/README.md qt/doc qt/test/v1architecturegate_test.cpp docs/superpowers/specs/2026-05-14-ipcraft-noc-package-v1-design.md
git commit -m "docs: document ipcraft package v1 flow"
```

## Plan Self-Review

Spec coverage:

- Constrained YAML is covered by Task 1.
- `specgen` CLI, extension expansion, and `ipcraft.json` are covered by Task 2.
- Bundled NoC package conversion is covered by Task 3.
- Qt manifest load, strict JSON, package indexes, and partial-load rejection are covered by Task 4.
- Catalog/module/view runtime loading is covered by Task 5.
- Interface-based project connections are covered by Task 6.
- Connection classes, role checks, ambiguity warnings, property panel, and log panel are covered by Task 7.
- Qt built-in validation before validate/generate is covered by Task 8.
- Optional IP-XACT connection-only strict check is covered by Task 9.
- Command input schema version and `ipcraft.noc.project.v1` export are covered by Task 10.
- Topology/view behavior cleanup is covered by Task 11.
- Removal of generated runtime bundle dependency is covered by Task 12.
- UI end-to-end flow is covered by Task 13.
- Documentation and final architecture gates are covered by Task 14.

Placeholder scan:

- This plan intentionally contains no placeholder markers or unspecified follow-up implementation steps.

Type consistency:

- Authoring source file is `ipcore.yml`.
- Runtime manifest file is `ipcraft.json`.
- Public package schema is `ipcraft.package.v1`.
- Runtime manifest schema is `ipcraft.manifest.v1`.
- Command project input schema is `ipcraft.noc.project.v1`.
- Frontend connection references use `interfaces`, `class`, `status`, and `alternatives`.
