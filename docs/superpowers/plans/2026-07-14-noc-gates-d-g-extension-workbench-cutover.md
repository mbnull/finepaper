# Default NoC Gates D-G Extension, Workbench, and Cutover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze the Extension ABI through a restricted capability spike, build the new Workbench only over proven headless use cases, migrate all public Packages, harden runtime/recovery/performance, and remove the legacy architecture after blinded acceptance.

**Architecture:** Gate D adds an out-of-process Provider as an alternative single Structure Authority without widening the Core aggregate. Gate E projects the Gate C application ports into a three-pane Qt Workbench. Gate F migrates the remaining public Packages and hardens operational behavior. Gate G performs hidden acceptance and then deletes the legacy composition root, save path, authority Graph, and second history.

**Tech Stack:** Qt 6 Widgets/Core/Concurrent/Test, C++23, NDJSON Provider protocol, QProcess, xmake, existing Gate A-C domain/application libraries.

---

## File Structure

- Create `qt/inc/noc/v1/extension/` and `qt/src/noc/v1/extension/` for the Provider client, protocol envelopes, process lifecycle, and Host envelope injection.
- Create `qt/inc/noc/v1/workbench/` and `qt/src/noc/v1/workbench/` for wizard, composition root, projection models, Canvas, Inspector, Problems/Output, and interaction controllers.
- Create `qt/test/noc/extension/`, `qt/test/noc/workbench/`, and `qt/test/noc/hardening/`.
- Create `ipcores/ravenoc-v1/` and `ipcores/opennoc-v1/`; retain `ipcores/vendor-meshnoc` as fixture-only.
- Modify `qt/src/app/main.cpp` and `qt/xmake.lua` first to add the isolated internal startup option, then at Gate G to remove the option and legacy root.
- Delete legacy production files only in the final Gate G task and only together with their tests.

## Task 1: Implement the closed Provider protocol client

**Files:**
- Create: `qt/inc/noc/v1/extension/providerprotocol.h`
- Create: `qt/inc/noc/v1/extension/providerclient.h`
- Create: `qt/inc/noc/v1/extension/providerprocess.h`
- Create: `qt/src/noc/v1/extension/providerprotocol.cpp`
- Create: `qt/src/noc/v1/extension/providerclient.cpp`
- Create: `qt/src/noc/v1/extension/providerprocess.cpp`
- Create: `qt/test/noc/extension/noc_provider_protocol_test.cpp`
- Create: `qt/test/support/noc/fake_provider_main.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write handshake and envelope tests**

Cover protocol version success/failure, capability negotiation, request IDs, maximum 16 MiB NDJSON message, malformed JSON, stdout contamination, and non-null empty reconcile Patch body.

- [ ] **Step 2: Implement the closed reconcile request**

Send only normalized topology input, current Derived State, exact applicability, dependency/Authority identities, and protocol metadata. Never send ProjectDesign, Attachments, Domains, Draft Overlay, Views, runs, or non-driving Interface data.

- [ ] **Step 3: Make Host construct the trusted Patch envelope**

Provider returns applicability plus Patch body/operations and optional preview/diagnostics. Host injects source identity and Session provenance after request correlation and applicability validation.

- [ ] **Step 4: Run protocol tests**

Run: `xmake run -P qt noc_provider_protocol_test`

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1/extension qt/src/noc/v1/extension qt/test/noc/extension qt/test/support/noc/fake_provider_main.cpp qt/xmake.lua
git commit -m "feat: add closed NoC Provider protocol"
```

## Task 2: Enforce single Structure Authority, ownership, and failure isolation

**Files:**
- Extend: `qt/src/noc/v1/extension/providerclient.cpp`
- Extend: `qt/src/noc/v1/application/reconciliationcoordinator.cpp`
- Create: `qt/test/noc/extension/noc_provider_patch_ownership_test.cpp`
- Create: `qt/test/noc/extension/noc_provider_failure_test.cpp`

- [ ] **Step 1: Write ownership rejection tests**

