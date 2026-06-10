# IP Package Authoring Flow

Finepaper packages are the public extension surface for IP integration. Internal plugins are Finepaper C++ architecture modules; extensions/packages are external or first-party IP deliverables described by V1 data and package-owned tools.

This guide is the handoff between an existing IP codebase and the Qt frontend flow. It does not define a legacy generator compatibility path and does not require a native C++ ABI.

## Package Anatomy

A commercial package can contain:

```text
ipcraft.json
tools/
generator/
rtl/
vendor/
docs/
examples/
```

`ipcraft.json` is the entry point and uses `ipcraft.package.v1`. The package owns:

- identity, version, display name, and documentation links;
- module and interface declarations;
- configuration schema and default values;
- package-declared connection rules and connection classes;
- capability sections such as NoC topology metadata;
- view/editor descriptors consumed by internal plugins;
- emitters and flows;
- artifact declarations;
- diagnostic parser declarations;
- examples and validation data.

Concrete IP behavior belongs in this package data and package tools. The kernel and workbench shell should not learn the difference between `finepaper.noc`, `finepaper.ravenoc`, `finepaper.opennoc`, or a future package by hardcoded branches.

## Qt Frontend Path

The frontend path for an authored package is:

```text
package root
  -> loadIpcraftPackageManifests
  -> PackageService / IpCatalogService
  -> editor projection and property panels
  -> ConnectionRuleService
  -> ProjectGenerationRunner
  -> FlowRunner
  -> emitted inputs, generated artifacts, diagnostics, reports
```

The package is discovered and parsed once into typed descriptors. Internal plugins register services that interpret those descriptors. Packages do not call each other directly; they declare metadata and rule providers are invoked by platform services.

## Schema Contracts

Use the existing V1 contracts:

- `ipcraft.package.v1` for package authoring;
- `ipcraft.project.v1` for durable project state;
- `ipcraft.graph-config.v1` for instance internal graph/configuration;
- `ipcraft.emitted-inputs.v1` for generator command inputs;
- `ipcraft.diagnostics.v1` and `ipcraft.diagnostic.v1` for structured diagnostics.

Do not add a package-specific JSON dialect when an existing V1 schema fits. Add new descriptors only when an internal plugin needs a missing extension point, and keep the descriptor in the package schema boundary.

## Connection Rules

Fast connection checks should be data-driven:

- the package declares interfaces, roles, bus classes, cardinality, topology sides, and accept rules;
- `ConnectionRuleService` reads package manifests and calls registered rule providers;
- NoC-specific algorithms can live in the NoC internal plugin when the rule cannot be represented as package data;
- packages do not directly invoke other packages or internal plugins.

Adding a new bus or topology family should mean adding package declarations and, if needed, one internal provider registration. It should not require broad edits across editor widgets.

## Generation Flow

Generation should use package flows rather than direct generator calls from UI code:

1. `ProjectGenerationRunner` selects each project IP instance.
2. The package flow emits `ipcraft.emitted-inputs.v1` into an isolated run root.
3. `FlowRunner` executes package commands with placeholders such as `{package.manifest}`, `{inputs.manifest}`, and `{out}`.
4. The package tool reads `ipcraft.emitted-inputs.v1` and any referenced `ipcraft.graph-config.v1` files.
5. Artifact collection uses package artifact declarations and writes generation manifests.

The public output directory should contain generated files and audit inputs. The protected run root is the command execution boundary, so generator-side changes to emitted input manifests are treated as errors rather than silently accepted.

## Agent Onboarding Checklist

When adapting an existing IP codebase:

1. Inventory RTL, generated files, vendor files, scripts, examples, tests, parameters, interfaces, clocks, resets, and license constraints.
2. Choose the package id and module ids.
3. Write `ipcraft.json` with `ipcraft.package.v1`.
4. Map parameters into config schema fields.
5. Map ports into package interfaces and connection classes.
6. Declare topology capability data when the IP needs topology-aware editing.
7. Define emitters and a `generate` flow.
8. Declare artifacts that prove a usable commercial output was produced.
9. Add a focused Qt test for package load, editor graph construction, validation, generation, and artifact collection.
10. Verify against `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc` when the package is NoC-like.

The expected agent output is a short onboarding report with the package id, files inspected, config mapping, interface mapping, flow command, artifact list, verification commands, and unsupported source-IP requirements.
