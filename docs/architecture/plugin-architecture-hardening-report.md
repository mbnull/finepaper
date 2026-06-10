# Plugin Architecture Hardening Report

This report records the state before the final Phase 10 architecture review. It is a hardening gate, not a claim that every historical adapter has been deleted.

## Phase Coverage Matrix

| Phase | Boundary | Status | Evidence |
|-------|----------|--------|----------|
| Phase 2 | Project plugin and V1 source of truth | Complete | Project services, project scan gate, V1 project contract tests |
| Phase 3 | Package plugin and extension loading | Complete | `PackageService`, `PackagePlugin`, package scan gate |
| Phase 4 | Editor shell rebinding | Complete | `EditorProjectionService`, editor shell scan gate |
| Phase 5 | Data-driven connection checking | Complete | `ConnectionRuleProvider`, package rule provider, connection scan gate |
| Phase 6 | Tool/generator pipeline boundary | Complete | `GenerationFlowProvider`, `ToolPipelineService`, tool scan gate |
| Phase 7 | Commercial NoC workflow completion | Complete with vendor-fixture debt | `commercial_noc_mvp_test`, Phase 7 scan gate |
| Phase 8 | Agent IP onboarding skill/prompt | Complete | `finepaper-ip-onboarding`, package authoring flow, Phase 8 scan gate |
| Phase 9 | Hardening and deletion gates | In progress | This report and `plugin_architecture_phase9_scan_test` |
| Phase 10 | Final architecture review and report | Pending | Requires `qt-cpp-review` and completion report |

## Boundary Status

| Boundary | Current state | Hardening gate |
|----------|---------------|----------------|
| Project | `ProjectDocument`, `ProjectService`, and V1 project schema remain the durable source of truth. | New normal paths must not treat `Graph` as durable project state. |
| Package | Extension/package discovery and catalog construction are behind package services. | External/public IP deliverables must be called extensions or packages, not plugins. |
| Editor | The node editor remains as a projection shell. | Editor code may use projection `Graph` objects for interaction, but save/generate/validate must come from project/package state. |
| Connection | Connection checks are routed through providers and package declarations. | Core/UI must not hardcode concrete connection type behavior. |
| Tool | Validate/generate flow uses package-declared flows and `FlowRunner`. | UI code must not directly invoke package generators. |
| Commercial NoC | `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc` pass the commercial workflow gate. | Vendor fixtures remain a known test debt; the Qt-to-generator contract is covered. |
| Onboarding | `finepaper-ip-onboarding` guides agents through source-IP inspection and V1 package authoring. | The skill must stay operational and concise; detailed architecture belongs in docs. |

## Three-IP Anchor Matrix

| Anchor package | Package load | Catalog/graph | Connection checks | Generation | Artifacts |
|----------------|--------------|---------------|-------------------|------------|-----------|
| `finepaper.noc` | Covered | Covered | Covered | Covered | `rtl/top.v`, `filelist.f`, `manifest.json` |
| `finepaper.ravenoc` | Covered | Covered | Covered | Covered | `ravenoc_top.sv`, `ravenoc_config.svh`, `ravenoc_filelist.f`, `manifest.json` |
| `finepaper.opennoc` | Covered | Covered | Covered | Covered | `opennoc.mk`, `mesh_config.json`, `manifest.json` |

The anchor gate proves the Qt frontend path, emitted inputs, package flow execution, artifact collection, and generation manifest output. It uses minimal vendor fixtures for packages that normally require third-party sources.

## V1 Schema Reuse Matrix

| Schema | Role | Current owner |
|--------|------|---------------|
| `ipcraft.project.v1` | Durable project state | Project boundary |
| `ipcraft.package.v1` | Extension/package descriptor | Package boundary |
| `ipcraft.graph-config.v1` | Instance internal graph/config | Package input and editor projection boundary |
| `ipcraft.emitted-inputs.v1` | Generator command input manifest | Tool pipeline boundary |
| `ipcraft.diagnostics.v1` | Diagnostic store | Validation, connection, package, and tool boundaries |
| `ipcraft.diagnostic.v1` | Single diagnostic record | Diagnostic model |

The architecture does not introduce replacement schemas for project, package, graph config, emitted inputs, or diagnostics.

## Hardening And Deletion Gates

| Gate | Required state | Phase 9 classification |
|------|----------------|------------------------|
| Graph source-of-truth | `Graph` may remain as projection/test data, not durable normal-path project state. | Isolate and scan. |
| `MainWindow` assembly | `MainWindow` should render workbench/service contributions instead of growing direct business wiring. | Continue reducing direct assembly; block new hardcoded package paths. |
| UI JSON parsing | UI and panels should not directly parse package capability JSON. | JSON field access belongs in package/descriptor readers. |
| Direct generator calls | UI and domain plugins should not shell out to generators directly. | Generation must use `ProjectGenerationRunner`, `GenerationFlowProvider`, and `FlowRunner`. |
| Connection hardcoding | Core/UI should not embed concrete NoC/AXI/CHI compatibility tables. | Package-declared connection rules and providers are the normal path. |
| Legacy compatibility paths | Old generator input compatibility must not become a new normal path. | No legacy generator dependency for new package onboarding. |

## Phase 10 Review Inputs

Phase 10 must review:

- all architecture scan targets from Phase 2 through Phase 9;
- `commercial_noc_mvp_test`;
- package authoring and onboarding docs;
- V1 schema reuse evidence;
- remaining legacy adapter/deletion debt;
- `qt-cpp-review` output for relevant Qt/C++ changes;
- final go/no-go verdict for the plugin-extensible IP platform architecture.
