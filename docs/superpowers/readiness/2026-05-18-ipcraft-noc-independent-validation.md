# Ipcraft NoC Independent Validation Guide

## Purpose

This document is the handoff for a third-party validation agent. It defines the functional support list, validation boundaries, and suggested tests for the NoC package/specgen/generation hardening work completed on 2026-05-18.

The agent may read repository code, specs, committed manifests, tests, and generated artifacts. The agent should not rely on private conversation context.

## Validation Scope

Validate the current framework behavior around these public package flows:

- `finepaper-noc`
- `opennoc`
- `ravenoc`

The core acceptance question is whether a NoC package author can maintain package metadata through `ipcore.yml` plus `views/*.xml`, regenerate `ipcraft.json` through `specgen`, load the package in Qt through configured package roots, use metadata-driven connection/topology UX, and invoke generation through the framework-owned command path.

## Functional Support List

The validation agent should check these supported capabilities.

1. Package source of truth
   - `ipcore.yml` and `views/*.xml` are the maintained package sources.
   - `ipcraft.json` is generated deterministically by `spec_generator/bin/spec-gen`.
   - Manual edits to committed `ipcraft.json` should be detected by the drift check.

2. Package discovery and UX
   - Qt package roots come from `AppSettings::ipcorePaths()`.
   - The UI exposes `Tools -> IP Core Packages...`.
   - Package roots can be added, removed, persisted, and reloaded without restarting.
   - Invalid roots and duplicate package IDs produce diagnostics without blocking unrelated valid packages.

3. Manifest and view loading
   - Qt loads runtime metadata from `ipcraft.json`.
   - Qt does not parse `ipcore.yml` in normal runtime paths.
   - View XML references are validated by `specgen`.
   - View metadata controls node geometry, anchors, and attachment presentation where provided.

4. Display names and connection labels
   - Module display labels come from manifest-declared display bindings when present.
   - Static manifest labels are the fallback when no display binding is declared.
   - Connection option labels use module/interface labels before internal IDs.
   - Duplicate display labels are disambiguated with author-facing short labels before stable debug IDs.

5. Topology metadata
   - Topology side, role, rule, and opposite-interface metadata are declared in package metadata.
   - Mesh/ring presets use manifest metadata for tested paths instead of package IDs or fixed module names.
   - Connection validation enforces `opposite_side` and explicit `opposite_interface` metadata.
   - Legacy name heuristics may remain only as fallback compatibility behavior.

6. Command input export
   - Generate and validate commands use `ipcraft.noc.project.v1` when declared.
   - The export preserves package ID, instance IDs, manifest module IDs, parameters, interfaces, selected connection classes, and normalized endpoint references.
   - The exported command input is not the saved `.fpproj` document and should not be treated as user-maintained project source.

7. Framework-owned generation
   - Package commands can declare `framework_tool: ipcraft-generate`.
   - Qt resolves framework tools from the repository/tool installation path, not from the current working directory.
   - The common generator accepts `--manifest`, `--input`, and `--output`.
   - The common generator produces key artifacts for `finepaper-noc`, `opennoc`, and `ravenoc`.
   - Package-local generator code may remain as a reference, but package command declarations should route new generate commands through the common path.

8. Diagnostics
   - Malformed package sources fail with actionable messages.
   - Invalid command input fails before partial output is accepted as successful.
   - Missing vendor assets are reported as environment limitations unless the tested command explicitly requires them.

## Explicit Non-Scope

Do not fail this validation for items outside the completed support surface:

- Arbitrary free-form topology placement.
- A general-purpose YAML programming language for generation.
- Byte-for-byte reproduction of timestamps, comments, or non-semantic formatting.
- Removing all legacy fallback heuristics.
- Removing every package-local generator file.
- Redesigning the saved `.fpproj` project format.
- Requiring undisclosed vendor assets.
- Requiring support for a new production NoC package unless the test only uses documented schema features.

If a new NoC package requires Qt source changes for behavior that should be generic, record that as a schema/framework gap. If it requires behavior outside the supported schema, record it as a future capability request, not a regression.

## Required Baseline Commands

