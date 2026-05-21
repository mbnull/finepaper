# Ipcraft NoC Specgen Hardening Phase Review Testcases

## Purpose

This document defines a phase review package for the Ipcraft NoC specgen
hardening work. It is not a CI-only automation plan. The review combines
generated fixtures, CLI checks, Qt runtime tests, user-visible UX inspection,
and generated artifact inspection to decide whether the implementation meets
the design and plan goals in:

- `docs/superpowers/specs/2026-05-18-ipcraft-noc-specgen-hardening-design.md`
- `docs/superpowers/plans/2026-05-18-ipcraft-noc-specgen-hardening.md`

The review verifies the complete target surface:

- `ipcore.yml` plus `views/*.xml` are the maintained source of package metadata.
- `spec-gen` validates package source and produces deterministic `ipcraft.json`.
- Qt consumes runtime packages through `ipcraft.json` and view XML, not source YAML.
- Package root management is discoverable, persistent, reloadable, and diagnostic.
- Display and connection UX use user-facing names while preserving stable identities.
- Topology behavior is driven by manifest/view metadata, not package IDs or fixed port names.
- Qt exports `ipcraft.noc.project.v1` command input distinct from saved `.fpproj` documents.
- Common generation is framework-owned and driven by manifest `generation` metadata.
- The three current packages remain compatible: `finepaper-noc`, `opennoc`, and `ravenoc`.

## Review Model

Each testcase must record:

- **Objective**: the design or plan requirement being reviewed.
- **Input**: package fixture, existing package, Qt profile, command input, or generated artifact.
- **Method**: CLI, Qt executable test, Qt UI inspection, generated artifact inspection, or manual evidence.
- **Evidence**: commands, stdout/stderr, Qt test output, logs, screenshots, generated files, and manifest diffs.
- **Pass criteria**: concrete observable behavior.
- **Failure classification**:
  - `REGRESSION`: documented target behavior fails through public or intended review surfaces.
  - `DOC_GAP`: design/plan lacks enough detail to judge the behavior.
  - `SCHEMA_GAP`: desired generic behavior cannot be expressed in documented package metadata.
  - `ENVIRONMENT_GAP`: required tool, binary, display server, or vendor asset is unavailable.
  - `FUTURE_SCOPE`: useful behavior outside the design and plan scope.

The phase verdict is:

- `APPROVED`: all required review cases pass.
- `APPROVED_WITH_GAPS`: no regression is found, but documented gaps remain.
- `REJECTED`: at least one `REGRESSION` is found.

## Required Review Inputs

### Tool Builds

Record the build identifiers or command versions for:

- `spec-gen`
- Qt test executable build, including `xmake -P qt` configuration if relevant
- Qt frontend binary used for manual UX review
- `ipcraft-generate`
- Ruby version
- Qt version if printed by the environment or build logs

### Synthetic Review Package

The review must generate a fresh package fixture named:

```text
phase.synthetic.noc
```

This package is the generic NoC capability probe. It must be authored only from
the design/plan contracts and must not copy existing repository tests.

Required source layout:

```text
phase-synthetic-noc/
  ipcore.yml
  views/
    Tile.xml
    Endpoint.xml
```

Required package metadata:

- `schema: ipcraft.package.v1`
- package id `phase.synthetic.noc`
- modules `Tile` and `Endpoint`
- `display.label_parameter` and `display.short_label_parameter` for both modules
- `Tile` parameters: `display_name`, `external_id`, `mesh_row`, `mesh_col`
- `Endpoint` parameters: `display_name`, `external_id`
- connection classes for fabric links and endpoint links
- Tile fabric interface IDs that are not cardinal direction names, for example:
  - `fabric_tx`
  - `fabric_rx`
  - `vertical_tx`
  - `vertical_rx`
- interface labels that are user-facing and not identical to all raw IDs
- semantic topology metadata in `ipcore.yml`, including `side` and `opposite`
- a `mesh` topology preset that can create a 2x2 Tile graph
- `generation.engine: ipcraft.common.v1`
- `generation.outputs` including `manifest.json`
- `commands.generate.framework_tool: ipcraft-generate`
- `commands.generate.input_schema: ipcraft.noc.project.v1`
- generate args:

```yaml
args: ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]
```

Required view metadata:

- `Tile.xml` has anchors for every Tile interface.
- `Endpoint.xml` has an anchor for the endpoint interface.
- view labels are human-readable.
- view XML does not define legality or topology semantics that should live in `ipcore.yml`.

### Existing Package Inputs

Use the public package source directories:

- `ipcores/finepaper-noc`
- `ipcores/opennoc`
- `ipcores/ravenoc`

Vendor-backed checks are optional unless the required vendor assets are present.
If a vendor-backed check cannot run due to missing vendor assets, classify that
specific check as `ENVIRONMENT_GAP`, not as a regression.

## Evidence Bundle

Store or attach the following evidence for the review:

- the generated `phase.synthetic.noc` fixture source
- generated `ipcraft.json` files
- CLI command transcripts
- Qt test transcripts
- generated command input JSON files inspected during review
- generated output directories, or a manifest of their important files
- manual UI screenshots or logs for package root management and connection labels
- a final verdict table listing all failures and classifications

## Specgen Source-of-Truth Review

### SR-01: Build Synthetic Package

- **Objective**: prove `ipcore.yml` plus `views/*.xml` can produce a runtime manifest.
- **Input**: generated `phase.synthetic.noc`.
- **Method**:

```bash
spec-gen build --ipcore <fixture>/ipcore.yml --package-root <fixture>
```

- **Pass criteria**:
  - command exits 0
  - `<fixture>/ipcraft.json` exists
  - manifest schema is `ipcraft.manifest.v1`
  - manifest id is `phase.synthetic.noc`
  - manifest includes display, topology, generation, commands, views, and topology preset metadata

### SR-02: Deterministic Manifest Generation

- **Objective**: prove generated manifests are stable.
- **Input**: generated `phase.synthetic.noc`.
- **Method**: run `spec-gen build` twice and compare normalized JSON.
- **Pass criteria**:
  - both commands exit 0
  - normalized manifests are identical

### SR-03: Drift Detection

- **Objective**: manual edits to generated `ipcraft.json` are invalid unless produced by `spec-gen`.
- **Input**: built synthetic package.
- **Method**:
  - mutate generated `ipcraft.json`
  - run:

```bash
spec-gen check --ipcore <fixture>/ipcore.yml --package-root <fixture>
```

- **Pass criteria**:
  - command exits nonzero
  - stderr/stdout names `ipcraft.json` drift, mismatch, or stale content

### SR-04: Missing View Anchor Target

- **Objective**: validate the manifest/view join.
- **Input**: synthetic package variant where `Tile.xml` references `missing_interface`.
- **Method**: run `spec-gen build`.
- **Pass criteria**:
  - command exits nonzero
  - diagnostic names the module/view anchor and unknown interface

### SR-05: Invalid Topology Opposite

- **Objective**: reject topology metadata that cannot be joined to module interfaces.
- **Input**: synthetic package variant with `topology.opposite: missing_rx`.
- **Method**: run `spec-gen build`.
- **Pass criteria**:
  - command exits nonzero
  - diagnostic names `topology.opposite` and the missing interface

### SR-06: Invalid Display Binding

- **Objective**: ensure display binding is explicit and resolves to a declared parameter.
- **Input**: synthetic package variant with `display.label_parameter: missing_label`.
- **Method**: run `spec-gen build`.
- **Pass criteria**:
  - command exits nonzero
  - diagnostic names the module display binding and unknown parameter

### SR-07: Unsafe Generation Output Path

- **Objective**: keep generation metadata bounded and safe.
- **Input**: synthetic package variant with `generation.outputs[].path: ../manifest.json`.
- **Method**: run `spec-gen build`.
- **Pass criteria**:
  - command exits nonzero
  - diagnostic says the output path escapes the allowed output/package boundary

### SR-08: Framework Tool Command Shape