Provider attempts to mutate user-owned Interface, Attachment, Domain, membership, or user-owned Package Relation. Expected: stable ownership error; authoritative design and formal history unchanged.

- [ ] **Step 2: Write lifecycle failure tests**

Cover timeout, cancellation, crash, restart, replay, stale generation, stale base Derived State, wrong dependency set, oversized response, and deterministic retry.

- [ ] **Step 3: Enforce one Authority per Package**

`structureAuthority` resolves to exactly `default-engine` or `extension-provider`. Default Engine is not run before/after Provider reconciliation in Provider mode. Host still applies the frozen side-effect contract.

- [ ] **Step 4: Run public Gate D tests**

```bash
xmake run -P qt noc_provider_protocol_test
xmake run -P qt noc_provider_patch_ownership_test
xmake run -P qt noc_provider_failure_test
```

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc/extension qt/xmake.lua
git commit -m "feat: enforce Provider authority and failure isolation"
```

## Task 3: Execute RAMCS-V1 and freeze the Extension ABI

**Files:**
- Create: `docs/contracts/RAMCS-V1-EVIDENCE.template.json`
- Create: `docs/contracts/EXTENSION-FREEZE.md`
- Modify: `docs/contracts/GATE-STATUS.md`

- [ ] **Step 1: Define the public evidence schema without private names**

The evidence contains restricted fixture digest, Provider bundle digest, three topology-change result digests, ID-preservation counts, engine-owned entity/relation lifecycle results, timeout/retry result, saved-Snapshot tool run digest, static-scan result, and unresolved-generic-gap count.

- [ ] **Step 2: Run the confidential `RAMCS-V1` activity**

Use an anonymized restricted Package selecting Provider Authority. Require all Appendix E E6 exit criteria; public implementation agents receive only pass/fail and neutral capability evidence.

- [ ] **Step 3: Run every Gate 0-C and public Gate D test**

Expected: all pass with no Core change. A Core gap requires an unfreeze ADR; do not patch around it in Provider or Package-specific branches.

- [ ] **Step 4: Freeze Extension ABI**

Record Provider protocol/schema digests, capability set, maximum message size, process lifecycle, public tests, restricted evidence digest, and repository commit in `EXTENSION-FREEZE.md`.

- [ ] **Step 5: Commit**

```bash
git add docs/contracts/RAMCS-V1-EVIDENCE.template.json docs/contracts/EXTENSION-FREEZE.md docs/contracts/GATE-STATUS.md
git commit -m "docs: freeze NoC Extension ABI after RAMCS-V1"
```

## Task 4: Add the isolated new Workbench composition root and wizard

**Files:**
- Create: `qt/inc/noc/v1/workbench/nocapplication.h`
- Create: `qt/inc/noc/v1/workbench/newprojectwizard.h`
- Create: `qt/src/noc/v1/workbench/nocapplication.cpp`
- Create: `qt/src/noc/v1/workbench/newprojectwizard.cpp`
- Create: `qt/test/noc/workbench/noc_wizard_test.cpp`
- Modify: `qt/src/app/main.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write startup isolation tests**

The internal process-startup option selects exactly one composition root. New Workbench construction must not instantiate any prohibited legacy service or authority Graph.

- [ ] **Step 2: Implement the wizard over typed application use cases**

Wizard selects Package, global topology parameters, project directory/name, and initial Interface/Domain choices. Finish creates one ProjectDesign, opens one Pending Group if derivation is required, and enables formal Save only after current/valid materialization.

- [ ] **Step 3: Keep IP Library scoped**

Package browsing appears in the wizard and developer mode only, not as a normal Workbench dock.

- [ ] **Step 4: Run wizard tests**

Run: `xmake run -P qt noc_wizard_test`

- [ ] **Step 5: Commit**

```bash
git add qt/inc/noc/v1/workbench qt/src/noc/v1/workbench qt/test/noc/workbench qt/src/app/main.cpp qt/xmake.lua
git commit -m "feat: add isolated NoC Workbench wizard"
```

