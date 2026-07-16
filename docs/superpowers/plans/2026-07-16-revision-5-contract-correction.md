# Default NoC Revision 5 Contract Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the Gate 0 contract so canonical digests, strict JSON admission, working-state validation, and freeze evidence are consistent across Python and Qt implementations.

**Architecture:** Revision 5 keeps the existing Pending Group, exact Default Engine lock, single Structure Authority, and Host Side-effect contracts. It replaces the split canonicalization assumptions with one RFC 8785-compatible numeric/string serializer, introduces one strict byte-level JSON admission boundary before Qt JSON values, and separates working-state, commit-valid, and save-eligible validation. The revision is published only after vectors, fixtures, schemas, and the deterministic archive are regenerated together.

**Tech Stack:** Python 3 stdlib, Qt 6/C++17, JSON Schema Draft 2020-12, Node.js used only as an independent RFC 8785 oracle in tests, GNU tar, xmake.

---

### Task 1: Unfreeze Revision 4 and establish the Revision 5 control record

**Files:**
- Create: `docs/contracts/UNFREEZE-REV4-ADR.md`
- Modify: `docs/contracts/CORE-FREEZE.md`
- Modify: `docs/contracts/GATE-STATUS.md`
- Modify: `docs/contracts/README.md`
- Modify: `docs/superpowers/specs/appendix-f-core-canonical-models.md`
- Modify: `docs/superpowers/specs/appendix-a-project-design-v1-contract.md`
- Modify: `docs/superpowers/specs/appendix-b-patch-command-reconciliation-contract.md`
- Modify: `docs/superpowers/specs/2026-07-12-default-noc-design-engine-workbench-design.md`

- [ ] **Step 1: Record the unfreeze reason and new normative status.**
  State that Revision 4 archive integrity passed, but its RFC 8785 implementation claim is withdrawn because Python and Qt canonicalizers accept different numeric domains and key-order rules. Mark Revision 5 as `In Progress / Unfrozen` and keep Gate A blocked.
- [ ] **Step 2: Define the single canonicalization contract.**
  State that all digestable JSON values are parsed as strict UTF-8 JSON, finite IEEE-754 binary64 numbers, strings, booleans, null, arrays, and objects; exact large integers/decimals must be represented as strings. Define RFC 8785 property ordering and Appendix F collection projection separately.
- [ ] **Step 3: Define validation modes.**
  Add `ProjectDesignWellFormed`, `ProjectDesignCommitValid`, and `ProjectDesignSaveEligible` with explicit disconnected-Domain behavior and Recovery ownership.
- [ ] **Step 4: Remove the product statement that standard Qt JSON APIs alone are sufficient.**
  Replace it with the strict scanner → Qt JSON → Schema → semantic-validation pipeline.
- [ ] **Step 5: Commit the control/document change.**
  Run `git diff --check` and commit with `docs: unfreeze revision 4 for revision 5 correction`.

### Task 2: Implement one RFC 8785-compatible Python canonicalization module

**Files:**
- Create: `docs/contracts/tools/rfc8785.py`
- Modify: `docs/contracts/tools/verify_canonical_vectors.py`
- Modify: `docs/contracts/tools/verify_contract_fixtures.py`
- Modify: `docs/contracts/tools/verify_engine_side_effect_vectors.py`
- Modify: `docs/contracts/tools/generate_canonical_vectors.py`
- Modify: `docs/contracts/tools/generate_engine_side_effect_vectors.py`
- Create: `docs/contracts/tools/test_rfc8785.py`

- [ ] **Step 1: Write failing numeric and string-order tests.**
  Cover `1.0 → 1`, `-0.0 → 0`, `1e-7 → 1e-7`, `1e20` fixed notation, `1e21` exponential notation, binary64 rounding, `U+E000` versus `U+1F600` property order, duplicate keys, and lone surrogates. Compare number output independently against `node -e 'console.log(JSON.stringify(...))'` where Node is available.
- [ ] **Step 2: Implement strict parsing helpers.**
  Load JSON with duplicate-key rejection, `parse_float=float`, `parse_int` that validates exact binary64 admission, and rejection of non-finite constants and unpaired surrogates.
