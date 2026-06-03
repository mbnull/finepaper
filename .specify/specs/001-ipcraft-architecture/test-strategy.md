# IpCraft Test Strategy

Testing must prove public contracts, architecture boundaries, generality, and inspection workflows. Public tests are visible implementation guides. Hidden tests are independent black-box acceptance checks maintained outside implementation branches.

## Test Classes

### Contract Tests

Purpose:

- Verify public schema parse/write/roundtrip.
- Explain public contracts to Implementation Agent.
- Prevent accidental schema drift.

Required coverage:

- `ipcraft.project.v1`
- `ipcraft.package.v1`
- `ipcraft.component.v1`
- `ipcraft.interface.v1`
- `ipcraft.connection_rules.v1`
- `ipcraft.topology.graph.v1`
- `ipcraft.topology.parametric.v1`
- `ipcraft.view.v1`
- `ipcraft.view.descriptor.v1`
- `ipcraft.tool.input.v1`
- `ipcraft.tool.result.v1`
- `ipcraft.diagnostic.v1`
- `ipcraft.artifact.v1`
- `ipcraft.patch.v1`

Every schema must have:

- parser test
- writer test
- roundtrip test
- negative validation test
- golden fixture

### Unit Tests

Purpose:

- Validate core data structures and services.

Required coverage:

- id validation
- duplicate detection
- endpoint ref validation
- extension block ownership
- patch validation
- patch apply/revert
- config provenance generation
- connection compatibility evaluation
- topology node/link validation
- artifact path confinement

### Integration Tests

Required coverage:

- minimal UART package loads
- blackbox Verilog package loads
- example NoC implementation package loads as ordinary package
- CPU -> NIC -> example NoC implementation project validates
- AnyNet explicit topology project validates
- generator receives `ipcraft.tool.input.v1`, not `.fpproj`
- diagnostics map to component/link/attachment/config refs
- layout edits do not affect semantic projection
- package defaults appear in resolved config but not authored config
- tool input preview is deterministic and inspectable

### Golden File Tests

Golden fixtures:

- minimal UART project
- CPU -> NIC -> NoC project
- example NoC implementation mesh topology
- AnyNet explicit topology
- blackbox Verilog project
- package manifests
- component/interface/rule files
- tool input/output
- layout documents
- diagnostic and artifact records

Rules:

- Golden files represent public examples, not hidden acceptance exhaustiveness.
- Production code must not branch on golden filenames or fixture ids.
- Golden output must use deterministic ordering.

### Package Tests

Required coverage:

- package manifest validation
- referenced files exist
- component schemas validate defaults and metadata
- interface schemas validate roles/directions
- connection rules positive/negative
- generator/validator command declaration
- package-local path confinement
- examples load and validate

### UI Smoke Tests

Required coverage:

- Project Overview opens project and shows packages/components/topologies/diagnostics/artifacts.
- Config Inspector shows source and resolved config.
- Component Config View renders package parameter metadata.
- Topology Graph View renders explicit AnyNet graph.
- Topology tables sync with graph selection.
- Tool Input Preview shows deterministic projection.
- Diagnostics selection highlights stable refs.
- Layout changes are represented as layout patches.

### Architecture Boundary Tests

Public and hidden scans must verify:

- `ipcraft-core` does not include Qt UI headers.
- `ipcraft-core` does not include NoC implementation package headers.
- `ipcraft-package` does not depend on UI.
- `ipcraft-capability-noc` depends on core/package/domain/topology, not vice versa.
- No global mutable `ModuleRegistry` singleton.
- No core package id special cases for `meshnoc`, `noc`, or `vendor.meshnoc`.
- No `mesh_router`, router endpoint hardcode, or direction-port hardcode in core UI.
- Generator does not read `.fpproj` directly.
- Plugin cannot directly mutate `ProjectDesign`.
- Layout does not enter component config.

## Negative Tests

Required:

- core schema rejects NoC-specific fields in core.
- package with missing files reports diagnostics.
- plugin patch with invalid target rejected.
- incompatible interface connection rejected.
- invalid topology link endpoint rejected.
- invalid attachment rejected.
- invalid patch rejected transactionally.
- generator result with invalid artifact path rejected.
- generator cannot assume project path or `.fpproj` input.
- UI cannot apply mutation without patch/service.
- view descriptor with Qt paint commands rejected.
- package command path escape rejected.

## Public Contract Tests

Visibility:

- Stored in repo.
- Visible to Implementation Agent.
- Designed to teach public behavior.

Minimum public test suite:

