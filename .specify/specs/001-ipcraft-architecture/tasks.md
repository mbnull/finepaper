# IpCraft Architecture Tasks

Tasks are split by commit-sized PR slices. Each phase starts with contract tests before implementation. File paths are likely targets and may be adapted to the final source tree, but the module boundaries and acceptance criteria are mandatory.

## Phase 0: Architecture Audit And Deletion Map

### T0001

**Title:** Audit Graph source-of-truth assumptions

**Files likely touched:**

- `docs/architecture/ipcraft-deletion-map.md`
- `qt/inc/graph/*`
- `qt/inc/nodeeditor/*`
- `qt/inc/commands/*`
- `qt/test/*graph*`

**Implementation steps:**

1. Scan source for `Graph`, `Module`, `Connection`, `NodeEditorWidget`, graph serializer, and graph command references.
2. Classify each use as delete, replace with `ProjectDesign`, keep as view adapter, or temporary adapter.
3. Record owner module, dependency direction, and removal condition.
4. Add architecture scan patterns for graph-as-root violations.

**Tests to add:**

- Public architecture scan that fails when `ProjectDocumentV1` includes graph UI headers.
- Public scan that flags project save/load paths that serialize `NodeEditorWidget` or graph canvas as root.

**Acceptance criteria:**

- Deletion map identifies every old graph-rooted save/load, command, and UI mutation path.
- Temporary adapters have explicit removal phase.
- No code behavior is changed in this audit task.

**Dependencies:** None.

### T0002

**Title:** Audit NoC implementation package hardcoded paths

**Files likely touched:**

- `docs/architecture/ipcraft-deletion-map.md`
- `ipcores/finepaper-noc/**`
- `ipcores/ravenoc/**`
- `ipcores/opennoc/**`
- `qt/inc/ipcore/**`
- `qt/test/*noc*`
- `qt/test/*meshnoc*`

**Implementation steps:**

1. Scan for `noc`, `meshnoc`, `mesh`, `router`, `endpoint`, `north`, `east`, `south`, `west`, `ipcraft.noc.project.v1`, and `mesh_router`.
2. Classify each use as delete, move to `ipcraft-capability-noc`, move to an ordinary package path such as `packages/vendor-meshnoc`, or keep as example/test fixture.
3. Identify generator inputs still using old NoC contracts.
4. Add forbidden string scan proposals.

**Tests to add:**

- Public architecture scan fixture demonstrating forbidden core hardcode.
- Public scan that allows package-specific NoC implementation references only under ordinary package paths such as `packages/vendor-meshnoc`.

**Acceptance criteria:**

- Every NoC implementation package core/UI special case has a target replacement path.
- Old `ipcraft.noc.project.v1` use is marked for removal.

**Dependencies:** T0001.

## Phase 1: Core IR

### T0100

**Title:** Add public schema contract gate matrix

**Files likely touched:**

- `qt/test/public_schema_gate_test.cpp`
- `qt/test/schema_contract_matrix.md`
- `schemas/`

**Implementation steps:**

1. Create a public matrix listing every public schema: `ipcraft.project.v1`, `ipcraft.package.v1`, `ipcraft.component.v1`, `ipcraft.interface.v1`, `ipcraft.connection_rules.v1`, `ipcraft.topology.graph.v1`, `ipcraft.topology.parametric.v1`, `ipcraft.view.v1`, `ipcraft.view.descriptor.v1`, `ipcraft.tool.input.v1`, `ipcraft.tool.result.v1`, `ipcraft.diagnostic.v1`, `ipcraft.artifact.v1`, and `ipcraft.patch.v1`.
2. For each schema, require parser, writer, roundtrip, negative validation, and golden fixture coverage.
3. Add a contract gate test that fails if any public schema lacks one of those coverage entries.
4. Mark capability-owned schemas used by public examples, such as `ipcraft.capability.noc.v1` and `ipcraft.capability.noc.extension.v1`, as capability contract schemas with parser, writer, roundtrip, negative validation, and golden coverage in their capability phase.

**Tests to add:**

