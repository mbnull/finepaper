# Ipcraft NoC Specgen Hardening Design

## Summary

This work hardens the current NoC package flow around the three existing IP cores:

- `finepaper-noc`
- `opennoc`
- `ravenoc`

The goal is to make `ipcore.yml` plus `views/*.xml` the maintained source for package metadata, with `specgen` producing the runtime `ipcraft.json` consumed by Qt. Qt should not contain IP-specific branches for these packages, and the generator path should move away from package-local handwritten generator logic toward framework-owned generation driven by declarative `generation` metadata.

The first implementation remains grounded in the three existing IP cores. Broader NoC acceptance material will be written after this implementation is complete, as a separate function, support, and test list for independent validation.

## Goals

- Make `specgen` the required source of runtime manifests for the three existing NoC packages.
- Keep package authors focused on `ipcore.yml`, view XML, and declarative `generation` metadata.
- Remove UX hardcoding that blocks ordinary users from discovering and loading IP packages.
- Make connection UX use display names and labels instead of internal UUIDs or raw runtime IDs.
- Move frontend assumptions such as fixed NoC node names, fixed endpoint names, and fixed cardinal directions into manifest/view metadata where possible.
- Standardize package command input on `ipcraft.noc.project.v1`.
- Start migrating generation behavior into a framework-owned common generator driven by manifest metadata.
- Preserve the existing three IP generation flows while adding tests around the new generic path.
- Define the boundary for later third-party acceptance tests without writing the acceptance checklist in this phase.

## Non-Goals

- Do not implement support for a new external NoC IP in this phase.
- Do not write the post-implementation function/support/test acceptance list in this phase.
- Do not make Qt parse `ipcore.yml` in the runtime path.
- Do not keep per-IP Qt branches for `finepaper-noc`, `opennoc`, or `ravenoc`.
- Do not require package authors to maintain Ruby, Python, or C++ generator code for ordinary NoC package adaptation.
- Do not remove legacy generators until the common generation path has regression coverage for the same behavior.
- Do not redesign the saved project document format beyond the command input export needed here.

## Current Problems

Package discovery exists but is not usable enough. `AppSettings` stores `ipcores/paths`, and the catalog loader reads package roots from that setting, but the main UI does not expose a clear action for adding, removing, or reloading package roots. A clean user environment can therefore show no IPs with no obvious recovery path.

The connection UX still leaks internal identifiers. When multiple legal connection options exist, option labels are built from module IDs and port IDs. These IDs are stable for the graph, but they are not the display names users expect.

Several frontend behaviors still assume a narrow NoC shape. `PortLayout` recognizes `north`, `east`, `south`, `west`, `ep*`, and `local*`. `TopologyPresetBuilder` understands mesh and ring with hardcoded logical directions. Some node layout and attachment behavior is metadata-aware, but there are still fallback paths that make the built-in NoC shape the implicit default.

The generation path is not yet spec-first. The three packages already expose `ipcraft.json`, and `opennoc` and `ravenoc` accept `ipcraft.noc.project.v1`, but package-local generator classes still contain package IDs, module type names, agent mappings, vendor file requirements, and output projection logic as code.

## Architecture

The design has four layers.

### Authoring Layer

Each NoC package source is maintained as:

```text
ipcore.yml
views/*.xml
```

`ipcore.yml` describes the package identity, modules, interfaces, parameters, connection classes, topology presets, commands, and `generation` metadata. View XML describes presentation only: shapes, anchors, attachment zones, labels, and collapsed/expanded geometry.

The `generation` section is configuration, not package-local generator code. It declares how framework-owned generation should project a command input graph into output artifacts.

### Specgen Layer

`specgen` validates package sources and writes `ipcraft.json`.

`specgen` must:

- validate the constrained `ipcore.yml` shape;
- load and validate view XML references;
- normalize display labels, module IDs, interfaces, connection classes, topology presets, and commands;
- validate `generation` metadata syntax;
- reject unresolved module/interface/parameter references;
- write deterministic `ipcraft.json`;
- fail with precise diagnostics.

For this phase, `specgen` is considered correct only if it can regenerate equivalent runtime manifests for `finepaper-noc`, `opennoc`, and `ravenoc`.

### Qt Runtime Layer

Qt consumes runtime packages through `ipcraft.json` and referenced view XML. It does not parse source YAML during normal editing.

Qt must:

- expose package root management in the UI;
- reload the catalog after package root changes;
- show package load diagnostics in a user-visible log;
- register modules, interfaces, presets, commands, and views from manifests;
- use labels and display names for user-facing connection choices;
- export `ipcraft.noc.project.v1` for validate/generate commands;
- avoid IP-specific code paths for the three existing NoC packages.

### Common Generation Layer

The common generator is framework-owned. It receives:

```text
ipcraft.json
ipcraft.noc.project.v1 command input
output directory
```

It uses the manifest `generation` metadata to create package-specific outputs. The package declares the mapping; the framework owns the execution logic.

The first supported generation capabilities should cover the behavior already present in the three existing packages:

- internal NoC RTL/template generation for `finepaper-noc`;
- mesh JSON projection and vendor tool invocation for `opennoc`;
- parameter/header/filelist/template projection for `ravenoc`.

The existing package-local generators remain as migration references until the common path has tests proving equivalent key outputs.

## Runtime Package Discovery UX

Add a top-level UI path for IP package roots. The exact UI can be a menu action or settings dialog, but it must support:

- adding a package root directory;
- removing a configured root;
- reloading packages without restarting the app;
- showing configured roots;
- showing load errors for invalid roots or malformed packages;
- persisting roots through `AppSettings::ipcorePaths()`.

Startup behavior should be explicit. If no package roots are configured or no valid packages are discovered, the log panel should say so and point users to the package-root action.

The catalog should not partially register a malformed package. One bad package should produce a diagnostic and not prevent unrelated valid packages from loading.

## Display Names and Connection Options

Connection option labels should be composed from display-oriented fields:

- module display name from the `display_name` parameter when present;
- manifest module label/name as a fallback;
- interface label/name from manifest metadata;
- port label/name as a final fallback;
- internal graph IDs only as a last-resort debug fallback.

The stable internal IDs remain unchanged. This is a presentation change, not a graph identity change.

Connection lists should still remain unambiguous. If two options have the same display text, append a small disambiguator derived from stable IDs.

## Frontend Hardcoding Cleanup

This phase should remove or isolate hardcoded NoC assumptions where they affect user-visible behavior or package generality.

The target changes are:

- `PortLayout` should prefer manifest/view-provided side, normal, attachment, and topology metadata before falling back to hardcoded `north/east/south/west` or `ep/local` heuristics.
- `TopologyPresetBuilder` should read topology links from manifest metadata instead of assuming that mesh ports are named `east`, `west`, `north`, and `south`.
- Preset module instantiation should use manifest-declared parameter bindings for coordinates, external IDs, display names, and collapsed state instead of checking fixed parameter names first.
- Connection rule checks should continue using manifest interface metadata and should not special-case package IDs.
- Node painting and attachment layout should use view XML and module metadata wherever that data exists.

Fallback heuristics can remain for legacy packages, but they must not be the primary path for the three current packages after `specgen` regeneration.

## Command Input JSON

`ipcraft.noc.project.v1` is the command input export from the current Qt project state. It is not a user-maintained source file and should not be described as the saved project document.

The export must contain enough design instance state for generators:

- package ID;
- project and IP instance identity;
- module instances with manifest module IDs;
- parameters;
- interfaces;
- connections with selected connection class and normalized interface references;
- package instance state.

Package commands must declare `input_schema: ipcraft.noc.project.v1`. Legacy graph input may remain for compatibility tests, but new command declarations should use the public schema.

## Generation Metadata

The `generation` section in `ipcore.yml` should be declarative. It describes what to generate and how project data maps to output fields.

The first schema should be small and driven by observed needs in the three existing packages. It should not attempt to model every future generator feature.

Examples of acceptable `generation` metadata:

- output artifact names;
- template file paths;
- vendor source requirements;
- command invocations with declared inputs and outputs;
- module-type to vendor-enum mappings;
- coordinate field bindings;
- attachment port group bindings;
- parameter projection rules;
- file copy rules.

Examples of behavior that should not live in package code:

- package ID constants duplicated in generator classes;
- fixed module type lists embedded in generator code;
- agent type maps embedded in generator code;
- vendor file lists embedded in generator code when they can be declared;
- output filenames embedded in per-IP generator classes.

If the common generator cannot express one of the three current package behaviors, the implementation should add a framework-owned generation capability and cover it with tests.

## Migration Strategy

The migration should be incremental.

Phase 1: make `specgen` authoritative for runtime manifests.

- Add or harden source `ipcore.yml` definitions for all three packages.
- Regenerate `ipcraft.json` deterministically.
- Add drift tests that fail when committed manifests do not match `specgen` output.