- **Objective**: package commands use a bounded framework tool contract.
- **Input**: synthetic package variants:
  - command with both `executable` and `framework_tool`
  - command with `framework_tool: ruby`
- **Method**: run `spec-gen build` for each variant.
- **Pass criteria**:
  - each invalid variant exits nonzero
  - diagnostics say exactly one of `executable` or `framework_tool` is allowed
  - unsupported framework tool is rejected or classified as a documented `SCHEMA_GAP`

### SR-09: Existing Package Manifest Equivalence

- **Objective**: `spec-gen` is authoritative for the three current packages.
- **Input**:
  - `ipcores/finepaper-noc`
  - `ipcores/opennoc`
  - `ipcores/ravenoc`
- **Method**:

```bash
spec-gen check --ipcore ipcores/finepaper-noc/ipcore.yml --package-root ipcores/finepaper-noc
spec-gen check --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
spec-gen check --ipcore ipcores/ravenoc/ipcore.yml --package-root ipcores/ravenoc
```

- **Pass criteria**:
  - all commands exit 0
  - if a package fails, the diff or diagnostic is attached to the review

## Qt Runtime Package Review

### QR-01: Manifest Reader Parses New Metadata

- **Objective**: Qt consumes display, topology, generation, and framework command metadata from `ipcraft.json`.
- **Input**: synthetic manifest or minimal JSON fixture.
- **Method**: Qt test target, preferably `ipcraftmanifest_test`.
- **Pass criteria**:
  - module display label and short label fields are parsed
  - interface topology side/opposite fields are parsed
  - generation engine and outputs are parsed
  - `commands.generate.framework_tool` is parsed

### QR-02: Runtime Does Not Parse Source YAML

- **Objective**: Qt runtime package loading is centered on `ipcraft.json`.
- **Input**: synthetic package runtime copy containing `ipcraft.json` and views, with `ipcore.yml` removed or renamed.
- **Method**: Qt test target or manual Qt load.
- **Pass criteria**:
  - package loads from runtime manifest and views
  - no error requires `ipcore.yml`

### QR-03: Empty Package Roots Diagnostic

- **Objective**: clean environments are explicit and recoverable.
- **Input**: clean Qt settings profile with no package roots.
- **Method**: Qt UI/manual check or UI-adjacent Qt test.
- **Pass criteria**:
  - catalog starts empty
  - user-visible log or panel says no package roots or no packages were discovered
  - diagnostic points to the package-root action

### QR-04: Add Root And Reload Without Restart

- **Objective**: package discovery UX is usable.
- **Input**: clean Qt profile and synthetic package root.
- **Method**: Qt UI/manual check or `ipcatalogpanel_test`/mainwindow seam test.
- **Pass criteria**:
  - `Tools -> IP Core Packages...` or equivalent action is visible
  - adding the root reloads the catalog without app restart
  - synthetic package appears in catalog

### QR-05: Remove Root And Reload Without Restart

- **Objective**: package roots are manageable and not stale.
- **Input**: Qt profile with synthetic package root configured.
- **Method**: Qt UI/manual check or UI-adjacent Qt test.
- **Pass criteria**:
  - removing the root updates settings
  - reload removes synthetic package from catalog without restart

### QR-06: Mixed Valid And Invalid Roots

- **Objective**: one malformed package does not block unrelated valid packages.
- **Input**: one valid synthetic package and one malformed package root.
- **Method**: Qt catalog service test and/or UI check.
- **Pass criteria**:
  - valid package registers
  - invalid package does not partially register
  - diagnostic names the invalid path and malformed data

### QR-07: Duplicate Package IDs

- **Objective**: duplicates are diagnosed instead of silently selecting one package.
- **Input**: two valid package roots with the same package id and different names.
- **Method**: Qt catalog service test and/or UI check.
- **Pass criteria**:
  - duplicate package id diagnostic is visible
  - duplicate package is not silently selected as unambiguous
  - unrelated packages still load

## Display And Connection UX Review

### UX-01: Node Caption Uses Display Binding