## Task 5: Build projection-only Canvas and topology interactions

**Files:**
- Create: `qt/inc/noc/v1/workbench/workbenchwindow.h`
- Create: `qt/inc/noc/v1/workbench/designprojectionmodel.h`
- Create: `qt/inc/noc/v1/workbench/meshcanvas.h`
- Create: `qt/inc/noc/v1/workbench/attachmentcontroller.h`
- Create: `qt/inc/noc/v1/workbench/domaincontroller.h`
- Create corresponding `.cpp` files
- Create: `qt/test/noc/workbench/noc_workbench_projection_test.cpp`
- Create: `qt/test/noc/workbench/noc_interface_drag_slot_picker_test.cpp`
- Create: `qt/test/noc/workbench/noc_domain_interaction_test.cpp`

- [ ] **Step 1: Write projection authority tests**

Refreshing from DesignSession preserves selection and viewport. Mutating Canvas objects directly cannot modify ProjectDesign. Closing/reopening reconstructs the view solely from the aggregate and workspace UI state.

- [ ] **Step 2: Implement the three-pane shell**

Left: templates/actions. Center: Mesh projection with draggable boundary Interfaces. Right: schema-driven Inspector. Bottom: Problems/Output tabs. Router remains secondary; Slot is a stable attachment resource.

- [ ] **Step 3: Implement Interface drop and Slot Picker**

Dropping on a Router queries legal Slots. One legal Slot may be accepted directly as the user's explicit selection; multiple legal Slots show a small picker. The controller submits `AttachInterface`, never Patch.

- [ ] **Step 4: Implement Domain interactions**

Selection gestures submit Move/Split/Merge typed commands. Domain display combines fill/border/pattern/labels and never relies only on color.

- [ ] **Step 5: Run interaction tests**

```bash
xmake run -P qt noc_workbench_projection_test
xmake run -P qt noc_interface_drag_slot_picker_test
xmake run -P qt noc_domain_interaction_test
```

- [ ] **Step 6: Commit**

```bash
git add qt/inc/noc/v1/workbench qt/src/noc/v1/workbench qt/test/noc/workbench qt/xmake.lua
git commit -m "feat: add projection-only NoC Canvas interactions"
```

## Task 6: Add Inspector, Problems/Output, pending UX, and accessibility

**Files:**
- Create: `qt/inc/noc/v1/workbench/inspectorpanel.h`
- Create: `qt/inc/noc/v1/workbench/problemsmodel.h`
- Create: `qt/inc/noc/v1/workbench/outputpanel.h`
- Create: `qt/inc/noc/v1/workbench/pendinggroupbanner.h`
- Create corresponding `.cpp` files
- Create Gate E tests named `noc_inspector_test`, `noc_problems_output_test`, and `noc_workbench_accessibility_test`

- [ ] **Step 1: Write pending/draft UX tests**

Assert visually distinct formal history, Group-local undo, and Draft-local undo; explicit retry/discard; destructive candidate digest/impact confirmation; blocked candidate cannot confirm; non-destructive candidate does not request a click.

- [ ] **Step 2: Implement schema-driven Inspector**

Inspector edits call typed commands when no Group is open and Draft Overlay actions while pending. Apply button commits one edit gesture; optional Auto Apply debounces one gesture into at most one command/request.

- [ ] **Step 3: Implement Problems and Output semantics**

Problems show current versus stale diagnostics with SubjectRef navigation. Output shows `current-canonical` or `last-successful-stale` plus exact stale reasons and run provenance. Raw logs remain in Output/support data, not diagnostic messages.

- [ ] **Step 4: Add keyboard equivalents and accessible names**

Cover attach/reattach/detach, Domain commands, candidate confirm/discard, Apply, undo scopes, Problems navigation, and wizard completion.

- [ ] **Step 5: Run all Gate E commands**

Run every command under Appendix E E7 plus all Gate A-D public commands.

- [ ] **Step 6: Record Gate E and commit**

