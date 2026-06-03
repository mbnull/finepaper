# IpCraft Public Schema Matrix

Public schema coverage matrix for `.specify/specs/001-ipcraft-architecture/contracts.md` and `.specify/specs/001-ipcraft-architecture/test-strategy.md`.

Coverage terms: parser, writer, roundtrip, negative, golden.

| Schema | Owner phase | Parser | Writer | Roundtrip | Negative | Golden | Public test target |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ipcraft.project.v1` | Foundation | required | required | required | required | required | `ipcraft_project_document_v1_foundation_test` |
| `ipcraft.package.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.component.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.interface.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.connection_rules.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.topology.graph.v1` | Topology IR | required | required | required | required | required | `ipcraft_topology_graph_contract_test` |
| `ipcraft.topology.parametric.v1` | Topology IR | required | required | required | required | required | `ipcraft_topology_graph_contract_test` |
| `ipcraft.view.v1` | UI view host | required | required | required | required | required | `ipcraft_view_contract_test` |
| `ipcraft.view.descriptor.v1` | UI view host | required | required | required | required | required | `ipcraft_view_contract_test` |
| `ipcraft.tool.input.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.tool.result.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.diagnostic.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.artifact.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.patch.v1` | Foundation | required | required | required | required | required | `ipcraft_patch_foundation_test` |
| `ipcraft.capability.noc.v1` | NoC capability | required | required | required | required | required | `noc_contract_test` |
| `ipcraft.capability.noc.extension.v1` | NoC capability | required | required | required | required | required | `noc_contract_test` |