- **Objective**: dynamic display names come from declared manifest bindings.
- **Input**: synthetic package graph with Tile `display_name` set to `Alpha Tile`.
- **Method**: QtTest/UI inspection.
- **Pass criteria**:
  - canvas caption shows `Alpha Tile`
  - runtime UUID is not the main caption

### UX-02: Static Fallback Without Display Binding

- **Objective**: `display_name` is not an implicit hidden convention.
- **Input**: package variant with no module display binding but with a `display_name` parameter.
- **Method**: QtTest/service test.
- **Pass criteria**:
  - UI uses static module label/name
  - it does not silently treat `display_name` as a binding

### UX-03: Connection Option Labels Use User-Facing Text

- **Objective**: connection UX uses display names and interface labels.
- **Input**: two connectable synthetic modules with display names and labeled interfaces.
- **Method**: `connectionruleservice_test` or UI interaction.
- **Pass criteria**:
  - option label contains module display names
  - option label contains interface labels
  - option label does not expose raw UUIDs as main text

### UX-04: Duplicate Labels Use Short Labels First

- **Objective**: ambiguous display labels remain usable without leading with debug IDs.
- **Input**: two endpoints with same `display_name` and different `external_id` short labels.
- **Method**: `connectionruleservice_test` or UI interaction.
- **Pass criteria**:
  - duplicate choices require selection
  - labels use short labels before stable debug IDs

### UX-05: No Package-ID Special Case In Connection Rules

- **Objective**: generic packages work without per-IP branches.
- **Input**: synthetic package id `phase.synthetic.noc`.
- **Method**: connection rule service test.
- **Pass criteria**:
  - valid connections are accepted through manifest metadata
  - invalid connections are rejected through manifest metadata
  - behavior does not depend on `finepaper.noc`, `finepaper.opennoc`, or `finepaper.ravenoc`

## Topology Metadata Review

### TR-01: Mesh From Non-Cardinal Interface IDs

- **Objective**: topology presets do not require `north/east/south/west` interface IDs.
- **Input**: synthetic package with `fabric_tx`, `fabric_rx`, `vertical_tx`, `vertical_rx`.
- **Method**: `topology_preset_test` and/or UI mesh creation.
- **Pass criteria**:
  - 2x2 mesh is created
  - graph has four Tile instances
  - generated links use manifest topology side/opposite metadata

### TR-02: Preset Resolves Ports From Metadata

- **Objective**: topology behavior is driven by manifest metadata.
- **Input**: preset variant where explicit `ports` mapping is missing or incomplete but interface topology metadata is present.
- **Method**: `topology_preset_test`.
- **Pass criteria**:
  - builder resolves source interface by `topology.side`
  - builder resolves target interface by `topology.opposite`

### TR-03: Non-Opposite Link Rejected

- **Objective**: topology opposite metadata is enforced.
- **Input**: attempted connection from fabric side to a non-opposite fabric side.
- **Method**: connection rule service test or UI interaction.
- **Pass criteria**:
  - connection is rejected
  - diagnostic names incompatible topology sides or opposite-interface mismatch

### TR-04: Preset Parameter Bindings

- **Objective**: preset instantiation uses manifest-declared parameter bindings.
- **Input**: 2x2 synthetic mesh.
- **Method**: Qt graph inspection after preset creation.
- **Pass criteria**:
  - each Tile has stable instance ID
  - each Tile has correct row/col parameters according to declared bindings
  - display/external/collapsed defaults are populated only where declared

### TR-05: Legacy Heuristics Are Not The Success Path

- **Objective**: fallback heuristics can remain but must not hide metadata failures.
- **Input**: synthetic package with non-legacy names.
- **Method**: code review plus topology/connection test evidence.
- **Pass criteria**:
  - tests fail if topology metadata is removed
  - tests do not pass solely because names resemble legacy NoC ports

## Command Input Boundary Review

### CI-01: Commands Receive `ipcraft.noc.project.v1`

