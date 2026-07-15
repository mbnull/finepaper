# Appendix E — Gate Acceptance Matrix

**Normative status:** V1 Revision 4 baseline; Gate 0 freezes Core contracts and Gate D freezes Extension ABI.

## E1. General Gate Rules

- Every named test target below is mandatory; if it does not exist, the Gate implementation creates it.
- Every Gate runs `xmake build -P qt qt` plus its named tests.
- A later Gate may add tests but may not weaken or delete an earlier new-path test.
- Legacy tests may be deleted only at Gate G and only with the legacy production code they cover.
- Gate status is recorded in `docs/contracts/GATE-STATUS.md` with date, commit, command output summary, and approver.
- Gate 0 Core freeze is recorded in `docs/contracts/CORE-FREEZE.md`; Gate D Extension ABI freeze is recorded in `docs/contracts/EXTENSION-FREEZE.md`. Each records schema/protocol IDs, canonical schema digests, golden-vector digests, error catalog revision, and repository commit.

## E2. Gate 0 — Executable Core Contract Freeze

Gate 0 is documentation/schema work and precedes implementation coding. Required artifacts:

- actual machine-readable JSON Schemas for ProjectDesign, Patch, Command Result, recovery, NoC Package, Interface Contract, Bundle Manifest, Tool Input/Result, Step Result, Pipeline Result, Diagnostic Report, and Artifact Manifest;
- canonical normalization specification and golden vectors, including randomized set-array permutations with identical digest;
- stable error-code catalog;
- valid/invalid golden fixtures for every schema;
- exact Pending Topology Group state machine and request/applicability vectors;
- Core ownership matrix, Host ID allocation rules, project locking, output freshness, and runtime/bundle policy;
- sealed runtime-distribution/platform-ABI/invocation/environment/network profiles and Provider-manifest self-reference rejection vectors;
- a self-contained normative review bundle containing the main specification, Appendices A–F, CONTEXT, active/superseded ADR status, schemas, fixtures, vectors, and freeze metadata;
- the build-target boundary plan from Appendix D.
- Appendix F exact Core models/projections, per-array canonical sort keys, candidate-wide localRef protocol, and Host side-effect contract vectors.
- `docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json`, `ipcraft.engine-bundle.v1.schema.json`, and `docs/contracts/vectors/core-canonical-projection-v1.json` remain schema-valid and digest-self-consistent.
- `docs/contracts/error-codes-v1.json` contains every normative Appendix B/C error code exactly once.
- exact install/resolve/revoke/incompatibility behavior for the immutable Default Engine Bundle and `engineHostContractVersion`.

Required contract checks:

```bash
xmake run -P qt noc_contract_schema_meta_test
xmake run -P qt noc_canonical_digest_vectors_test
xmake run -P qt noc_contract_fixture_catalog_test
xmake run -P qt noc_review_bundle_completeness_test
xmake run -P qt noc_core_canonical_models_schema_test
xmake run -P qt noc_default_engine_lock_contract_test
xmake run -P qt noc_host_side_effect_contract_test
```

### E2.1. Standalone Fixture Catalog and Error Classification

`ipcraft.fixture-catalog.v1` catalogs only deterministic validation of one standalone JSON document by JSON Schema or by a Core-semantic validator. Its closed entries contain exactly `path`, `schemaId`, `validationPhase`, `expected`, `errorCode`, and `behaviorEvidence`; entries are sorted by portable normalized `path` and cover every physical file below `fixtures/valid/` and `fixtures/invalid/` exactly once. `schemaId` and non-null `errorCode` resolve the schema and error catalogs. Accept entries use the valid prefix, null `errorCode`, and may point to one exact committed vector case as `vectors/<file>.json#<case-id>`; reject entries use the invalid prefix, a non-null stable error, and null `behaviorEvidence`.

