# Audit Coverage Matrix

| Area | Public Surface | Expected Check |
| --- | --- | --- |
| CLI JSON shape | `ipcraft-cli` | Every command returns `ipcraft.cli.result.v1`. |
| exit code behavior | `ipcraft-cli` | `ok: true` exits 0, `ok: false` exits nonzero. |
| project schema | `ipcraft.project.v1`, schemas, CLI | Project roots are strict and old schemas produce structured diagnostics. |
| package schema | `ipcraft.package.v1`, schemas, CLI | Runtime packages are strict and self-contained after normalization. |
| emitted inputs schema | `ipcraft.emitted-inputs.v1`, `emit-inputs`, `run-flow` | Emitted manifests use confined deterministic paths. |
| schema validation | schemas, CLI | Unsupported schemas produce structured diagnostics. |
| deterministic writing | CLI, emitted inputs | JSON output is stable for equivalent inputs. |
| duplicate IDs | project/package parsers | Duplicate ids emit stable duplicate diagnostics. |
| extension enforcement | package parser | Optional sections require explicit extensions. |
| native preservation | project/package round-trip | `native` and `preserved` survive round trips. |
| config validation | validate-project | Parameter required/type/enum/range/path checks. |
| table validation | validate-project | Row/cell validation with stable locations. |
| document validation | validate-project | Document content and preservation behavior. |
| composition validation | validate-project | Unknown instance/interface, required, fanout, compatibility. |
| graph-config validation | project parser | N-ary endpoints, duplicate objects, unknown endpoint objects. |
| path security | package/emitter/artifacts | Absolute/traversal/symlink escapes rejected. |
| flow security | run-flow | Missing executable, nonzero exit, timeout, truncation, policy. |
| diagnostics stability | all commands | Match rule_id/severity/source/location, not message. |
| artifact collection | collect-artifacts/run-flow | Confined globs, required artifacts, stable index. |
| migration behavior | migrate-project | Old schema rejected normally and migrated explicitly. |
| old schema rejection | inspect/validate/read | Old normal runtime schemas rejected. |
| package cutover | package loader | Runtime loads `ipcraft.package.v1` without `ipcore.yml`. |
