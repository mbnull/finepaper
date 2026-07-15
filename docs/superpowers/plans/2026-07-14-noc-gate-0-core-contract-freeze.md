# Default NoC Gate 0 Core Contract Freeze Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Revision 4 architecture into a self-contained, machine-checked Core contract freeze without adding product implementation code.

**Architecture:** Gate 0 treats Markdown as explanation and JSON Schema, fixtures, vectors, and stable catalogs as the executable source of truth. A small Qt console test layer validates the contract bundle, canonical projections, exact Default Engine locks, and the versioned Host side-effect contract before `CORE-FREEZE.md` is generated.

**Tech Stack:** JSON Schema Draft 2020-12, RFC 8785-style canonical JSON rules constrained by Appendix F, SHA-256, Qt 6 Core/Test, C++23, xmake.

---

## File Structure

- Modify `docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json` to close every Appendix F `$def` and canonical array rule.
- Modify `docs/contracts/schemas/ipcraft.engine-bundle.v1.schema.json` to close the exact immutable Default Engine lock and Host ABI fields.
- Create the remaining schemas under `docs/contracts/schemas/` using the normative IDs listed in Appendix E.
- Create `docs/contracts/fixtures/valid/` and `docs/contracts/fixtures/invalid/` with one catalog entry per expected result.
- Extend `docs/contracts/vectors/core-canonical-projection-v1.json` and create focused vectors for side effects, locks, recovery, and output freshness.
- Create `docs/contracts/fixture-catalog.json`, `docs/contracts/schema-catalog.json`, and `docs/contracts/freeze-inputs.json`.
- Create `qt/test/support/noc_contract/contractartifactloader.{h,cpp}` for deterministic artifact discovery and JSON loading.
- Create `qt/test/support/noc_contract/canonicaljson.{h,cpp}` for the frozen canonical projection and SHA-256 helpers.
- Create the seven Gate 0 tests named by Appendix E.
- Modify `qt/xmake.lua` only to register the Gate 0 console targets and their shared support library.
- Create `docs/contracts/CORE-FREEZE.md` only after every Gate 0 command passes.

## Task 1: Freeze the schema catalog and normative IDs

**Files:**
- Create: `docs/contracts/schema-catalog.json`
- Create: `docs/contracts/schemas/ipcraft.project-design.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.patch.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.command-result.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.recovery.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.noc-package.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.interface-contract.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.bundle-manifest.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.tool-input.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.tool-result.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.step-result.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.pipeline-result.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.diagnostic-report.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.artifact-manifest.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.pipeline-plan.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.output-manifest.v1.schema.json`

- [ ] **Step 1: Write the schema catalog first**

Use this exact item shape and list all schemas above plus the two existing Gate 0 schemas:

```json
{
  "schema": "ipcraft.contract-schema-catalog.v1",
  "items": [
    {
      "id": "ipcraft.project-design.v1",
      "path": "schemas/ipcraft.project-design.v1.schema.json",
      "freezeGate": "core"
    }
  ]
}
```

Sort `items` by `id`; reject duplicate IDs or paths.

- [ ] **Step 2: Add each schema as a closed root**