- **Objective**: validate/generate commands use the standardized command input schema.
- **Input**: synthetic package command declaration and Qt generation request.
- **Method**: `ipcoregraphexporter_test` or `projectgenerationrunner_test`.
- **Pass criteria**:
  - exported JSON has `schema: ipcraft.noc.project.v1`
  - declared package command input schema matches

### CI-02: Command Input Contains Required Design State

- **Objective**: command input is sufficient for generation without `.fpproj`.
- **Input**: synthetic graph with modules, parameters, interfaces, connections, and package instance state.
- **Method**: inspect generated command input JSON.
- **Pass criteria**:
  - includes package ID
  - includes project and IP instance identity
  - includes module instances with manifest module IDs
  - includes parameters
  - includes interface references
  - includes selected connection class and normalized endpoints
  - includes package instance state when present

### CI-03: Display Text Is Not Identity

- **Objective**: generators identify modules and interfaces without display names.
- **Input**: synthetic graph whose display names differ from module IDs and interface IDs.
- **Method**: command input inspection plus generator run.
- **Pass criteria**:
  - identity fields use stable manifest module/interface IDs and instance IDs
  - display text remains parameter data only
  - generation succeeds after display names are changed

### CI-04: Command Input Is Not Saved Project

- **Objective**: keep command input distinct from `.fpproj`.
- **Input**: Qt generation run.
- **Method**: inspect file names, logs, and exported JSON.
- **Pass criteria**:
  - command input filename contains `command-input` or `ipcraft-input`
  - UI/log text does not describe it as the saved project file
  - generator does not parse `.fpproj`

### CI-05: Legacy Export Boundary

- **Objective**: legacy graph input remains only where existing compatibility tests need it.
- **Input**: existing legacy generator/reference tests.
- **Method**: test transcript and review of command declarations.
- **Pass criteria**:
  - new package command declarations use `ipcraft.noc.project.v1`
  - any legacy input path is explicitly compatibility-scoped

## Common Generator Review

### GR-01: Synthetic Common Generation

- **Objective**: framework-owned generator can use manifest metadata and command input.
- **Input**:
  - synthetic `ipcraft.json`
  - synthetic `ipcraft.noc.project.v1` input
  - empty output directory
- **Method**:

```bash
ipcraft-generate --manifest <fixture>/ipcraft.json --input <input.json> --output <out>
```

- **Pass criteria**:
  - command exits 0
  - `<out>/manifest.json` exists
  - output manifest names the package and reports graph counts or generated artifacts

### GR-02: Generator Does Not Depend On CWD

- **Objective**: framework tool resolution and generation are path-explicit.
- **Input**: synthetic manifest/input/output paths.
- **Method**: run `ipcraft-generate` from a different current working directory.
- **Pass criteria**:
  - command exits 0
  - generated output is written to the requested output directory

### GR-03: Invalid Input Rejection

- **Objective**: generator validates black-box command input.
- **Input**: mutated synthetic command inputs:
  - package mismatch
  - unsupported schema
  - duplicate instance ID
  - unknown module ID
  - unknown instance reference
  - unknown interface reference
  - unknown connection class
  - invalid endpoint shape
  - non-rectangular or inconsistent mesh coordinates
- **Method**: run `ipcraft-generate` for each mutation.
- **Pass criteria**:
  - each invalid case exits nonzero
  - diagnostic names the relevant package, schema, instance, interface, connection, or graph shape
  - failed generation does not leave a success `manifest.json`

### GR-04: Finepaper NoC Structural Parity

- **Objective**: common generator preserves key `finepaper-noc` outputs.
- **Input**: package manifest and command input for a small mesh.
- **Method**: run common generator and inspect generated files.
- **Pass criteria**:
  - `manifest.json` exists
  - filelist exists
  - key RTL/template output exists
  - manifest reports expected router and endpoint counts

### GR-05: OpenNoC Projection Parity

- **Objective**: common generator can project OpenNoC mesh metadata.
- **Input**: OpenNoC 2x2 command input.
- **Method**: run common generator and inspect normalized JSON projection.
- **Pass criteria**:
  - mesh projection JSON exists
  - normalized XP coordinate and agent projection matches expected structural values
  - missing vendor assets are reported only when vendor-backed output is requested