The standalone catalog does not represent filesystem trees or resolver context. Bundle exhaustiveness/symlink/special-file checks, resolver availability, Provider manifest self-digest, degraded-inspect selection, and output freshness remain focused behavioral vectors or tool tests. A structurally valid Project with unavailable or unsupported exact dependency metadata is therefore a schema-accept fixture and may link to the exact degraded-inspect behavior vector; the fixture alone does not derive runtime state. Fixture coverage is one invalid fixture per named normative rule family or conditional, not one per repeated JSON Schema keyword occurrence. V1 has no fixture-context or filesystem-directory entry type.

Stable standalone validation classification is exact:

| Subject/failure boundary | Stable error code |
|---|---|
| Generic JSON Schema or root structural failure | `contract.schema_invalid` |
| Project legacy root discrimination | `project.legacy_format_unsupported` |
| Project duplicate IDs / unknown references / other Core invariant | `project.duplicate_id` / `project.unknown_reference` / `project.invariant_violation` |
| Patch envelope or JSON Schema failure before dispatch | `patch.schema_invalid` |
| Illegal Patch operation discriminant or required operation shape | `patch.operation_invalid` |
| Structurally valid Patch operation produces a subject violating its subject schema | `patch.schema_violation` |
| Patch cross-object invariant, ownership, reference, or other existing specialized failure | `patch.invariant_violation` or the existing exact Patch code |
| Recovery JSON structure / saved-base binding | `contract.schema_invalid` / `recovery.binding_mismatch` |
| NoC Package / Interface Contract semantic declaration | `package.invariant_violation` / `contract.invariant_violation` |
| Engine migration semantic binding | `engine.migration_invalid` |
| Tool Input structural or semantic manifest input | `tool.input_invalid` |
| Command Result / Diagnostic Report / Output Manifest standalone artifact | `command.result_invalid` / `diagnostic.report_invalid` / `output.manifest_invalid` |
| Contradictory Host side-effect result | `host.side_effect_result_invalid` |
| Bundle Manifest standalone structure | `dependency.manifest_invalid` |

Existing specialized Tool Result, Pipeline Result, and Artifact codes remain authoritative. Bundle filesystem/exhaustiveness disagreement remains behavioral `dependency.bundle_mismatch`, not a single-document fixture. Output stale reasons do not create validation error codes.

Gate 0 exit requires `docs/contracts/CORE-FREEZE.md`. Core contract changes after this point require an unfreeze ADR, a new freeze digest, and rerunning every affected Gate. Provider reconcile ABI and confidential capability-specific extensions remain explicitly unfrozen until Gate D.

## E3. Gate A — Project Foundation

Required artifacts:

- Appendix A JSON Schema implementation for `ipcraft.project-design.v1`.
- `DesignSession`, repository port/JSON implementation, typed-command dispatcher, internal Patch engine.
- separate `ipcraft_noc_domain` and `ipcraft_noc_application` targets that do not link legacy authority; a thin launcher may link both composition roots before Gate G.
- architecture scan prohibiting legacy dependencies from the new path.
- Appendix A valid and invalid fixtures.

Required commands:

```bash
xmake build -P qt qt
xmake run -P qt noc_project_design_contract_test
xmake run -P qt noc_patch_engine_test
xmake run -P qt noc_command_history_test
xmake run -P qt noc_architecture_boundary_test
```

Acceptance:

- all Appendix A fixtures produce expected success or stable diagnostic codes;
- JSON round trip is semantically identical and canonical digest stable;
- duplicate keys and legacy schema IDs are rejected;
- every Patch failure is atomic;
- ownership and precondition failures use Appendix B codes;
- undo/redo restores tombstoned IDs;
- new targets do not link prohibited legacy authority modules.
- two-process/open-path tests enforce exclusive mutation lock and optimistic Save digest.

Legacy requirement: legacy application still builds; no new legacy behavior is added.

## E4. Gate B — Headless Mesh, Identity, Domain, Reconciliation

Required commands:

```bash
xmake run -P qt noc_default_engine_mesh_test
xmake run -P qt noc_reconciliation_contract_test
xmake run -P qt noc_interface_attachment_test
xmake run -P qt noc_domain_command_test
xmake run -P qt noc_recovery_contract_test
```

