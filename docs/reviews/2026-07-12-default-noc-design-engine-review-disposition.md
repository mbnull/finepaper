# Default NoC Design Engine Review Disposition

**Status:** Revision 1–3 review findings integrated; Revision 4 Core-contract closure candidate prepared on 2026-07-14.

This document records how the 2026-07-12 third-party architectural review was resolved. It prevents later implementation agents from re-opening settled trade-offs without new evidence or an approved ADR.

## P0 Disposition

| Review item | Disposition | Resolution |
|---|---|---|
| Product strategy and existing generic core | Accepted | V1 is a NoC Application Profile over one unified generic `ProjectDesign V1`; no second NoC project IR is allowed. |
| Saving incomplete or stale designs | Rejected by product decision | `.nocproj` is a reproducible, regenerable valid artifact rather than a collaborative draft. Only current structurally valid designs may be formally saved. Disposable recovery preserves interaction state and Save blockers must be explicit. |
| Single revision starvation | Superseded by Revision 2 detail | Revision 2 uses one Pending Topology Group plus topology and Derived-State tuples; `sessionRevision` is provenance only for reconciliation. |
| Undefined entity equivalence | Accepted with alternative | No extra logical key is persisted. Engine/Provider receives current opaque IDs and returns an explicit update/create/delete diff; deleted entities receive new IDs if later recreated. |
| Package and tool lifecycle | Accepted with pre-1.0 policy | Before stable 1.0, old schemas, Contracts, cores, tools, and projects may be discarded. From 1.0, exact Bundle digests/Host contracts are pinned, missing dependencies open degraded inspect without fallback, and upgrades require explicit migration. |
| Complex validation after freeze | Accepted | A confidential reduced capability spike occurs before freeze; a separate blinded full acceptance occurs after freeze and prohibits core exceptions. |

## P1 Disposition

| Review item | Resolution |
|---|---|
| Patch algebra exposed to UI | UI uses fixed typed commands; Patch is internal atomic mutation IR only. |
| Domain ambiguity | V1 is fixed to total coverage, same-type exclusive membership, undirected structural connectivity, Default Domain assignment, and atomic move/split/merge commands. |
| Access Slot ambiguity | V1 Slot is a stable single-occupancy attachment resource affecting generation; shared NI grouping remains generator-private. |
| Generic-platform overreach | V1 editable configuration supports only global/Interface/Domain scopes and a small field/condition set; unknown namespaced content follows generic-schema or opaque fallback rather than being mistaken for editable support. |
| AXI5/ACE/CHI support level | Contract-level configuration, matching, persistence, diagnostics, and generator projection only; no endpoint composition, address allocation, transaction, or coherence correctness. |
| Provider filesystem isolation | Protocol does not expose project paths and prohibits direct access, but no OS sandbox is claimed. V1 uses serialized NDJSON requests with fixed limits. |
| Asynchronous run ownership | `RunCoordinator` owns jobs. Results carry run ID, Snapshot revision/digest, and dependency identities; stale results cannot promote canonical output. |
| Output atomicity wording | Replaced with rollback-safe transactional promotion on supported same-filesystem configurations. |
| DesignSession count | Each open System Design has one authoritative DesignSession; multi-window is not prohibited. |
| Developer logs | Developer UI is hidden in normal mode, but support bundles, crash logs, dependency information, and user-requested raw tool output remain available. |
| Auto Apply and undo | One editing gesture or debounce window creates one typed command and at most one reconciliation request; Escape cancels draft and failed Apply preserves it. |

## Execution Rule

Implementation agents follow the normative specification and its delivery gates. A rejected review suggestion must not be reintroduced without new evidence and an approved ADR. A reserved future capability is not authorization to implement it in V1.

## Revision 2 Follow-up Disposition

| Review item | Resolution |
|---|---|
| Async reconciliation versus exact causal Undo | One Pending Topology Group is the only V1 topology transaction. Intermediate topology values are not accepted commands; materialization creates one atomic formal transaction. |
| Ordinary edits while pending | Independent Draft Overlay only, with local undo; no authoritative mutation/history/save/reconcile effect. Drafts revalidate and submit independently after materialization. |
| Reconcile applicability/input boundary | Closed payload and exact Group/generation/topology/base-Derived-State/Authority/bundle tuple. Session revision is provenance only. |
| Provider structural ownership | Package selects exactly one Structure Authority. Provider can own core Derived State only in `extension-provider` mode; host always allocates IDs and enforces invariants. |
| Dependency content/runtime locking | Canonical per-file Bundle Manifest plus runtime lock; no bare PATH lookup and no cross-OS bit-for-bit claim. |
| Tool result/progress/generation gate | Required Tool Result, stdout NDJSON progress, stderr logs, saved-Snapshot DRC→Generate→verify→promote pipeline, and retained canonical inputs. |
| Schema/config/relation gaps | Separate `contractConfig`/`nocConfig`/capabilities, one Domain configuration source, relation endpoint/cardinality schema, Command Result schema, normalized set ordering. |
| Domain/Attachment edges | Topology-empty non-Default Domain is deleted/tombstoned atomically; Detach clears resolved or unresolved intent. |
| Legacy link isolation | New domain/application libraries split from legacy; only thin startup launcher may link both roots before Gate G; temporary versioned new-path type is allowed without adapters. |
| Process/output/non-functional behavior | Exclusive project mutation lock, optimistic Save digest, output freshness manifest, and 32×32 performance/payload Gate. |
| Freeze timing | Gate 0 Core contract freeze; Gate D Extension ABI freeze after named `RAMCS-V1` with explicit evidence criteria. |