- [ ] **Step 3: Implement RFC 8785 number serialization.**
  Use Python’s shortest binary64 representation and normalize exponent sign/zero and ECMAScript fixed-versus-exponential thresholds (`1e-6 <= abs(x) < 1e21`). Do not use `Decimal` as the canonical numeric model.
- [ ] **Step 4: Implement UTF-16 property ordering and JSON string escaping.**
  Sort object names by UTF-16 code units, reject lone surrogates, and emit UTF-8 bytes without non-normative whitespace.
- [ ] **Step 5: Route every verifier/generator digest through the module.**
  Remove local `json.dumps(sort_keys=True)`, Decimal canonical number logic, and vector-domain float rejection. Keep Appendix F collection normalization as a separate pre-pass.
- [ ] **Step 6: Run the focused tests and commit.**
  Run `PYTHONDONTWRITEBYTECODE=1 python3 -B docs/contracts/tools/test_rfc8785.py` and the canonical vector verifier. Commit with `test: add independent RFC 8785 canonicalization vectors`.

### Task 3: Add the Qt strict JSON admission and RFC 8785 canonical serializer

**Files:**
- Create: `qt/test/support/noc_contract/strictjson.{h,cpp}`
- Modify: `qt/test/support/noc_contract/contractartifactloader.cpp`
- Modify: `qt/test/support/noc_contract/canonicaljson.cpp`
- Modify: `qt/test/support/noc_contract/canonicaljson.h`
- Modify: `qt/xmake.lua`
- Modify: `qt/test/support/noc_contract/testdata/duplicate-key.json`
- Create: `qt/test/support/noc_contract/testdata/number-boundaries.json`
- Create: `qt/test/support/noc_contract/strictjson_test.cpp`

- [ ] **Step 1: Write failing Qt admission tests.**
  Assert duplicate decoded keys, malformed UTF-8, lone surrogates, non-finite values, and unsupported numeric precision are rejected before `QJsonDocument`; assert `1`, `1.0`, and `1e0` are admitted as the same semantic number.
- [ ] **Step 2: Implement the byte-level scanner.**
  Scan UTF-8 strings, object keys, arrays, and JSON number tokens; decode keys for duplicate comparison; validate surrogate pairing and numeric finiteness before handing bytes to Qt.
- [ ] **Step 3: Implement binary64 RFC 8785 number output.**
  Use `std::to_chars` shortest-roundtrip output and normalize ECMAScript notation thresholds and exponent formatting; add explicit handling for `-0`.
- [ ] **Step 4: Route all Qt contract artifact loading through strict admission.**
  Preserve `QJsonDocument` as the semantic object model, but never use it as the first or only duplicate-key validator.
- [ ] **Step 5: Add the target and run Qt focused tests.**
  Build and run `noc_canonical_digest_vectors_test`, `noc_contract_schema_meta_test`, and the new strict JSON test. Commit with `test: enforce strict JSON and RFC 8785 admission in Qt`.

### Task 4: Close numeric schema semantics and validation modes

**Files:**
- Modify: `docs/contracts/schemas/ipcraft.interface-contract.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.noc-package.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.project-design.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.recovery.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json`
- Modify: `docs/contracts/tools/verify_contract_fixtures.py`
- Modify: `docs/contracts/tools/test_contract_fixtures.py`
- Create: `docs/contracts/fixtures/valid/project-disconnected-domain-working-state.json`
- Create: `docs/contracts/fixtures/invalid/project-disconnected-domain-save-eligible.json`
- Create: `docs/contracts/fixtures/invalid/recovery-missing-disconnected-diagnostic.json`

- [ ] **Step 1: Add explicit validation-mode metadata and schemas.**
  Define working-state and save-eligibility projections without creating a second persisted authority. Recovery references the working-state projection.
- [ ] **Step 2: Change scalar semantics.**
  Permit finite binary64 numbers for `double`; accept integral numeric spellings for `int` when the numeric value is exact; remove lexical-token checks that reject `8.0` solely because it was written with a decimal point.
