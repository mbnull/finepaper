---
name: finepaper-ip-onboarding
description: Use when adapting an existing RTL/IP codebase into Finepaper's Qt frontend, V1 package schema, and extension/package generation flow.
---

# Finepaper IP Onboarding

## Purpose

Use this skill to onboard an existing IP codebase into Finepaper. This is not a legacy migration path and not a native plugin ABI task. The deliverable is a Finepaper extension/package that the Qt frontend can load, configure, validate, generate, and report through the existing V1 contracts.

## Terms

- **Internal plugin**: Finepaper C++ architecture module, such as Project, Package, NoC, Tool Pipeline, or Report.
- **Extension/package**: Public or third-party IP integration delivered through `ipcraft.json`, package tools, docs, examples, and artifacts.
- **Anchor examples**: `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`.

Do not call an external IP package a plugin in user-facing docs or package metadata.

## Workflow

1. Inspect the source IP.
   - Find the top module, parameter names and ranges, clock/reset requirements, bus interfaces, address maps, generated files, vendor files, scripts, examples, tests, and license boundaries.
   - Identify which files can be copied, which must remain vendor-provided, and which outputs Finepaper should generate.

2. Create or update the package root.
   - Use `ipcraft.json` with `schema: "ipcraft.package.v1"`.
   - Keep package id, version, modules, interfaces, config schema, connection classes, capability sections, views, emitters, flows, artifact declarations, diagnostics, docs, and examples in package-owned data.
   - Keep IP-specific behavior out of the kernel and out of unrelated internal plugins.

3. Connect the package to the Qt frontend path.
   - Load package manifests with `loadIpcraftPackageManifests`.
   - Build catalog entries through `IpCatalogService`.
   - Let editor projections and topology tools consume package module/interface metadata.
   - Let connection checks consume package-declared connection rules. Do not make packages call each other directly.
   - Run generation through `ProjectGenerationRunner`, package flow providers, and `FlowRunner`.

4. Use the V1 input contracts.
   - Generate command inputs from `ipcraft.emitted-inputs.v1`.
   - Put graph structure in `ipcraft.graph-config.v1`.
   - Prefer flow steps such as `emit_inputs`, `exec`, and `collect_artifacts`.
   - Use placeholders such as `{package.manifest}`, `{inputs.manifest}`, and `{out}` instead of hardcoding repository paths.
   - Do not add a dependency on a legacy generator input format.

5. Verify the package.
   - Add a focused Qt test when the package creates new frontend behavior.
   - For NoC-style packages, compare against `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`.
   - Run the relevant package test, `xmake run -P qt commercial_noc_mvp_test`, and `xmake build -P qt qt`.
   - Check generated artifacts are real files with useful contents, not empty placeholders.

## Required Agent Output

When finishing an onboarding task, report:

- package id and package root;
- source IP files inspected;
- configuration mapping;
- interface and connection-rule mapping;
- generation flow command and emitted input files;
- artifact declarations and generated outputs;
- verification commands and results;
- unsupported source-IP requirements that remain outside the MVP.