Phase 2: harden Qt UX and remove visible hardcoding.

- Add package path management and reload.
- Fix connection option display labels.
- Move preset and port-layout behavior toward manifest/view metadata.
- Add focused Qt tests for the generic metadata paths.

Phase 3: introduce the common generator path.

- Define the first `generation` metadata subset.
- Implement framework-owned generation for the three current output shapes.
- Keep package-local generators as references during migration.
- Add golden or structural output tests.

Phase 4: retire duplicated package-local generator logic.

- Point package command manifests at the common generator.
- Remove or quarantine package-local generator code only after parity tests pass.
- Keep compatibility tests for legacy command input where needed.

## Test Plan

Implementation should follow TDD. Tests should be added before or alongside behavior changes.

### Specgen Tests

- `specgen` accepts the three package source directories.
- `specgen` rejects malformed `ipcore.yml` with precise errors.
- `specgen` rejects view XML that references missing modules or interfaces.
- Generated `ipcraft.json` is deterministic.
- Generated `ipcraft.json` matches committed runtime manifests for the three packages.
- `generation` metadata validates unresolved module, parameter, interface, template, and vendor references.

### Qt UX Tests

- `AppSettings` persists IP package roots.
- The catalog service reloads packages from configured roots.
- Invalid package roots produce diagnostics and do not register partial packages.
- An empty catalog produces a user-facing diagnostic.
- Connection options use display names and labels instead of internal graph IDs where metadata exists.
- Duplicate display labels are disambiguated.
- Topology presets can create package-owned graphs using manifest metadata.
- Preset creation does not depend on hardcoded package IDs or module type names.

### Command Input Tests

- Generate and validate commands receive `ipcraft.noc.project.v1` when declared.
- The export uses manifest module IDs and normalized interface references.
- The export preserves selected connection class, connection status, and alternatives.
- Legacy graph export remains available only where existing tests still need it.

### Common Generator Tests

- The common generator can reproduce key `finepaper-noc` artifacts from command input.
- The common generator can reproduce key `opennoc` mesh projection artifacts from command input.
- The common generator can reproduce key `ravenoc` configuration/template/filelist artifacts from command input.
- Missing vendor requirements fail with actionable errors.
- Invalid graph shapes fail before writing partial output.

### Regression Commands

The implementation plan should select concrete commands from the repository. Expected categories are:

- specgen unit tests;
- Qt unit tests;
- package generator tests for the three existing IPs;
- smoke tests that do not require unavailable vendor sources;
- optional vendor-backed smoke tests when vendor sources are present.

## Third-Party Acceptance Boundary

The independent acceptance material is intentionally not written in this phase. It should be created only after implementation is complete.

The later acceptance document may assume that third-party agents can read code, specs, tests, and generated artifacts. It should also constrain those agents:

- They should validate public package behavior, not private implementation details.
- They should not require support for schema capabilities not declared by this implementation.
- They may construct synthetic NoC packages to verify that a new package can load without Qt changes.
- They may classify a required Qt change as a public schema gap when the requested NoC behavior is generic.
- They should not require undisclosed vendor assets unless an acceptance list explicitly permits those assets.
- They should test documented boundaries: discovery, manifest validation, view loading, connection rules, topology presets, command input export, and generation metadata.

The implementation spec should therefore leave enough public contracts and tests for a later independent agent to derive acceptance checks without relying on private conversation context.

## Risks

The largest risk is making `generation` metadata too broad too early. The first schema should be limited to capabilities proven by the three existing packages.

The second risk is preserving hidden frontend assumptions through fallback behavior. Tests should exercise metadata-driven paths directly, not only legacy names that happen to work.

The third risk is migrating generators without parity checks. Package-local generators should remain available until the common generator has structural output tests for each existing package.

## Implementation Defaults

The implementation plan should use these defaults unless code inspection shows a concrete blocker:

- Common generator CLI: `ipcraft-generate --manifest <ipcraft.json> --input <command-input.json> --output <dir>`.
- First `generation` metadata subset: outputs, templates, file copies, vendor requirements, external command invocations, module mappings, coordinate bindings, attachment bindings, and parameter projections.
- Manifest drift tests compare normalized full JSON objects, not raw formatting.
- Package root management starts as a menu action that opens a lightweight dialog with add, remove, reload, and diagnostics. A fuller preferences window can come later.
- Legacy generator tests become parity tests during migration. Remove them only after the common generator command is the package command and parity coverage is in place.