### GR-06: RaveNoC Projection Parity

- **Objective**: common generator can project RaveNoC config/filelist outputs.
- **Input**: RaveNoC 2x2 command input.
- **Method**: run common generator and inspect generated files.
- **Pass criteria**:
  - configuration/header output exists
  - filelist exists
  - manifest reports expected tile count and dimensions

### GR-07: Failure Does Not Present Partial Success

- **Objective**: failed generation cannot look successful.
- **Input**: output directory that already contains a prior successful `manifest.json`, then invalid command input.
- **Method**: run common generator with invalid input into the same output directory.
- **Pass criteria**:
  - command exits nonzero
  - review evidence makes clear the failed run did not produce or preserve a misleading success result

## Package Command Routing Review

### PR-01: Package Generate Commands Use Framework Tool

- **Objective**: packages route generation through framework-owned generator where declared.
- **Input**: regenerated manifests for all three packages.
- **Method**: inspect `ipcraft.json` and run Qt generation runner tests.
- **Pass criteria**:
  - `commands.generate.framework_tool` is `ipcraft-generate`
  - `input_schema` is `ipcraft.noc.project.v1`
  - args use `{manifest}`, `{input}`, and `{output}`

### PR-02: Framework Tool Resolution

- **Objective**: Qt can find framework-owned tools without relying on process cwd.
- **Input**: test tool search path containing an executable `ipcraft-generate`.
- **Method**: `projectgenerationrunner_test`.
- **Pass criteria**:
  - runner resolves the tool from configured search paths
  - missing tool error names searched paths

### PR-03: Package-Local Generators Remain References

- **Objective**: migration does not delete reference flows before parity exists.
- **Input**: package-local generator tests for the three packages.
- **Method**:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
```

- **Pass criteria**:
  - tests pass, or failures are explicitly tied to intentional command path assertion updates
  - no legacy generator is removed before common parity evidence exists

## Manual UX Review

Manual UX review is required for behavior that is user-visible and expensive to
prove through service-level tests alone. It must be performed with a clean Qt
settings profile.

### MU-01: Package Root Management Discovery

- **Objective**: ordinary users can discover package root management.
- **Method**:
  - start Qt frontend with clean settings
  - inspect top-level menus
- **Pass criteria**:
  - expected path `Tools -> IP Core Packages...` or equivalent is visible
  - action wording clearly refers to IP package roots

### MU-02: Add, Persist, Restart

- **Objective**: package roots persist across app sessions.
- **Method**:
  - add synthetic package root through UI
  - confirm package appears
  - restart app with the same profile
- **Pass criteria**:
  - root remains configured
  - package is loaded after restart

### MU-03: Diagnostics Visibility

- **Objective**: package load failures are visible.
- **Method**:
  - add one valid and one malformed package root
  - inspect log panel or diagnostics UI
- **Pass criteria**:
  - malformed package diagnostic is visible
  - valid package remains usable

### MU-04: Display Labels In Canvas And Connection UI

- **Objective**: user-facing names are visible in normal workflows.
- **Method**:
  - create synthetic nodes
  - set display names and short labels
  - open connection selection UI
- **Pass criteria**:
  - canvas uses display names
  - connection choices use display/interface labels
  - duplicate display labels use short labels before debug IDs

### MU-05: Run Generate From Qt

- **Objective**: command input and framework tool routing work from the UI.
- **Method**:
  - create a small synthetic graph
  - run generate
  - inspect logs and output directory
- **Pass criteria**:
  - command input is described as command input, not a saved project file
  - output `manifest.json` exists
  - logs show the framework tool command and output path

## Suggested Review Commands

The runnable review harness for the synthetic package and CLI/common-generator
checks is:

```bash
ruby docs/superpowers/readiness/ipcraft_noc_phase_review_test.rb
```

The same harness can also build and run the Qt review targets when explicitly
requested:

```bash
IPCRAFT_PHASE_REVIEW_QT=1 ruby docs/superpowers/readiness/ipcraft_noc_phase_review_test.rb
```

Set `IPCRAFT_PHASE_REVIEW_QT_UI=1` together with `IPCRAFT_PHASE_REVIEW_QT=1`
to include the heavier UI-adjacent `ipcatalogpanel_test` target.

Set `IPCRAFT_PHASE_REVIEW_EVIDENCE_DIR=<dir>` to keep generated fixtures and
command inputs in a review evidence directory. Set `IPCRAFT_PHASE_REVIEW_KEEP=1`
to keep the temporary evidence directory for failed local runs.

Run the focused automated evidence commands when the environment supports them:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
spec-gen check --ipcore ipcores/finepaper-noc/ipcore.yml --package-root ipcores/finepaper-noc
spec-gen check --ipcore ipcores/opennoc/ipcore.yml --package-root ipcores/opennoc
spec-gen check --ipcore ipcores/ravenoc/ipcore.yml --package-root ipcores/ravenoc
xmake build -P qt appsettings_test
xmake build -P qt ipcatalogservice_test
xmake build -P qt ipcraftmanifest_test
xmake build -P qt ipcraft_phase_review_test
xmake build -P qt connectionruleservice_test
xmake build -P qt topology_preset_test
xmake build -P qt ipcoregraphexporter_test
xmake build -P qt projectgenerationrunner_test
xmake run -P qt appsettings_test
xmake run -P qt ipcatalogservice_test
xmake run -P qt ipcraftmanifest_test
xmake run -P qt ipcraft_phase_review_test
xmake run -P qt connectionruleservice_test
xmake run -P qt topology_preset_test
xmake run -P qt ipcoregraphexporter_test
xmake run -P qt projectgenerationrunner_test
# Optional UI-adjacent target:
xmake build -P qt ipcatalogpanel_test
xmake run -P qt ipcatalogpanel_test
```