```bash
git add qt/inc/noc/v1/workbench qt/src/noc/v1/workbench qt/test/noc/workbench qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "feat: complete NoC Gate E Workbench"
```

## Task 7: Migrate the remaining public Packages without Providers

**Files:**
- Create: `ipcores/ravenoc-v1/`
- Create: `ipcores/opennoc-v1/`
- Create: `qt/test/noc/hardening/noc_public_packages_test.cpp`

- [ ] **Step 1: Add failing public-package matrix tests**

For `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`, assert selection, wizard creation, Default Engine reconciliation, Interface/Domain edit, formal Save/reopen, DRC, Generate, and output promotion without Provider.

- [ ] **Step 2: Migrate Ravenoc declaratively**

Map common fields to V1 global/Interface/Domain configuration and templates. Preserve unsupported package-specific content under the frozen opaque/default path without adding Core branches.

- [ ] **Step 3: Migrate OpenNoC declaratively**

Apply the same contract. If a required behavior cannot be expressed, stop Gate F and raise an Extension/Core gap; do not branch on Package ID.

- [ ] **Step 4: Keep vendor MeshNoC fixture-only**

It exercises unknown generic/opaque fallback and must not appear in the shipped Package catalog.

- [ ] **Step 5: Run package matrix**

Run: `xmake run -P qt noc_public_packages_test`

- [ ] **Step 6: Commit**

```bash
git add ipcores/ravenoc-v1 ipcores/opennoc-v1 qt/test/noc/hardening/noc_public_packages_test.cpp qt/xmake.lua
git commit -m "feat: migrate public NoC packages to V1"
```

## Task 8: Harden runs, recovery, degraded inspect, support data, and isolation

**Files:**
- Create Gate F tests: `noc_run_coordinator_test.cpp`, `noc_recovery_integration_test.cpp`, `noc_degraded_mode_test.cpp`, `noc_support_bundle_test.cpp`, `noc_legacy_next_isolation_test.cpp`
- Extend corresponding Gate A-C production components only through their public ports

- [ ] **Step 1: Add stale-run and concurrent-run tests**

Older revisions may archive historical results but never promote or replace current Problems. Cancellation/timeout and late arrival cannot steal latest-run eligibility.

- [ ] **Step 2: Add degraded inspect tests**

Missing/revoked/incompatible exact Engine/Package/Contract/Provider/tool/runtime preserves raw data read-only, exposes dependency diagnostics, performs no fallback, and allows only explicitly supported confirmed migration.

- [ ] **Step 3: Add recovery and second-process tests**

Recovery binds to saved digest, re-derives candidates, and stays disposable. Read-only second process writes no shared UI state, recovery, reports, or output.

- [ ] **Step 4: Add support bundle tests**

Bundle includes version/dependency locks, sanitized logs, run metadata, diagnostics, and crash information without exposing arbitrary project files or private Provider inputs.

- [ ] **Step 5: Run Gate F functional tests**