## Revision 3 Contract-Closure Disposition

| Review item | Resolution |
|---|---|
| Missing Appendix A in review archive | Repository Appendix A was already Revision 2; review/freeze bundle must now prove inclusion of main spec, A–E, glossary, ADR status, schemas, fixtures, vectors, and freeze metadata. |
| Provider bundle digest cycle | Provider manifest no longer contains its own bundle digest. External dependency lock and Package provider reference supply verified digest to launch, handshake, applicability, transaction provenance, and diagnostics. |
| Multi-tool Generate represented as one tool run | Parent Pipeline Run owns ordered steps; every external process has an Invocation ID/directory/input/optional raw result/logs, every step has a host-normalized result, and Pipeline Result aggregates attribution. |
| Undeclared confirmation phase | Authority response creates immutable Topology Candidate. Non-destructive candidates auto-commit; data-loss candidates require digest confirmation; illegal user-reference impacts block confirmation; recovery re-derives. |
| Patch causality incomplete | Authority Patch embeds the complete applicability object, including base authoritative-design digest. |
| Undo/Redo revisions | Formal Undo/Redo always increments Session; topology transaction Undo/Redo also allocates topology-input revision and increments Derived-State revision. |
| Runtime lock incomplete | Runtime Closure now includes distribution bundle, loader/library/module closure, platform ABI, invocation/search/environment profiles, and prohibited-network declaration. |
| Package Relation target deletion | Permitted unresolved endpoint converts via Application candidate side effect; forbidden unresolved endpoint blocks candidate. |
| Interface edit invalidates Slot | Edit is rejected with actionable diagnostic; Core DRC also checks Contract/role/capability Slot compatibility. |
| Output freshness | Current canonical requires promoted digest to match current authoritative and saved designs, dependency set, and absence of Group/Draft; otherwise output is stale with reason. |
| Read-only workspace race | Secondary read-only process writes no shared workspace, reports, recovery, or output state. |
| Configuration precedence/manifests | Contract default → template override → instance value; capability semantics and manifest timeout/environment/network fields are explicit. |

## Revision 4 Core-Contract Disposition

| Review item | Resolution |
|---|---|
| Candidate cross-Patch identity | Candidate-wide `localRef` namespace spans Authority/Application sub-patches; digest uses local graph/allocation order, commit allocates Host IDs canonically, history stores mapping, discard publishes no IDs. |
| Prose-only digest domains | Added normative Appendix F plus machine-readable Core schema and digest-checked projection vectors for intent, normalized input, Derived State, applicability, Patch body, candidate, impact, pipeline, and output. |
| Runtime dependency shape conflict | Runtime is one `kind: runtime` dependency with nested `runtimeClosure`; argv construction and closure/profile fields are normative. |
| Default Engine replay identity | Exactly one immutable installable `default-engine` dependency; digest is sole identity, Host ABI/side-effect contracts are versioned, no fallback, explicit candidate migration only. |
| Execution/archive path conflict | External process sees Host temporary executionRoot only; Host validates then archives evidence to project reports and builds clean promotion tree. |
| Non-exhaustive manifests | Bundle and Artifact Manifests enumerate complete visible/promoted closures; unlisted/colliding/link/special entries reject verification. |
| Project rename Patch gap | Added singleton `project` entity kind with name-only ordinary mutation; migration-only dependency replacement is source-restricted. |
| Provider trusted envelope/provenance | Provider returns Patch body + applicability; Host injects source, Session provenance, transaction/Patch IDs. Reconcile requires non-null Patch body even with empty operations. |
| Router/Link/Slot extension ambiguity | Removed per-object opaque extension capability; only schema-declared properties or engine-owned Package objects remain. |
| Draft local identity | No cross-entry draft refs; create-Interface draft is one mutable entry until accepted Host ID exists. |
| Impact digest localization | Candidate digest includes structured impact codes/data only; localized presentation remains outside. |
| Undo-history terminology | Invariant is one formal authoritative command history; Draft/Group stacks are interaction-only. |
| Domain/diagnostic/output artifacts | Connectivity covers all topology changes; blocking requires severity error; Pipeline Plan and Output Manifest have schema IDs/machine definitions. |