- `public_schema_matrix_lists_all_public_schemas`
- `public_schema_matrix_requires_parser_writer_roundtrip_negative_golden`

**Acceptance criteria:**

- No phase can claim contract coverage while omitting patch, diagnostic, artifact, component, interface, connection rule, or tool schemas.

**Dependencies:** T0001, T0002.

### T0101

**Title:** Add public `ipcraft.project.v1` contract tests

**Files likely touched:**

- `schemas/ipcraft.project.v1.schema.json`
- `qt/test/ipcraft_project_v1_contract_test.cpp`
- `examples/contracts/minimal_uart.fpproj`
- `examples/contracts/cpu_nic_noc.fpproj`
- `examples/contracts/anynet_explicit_graph.fpproj`

**Implementation steps:**

1. Write failing parser/writer/roundtrip tests for minimal UART.
2. Write failing roundtrip test for CPU -> NIC -> NoC where NoC data is in extension blocks and topology attachments own attachment points.
3. Write failing roundtrip test for AnyNet explicit graph.
4. Write negative test proving layout fields inside component config are rejected.
5. Write project golden fixture tests for minimal UART, CPU -> NIC -> NoC, AnyNet, and blackbox Verilog.
6. Write structural-only test proving Phase 1 does not require package registry or interface schema resolution.

**Tests to add:**

- `ipcraft.project.v1 minimal parse/write/roundtrip`
- `project_v1_golden_fixtures`
- `layout_not_allowed_in_component_config`
- `noc_fields_rejected_from_core_component_config`
- `project_v1_structural_load_without_package_registry`

**Acceptance criteria:**

- Tests fail before implementation because schema/model does not exist.
- Public examples do not use old NoC-specific schemas such as `ipcraft.noc.project.v1`.
- Phase 1 project validation does not require package capability resolution.

**Dependencies:** T0100.

### T0102

**Title:** Implement `ProjectDesign` value model

**Files likely touched:**

- `qt/inc/ipcraft/core/project_design.h`
- `qt/src/ipcraft/core/project_design.cpp`
- `qt/{inc,src}/ipcraft/core/component_instance.*`
- `qt/{inc,src}/ipcraft/core/interface_instance.*`
- `qt/{inc,src}/ipcraft/core/connection.*`
- `qt/{inc,src}/ipcraft/core/extension_block.*`
- `qt/{inc,src}/ipcraft/core/diagnostic.*`
- `qt/{inc,src}/ipcraft/core/artifact.*`

**Implementation steps:**

1. Add immutable-value-friendly structs/classes for `ProjectDesign`, `ComponentInstance`, `InterfaceInstance`, `Connection`, `ExtensionBlock`, `Diagnostic`, and `Artifact`.
2. Keep layout and view documents as separate fields.
3. Add validation helpers for ids and stable refs.
4. Avoid Qt UI dependencies.

**Tests to add:**

- Unit tests for duplicate id detection.
- Unit tests for extension block owner/schema/version.
- Unit tests for stable diagnostic refs.

**Acceptance criteria:**

- Core model compiles without UI headers.
- No NoC implementation package symbols exist in core model.

**Dependencies:** T0101.

### T0103

**Title:** Implement `ProjectDocumentV1` reader/writer

**Files likely touched:**

- `qt/{inc,src}/ipcraft/core/project_document_v1_reader.*`
- `qt/{inc,src}/ipcraft/core/project_document_v1_writer.*`
- `schemas/ipcraft.project.v1.schema.json`
- `qt/test/ipcraft_project_v1_contract_test.cpp`

**Implementation steps:**

1. Parse `schema: ipcraft.project.v1` structurally without package registry resolution.
2. Preserve package-owned extension blocks.
3. Reject unknown core fields that conflict with schema.
4. Serialize deterministically.
5. Ensure layout lives under views/layout only.
6. Reject `connections[].kind: attachment`; topology attachment belongs only to `topologies[].attachments`.

**Tests to add:**