Run all commands under Appendix E E8.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/noc/v1 qt/src/noc/v1 qt/test/noc/hardening qt/xmake.lua
git commit -m "test: harden NoC runtime and recovery behavior"
```

## Task 9: Meet the recorded performance envelope

**Files:**
- Create: `qt/test/noc/hardening/noc_performance_gate.cpp`
- Modify: `qt/xmake.lua`
- Modify only measured new-path components after profiling evidence

- [ ] **Step 1: Build the fixed benchmark fixture**

32×32 Mesh, four Slots per Router, four Domain types, 128 Interfaces, exact dependency locks, no Provider. Store fixture digest and reference runner identity with results.

- [ ] **Step 2: Measure before optimizing**

Record ten warm runs each for canonical parse+validation, reconciliation, Core DRC, and headless projection; record median and peak incremental RSS.

- [ ] **Step 3: Optimize only failing stages**

Use profiles to remove repeated canonicalization, quadratic ID lookup, full projection rebuild, or redundant DRC traversal. Preserve contract behavior and rerun all affected tests after each change.

- [ ] **Step 4: Verify limits**

Each median ≤2 seconds and peak incremental RSS ≤512 MiB on the recorded release-CI runner. Provider reconcile serialization stays below 16 MiB or returns the stable pre-launch size error.

- [ ] **Step 5: Record Gate F and commit**

```bash
git add qt/test/noc/hardening qt/inc/noc/v1 qt/src/noc/v1 qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "perf: satisfy NoC Gate F reference envelope"
```

## Task 10: Run blinded acceptance before deleting legacy code

**Files:**
- Create: `qt/test/noc/hardening/noc_hidden_acceptance_boundary_scan_test.cpp`
- Modify: `docs/contracts/GATE-STATUS.md`

- [ ] **Step 1: Add public anti-hardcoding scans**

Reject production comparisons against hidden/public Package IDs, fixture paths, private field names, randomized expected IDs, or known expected output digests.

- [ ] **Step 2: Run the externally controlled hidden acceptance**

Require hidden Package adaptation with randomized Package/object IDs, field order, and selected values, without production Core change. Removing the hidden Package must leave all public tests passing.

- [ ] **Step 3: Stop on any generic gap**

A failure before cutover returns to the appropriate frozen-contract process. Do not delete legacy until hidden acceptance and every Gate A-F test pass together.

- [ ] **Step 4: Commit only public scan/evidence metadata**

```bash
git add qt/test/noc/hardening/noc_hidden_acceptance_boundary_scan_test.cpp qt/xmake.lua docs/contracts/GATE-STATUS.md
git commit -m "test: pass NoC blinded acceptance boundary"
```

## Task 11: Hard cut over and remove legacy authority

**Files:**
- Delete: legacy `.fpproj` production reader/writer and supplement bridge
- Delete: legacy `ProjectService`, `DesignEditingService`, `ProjectStateService`, `ProjectIpService`, old CommandManager production path, authority Graph composition, and legacy-only Workbench composition
- Delete: obsolete tests only with the production code they cover
- Modify: `qt/src/app/main.cpp`
- Modify: `qt/xmake.lua`
- Create: `qt/test/noc/hardening/noc_legacy_removal_scan_test.cpp`
- Create: `qt/test/noc/hardening/noc_single_authority_scan_test.cpp`

- [ ] **Step 1: Write removal scans before deletion**

Scans require exactly one production ProjectDesign save path, one formal command history implementation, one authority aggregate per open design, no legacy startup option, and no prohibited legacy production symbols.

- [ ] **Step 2: Make the new composition root the only root**

Remove the internal startup switch. Normal startup creates the V1 Workbench and `.nocproj` repository directly.

- [ ] **Step 3: Delete legacy production paths and paired tests**

Use the Appendix D map. Preserve only independently audited stateless utilities; they must not retain legacy authority types in their public API.

- [ ] **Step 4: Collapse temporary V1 names only after deletion**

If desired, rename `ipcraft::noc::v1` public canonical types after the legacy names are gone. Perform this as a mechanical change with all tests green before and after.

- [ ] **Step 5: Run Gate G and full build**

```bash
xmake run -P qt noc_hidden_acceptance_boundary_scan_test
xmake run -P qt noc_legacy_removal_scan_test
xmake run -P qt noc_single_authority_scan_test
xmake build -P qt qt
```

Then run every retained Gate 0-F target. Expected: all pass.

- [ ] **Step 6: Record Gate G and commit**

```bash
git add -A qt docs/contracts/GATE-STATUS.md
git commit -m "refactor: complete NoC V1 hard cutover"
```

## Gates D-G Exit Check

The V1 architecture is complete only when:

- Extension ABI is frozen after public tests and RAMCS-V1;
- Workbench uses typed commands and projection-only models over Gate C use cases;
- all three shipped Packages work without Provider;
- hidden acceptance passes without Core/package-name hardcoding;
- only `.nocproj`, ProjectDesign V1, one DesignSession authority, and one formal history remain in production;
- the exact Engine/Host/side-effect identity is preserved through design, runs, output, degraded inspect, migration, and Undo.