If a command is unavailable because the review environment lacks build outputs,
Qt platform support, or vendor assets, record it as `ENVIRONMENT_GAP` with the
exact failing command and stderr.

## Coverage Map

| Design/Plan Target | Review Cases |
| --- | --- |
| `ipcore.yml` plus views are source of truth | SR-01, SR-02, SR-03, SR-09 |
| specgen validates malformed package source | SR-04, SR-05, SR-06, SR-07, SR-08 |
| Qt runtime consumes `ipcraft.json`, not YAML | QR-01, QR-02 |
| package root management is discoverable and reloadable | QR-03, QR-04, QR-05, MU-01, MU-02 |
| invalid packages and duplicate IDs are diagnosed | QR-06, QR-07, MU-03 |
| display labels replace internal IDs in UX | UX-01, UX-02, UX-03, UX-04, MU-04 |
| topology uses metadata, not hardcoded names | TR-01, TR-02, TR-03, TR-04, TR-05 |
| command input is `ipcraft.noc.project.v1` | CI-01, CI-02, CI-03 |
| command input is distinct from `.fpproj` | CI-04, CI-05, MU-05 |
| common generator is framework-owned | GR-01, GR-02, PR-01, PR-02 |
| common generator validates bad input | GR-03, GR-07 |
| finepaper/opennoc/ravenoc parity exists | GR-04, GR-05, GR-06, PR-03 |
| legacy generators remain until parity exists | PR-03 |

## Final Report Template

The phase review report should include:

```markdown
# Ipcraft NoC Specgen Hardening Phase Review Report

## Environment

- Date:
- Reviewer:
- Git revision:
- Ruby:
- Qt/xmake:
- Tools:

## Inputs

- Synthetic fixture path:
- Existing package roots:
- Qt settings profile:
- Vendor assets present:

## Executed Evidence

| Case | Method | Evidence Path/Command | Result | Classification |
| --- | --- | --- | --- | --- |

## Failures

| Case | Classification | Reproduction | Expected | Actual |
| --- | --- | --- | --- | --- |

## Gaps

| Gap | Classification | Required Clarification or Environment |
| --- | --- | --- |

## Verdict

APPROVED / APPROVED_WITH_GAPS / REJECTED
```