Every root uses Draft 2020-12, an exact `$id`, `additionalProperties: false`, and explicit `required` fields. Reference shared Core objects through the exact relative reference:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "ipcraft.patch.v1",
  "type": "object",
  "additionalProperties": false,
  "required": ["schema", "source", "causality", "operations"],
  "properties": {
    "schema": { "const": "ipcraft.patch.v1" },
    "source": { "enum": ["application", "default-engine", "extension-provider"] },
    "causality": { "$ref": "ipcraft.core-canonical-models.v1.schema.json#/$defs/reconcileApplicability" },
    "operations": { "$ref": "ipcraft.core-canonical-models.v1.schema.json#/$defs/patchOperations" }
  }
}
```

Provider wire responses do not author this trusted envelope; the Host constructs it from the returned patch body.

- [ ] **Step 3: Validate JSON syntax and unique schema IDs**

Run:

```bash
python3 -m json.tool docs/contracts/schema-catalog.json >/dev/null
for file in docs/contracts/schemas/*.json; do python3 -m json.tool "$file" >/dev/null; done
```

Expected: exit 0 with no output.

- [ ] **Step 4: Commit the schema roots**

```bash
git add docs/contracts/schema-catalog.json docs/contracts/schemas
git commit -m "docs: define Gate 0 contract schema roots"
```

## Task 2: Close canonical Core projections and digest rules

**Files:**
- Modify: `docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json`
- Modify: `docs/contracts/vectors/core-canonical-projection-v1.json`
- Create: `docs/contracts/tools/verify_canonical_rules.py`
- Create: `docs/contracts/vectors/core-set-permutation-v1.json`
- Create: `docs/contracts/vectors/candidate-local-ref-v1.json`

- [ ] **Step 1: Add an explicit schema-addressed canonical collection table to the vector document**

Represent every physical frozen array schema node with its catalogued schema ID, exact RFC 6901 schema pointer, kind, and sort key. Resolve `$ref` reuse to the defining node rather than duplicating display aliases:

```json
{
  "schemaId": "ipcraft.core-canonical-models.v1",
  "schemaPointer": "/$defs/derivedState/properties/routers",
  "kind": "set",
  "sortKey": ["id"]
},
{
  "schemaId": "ipcraft.core-canonical-models.v1",
  "schemaPointer": "/$defs/slotTemplate/properties/allowedContracts",
  "kind": "set",
  "sortKey": ["contractId", "version", "bundleManifestDigest"]
}
```

Include dependencies, domains, memberships, package entities, package relations, relation endpoint sets, roles, extensions, Routers, Links, Slots, allowed contracts, update-set reuse, and all other reachable catalogued arrays. Mark only pipeline steps, operation order, and other explicitly semantic sequences as ordered. Keep prose-only Provider ABI collections in `deferredExtensionCollections` with `freezeGate: extension`; they are not Gate 0 Core-completeness entries.

- [ ] **Step 2: Freeze transaction-wide local references**

Add vectors proving that Authority and Application operations share one candidate-local namespace and that digest input contains no final Host ID:

```json
{
  "authorityPatch": {
    "operations": [{ "op": "createEntity", "kind": "router", "localRef": "authority:router-0", "value": {} }]
  },
  "applicationSideEffectPatch": {
    "operations": [{
      "op": "createRelation",
      "kind": "domain-membership",
      "localRef": "application:000001",
      "source": { "localRef": "authority:router-0" },
      "target": { "id": "domain.default" }
    }]
  },
  "allocationOrder": ["application:000001", "authority:router-0"]
}
```

- [ ] **Step 3: Add complete collection permutation cases**

For each set-valued array, store canonical, reverse, and fixed-seed shuffled inputs containing the same non-empty values and one expected normalized array/canonical JSON/digest. For ordered arrays, store a counter-vector whose canonical JSON and digest differ. For derived-ordered arrays, store the valid derived order and a noncanonical supplied order with its stable error code.

Run the stdlib-only authoring verifier before computing vector digests:

```bash
python3 docs/contracts/tools/verify_canonical_rules.py
```

It must prove one rule per reachable physical annotated array schema node, exact schema/table metadata equality, valid RFC 6901 locations, and exclusion of deferred Gate D display paths from Core completeness.

- [ ] **Step 4: Recompute vector digests with one reference implementation**

Use a temporary one-shot script outside the repository or an existing canonicalizer; do not commit a second normative implementation. Store `sha256:` followed by exactly 64 lowercase hexadecimal characters.

- [ ] **Step 5: Commit the canonical models**

```bash
git add docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json docs/contracts/vectors
git commit -m "docs: freeze Core canonical projection vectors"
```

## Task 3: Close exact Default Engine and Host side-effect contracts

**Files:**
- Modify: `docs/contracts/schemas/ipcraft.project-design.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.core-canonical-models.v1.schema.json`
- Modify: `docs/contracts/schemas/ipcraft.engine-bundle.v1.schema.json`
- Create: `docs/contracts/schemas/ipcraft.noc-side-effects.v1.schema.json`
- Modify: `docs/contracts/schema-catalog.json`
- Modify: `docs/contracts/vectors/core-canonical-projection-v1.json`
- Regenerate: `docs/contracts/vectors/core-set-permutation-v1.json`
- Regenerate: `docs/contracts/vectors/candidate-local-ref-v1.json`

Task 3A closes schema and normative contracts only. Full Default Engine resolution/migration and Host side-effect behavioral vector catalogs remain in the later Task 3 vector pass. Engine Host/side-effect mismatch fixtures are structurally valid degraded-inspect witnesses, not schema-invalid fixtures.

- [ ] **Step 1: Make the exact digest the sole implementation identity**

Require this dependency lock shape:

```json
{
  "lockId": "dep.engine.default",
  "kind": "default-engine",
  "id": "ipcraft.default-noc-engine",
  "version": "1.0.0",
  "bundleManifestDigest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "engineCompatibilityVersion": "1",
  "engineHostContractVersion": "ipcraft.engine-host.v1",
  "hostSideEffectContractVersion": "ipcraft.noc-side-effects.v1",
  "supportedPlatformAbis": ["linux-x86_64-gnu-v1"]
}
```

`id`, `version`, and `engineCompatibilityVersion` never satisfy exact resolution when the digest differs.

- [ ] **Step 2: Close resolver outcome semantics**

Specify and witness exact available, missing, revoked, corrupt/mismatch, platform incompatible, Host ABI incompatible, side-effect contract incompatible, and different-digest/same-compatibility-version. Every non-exact case expects degraded inspect and no fallback; full behavioral vectors are deferred to the Task 3 vector pass.

- [ ] **Step 3: Close migration transaction schema**

The candidate must atomically contain dependency-lock update, Derived State Patch, Host side effects, and provenance. Its inverse must restore all four without invoking an Engine.

- [ ] **Step 4: Version Host-owned side effects**

The closed `ipcraft.noc-side-effects.v1` root defines inputs and expected outputs for Default Domain membership creation, membership deletion, Attachment unresolved conversion, Package Relation unresolved/blocking behavior, empty non-Default Domain tombstoning, and connectivity diagnostics. Add only minimal collection-permutation cases required by canonical completeness; behavioral scenarios remain in the Task 3 vector pass.

- [ ] **Step 5: Commit Engine and side-effect contracts**

```bash
git add docs/contracts docs/adr/0054-lock-default-engine-as-an-installable-exact-bundle.md docs/superpowers/specs docs/superpowers/plans/2026-07-14-noc-gate-0-core-contract-freeze.md
git commit -m "docs: close exact Default Engine and side-effect contracts"
```

### Task 3B: Complete Engine and Host side-effect behavioral vectors

**Files:**
- Create: `docs/contracts/vectors/default-engine-lock-v1.json`
- Create: `docs/contracts/vectors/host-side-effects-v1.json`
- Create: `docs/contracts/tools/generate_engine_side_effect_vectors.py`
- Create: `docs/contracts/tools/verify_engine_side_effect_vectors.py`
- Modify: `docs/contracts/vectors/core-canonical-projection-v1.json`
- Modify: `docs/superpowers/specs/appendix-f-core-canonical-models.md`

- [x] **Step 1: Close exact resolution, migration, and freshness catalogs**

Cover the exact committed set of 18 resolution IDs with no fallback, every stable degraded diagnostic, metadata mismatch dimensions, upgrade-only discovery, and retained unsupported Bundles; six migration IDs whose offered variants carry complete before/after Snapshots, causal side effects, normalized candidate digests, forward/inverse application, exact Host IDs/provenance, and zero-Engine Undo/Redo, while the incompatible discovery-only variant forbids transaction/after/execution evidence; and eight computed freshness IDs. Every migration Snapshot Derived State is canonically hashed, bound to derivation, and the before revision/digest is repeated exactly in applicability.

- [x] **Step 2: Close causal Host side-effect vectors**

Use exactly 14 complete non-empty `ipcraft.noc-side-effects.v1` documents. Derive Application operations, impacts, tombstones, diagnostics, allocation order, and disposition from current state plus Authority Patch. Connectivity materializes Router/Structural-Link/Slot creates, updates, and deletes plus generated Membership operations before evaluating the post-candidate undirected graph; isolated vectors cover Router deletion, Link deletion, Link endpoint update, and new-Router Default-membership placement.

- [x] **Step 3: Require generator/verifier independence and mutations**

The stdlib-only generator writes both byte-stable catalogs to `--output-dir`. The independent verifier imports neither generator nor smoke witness, validates closed envelopes and the catalogued schema subset, recomputes transitions/behavior/digests/order, requires exact ID sets, and rejects causal, inverse-state, provenance, identity, ordering, ID-set, and extra-field corruptions.

- [x] **Step 4: Register behavior catalogs and verify regeneration**

The Core vector catalog lists both behavioral catalogs. Appendix F records the closed formats, complete coverage, independent evaluation, and mutation requirements. Authoring verification regenerates to a temporary directory and byte-compares both committed files.

## Task 4A: Freeze the standalone fixture envelope and validation errors

**Files:**
- Create: `docs/contracts/schemas/ipcraft.fixture-catalog.v1.schema.json`
- Create: `docs/contracts/fixture-catalog.json`
- Create: `docs/contracts/tools/verify_fixture_catalog.py`
- Modify: `docs/contracts/schema-catalog.json`
- Modify: `docs/contracts/error-codes-v1.json`
- Modify: `docs/contracts/vectors/core-canonical-projection-v1.json`
- Modify: `docs/contracts/vectors/core-set-permutation-v1.json`
- Modify: `docs/superpowers/specs/appendix-e-gate-acceptance-matrix.md`
- Modify: `docs/superpowers/specs/appendix-f-core-canonical-models.md`

- [x] **Step 1: Define the closed catalog root and entry**

The root schema is exactly `ipcraft.fixture-catalog.v1` plus canonical-set `items` sorted by `path`. Each entry contains exactly `path`, `schemaId`, `validationPhase`, `expected`, `errorCode`, and `behaviorEvidence`. Paths are portable normalized JSON paths below `fixtures/valid/` or `fixtures/invalid/`; `validationPhase` is `schema` or `core-semantic`. Accept entries require the valid prefix and null error. Reject entries require the invalid prefix, a catalogued non-null error, and null behavior evidence. Optional accept evidence is exactly `vectors/<file>.json#<case-id>` and resolves an exact committed case.

- [x] **Step 2: Freeze catalog scope and totality**

The standalone catalog covers deterministic JSON Schema or Core-semantic validation of one JSON document. It excludes filesystem trees, resolver availability, Provider self-digest, degraded-inspect selection, and output freshness; those remain behavioral vectors/tool tests. A structurally valid Project with unavailable exact dependency metadata may be accepted and link to degraded-inspect behavior evidence without claiming that state from the JSON alone. V1 has no fixture-context/directory entry. Coverage means one invalid fixture per named normative rule family/conditional, not every repeated schema keyword. The authoring verifier enforces sorting, uniqueness, physical totality, and exact schema/error/evidence resolution; `--allow-empty` is temporary Task 4A support and default verification rejects empty.

- [x] **Step 3: Freeze standalone error classification**

Use `contract.schema_invalid` for generic JSON/root structure; `project.legacy_format_unsupported`, `project.duplicate_id`, `project.unknown_reference`, and `project.invariant_violation` for Project families; `recovery.binding_mismatch`; `package.invariant_violation`; `contract.invariant_violation`; `engine.migration_invalid`; `tool.input_invalid`; `command.result_invalid`; `diagnostic.report_invalid`; `output.manifest_invalid`; `host.side_effect_result_invalid`; and `dependency.manifest_invalid`. Existing specialized Tool Result/Pipeline Result/Artifact codes remain. `dependency.bundle_mismatch` stays behavioral and output stale reasons gain no codes.

Patch classification is exact: envelope/JSON Schema failure before operation dispatch is `patch.schema_invalid`; an illegal operation discriminant or required operation shape is `patch.operation_invalid`; applying a structurally valid operation that produces a subject violating its subject schema is `patch.schema_violation`; a cross-object invariant is `patch.invariant_violation`. Existing ownership/reference/specialized codes retain their meanings.

- [x] **Step 4: Integrate canonical evidence and verify mutations**

Catalog `ipcraft.fixture-catalog.v1` as the nineteenth schema. Its `items` array is the ninety-ninth physical canonical rule and has one regenerated collection case. Verify JSON syntax, schema catalog sorting/references, canonical rule/vector regeneration, `verify_fixture_catalog.py --allow-empty`, entry-field/conditional/link/sort/uniqueness mutations, Python compilation, the existing Qt contract-example test, and `git diff --check`.

## Task 4B: Build the valid/invalid fixture set

**Files:**
- Create: `docs/contracts/fixtures/valid/*.json`
- Create: `docs/contracts/fixtures/invalid/*.json`
- Modify: `docs/contracts/fixture-catalog.json`

- [ ] **Step 1: Define a closed fixture entry**

```json
{
  "path": "fixtures/invalid/project-duplicate-id.json",
  "schemaId": "ipcraft.project-design.v1",
  "validationPhase": "core-semantic",
  "expected": "reject",
  "errorCode": "project.duplicate_id",
  "behaviorEvidence": null
}
```

Sort by `path`; every fixture appears exactly once.

- [ ] **Step 2: Add representative valid fixtures per persisted root**

Use minimal, representative, and maximum-shape fixtures. The ProjectDesign set must include 1×1, 2×2 with Interface/Attachment/Domain, and a structurally valid exact-lock unavailable witness linked by `behaviorEvidence` to the degraded-inspect vector.

- [ ] **Step 3: Add one invalid fixture per named normative rule family/conditional and stable error**

Include standalone duplicate-ID, forbidden-reference, legacy-root, ownership, applicability, declaration, and artifact-envelope families. Do not encode unlisted Bundle files, symlink/special-file trees, resolver/runtime availability, output freshness, or Provider self-digest as single-document fixtures; retain those in their focused behavioral vectors/tool tests.

- [ ] **Step 4: Check catalog completeness**

Run:

```bash
find docs/contracts/fixtures -type f -name '*.json' -print | sort
python3 -m json.tool docs/contracts/fixture-catalog.json >/dev/null
```

Expected: every printed fixture has one catalog entry and every JSON parses.

- [ ] **Step 5: Commit fixtures**

```bash
git add docs/contracts/fixtures docs/contracts/fixture-catalog.json
git commit -m "test: add Gate 0 contract fixtures"
```

## Task 5: Add the shared contract-test support layer

**Files:**
- Create: `qt/test/support/noc_contract/contractartifactloader.h`
- Create: `qt/test/support/noc_contract/contractartifactloader.cpp`
- Create: `qt/test/support/noc_contract/canonicaljson.h`
- Create: `qt/test/support/noc_contract/canonicaljson.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write a failing loader test in `noc_contract_schema_meta_test.cpp`**

```cpp
QVERIFY2(!ContractArtifactLoader::repositoryRoot().isEmpty(), "repository root must resolve");
const auto catalog = ContractArtifactLoader::loadObject("docs/contracts/schema-catalog.json");
QCOMPARE(catalog.value("schema").toString(), QString("ipcraft.contract-schema-catalog.v1"));
```

- [ ] **Step 2: Register a reusable static support target**

Add `noc_contract_test_support` with Qt Core, `qt/test/support/noc_contract/*.cpp`, and public includes for that directory. Gate 0 test binaries link only this support target.

- [ ] **Step 3: Implement deterministic artifact loading**

Expose exactly:

```cpp
class ContractArtifactLoader final {
public:
    static QString repositoryRoot();
    static QJsonObject loadObject(const QString &relativePath);
    static QJsonArray loadArray(const QString &relativePath);
    static QByteArray loadBytes(const QString &relativePath);
};
```

Reject paths escaping the repository root and JSON parse errors with `std::runtime_error` containing the relative path.

- [ ] **Step 4: Implement canonical projection helpers**

Expose `QByteArray canonicalJson(const QJsonValue &, const CanonicalRuleSet &)` and `QString sha256Digest(QByteArrayView)`. Do not infer sorting: load the explicit rule table from the vector document.

- [ ] **Step 5: Run the first test**

Run: `xmake run -P qt noc_contract_schema_meta_test`

Expected: PASS with `noc_contract_schema_meta_test passed`.

- [ ] **Step 6: Commit support code**

```bash
git add qt/test/support/noc_contract qt/test/noc_contract_schema_meta_test.cpp qt/xmake.lua
git commit -m "test: add NoC contract artifact support"
```

## Task 6: Implement the seven Gate 0 executable checks

**Files:**
- Create: `qt/test/noc_contract_schema_meta_test.cpp`
- Create: `qt/test/noc_canonical_digest_vectors_test.cpp`
- Create: `qt/test/noc_contract_fixture_catalog_test.cpp`
- Create: `qt/test/noc_review_bundle_completeness_test.cpp`
- Create: `qt/test/noc_core_canonical_models_schema_test.cpp`
- Create: `qt/test/noc_default_engine_lock_contract_test.cpp`
- Create: `qt/test/noc_host_side_effect_contract_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add all targets before implementation**

Each target is a non-default Qt console binary, prints `<target> passed`, and links only Qt Core/Test plus `noc_contract_test_support`.

- [ ] **Step 2: Make schema meta-test enforce closed roots**

Assert exact `$id`, Draft 2020-12, catalog uniqueness, referenced file existence, and no unresolved local `$ref`.

- [ ] **Step 3: Make digest-vector test enforce canonical equivalence**

For every equality vector, recompute canonical bytes and digest; for every inequality vector, assert different bytes and digest.

- [ ] **Step 4: Make fixture test enforce catalog totality**

Walk both fixture directories, compare the set with catalog paths, validate expected accepts/rejects, and compare stable error codes.

- [ ] **Step 5: Make review-bundle test enforce normative completeness**

Require the main spec, CONTEXT, Appendices A–F, every active ADR, schema catalog, fixture catalog, vectors, error catalog, and freeze-input manifest. Reject broken relative Markdown links.

- [ ] **Step 6: Make Engine and side-effect tests enforce no fallback**

Iterate the vectors and assert only an exact digest plus compatible Host/platform contracts resolves writable; all other outcomes equal `degraded-inspect`.

- [ ] **Step 7: Run all Gate 0 tests**

```bash
xmake run -P qt noc_contract_schema_meta_test
xmake run -P qt noc_canonical_digest_vectors_test
xmake run -P qt noc_contract_fixture_catalog_test
xmake run -P qt noc_review_bundle_completeness_test
xmake run -P qt noc_core_canonical_models_schema_test
xmake run -P qt noc_default_engine_lock_contract_test
xmake run -P qt noc_host_side_effect_contract_test
```

Expected: all seven print their target name followed by `passed`.

- [ ] **Step 8: Commit the executable checks**

```bash
git add qt/test/noc_* qt/xmake.lua
git commit -m "test: enforce Gate 0 Core contracts"
```

## Task 7: Freeze stable errors and the review input manifest

**Files:**
- Modify: `docs/contracts/error-codes-v1.json`
- Create: `docs/contracts/freeze-inputs.json`
- Modify: `docs/contracts/README.md`

- [ ] **Step 1: Reconcile every normative error code**

Search Appendices B/C/F for code literals, then ensure each appears exactly once with `code`, `owner`, `blocking`, and English message template. Codes are identities; messages are not digest inputs.

- [ ] **Step 2: Create the freeze input manifest**

```json
{
  "schema": "ipcraft.core-freeze-inputs.v1",
  "normativeRevision": 4,
  "files": [
    { "path": "docs/contracts/schema-catalog.json", "sha256": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }
  ]
}
```

Include every normative document and artifact; sort by path. Compute digests from raw file bytes.

- [ ] **Step 3: Run all checks again**

Run the seven Gate 0 commands from Task 6. Expected: PASS.

- [ ] **Step 4: Commit catalogs**

```bash
git add docs/contracts/error-codes-v1.json docs/contracts/freeze-inputs.json docs/contracts/README.md
git commit -m "docs: finalize Gate 0 freeze inputs"
```

## Task 8: Produce the Core freeze record

**Files:**
- Create: `docs/contracts/CORE-FREEZE.md`
- Create or Modify: `docs/contracts/GATE-STATUS.md`
- Rebuild: `docs.tar`

- [ ] **Step 1: Run the complete Gate 0 verification**

```bash
xmake build -P qt qt
xmake run -P qt noc_contract_schema_meta_test
xmake run -P qt noc_canonical_digest_vectors_test
xmake run -P qt noc_contract_fixture_catalog_test
xmake run -P qt noc_review_bundle_completeness_test
xmake run -P qt noc_core_canonical_models_schema_test
xmake run -P qt noc_default_engine_lock_contract_test
xmake run -P qt noc_host_side_effect_contract_test
git diff --check
```

Expected: build and tests pass; `git diff --check` prints nothing.

- [ ] **Step 2: Record the immutable freeze set**

`CORE-FREEZE.md` must state the repository commit, Revision 4, schema/vector/catalog digests, `ipcraft.engine-host.v1`, `ipcraft.noc-side-effects.v1`, and the rule that any Core change requires an unfreeze ADR and downstream Gate replay.

- [ ] **Step 3: Build a deterministic review archive**

Archive only paths listed by `freeze-inputs.json`, plus `CORE-FREEZE.md` and `GATE-STATUS.md`, with sorted names and normalized timestamps. Unpack it in `/tmp`, rerun JSON syntax and link checks against the unpacked copy, then record its SHA-256.

- [ ] **Step 4: Commit the freeze**

```bash
git add docs/contracts/CORE-FREEZE.md docs/contracts/GATE-STATUS.md docs.tar
git commit -m "docs: freeze Default NoC Core contracts"
```

## Gate 0 Exit Check

Do not start Gate A unless all conditions are true:

- the review archive is self-contained and reproducible;
- all seven required tests pass from a clean build directory;
- every schema, fixture, vector, and error is cataloged exactly once;
- exact Default Engine resolution has no fallback path;
- Host side effects are fixed at `ipcraft.noc-side-effects.v1`;
- `CORE-FREEZE.md` records exact digests and an approved commit.