Required scenarios:

1. 1×1 and 2×2 creation produce complete Default Domain memberships.
2. 2×2 → 2×3 retains existing Router/Link/Slot IDs under Appendix A/C rules.
3. 3×3 → 2×2 → 3×3 gives new IDs to normally recreated objects.
4. Undo of the shrink restores exact tombstoned IDs and Attachments.
5. Removed Slot makes Attachment unresolved and blocks formal Save.
6. New Router enters every Default Domain atomically.
7. Router deletion removes Domain Memberships.
8. Topology change may produce current structure plus blocking disconnected-Domain DRC.
9. Move/Split/Merge commands preserve total/exclusive/connectivity rules and identity behavior.
10. Session/topology/Derived-State revisions and both digests follow Appendix B and remain monotonic through Undo/Redo.
11. C1 changes rows to 3, C2 changes rows to 4 before response: one Group exists, generation 1 cannot commit, and materialization creates exactly one formal history transaction for final intent.
12. Undo/edit/retry/discard during pending state follows the local Group history and never exposes an accepted intermediate topology command.
13. Ordinary edits during pending become Draft Overlay only, have local undo, block Save, and are independently revalidated/submitted after materialization.
14. Attachment and Domain membership commands are disabled while pending; no later command can be mutated retroactively by materialization side effects.
15. A response with wrong generation, topology tuple, Derived-State tuple, Authority identity, or bundle digest is rejected; differing provenance-only session revision is accepted.
16. Provider/Engine create operations receive Host IDs only from the host at atomic commit; rejected candidates allocate no visible IDs.
17. Topology shrink that empties a non-Default Domain deletes/tombstones it atomically; Undo restores its ID/config.
18. Detach clears both resolved and unresolved Attachment intent.
19. A destructive Domain-deletion candidate enters `ready-to-commit`; wrong digest cannot confirm; exact digest commits; recovery re-derives rather than restores the candidate.
20. A non-destructive candidate auto-commits; topology/retry/dependency/Authority/base-design changes invalidate the previous candidate and increment generation.
21. A deleted target converts a Package Relation endpoint only when `unresolvedAllowed`; otherwise the candidate becomes blocked until Group discard and user repair.
22. Formal topology Undo/Redo increments Session, topology-input, and Derived-State revisions monotonically while restoring exact content/IDs.
23. A capability/config change that would invalidate a resolved Slot is rejected, and Core DRC detects any persisted Slot compatibility violation.
24. Authority and Application sub-patches share candidate-wide localRefs; candidate digest is Host-ID independent; commit allocates in canonical order and history stores the mapping.
25. `RenameDesign` compiles to singleton `project` update; forbidden root mutations fail ownership.
26. Default Engine migration always creates a confirmable candidate, atomically changes exact lock/Derived State/side effects/provenance, and Undo executes no Engine.
27. Missing/revoked/incompatible exact Engine or unsupported Host side-effect contract opens degraded inspect without fallback.

No Qt Widget or legacy Service is allowed in these tests.

## E5. Gate C — First Package and Tool Vertical Slice

Reference Package: `finepaper.noc`.

Required commands:

```bash
xmake run -P qt noc_package_contract_test
xmake run -P qt noc_interface_contract_test
xmake run -P qt noc_finepaper_package_e2e_test
xmake run -P qt noc_tool_input_contract_test
xmake run -P qt noc_tool_result_contract_test
xmake run -P qt noc_pipeline_result_contract_test
xmake run -P qt noc_pipeline_invocation_isolation_test
xmake run -P qt noc_bundle_manifest_test
xmake run -P qt noc_runtime_lock_test
xmake run -P qt noc_diagnostic_report_test
xmake run -P qt noc_artifact_manifest_test
xmake run -P qt noc_output_promotion_test
```

Acceptance:

- migrated `finepaper.noc` uses `ipcraft.noc-package.v1` and no Extension Provider;
- unknown generic and opaque extensions follow Appendix C fallback;
- AXI5/ACE/CHI Contract fixtures validate at Contract level only;
- Tool receives no project path and only controlled relative staging paths;
- semantic blocking DRC prevents Generate;
- missing, stale, failed, or mismatched semantic DRC prevents Generator launch;
- Generate requires the current formally saved Snapshot and preserves canonical project/tool inputs in the run report;
- stdout NDJSON progress, stderr logs, exit/result joint success, cancellation grace/kill, and timeout final states follow Appendix C;
- semantic DRC and Generator receive distinct invocation IDs/directories/results/logs; parent pipeline attributes failure to the exact step;
- timeout/crash/cancellation without raw Tool Result still produces one host-authored normalized Step Result and never fabricates a Tool Result;
- malformed diagnostic/run/snapshot digest is rejected;
- path traversal, symlink, special file, digest mismatch, and host policy limit failures prevent promotion;
- failed or cancelled Generate preserves recoverable prior output;
- cross-volume output override is rejected with `tool.output_filesystem_unsupported`.
- external tools see only executionRoot outside the project; Host validates then archives evidence; all path bases/read-write permissions match Appendix C.
- Bundle and Artifact Manifests are exhaustive, extra/unlisted/link/special/colliding entries fail, and promotion uses a Host-built clean tree.
- `pipeline.json` and `.ipcraft-output.json` validate against their machine schemas and carry exact Default Engine/Host contract provenance.

## E6. Gate D — Extension Provider ABI and Restricted Advanced Mesh Capability Spike V1

Public required commands:

```bash
xmake run -P qt noc_provider_protocol_test
xmake run -P qt noc_provider_patch_ownership_test
xmake run -P qt noc_provider_failure_test
```

Provider tests cover:

- handshake success/failure;
- unsupported capability;
- malformed/oversized NDJSON;
- stdout log contamination;
- timeout, crash, restart, and request replay;
- stale input/base Derived State rejection;
- single `structureAuthority` enforcement in both default-engine and extension-provider modes;
- closed reconcile payload contains no ProjectDesign, Attachment, Domain, Draft Overlay, View, run, or non-driving Interface data;
- Provider attempt to mutate user-owned data;
- Provider manifest contains no self bundle digest and launch/handshake/provenance use the external lock digest;
- Patch-body plus non-state preview/diagnostics response;
- reconcile always returns a non-null Patch body, including empty operations; Host injects trusted Patch envelope/source/Session provenance;
- deterministic response for an identical closed reconcile payload and dependency set.

The confidential activity has the normative name **Restricted Advanced Mesh Capability Spike V1 (`RAMCS-V1`)**. Its verifiable exit criteria are:

- an anonymized restricted Package selects `extension-provider` Authority and materializes at least three topology changes while preserving all compatible Host IDs;
- it creates, updates, deletes, saves, reopens, and regenerates ownership=`engine` Package Entities/Relations included in Derived State digest;
- Provider timeout/restart/retry and stale-generation rejection leave authoritative design/history unchanged;
- the same formally saved Snapshot completes semantic DRC and Generate through Tool Result/Artifact verification;
- public static scans show no Package ID/private field branch and all Gate 0–C tests remain passing;
- a signed restricted evidence record contains fixture digest, command/test result digests, capability checklist, and zero unresolved generic gaps, while revealing no private field names or expected outputs to public implementation agents;
- `docs/contracts/EXTENSION-FREEZE.md` is created only after the public Provider tests and `RAMCS-V1` pass.

Gate E MUST NOT begin before Gate D is approved.

## E7. Gate E — New Workbench

Required commands:

```bash
xmake run -P qt noc_wizard_test
xmake run -P qt noc_workbench_projection_test
xmake run -P qt noc_interface_drag_slot_picker_test
xmake run -P qt noc_domain_interaction_test
xmake run -P qt noc_inspector_test
xmake run -P qt noc_problems_output_test
xmake run -P qt noc_workbench_accessibility_test
```

Acceptance:

- new composition root is selected only by the internal startup option;
- no new Workbench test constructs a legacy Service or authority Graph;
- wizard produces the Gate C headless baseline and formal Save only after current/valid state;
- IP Library exists only in wizard and developer mode;
- Interface drag/drop creates typed commands; Slot Picker never submits Patch directly;
- Domain interactions submit Move/Split/Merge typed commands;
- Save blockers are explicit;
- pending/stale/failed working state and disposable recovery are visible and recoverable;
- Pending Topology Group, Draft Overlay, their separate undo scopes, retry/discard, and post-materialization draft submission are visually distinct;
- destructive candidate confirmation displays candidate digest/impact report; blocked candidates cannot be confirmed; non-destructive candidates add no confirmation click;
- keyboard-equivalent actions exist;
- Domain presentation uses non-color cues;
- projection refresh preserves selection and viewport.

## E8. Gate F — Three Public Packages and Runtime Hardening

Required Package set:

```text
finepaper.noc
finepaper.ravenoc
finepaper.opennoc
```

Fixture-only Package:

```text
vendor.meshnoc
```

Required commands:

```bash
xmake run -P qt noc_public_packages_test
xmake run -P qt noc_run_coordinator_test
xmake run -P qt noc_recovery_integration_test
xmake run -P qt noc_degraded_mode_test
xmake run -P qt noc_support_bundle_test
xmake run -P qt noc_legacy_next_isolation_test
```

Acceptance:

- all three public Packages are selectable and complete their declared flow without Extension Provider;
- `vendor.meshnoc` remains non-shipped fixture and exercises generic/opaque fallback;
- exact stable dependency locks and degraded inspect mode without fallback work;
- pre-1.0 incompatible dependency behavior is explicit;
- stale run results never promote output or silently replace current Problems;
- promoted output manifest distinguishes `current-canonical` from `last-successful-stale`;
- accepted unsaved edits, Pending Group, Draft Overlay, and dependency mismatch each make promoted output stale with a distinct reason;
- Default Engine digest, Engine Host contract, or Host side-effect contract mismatch makes output stale and project degraded without fallback;
- a second read-only process writes no shared workspace, reports, recovery, or output state;
- on the recorded release-CI reference runner, a 32×32 Mesh with four Slots per Router, four Domain types, and 128 Interfaces has median canonical parse+validation, Default Engine reconciliation, Core DRC, and headless projection time no greater than 2 seconds each across 10 warm runs, with peak incremental RSS no greater than 512 MiB; the serialized Provider reconcile message remains below 16 MiB or fails before launch with the stable size diagnostic;
- recovery binds to saved Project digest and never becomes project authority;
- legacy and new startup roots remain mutually exclusive;
- legacy source contains no new product features.

## E9. Gate G — Blinded Acceptance and Hard Cutover

Private acceptance command/fixture is controlled outside public implementation tasks. Public cutover commands include:

```bash
xmake run -P qt noc_hidden_acceptance_boundary_scan_test
xmake run -P qt noc_legacy_removal_scan_test
xmake run -P qt noc_single_authority_scan_test
xmake build -P qt qt
```

Acceptance:

- hidden Package adapts without production core changes or Package-ID/field-name branches;
- randomized Package ID, object IDs, field order, and selected values still pass;
- removing the hidden Package leaves all public tests passing;
- legacy composition root and internal startup option are removed;
- `.fpproj` production reader/writer, supplement bridge, legacy authority services, old CommandManager path, and authority Graph are removed;
- obsolete legacy-only tests are deleted with their code;
- only `.nocproj` ProjectDesign V1 production save path remains;
- repository scans confirm exactly one production command history and one authority aggregate per open design.

## E10. Gate Failure Policy

- A failed Gate remains incomplete; downstream feature work is not authorized.
- Near-passing or skipped tests do not satisfy a Gate.
- A frozen Core or Extension ABI gap requires an ADR, the corresponding unfreeze/new freeze record, and repetition of all affected downstream Gates.
- Package-specific exceptions, test-only core branches, fixture-name checks, and expected-output hardcoding are automatic Gate failure.