- `project_v1_minimal_uart_roundtrip`
- `project_v1_cpu_nic_noc_roundtrip`
- `project_v1_anynet_explicit_graph_roundtrip`
- `package_v1_minimal_uart_load`
- `component_v1_defaults_and_metadata`
- `interface_connection_positive_negative`
- `topology_graph_anynet_roundtrip`
- `topology_parametric_mesh_roundtrip`
- `view_descriptor_rejects_drawing_dsl`
- `project_view_requires_target_ref`
- `tool_input_dummy_projection_golden`
- `tool_result_diagnostics_artifacts_parse`
- `patch_apply_reject_invalid_target`
- `resolution_defaults_with_provenance`
- `layout_change_not_semantic_projection`

## Hidden Acceptance Tests

Visibility:

- Not stored in implementation branch.
- Not readable by Implementation Agent.
- Maintained by Test/QA Agent and Acceptance Agent.

Required coverage:

- arbitrary topology graphs not present in public examples
- non-mesh NoC package
- package with explicit graph topology but no mesh support
- NIC package from a different vendor
- payload IP that is NoC-aware and bypasses NIC
- topology with multi-links and non-grid layout
- inspection-only project with no manual edits
- invalid package diagnostics
- invalid patch rejection
- tool protocol isolation
- layout/semantic separation
- package capability discovery
- architecture forbidden patterns
- example NoC implementation package not hardcoded
- NoC not hardcoded into core

### Hidden Property-Based Tests

Generate:

- random explicit topology graph
- random valid/invalid package manifest
- random component config within schema
- random `ProjectPatch` sequences
- random layout changes that must not affect semantic projection

Acceptance properties:

- roundtrip preserves semantic graph.
- invalid refs are rejected.
- patch apply is transactional.
- layout-only changes do not affect resolved semantic config.
- package discovery does not depend on known ids.

### Hidden Metamorphic Tests

Required transformations:

- Reordering components does not change semantic resolution.
- Moving nodes in layout does not change generator input.
- Renaming a view id does not alter topology.
- Adding an unused package does not affect existing projection.
- Changing package defaults changes resolved config with provenance.
- Reordering package manifest capability files does not change registry result.

### Hidden UX/Data-Flow Tests

Required:

- config viewer can show source config.
- config viewer can show resolved config.
- config viewer can show provenance.
- topology graph can be viewed without manual editing.
- generated tool input can be inspected.
- diagnostics link back to config/topology/component refs.
- NoC attachment audit view shows payload IP -> NIC -> attachment mapping.

## Multi-Agent Test Governance

### Spec Agent

Creates public spec and public contract examples. Does not implement production code or write hidden expected outputs.

### Implementation Agent

May run public tests. Must not read hidden tests or acceptance harness private files. Must not modify tests merely to match implementation.

### Test/QA Agent

Builds public contract tests and hidden acceptance tests from this spec. Hidden tests must be black-box and independent of implementation details.

### Acceptance Agent

Runs:

1. public test suite
2. hidden acceptance suite
3. architecture boundary checks
4. forbidden pattern source scans
5. manual review checklist

Final decisions:

- `ACCEPTED`
- `REJECTED`
- `CONDITIONALLY_ACCEPTED_WITH_REQUIRED_FIXES`

## Anti Test-Oriented Coding Checks

Acceptance Agent must scan for:

- production references to hidden case ids
- production references to public golden filenames as behavior selectors
- parser fixture matching instead of schema validation
- topology support fixed to sample node/link counts
- package-specific NoC implementation special-case paths
- package registry seeded by known package ids rather than manifests
- generator that accepts only public example shapes
- patch applier trusting caller
- UI direct model mutation

## Phase Test Gates

### Phase 1 Gate

- project schema tests pass.
- patch tests pass.
- core has no UI/NoC implementation package dependencies.

### Phase 2 Gate

- package/component/interface/rule tests pass.
- no global package registry singleton.
- package diagnostics deterministic.

### Phase 3 Gate

- AnyNet explicit graph tests pass.
- parametric mesh expansion tests pass.
- layout separation tests pass.

### Phase 4 Gate

- tool input/result tests pass.
- resolution/provenance tests pass.
- semantic diff/layout diff tests pass.
- golden projections deterministic.
- generator `.fpproj` isolation tests pass.

### Phase 5 Gate

- domain mutation uses patches.
- validation/generation pipelines consume `ipcraft.tool.input.v1`.

### Phase 6 Gate

- UI smoke tests pass.
- Config Inspector works for inspection-only project.
- topology graph/table sync works.

### Phase 7 Gate

- NoC capability tests pass.
- payload IP -> NIC -> attachment validated.
- NoC-aware direct endpoint validated.

### Phase 8 Gate

- example NoC implementation package ordinary package tests pass.
- example NoC implementation package tool protocol tests pass.
- core hardcode scans pass.

### Phase 9 Gate

- package CLI tests pass.
- package preview/test/run-generator/pack work without native plugin.

### Phase 10 Gate

- old schema runtime paths removed.
- architecture scans and hidden acceptance tests pass.
- Acceptance Agent issues final decision.
