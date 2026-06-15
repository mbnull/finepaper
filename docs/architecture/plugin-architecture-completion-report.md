# Plugin Architecture Completion Report

This is the Phase 10 completion report for the plugin-extensible IP creation architecture. It reviews Phases 2 through 10, records the `qt-cpp-review` outcome, and states the remaining architecture debt.

## Final Verdict

Final verdict: go-with-debt.

The architecture is acceptable for the NoC IP creation MVP target because project, package, editor projection, connection rules, generation flow, commercial NoC workflow, and agent onboarding are now covered by V1 contracts and scan gates. The verdict is not a full deletion claim: Graph-centric adapters and some static application wiring remain as documented deletion debt.

## Phase Completion Matrix

| Phase | Boundary | Status | Evidence |
|-------|----------|--------|----------|
| Phase 2 | Project plugin and V1 project source of truth | Complete | Project services and Phase 2 scan gate |
| Phase 3 | Package plugin and extension loading | Complete | `PackageService`, package catalog, and Phase 3 scan gate |
| Phase 4 | Editor shell rebinding | Complete with projection debt | `EditorProjectionService` keeps Graph as projection, not source of truth |
| Phase 5 | Data-driven connection checking | Complete | `ConnectionRuleProvider`, package rules, provider scan gate |
| Phase 6 | Tool and generator pipeline boundary | Complete | `GenerationFlowProvider`, `ToolPipelineService`, `FlowRunner` |
| Phase 7 | Commercial NoC workflow | Complete with vendor-fixture debt | `commercial_noc_mvp_test`, Phase 7 scan gate |
| Phase 8 | Agent IP onboarding | Complete | `finepaper-ip-onboarding` skill, package authoring guide, Phase 8 scan gate |
| Phase 9 | Hardening and deletion gates | Complete | hardening report and Phase 9 scan gate |
| Phase 10 | Final review and report | Complete when this report and Phase 10 scan pass | `qt-cpp-review`, completion report, Phase 10 scan |

## Three-IP Anchor Matrix

| Package | Load and catalog | Editor/project path | Connection checks | Generation | Commercial output evidence |
|---------|------------------|---------------------|-------------------|------------|----------------------------|
| `finepaper.noc` | Covered | Covered | Covered | Covered | RTL, filelist, generation manifest |
| `finepaper.ravenoc` | Covered | Covered | Covered | Covered | generated top/config, vendor RTL fixture, filelist, manifest |
| `finepaper.opennoc` | Covered | Covered | Covered | Covered | mesh wrapper, vendor RTL fixture, filelist, manifest |

The anchor gate proves the Qt frontend path, V1 package load, package-owned flow execution, emitted inputs, artifact collection, and generation manifest output for all three MVP package families.

## V1 Schema Reuse Matrix

| Schema | Role | Status |
|--------|------|--------|
| `ipcraft.project.v1` | Durable project document | Reused as project source of truth |
| `ipcraft.package.v1` | Extension/package descriptor | Reused for package discovery, modules, flows, artifacts, and rules |
| `ipcraft.graph-config.v1` | Instance internal graph/config | Reused at editor projection and generator input boundary |
| `ipcraft.emitted-inputs.v1` | Generator command input manifest | Reused and now validated after provider materialization |
| `ipcraft.diagnostics.v1` | Diagnostic store | Reused across validation, package, flow, and project load errors |
| `ipcraft.diagnostic.v1` | Single diagnostic record | Reused for structured diagnostic records |

No replacement schema was introduced for the MVP architecture.

## Terminology Status

Internal C++ architecture modules are plugins. Public or third-party deliverables are extensions or packages. Package behavior is declared through `ipcraft.package.v1` plus package-owned tools; packages do not call one another directly.

## Legacy Path And Deletion Gate Status

| Gate | Status | Required follow-up |
|------|--------|--------------------|
| Graph source of truth | accepted debt remains only in editor projection | Normal generation uses `ProjectGenerationRequest::projectDesign` and instance-owned `graphConfig`; keep `GraphProjectSerializer` isolated to `EditorProjectionService` until the editor projection shell is replaced |
| `MainWindow` direct assembly | Partially isolated | Move remaining static service assembly behind internal plugin activation when app startup is ready |
| UI JSON parsing | Guarded | Keep package JSON parsing inside package/descriptor readers |
| Direct generator calls | Guarded | UI and domain code must continue using `ProjectGenerationRunner`, providers, and `FlowRunner` |
| Connection hardcoding | Guarded | Add bus/topology behavior through package declarations and rule providers |
| Legacy generator compatibility | Not a normal path | New package onboarding does not require legacy generator compatibility |

