# Diagnostic Rule-ID Catalog

Messages are not stable. Hidden tests should match `rule_id`, `severity`, `source`, and stable locations.
The catalog covers diagnostics surfaced through `ipcraft.project.v1`, `ipcraft.package.v1`, `ipcraft.emitted-inputs.v1`, and `ipcraft.cli.result.v1`.

| rule_id | severity | source | category | locations | Contract |
| --- | --- | --- | --- | --- | --- |
| project.unsupported_schema | error | project.reader | project | document_path | Project schema |
| project.duplicate_id | error | project.reader | project | document_path | Project ids |
| project.unknown_package | error | core | project | document_path | Package resolution from project instances |
| project.unknown_field | error | project.reader | project | document_path | Strict project schema |
| project.type_mismatch | error | project.reader | project | document_path | Project schema |
| project.missing_required | error | project.reader | project | document_path | Project schema |
| project.unknown_instance | error | project.reader | project | document_path | Composition references |
| project.config_invalid | error | core | project | document_path | Project instance config validation |
| project.read_failed | error | project.reader | project | file | Project file loading |
| project.invalid_json | error | project.reader | project | file | Project JSON parser |
| project.file_too_large | error | project.reader | project | file | Project file loading |
| project.invalid_value | error | project.reader | project | document_path | Project schema |
| package.unsupported_schema | error | package.parser | package | document_path | Package schema |
| package.extension_required | error | package.parser | package | document_path | Extension enablement |
| package.unknown_extension | error | package.parser | package | document_path | Extension enablement |
| package.path_escape | error | package.parser | package | document_path | Package path security |
| package.duplicate_version | error | package.resolver | package | document_path | Package resolution |
| package.version_not_found | error | package.resolver | package | document_path | Package resolution |
| package.not_found | error | package.resolver | package | file/document_path | Package resolution |
| package.missing_required | error | package.parser | package | document_path | Package schema |
| package.type_mismatch | error | package.parser | package | document_path | Package schema |
| package.duplicate_id | error | package.parser | package | document_path | Package ids |
| package.duplicate_table | error | package.parser | package | document_path | Config table ids |
| package.invalid_flow | error | package.parser | package | document_path | Flow schema |
| package.duplicate_key | error | package.parser | package | file | Package JSON parser |
| package.invalid_json | error | package.parser | package | file | Package JSON parser |
| package.read_failed | error | package.parser | package | file | Package file loading |
| package.file_too_large | error | package.parser | package | file | Package file loading |
| package.invalid_value | error | package.parser | package | document_path | Package schema |
| package.unknown_field | error | package.parser | package | document_path | Strict package schema |
| config.required_missing | error | core | config | parameter/table/document_path | Config validation |
| config.type_mismatch | error | core | config | parameter/table/document_path | Config validation |
| config.unknown_parameter | error | core | config | parameter/document_path | Config validation |
| config.unknown_table_column | error | core | config | table_cell/document_path | Table config |
| config.unknown_document | error | core | config | document_path | Config documents |
| config.unknown_file | error | core | config | document_path | File inputs |
| config.enum_invalid | error | core | config | parameter/document_path | Config validation |
| config.range_invalid | error | core | config | parameter/document_path | Config validation |
| config.path_escape | error | core | config | parameter/document_path | Config path security |
| config.expression_unsupported | error | core | config | document_path | Expression boundary |
| config.duplicate_id | error | core | config | document_path | Config schema |
| config.document_format_invalid | error | core | config | document_path | Config documents |
| config.file_extension_invalid | error | core | config | document_path | Config files |
| config.table_column_missing | error | core | config | document_path | Table config |
| composition.unknown_instance | error | core | composition | document_path | Composition validation |
| composition.unknown_interface | error | core | composition | document_path/interface | Composition validation |
| composition.unknown_connection_class | error | core | composition | document_path/connection | Composition validation |
| composition.role_mismatch | error | core | composition | document_path/connection | Composition validation |
| composition.required_interface_unconnected | error | core | composition | interface | Composition validation |
| composition.multiply_driven_input | error | core | composition | document_path/connection | Composition validation |
| composition.clock_reset_source_count | error | core | composition | document_path/connection | Composition validation |
| composition.incompatible_endpoint | error | core | composition | document_path/connection | Composition validation |
| graph_config.duplicate_object | error | core | graph_config | document_path/graph_object | Graph config |
| graph_config.duplicate_relationship | error | core | graph_config | document_path | Graph config |
| graph_config.unknown_endpoint_object | error | core | graph_config | document_path/graph_object | Graph config |
| graph_config.unknown_top_level_field | error | core | graph_config | document_path | Graph config |
| graph_config.type_mismatch | error | core | graph_config | document_path | Graph config |
| graph_config.unsupported_schema | error | core | graph_config | document_path | Graph config |
| emitter.path_absolute | error | core | emitter | document_path | Emitter path security |
| emitter.path_escape | error | core | emitter | document_path | Emitter path security |
| emitter.write_failed | error | core | emitter | document_path/file | Emitter writing |
| emitter.source_missing | error | core | emitter | document_path | Emitter input |
| emitter.duplicate_output_path | error | core | emitter | document_path | Emitter writing |
| emitter.plugin_unavailable | error | core | emitter | document_path | Plugin boundary |
| flow.executable_missing | error | core | flow | document_path | Flow process |
| flow.exec_failed | error | core | flow | document_path/file | Flow process |
| flow.timeout | error | core | flow | document_path/file | Flow process |
| flow.command_policy_violation | error | core | flow | document_path | Flow security |
| flow.output_truncated | warning | core | flow | document_path/file | Flow capture |
| flow.unknown_flow | error | core/cli | flow | document_path | Flow selection |
| flow.plugin_unavailable | error | core | flow | document_path | Plugin boundary |
| artifact.glob_escape | error | core | artifact | document_path | Artifact security |
| artifact.required_missing | error | core | artifact | document_path/artifact | Artifact collection |
| cli.unknown_command | error | cli | cli | document_path | CLI contract |
| cli.missing_argument | error | cli | cli | document_path | CLI contract |
| cli.argument_conflict | error | cli | cli | document_path | CLI contract |
| cli.path_escape | error | cli | cli | document_path | CLI output path security |
| cli.instance_scope_required | error | cli | cli | document_path | CLI run-flow |
| cli.instance_not_found | error | cli | cli | document_path | CLI instance targeting |
| migration.target_required | error | migration | migration | document_path | Migration CLI |
| migration.unsupported_target | error | migration | migration | document_path | Migration CLI |
| migration.unsupported_legacy_content | error | migration | migration | document_path | Migration |
| migration.unsupported_schema | error | migration | migration | document_path | Migration input schema |
| migration.input_missing | error | migration | migration | file | Migration file loading |
| migration.invalid_input | error | migration | migration | file | Migration JSON parser |
| migration.already_current | warning | migration | migration | document_path | Migration idempotency |