Run these commands from the repository root.

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --check
ruby -I ipcraft_generator/test ipcraft_generator/test/ipcraft_generator_test.rb
```

Qt test targets should be built and run individually if multi-target `xmake` invocation is not supported in the local environment.

```bash
xmake build -P qt appsettings_test
xmake build -P qt ipcatalogservice_test
xmake build -P qt ipcraftmanifest_test
xmake build -P qt connectionruleservice_test
xmake build -P qt topology_preset_test
xmake build -P qt ipcoregraphexporter_test
xmake build -P qt projectgenerationrunner_test
xmake build -P qt ipcoreruntime_test
xmake build -P qt nodeeditor_geometry_test
xmake build -P qt v1architecturegate_test

xmake run -P qt appsettings_test
xmake run -P qt ipcatalogservice_test
xmake run -P qt ipcraftmanifest_test
xmake run -P qt connectionruleservice_test
xmake run -P qt topology_preset_test
xmake run -P qt ipcoregraphexporter_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt ipcoreruntime_test
xmake run -P qt nodeeditor_geometry_test
xmake run -P qt v1architecturegate_test
```

Run package reference tests and available smoke tests.

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_smoke.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected result: all commands exit 0. If a local machine lacks an optional external tool or vendor source, capture the exact skip/failure message and classify whether the command was supposed to be vendor-free.

## Suggested Independent Tests

The validation agent should add tests in a temporary branch or separate review patch. Production source changes are not required unless a test reveals a real defect.

1. Synthetic package root test
   - Create a temporary package root containing one valid package and one malformed package.
   - Configure `AppSettings::ipcorePaths()` to that root.
   - Assert the valid package loads, the malformed package is rejected, and diagnostics mention the malformed package path.

2. Duplicate package ID test
   - Create two package roots with the same package ID.
   - Assert duplicate diagnostics are emitted and no ambiguous package is silently selected.

3. Non-directional interface name topology test
   - Create a synthetic NoC module with interface IDs such as `link_a_out`, `link_a_in`, `link_b_out`, and `link_b_in`.
   - Declare topology side and opposite-interface metadata in the manifest source.
   - Assert topology preset and connection validation work without interface IDs named `north`, `east`, `south`, or `west`.

4. Display label binding test
   - Create two module instances with different `display_name` values and stable internal IDs.
   - Assert canvas labels and connection option labels show display text, not UUIDs or raw runtime IDs.
   - Create duplicate display text and assert short labels are used before debug IDs.

5. Command input identity test
   - Export `ipcraft.noc.project.v1` from a graph with routers/endpoints and selected connection classes.
   - Assert module identity fields use manifest module IDs and instance IDs, not display names.
   - Assert display parameters are preserved only as parameters.

6. Framework tool resolution test
   - Run generation from a working directory that does not contain `ipcraft_generator/bin/ipcraft-generate`.
   - Assert Qt resolves the framework tool from the repository/tool search path.
   - Assert package-local command paths are not used for `framework_tool` commands.

7. Common generator invalid graph test
   - Feed the common generator malformed `ipcraft.noc.project.v1` inputs: unknown instance, unknown interface, mismatched package ID, and invalid mesh shape.
   - Assert it fails before producing a successful output manifest.

8. Drift enforcement test
   - Modify a generated `ipcraft.json` field by hand.
   - Assert `ruby spec_generator/bin/spec-gen --check` fails.
   - Restore the manifest by rerunning `specgen` and assert the check passes.

## Manual UX Checks

Manual checks should be short and only verify behavior not covered by unit tests.

1. Start the Qt frontend.
2. Open `Tools -> IP Core Packages...`.
3. Add the repository `ipcores` directory as a package root.
4. Confirm the catalog reloads and the log records the package count or diagnostics.
5. Create a NoC topology preset from one of the loaded packages.
6. Open a connection choice where multiple targets are legal and confirm user-facing labels are shown.
7. Run a package validate/generate command and confirm the generated command input path is named as command input, not as a saved project file.

## Review Output Format

The validation agent should report:

- Baseline command results with exact command names.
- Additional tests added, with file paths.
- Any failures classified as one of:
  - regression in supported behavior;
  - missing test coverage but behavior works;
  - schema/framework gap for generic NoC support;
  - future capability outside current scope;
  - environment/vendor asset limitation.
- Whether a new metadata-only NoC package can be loaded and exercised without Qt source changes for the tested behavior.

The final verdict should be either:

- `APPROVED`: baseline commands pass and no supported behavior regression was found.
- `APPROVED_WITH_GAPS`: implementation works for current packages, but generic NoC support has documented schema/framework gaps.
- `REJECTED`: a supported behavior from this document fails.