## Architecture Scan Status

Architecture scans for Phases 1 through 10 pass at Phase 10 review time. Phase 10 adds a completion scan to verify this report names Phases 2-10, the three anchor package ids, V1 schemas, `qt-cpp-review`, scan status, and deletion-gate status.

## Verification Command Evidence

The following commands were run on this branch during Phase 10 review:

| Command | Result |
|---------|--------|
| `ruby spec_generator/bin/spec-gen --check` | passed, repository manifests up to date |
| `ruby spec_generator/test/spec_generator_test.rb` | passed, 89 runs, 494 assertions |
| `ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb` | passed, 29 runs, 186 assertions |
| `xmake run -P qt commercial_noc_mvp_test` | passed |
| `xmake run -P qt plugin_architecture_phase2_scan_test` | passed after final review scan refresh |
| `xmake run -P qt plugin_architecture_phase5_scan_test` | passed after final review scan refresh |
| `xmake run -P qt plugin_architecture_phase9_scan_test` | passed |
| `xmake run -P qt plugin_architecture_phase8_scan_test` | passed |
| `xmake run -P qt plugin_architecture_phase7_scan_test` | passed |
| `xmake run -P qt graph_test` | passed after connection-service signature cleanup |
| `xmake run -P qt nodeeditor_geometry_test` | passed after connection-command signature cleanup |
| `xmake build -P qt qt` | passed |

## qt-cpp-review Summary

`qt-cpp-review` was run against the changed Qt/C++ files in this branch. The deterministic linter was rerun after the final scan refresh and returned exit 0 with no findings.

Deep review found these high-confidence blockers, all fixed before this report:

| Finding | Resolution |
|---------|------------|
| Public enum missing trailing comma | Fixed `WorkbenchPanelArea` |
| Project load collapsed reader diagnostics | `ProjectService::loadFile()` now uses `ProjectReader::readFile()` diagnostics directly |
| Malformed flow `steps` could succeed as a no-op | `FlowRunner` now rejects missing or non-array `steps` |
| Provider-emitted inputs accepted by existence only | `ProjectGenerationRunner` now parses and validates `ipcraft.emitted-inputs.v1` |
| Provider `runRoot` trust boundary was implicit | `ProjectGenerationRunner` rejects unexpected provider run roots |
| Per-candidate connection validator copies | `ConnectionRuleService` now reuses a validator per option build |
| Dead connection service instance-record state | Removed the stored state and tightened constructor/call sites |

The review also produced investigation targets. The accepted debt items are listed below and are not blockers for the static MVP architecture.

## Accepted Debt And Follow-up

| Debt | Why accepted for MVP | Follow-up |
|------|----------------------|-----------|
| Graph projection adapters remain | Current editor still needs interactive Graph projection | Move generation/save/validate fully to `ProjectDocument`, then delete adapter paths |
| Plugin host is not yet the full app startup path | Internal plugin architecture exists, but MainWindow still owns some static assembly | Route startup through `PluginHost` and plugin registrations |
| Workbench contribution lifetime is static | MVP does not unload internal plugins dynamically | Add contribution removal/deactivation before runtime plugin unload is supported |
| Flow command JSON fields are still partly permissive | Critical `steps`, timeout, capture, run root, and emitted inputs boundaries are guarded | Fail closed for malformed `args`, `env.allow`, and other command subfields |
| Package reload partial state semantics need policy | Current loader exposes diagnostics and partial catalog behavior | Split warning vs fatal diagnostics or preserve previous state on fatal reload |
| Vendor source fixtures are minimal | Qt-to-generator contract and vendor-copy behavior are covered | Add full vendor regression fixtures when licensing and storage policy allow |

## Completion Criteria

The architecture is complete for Phase 10 when:

- this report is linked from the architecture README;
- `plugin_architecture_phase10_scan_test` passes;
- final verification passes after the report and scan are committed;
- final automatic architecture and code-quality reviewers report no high-confidence blocker.