- [ ] **Step 3: Separate disconnected-Domain behavior.**
  Working-state and commit validation accept a disconnected Domain only with matching blocking `domain.disconnected` diagnostics; Save/Validate/Generate eligibility rejects it.
- [ ] **Step 4: Add positive and negative fixtures.**
  Include legal auto-materialization, recovery reopening, missing diagnostic rejection, and formal-save rejection for the same state.
- [ ] **Step 5: Run fixture and schema tests.**
  Update catalog counts and error policy, then run the focused fixture tests. Commit with `fix: separate working and save validation modes`.

### Task 5: Make schema catalog resolution and ordering names explicit

**Files:**
- Modify: `docs/contracts/schema-catalog.json`
- Modify: all 19 files under `docs/contracts/schemas/`
- Modify: `docs/contracts/tools/verify_contract_fixtures.py`
- Modify: `docs/contracts/tools/verify_canonical_vectors.py`
- Modify: `docs/contracts/tools/verify_fixture_catalog.py`
- Modify: `docs/superpowers/specs/appendix-f-core-canonical-models.md`

- [ ] **Step 1: Freeze the logical-ID retrieval registry.**
  Preserve the public logical schema IDs used by existing ProjectDesign discriminators, and make `schema-catalog.json` the normative ID-to-file/retrieval registry. A generic validator must install this registry before resolving `$ref`.
- [ ] **Step 2: Define catalog resolution.**
  Make logical-ID and relative `$ref` resolution normative and test it with a fresh validator instance, not only the custom authoring loader.
- [ ] **Step 3: Label every ordering rule.**
  Distinguish RFC 8785 UTF-16 property order, Unicode scalar order, and UTF-8 byte order in Appendix F and the canonical rule metadata.
- [ ] **Step 4: Add non-ASCII and ordering vectors.**
  Include private-use versus non-BMP names and all affected collection sort keys. Commit with `docs: close schema URI and ordering contracts`.

### Task 6: Regenerate catalogs, fixtures, vectors, freeze inputs, and archive

**Files:**
- Modify: generated files under `docs/contracts/`
- Modify: `docs/contracts/freeze-inputs.json`
- Modify: `docs/contracts/CORE-FREEZE.md`
- Modify: `docs/contracts/GATE-STATUS.md`
- Modify: `docs.tar`
- Modify: `docs.tar.sha256`
- Create: `docs/contracts/revision-5-release-attestation.json`

- [ ] **Step 1: Regenerate all canonical artifacts.**
  Rebuild vectors, fixtures, catalogs, error policy, and coverage from the single canonicalization module.
- [ ] **Step 2: Run fast conformance and hardening suites separately.**
  Record command, duration, memory budget, and result for each suite.
- [ ] **Step 3: Rebuild deterministic `docs.tar`.**
  Use the existing ustar procedure with the Revision 5 input commit epoch and verify the detached SHA-256.
- [ ] **Step 4: Bind the archive to the revision record.**
  Record contract commit, archive SHA, content digest, toolchain, and test results in the attestation without introducing recursive archive identity.
- [ ] **Step 5: Commit Revision 5 freeze evidence.**
  Run `git diff --check`, archive audit, all Gate 0 targets, and commit with `docs: freeze revision 5 core contract`.

### Task 7: Full Gate 0 verification and implementation handoff

**Files:**
- Modify: `docs/contracts/GATE-STATUS.md`
- Create: `docs/contracts/revision-5-implementation-handoff.md`

- [ ] **Step 1: Run all Qt Gate 0 targets from a clean output tree.**
  Include the strict JSON target and existing validation regression.
- [ ] **Step 2: Run all Python suites from the repository root and from an unpacked archive.**
  Confirm both paths use the same canonical vectors and catalog resolution.
- [ ] **Step 3: Verify source/archive portability rules.**
  Run the source tar audit separately; reject absolute links and development-only directories.
- [ ] **Step 4: Mark Gate 0 Revision 5 frozen and Gate A authorized.**
  Do not claim product implementation completion; the handoff must state the exact contract/implementation boundary.
- [ ] **Step 5: Commit final handoff evidence.**
  Use `docs: record revision 5 gate 0 handoff`.