- Minimal UART roundtrip.
- CPU -> NIC -> NoC roundtrip.
- example NoC implementation mesh document parse.
- AnyNet explicit graph roundtrip.
- Blackbox Verilog component parse.
- Negative: old schema rejected.
- Negative: `connections[].kind: attachment` rejected.
- Structural load succeeds even when component type schemas are not loaded.

**Acceptance criteria:**

- Public examples roundtrip byte-stably after canonical formatting.
- Rejected documents produce `ipcraft.diagnostic.v1`.

**Dependencies:** T0102.

### T0104

**Title:** Add `ProjectPatch` and `PatchApplier`

**Files likely touched:**

- `schemas/ipcraft.patch.v1.schema.json`
- `qt/{inc,src}/ipcraft/core/project_patch.*`
- `qt/{inc,src}/ipcraft/core/patch_applier.*`
- `qt/test/ipcraft_patch_v1_contract_test.cpp`

**Implementation steps:**

1. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.patch.v1`.
2. Define patch ops: add/remove component, set config, add/remove connection, add topology node/link, set attachment, set layout, set extension data, register artifact, add diagnostic.
3. Validate target refs before apply.
4. Validate layout ops separately from semantic ops.
5. Return diagnostics for rejected ops.
6. Add reversible transaction data for undo/redo.

**Tests to add:**

- `patch_v1_parse_write_roundtrip`
- `patch_v1_golden_fixture`
- Apply/revert add component.
- Reject invalid target.
- Reject layout field in config patch.
- Reject plugin patch outside allowed scope.

**Acceptance criteria:**

- No caller can bypass validation through public patch API.
- Patch tests include negative cases.
- Patch schema satisfies parser/writer/roundtrip/negative/golden coverage required by T0100.

**Dependencies:** T0103.

## Phase 2: Package Capability

### T0201

**Title:** Add package/component/interface/connection rule contract tests

**Files likely touched:**

- `schemas/ipcraft.package.v1.schema.json`
- `schemas/ipcraft.component.v1.schema.json`
- `schemas/ipcraft.interface.v1.schema.json`
- `schemas/ipcraft.connection_rules.v1.schema.json`
- `qt/test/ipcraft_package_contract_test.cpp`
- `packages/examples/minimal-uart/package.yml`

**Implementation steps:**

1. Write manifest parse/validate tests.
2. Write manifest writer/roundtrip/negative/golden tests.
3. Write component parser/writer/roundtrip/negative/golden tests with defaults and parameter metadata.
4. Write interface parser/writer/roundtrip/negative/golden tests with compatibility metadata.
5. Write connection rule parser/writer/roundtrip/negative/golden tests with adapter recommendation coverage.

**Tests to add:**

- `package_manifest_roundtrip`
- `package_manifest_rejects_top_level_capability_resource_lists`
- `component_schema_defaults_with_provenance`
- `component_schema_roundtrip_and_negative`
- `interface_rule_rejects_incompatible_protocol`
- `interface_schema_roundtrip_and_negative`
- `connection_rule_recommends_adapter`
- `connection_rules_roundtrip_and_negative`

**Acceptance criteria:**

- Tests fail before registry implementation.
- Examples include parameter labels, units, descriptions, constraints, and docs links.
- Package manifests use only `capabilities.*` for resource lists; top-level `components`, `interfaces`, `connection_rules`, `views`, `generators`, and `validators` are rejected.

**Dependencies:** T0104.

### T0202

**Title:** Implement injected `PackageRegistry`

**Files likely touched:**

- `qt/{inc,src}/ipcraft/package/package_manifest.*`
- `qt/{inc,src}/ipcraft/package/package_registry.*`
- `qt/{inc,src}/ipcraft/package/package_validator.*`
- `qt/{inc,src}/ipcraft/package/capability_registry.*`

**Implementation steps:**

1. Load package manifests from explicit roots.
2. Resolve paths relative to package root.
3. Emit deterministic diagnostics for missing files and invalid schemas.
4. Support registry instances injected into `ProjectSession`.
5. Delete or isolate global singleton registry usage.

**Tests to add:**

- Load minimal UART package.
- Missing file reports diagnostic.
- Two independent registry instances do not share mutable state.

**Acceptance criteria:**

- No global mutable `ModuleRegistry` or equivalent is used for package capabilities.
- Package check validates every referenced file.

**Dependencies:** T0201.

### T0203

**Title:** Implement component/interface/connection rule registries

**Files likely touched:**

- `qt/{inc,src}/ipcraft/package/component_type_registry.*`
- `qt/{inc,src}/ipcraft/package/interface_type_registry.*`
- `qt/{inc,src}/ipcraft/package/connection_rule_registry.*`
- `qt/{inc,src}/ipcraft/domain/connection_compatibility_service.*`

**Implementation steps:**

1. Register component schemas by package id/version/type id.
2. Register interface schemas and protocol metadata.
3. Register data-driven connection rules.
4. Resolve compatibility using rule registry, not UI code.

**Tests to add:**

- Direct compatible interface connection accepted.
- Direction mismatch rejected.
- Clock/reset constraint rejected.
- Adapter recommendation returned.

**Acceptance criteria:**

- UI modules contain no AXI/NoC/UART or package-specific compatibility rules.

**Dependencies:** T0202.

## Phase 3: Topology IR

### T0301

**Title:** Add topology graph contract tests

**Files likely touched:**

- `schemas/ipcraft.topology.graph.v1.schema.json`
- `schemas/ipcraft.topology.parametric.v1.schema.json`
- `qt/test/ipcraft_topology_graph_contract_test.cpp`
- `examples/contracts/anynet_explicit_graph.fpproj`

**Implementation steps:**

1. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.topology.graph.v1`.
2. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.topology.parametric.v1`.
3. Write explicit graph parse/roundtrip tests with arbitrary node/link ids.
4. Write authored parametric mesh request roundtrip tests.
5. Write multi-link test.
6. Write directed and undirected link tests.
7. Write attachment tests for component/interface refs and provider-coordinate attachment points.
8. Write negative tests for invalid endpoints and empty `expanded_parametric` placeholder graphs.

**Tests to add:**

- Topology graph parser/writer coverage.
- AnyNet explicit graph roundtrip.
- Parametric mesh request roundtrip.
- Multi-link topology roundtrip.
- Invalid attachment rejected.
- Empty expanded parametric topology rejected.
- Topology graph golden fixture.

**Acceptance criteria:**

- Test data is not a mesh.
- Layout coordinates are absent from semantic topology graph.
- Topology schema satisfies parser/writer/roundtrip/negative/golden coverage required by T0100.
- Parametric topology schema satisfies parser/writer/roundtrip/negative/golden coverage required by T0100.

**Dependencies:** T0103.

### T0302

**Title:** Implement topology graph and attachment model

**Files likely touched:**

- `qt/{inc,src}/ipcraft/topology/topology_graph.*`
- `qt/{inc,src}/ipcraft/topology/topology_node.*`
- `qt/{inc,src}/ipcraft/topology/topology_link.*`
- `qt/{inc,src}/ipcraft/topology/attachment.*`

**Implementation steps:**

1. Implement topology nodes with kind, optional component ref, ports, config, metadata.
2. Implement topology links with endpoint refs, direction, kind, width/latency/VC/protocol config.
3. Implement attachment records with topology id, attachment point, component ref, interface ref, optional adapter ref.
4. Validate node/link/attachment refs.

**Tests to add:**

- Arbitrary graph validation.
- Attachment point lookup.
- Duplicate topology node rejected.

**Acceptance criteria:**

- AnyNet is represented as explicit graph.
- No NoC-specific fields are required by core topology model.

**Dependencies:** T0301.

### T0303

**Title:** Add parametric topology expansion

**Files likely touched:**

- `qt/{inc,src}/ipcraft/topology/parametric_topology_provider.*`
- `qt/{inc,src}/ipcraft/topology/explicit_graph_topology_provider.*`
- `qt/test/parametric_mesh_test.cpp`

**Implementation steps:**

1. Define provider interface for parametric topologies.
2. Implement mesh expansion as built-in provider.
3. Keep provider output as normal `TopologyGraph`.
4. Add hook for package-defined topology providers.

**Tests to add:**

- Mesh expansion 2x2.
- Expansion deterministic order.
- Generated layout not included in semantic graph.

**Acceptance criteria:**

- Mesh is one provider, not the topology model.

**Dependencies:** T0302.

## Phase 4: Resolution And Tool Protocol

### T0401

**Title:** Add resolution, provenance, and tool protocol contract tests

**Files likely touched:**

- `schemas/ipcraft.tool.input.v1.schema.json`
- `schemas/ipcraft.tool.result.v1.schema.json`
- `schemas/ipcraft.diagnostic.v1.schema.json`
- `schemas/ipcraft.artifact.v1.schema.json`
- `qt/test/ipcraft_resolution_contract_test.cpp`
- `qt/test/ipcraft_tool_protocol_contract_test.cpp`
- `examples/contracts/tool_input/*.json`

**Implementation steps:**

1. Write failing tests for package defaults appearing in resolved config.
2. Write failing tests proving defaults are not written into authored config.
3. Write derived config provenance tests.
4. Write semantic diff versus layout diff tests.
5. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.tool.input.v1`.
6. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.tool.result.v1`.
7. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.diagnostic.v1`.
8. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.artifact.v1`.
9. Write tool input generation golden tests for minimal UART, CPU -> NIC -> NoC, and AnyNet.
10. Write tool result parse tests with diagnostics/artifacts/patch suggestions.
11. Write negative test for artifact path escape.
12. Write negative test proving generator input is not `.fpproj`.

**Tests to add:**

- `resolved_config_explains_default_source`
- `authored_config_omits_defaults`
- `layout_change_not_semantic_diff`
- `tool_input_is_deterministic`
- `tool_input_v1_parse_write_roundtrip_negative_golden`
- `tool_result_v1_parse_write_roundtrip_negative_golden`
- `diagnostic_v1_parse_write_roundtrip_negative_golden`
- `artifact_v1_parse_write_roundtrip_negative_golden`
- `layout_changes_do_not_change_tool_input`
- `tool_result_invalid_artifact_path_rejected`

**Acceptance criteria:**

- Resolution and tool protocol schemas are public and versioned.
- Tool input, tool result, diagnostic, and artifact schemas satisfy parser/writer/roundtrip/negative/golden coverage required by T0100.
- Tests fail before implementation.

**Dependencies:** T0203, T0302.

### T0402

**Title:** Add `ResolutionService` and config provenance model

**Files likely touched:**

- `qt/{inc,src}/ipcraft/domain/resolution_service.*`
- `qt/{inc,src}/ipcraft/domain/config_provenance.*`
- `qt/test/ipcraft_resolution_contract_test.cpp`

**Implementation steps:**

1. Define `ResolutionService` interface and read-only resolved project model.
2. Resolve authored config plus package defaults without mutating `ProjectDesign`.
3. Produce provenance for every resolved field.
4. Produce semantic diff separate from layout diff.
5. Expose `resolveToolInput(target)` as the canonical input for projection.

**Tests to add:**

- `resolved_config_explains_default_source`
- `authored_config_omits_defaults`
- `layout_change_not_semantic_diff`
- `tool_input_preview_uses_resolved_config`

**Acceptance criteria:**

- Every resolved field can explain value, type, source layer, source ref, schema owner, default/explicit state, validation status, docs, unit, constraints, and consumers.
- Resolved config never mutates `ProjectDesign`.

**Dependencies:** T0401.

### T0403

**Title:** Implement `ProjectionService` and `ToolInputBuilder`

**Files likely touched:**

- `qt/{inc,src}/ipcraft/domain/projection_service.*`
- `qt/{inc,src}/ipcraft/domain/tool_input_builder.*`
- `qt/{inc,src}/ipcraft/domain/tool_result_parser.*`

**Implementation steps:**

1. Build component-level projection.
2. Build package-level projection.
3. Build NoC topology projection through capability hook without core hardcode.
4. Build full project projection.
5. Include resolved config and provenance from `ResolutionService`.
6. Exclude view layout unless a view/layout tool explicitly targets layout.

**Tests to add:**

- Golden projections.
- Layout-only mutation no-op for semantic projection.
- Package extension data included only for relevant package/capability.

**Acceptance criteria:**

- Generator/validator never receives `.fpproj` path as implied input.

**Dependencies:** T0401, T0402.

## Phase 5: Domain Services

### T0501

**Title:** Implement editing, validation, and generation domain services

**Files likely touched:**

- `qt/{inc,src}/ipcraft/domain/project_session.*`
- `qt/{inc,src}/ipcraft/domain/design_editing_service.*`
- `qt/{inc,src}/ipcraft/domain/topology_service.*`
- `qt/{inc,src}/ipcraft/domain/validation_pipeline.*`
- `qt/{inc,src}/ipcraft/domain/generation_pipeline.*`
- `qt/{inc,src}/ipcraft/domain/import_export_service.*`

**Implementation steps:**

1. Implement `ProjectSession` with injected registries and current `ProjectDesign`.
2. Implement UI intent to patch conversion.
3. Implement validation pipeline over package validators and core checks.
4. Implement generation pipeline over tool protocol.
5. Add transaction support for patch command groups.

**Tests to add:**

- UI intent does not mutate directly.
- Validation returns diagnostics with stable refs.
- Generation records artifacts through patch.

**Acceptance criteria:**

- All durable mutations use patch/transaction boundary.

**Dependencies:** T0104, T0403, T0402.

## Phase 6: UI View Host

### T0601

**Title:** Define view descriptor and view host contracts

**Files likely touched:**

- `schemas/ipcraft.view.v1.schema.json`
- `schemas/ipcraft.view.descriptor.v1.schema.json`
- `qt/{inc,src}/ipcraft/ui/view_host.*`
- `qt/{inc,src}/ipcraft/ui/view_provider.*`
- `qt/test/ipcraft_view_contract_test.cpp`

**Implementation steps:**

1. Write parser, writer, roundtrip, negative validation, and golden fixture tests for project-bound `ipcraft.view.v1`.
2. Write parser, writer, roundtrip, negative validation, and golden fixture tests for package-authored `ipcraft.view.descriptor.v1`.
3. Reject raw drawing primitives as primary mechanism.
4. Define provider id/source ref/template/layout preference fields.
5. Add diagnostics overlay options.

**Tests to add:**

- Project view parser/writer/roundtrip/golden coverage.
- Package view descriptor parser/writer/roundtrip/golden coverage.
- Project view requires `targetRef`.
- Package view descriptor rejects `targetRef`, `layout`, and `presentationState`.
- Negative: Qt paint command rejected.
- Negative: generator logic in view rejected.

**Acceptance criteria:**

- View YAML/JSON is descriptor, not drawing DSL.
- View schema satisfies parser/writer/roundtrip/negative/golden coverage required by T0100.

**Dependencies:** T0103.

### T0602

**Title:** Implement Config Inspector MVP

**Files likely touched:**

- `qt/{inc,src}/ipcraft/ui/config_inspector.*`
- `qt/{inc,src}/ipcraft/ui/source_config_view.*`
- `qt/{inc,src}/ipcraft/ui/resolved_config_view.*`
- `qt/{inc,src}/ipcraft/ui/tool_input_preview.*`
- `qt/{inc,src}/ipcraft/ui/config_diff_view.*`

**Implementation steps:**

1. Render source project YAML/JSON read-only.
2. Render resolved config with provenance labels.
3. Render schema-driven component config table/form.
4. Render tool input preview read-only.
5. Render semantic diff separate from layout diff.
6. Route edits through `DesignEditingService`.

**Tests to add:**

- UI smoke test opens inspection-only project.
- Source/resolved views show defaults and provenance.
- Tool input preview matches golden projection.

**Acceptance criteria:**

- Config Inspector works before manual editing or generation.

**Dependencies:** T0402, T0403, T0501.

### T0603

**Title:** Implement topology graph and table view MVP

**Files likely touched:**

- `qt/{inc,src}/ipcraft/ui/topology_graph_view_provider.*`
- `qt/{inc,src}/ipcraft/ui/topology_node_table.*`
- `qt/{inc,src}/ipcraft/ui/topology_link_table.*`
- `qt/{inc,src}/ipcraft/ui/topology_attachment_table.*`

**Implementation steps:**

1. Render explicit graph topology from semantic IR.
2. Support manual layout through layout patches.
3. Add node/link/attachment table sync.
4. Add edge inspector.
5. Add diagnostics overlay.
6. Add path highlight.

**Tests to add:**

- UI smoke test for AnyNet graph.
- Layout patch does not change topology.
- Invalid link diagnostic highlights stable ref.

**Acceptance criteria:**

- Interactive line-based topology editing exists without graph-as-root.

**Dependencies:** T0302, T0501, T0601.

## Phase 7: NoC Capability

### T0701

**Title:** Add NoC capability contracts and tests

**Files likely touched:**

- `schemas/ipcraft.capability.noc.v1.schema.json`
- `schemas/ipcraft.capability.noc.extension.v1.schema.json`
- `qt/{inc,src}/ipcraft/capability_noc/noc_capability.*`
- `tests/capability_noc/noc_contract_test.cpp`

**Implementation steps:**

1. Define NoC capability declaration schema.
2. Define NoC extension block schema.
3. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.capability.noc.v1`.
4. Write parser, writer, roundtrip, negative validation, and golden fixture tests for `ipcraft.capability.noc.extension.v1`.
5. Write payload IP -> NIC -> NoC attachment tests.
6. Write NoC-aware direct endpoint test.
7. Write invalid attachment negative test.

**Tests to add:**

- `noc_capability_v1_parse_write_roundtrip_negative_golden`
- `noc_extension_v1_parse_write_roundtrip_negative_golden`
- CPU -> NIC -> NoC valid.
- NoC-aware IP direct attach valid.
- Payload forced to internal endpoint rejected.

**Acceptance criteria:**

- NoC state is extension/capability-owned, not core-owned.
- NoC capability-owned schemas meet the same public schema quality bar as core public contracts.

**Dependencies:** T0302, T0203.

### T0702

**Title:** Implement NoC projection and validation

**Files likely touched:**

- `qt/{inc,src}/ipcraft/capability_noc/noc_projection.*`
- `qt/{inc,src}/ipcraft/capability_noc/noc_validation.*`
- `qt/{inc,src}/ipcraft/capability_noc/noc_view_integration.*`

**Implementation steps:**

1. Resolve NoC topology extension data.
2. Validate routers/switches/channels/attachments.
3. Project NoC-specific generator input data into `ipcraft.tool.input.v1`.
4. Integrate diagnostics with topology view refs.

**Tests to add:**

- Mesh NoC projection.
- Explicit graph NoC projection.
- Attachment diagnostic maps to attachment ref.

**Acceptance criteria:**

- Core and package modules do not depend on NoC capability.

**Dependencies:** T0701, T0403.

## Phase 8: Example NoC Implementation Package

### T0801

**Title:** Convert example NoC implementation into ordinary package

**Files likely touched:**

- `packages/vendor-meshnoc/package.yml`
- `packages/vendor-meshnoc/components/meshnoc.yml`
- `packages/vendor-meshnoc/components/axi_nic.yml`
- `packages/vendor-meshnoc/interfaces/noc_endpoint.yml`
- `packages/vendor-meshnoc/interfaces/noc_link.yml`
- `packages/vendor-meshnoc/capabilities/noc.yml`
- `packages/vendor-meshnoc/views/meshnoc.block.yml`
- `packages/vendor-meshnoc/views/meshnoc.topology.yml`

**Implementation steps:**

1. Declare package metadata and NoC capability requirement.
2. Declare an example NoC component and AXI NIC component.
3. Declare NoC endpoint/link interfaces.
4. Declare mesh support initially.
5. Add view descriptors.

**Tests to add:**

- Example NoC implementation package loads as ordinary package.
- Core scan rejects package-specific NoC implementation special cases.

**Acceptance criteria:**

- No core code checks the example NoC package id or module type.

**Dependencies:** T0702.

### T0802

**Title:** Migrate the example NoC implementation package tools to tool protocol

**Files likely touched:**

- `packages/vendor-meshnoc/tools/generate`
- `packages/vendor-meshnoc/tools/validate`
- `packages/vendor-meshnoc/examples/mesh_2x2.fpproj`
- `packages/vendor-meshnoc/examples/cpu_nic_mesh.fpproj`

**Implementation steps:**

1. Make generator read `ipcraft.tool.input.v1` from stdin or declared input path.
2. Make validator return `ipcraft.tool.result.v1`.
3. Add mesh 2x2 example.
4. Add CPU -> NIC -> NoC example.
5. Remove old `.fpproj` and `ipcraft.noc.project.v1` assumptions.

**Tests to add:**

- Generator receives tool input.
- Validator returns structured diagnostics.
- CPU -> NIC -> example NoC implementation validates.

**Acceptance criteria:**

- Example NoC implementation generator does not read `.fpproj` directly.

**Dependencies:** T0801.

## Phase 9: Package Authoring CLI

### T0901

**Title:** Implement package authoring CLI skeleton

**Files likely touched:**

- `qt/{inc,src}/ipcraft/cli/ipcraft_package_cli.*`
- `tests/cli/package_cli_test.cpp`

**Implementation steps:**

1. Add command dispatcher for `ipcraft package`.
2. Implement `init` template creation.
3. Implement `check` using `PackageValidator`.
4. Implement structured diagnostics output.

**Tests to add:**

- `ipcraft package init` creates minimal package.
- `ipcraft package check` reports missing files.

**Acceptance criteria:**

- CLI does not depend on UI or native plugins.

**Dependencies:** T0202.

### T0902

**Title:** Implement package preview/test/run-generator/pack

**Files likely touched:**

- `qt/{inc,src}/ipcraft/cli/ipcraft_package_cli.*`
- `qt/{inc,src}/ipcraft/package/package_preview.*`
- `qt/{inc,src}/ipcraft/package/package_test_runner.*`
- `tests/cli/package_cli_test.cpp`

**Implementation steps:**

1. Implement `preview` to render package capabilities and sample projections.
2. Implement `test` to run package examples and validators.
3. Implement `run-generator` to build tool input and execute a selected generator.
4. Implement `pack` with manifest validation and package-local path checks.

**Tests to add:**

- Preview minimal UART.
- Run dummy generator.
- Pack rejects path escape.

**Acceptance criteria:**

- Package authoring flow works without Qt/C++ plugin.

**Dependencies:** T0901, T0403.

## Phase 10: Cleanup And Architecture Gates

### T1001

**Title:** Remove old runtime schemas and graph-rooted project paths

**Files likely touched:**

- `schemas/ipcraft.*.schema.json`
- old project reader/writer
- old graph project serializer paths
- architecture docs

**Implementation steps:**

1. Remove old runtime schema loading from normal project path.
2. Keep explicit import/conversion adapter only if documented.
3. Delete graph-rooted project serialization.
4. Update docs and examples.

**Tests to add:**

- Old project schema rejected by normal loader.
- Explicit import command can be tested separately if retained.

**Acceptance criteria:**

- Runtime does not silently load old documents.

**Dependencies:** All previous phases that replace behavior.

### T1002

**Title:** Enforce final architecture gates

**Files likely touched:**

- `tests/architecture/ipcraft_architecture_scan_test.*`
- CI configuration
- `review-checklist.md`

**Implementation steps:**

1. Add dependency scans.
2. Add include/import scans.
3. Add forbidden string scans.
4. Add generator path scans for `.fpproj`.
5. Add layout-in-config scans.
6. Add Acceptance Agent report template.

**Tests to add:**

- Core cannot include UI/NoC implementation package.
- Package cannot include UI.
- No global mutable `ModuleRegistry`.
- No hardcoded package ids in core.

**Acceptance criteria:**

- Acceptance Agent has public and hidden hooks for every constitutional boundary.

**Dependencies:** T1001.
