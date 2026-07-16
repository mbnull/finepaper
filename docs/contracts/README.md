# Gate 0 Contract Artifacts

This directory contains machine-readable companions to the approved architecture specification.

- `schemas/ipcraft.core-canonical-models.v1.schema.json` closes the Appendix F Core model envelopes.
- `schemas/ipcraft.engine-bundle.v1.schema.json` closes the independently installable Default Engine Bundle manifest.
- `vectors/core-canonical-projection-v1.json` records canonical JSON/digest and set-order examples.
- `error-codes-v1.json` is the machine-readable stable Core/runtime error catalog candidate.
- `fixture-error-policy-v1.json` closes the standalone fixture `(schemaId, validationPhase, failureBoundary)` classification to one exact stable error code per permitted V1 tuple.
- `fixture-coverage-v1.json` freezes the exact 18-root, three-tier authoring requirements, including all four Recovery safe-draft command shapes, at `sha256:a0fa30dd200104c4783f07e14eb8a345d5f3b4e0ff20bc2b4bdb82f4303ff0e4` and the complete root/tier fixture mapping plus requirements at `sha256:5923528a78cd6d1addecad6c914786b9151e5157b4401da1383e9d8adfaf4eb4`; the verifier pins both digests in code.
- `patch-validation-context-v1.json` freezes the deterministic Patch current-state, current Session revision, complete reconcile applicability, dependency locks, resolved Attachment compatibility, source-specific Domain diagnostics, closed Host side effects, ordinary trusted Patch digests, and formal topology/migration history replay records at `sha256:a213f22e88d68adfd49fb84f1b416bf93055d9335ff468ea62b7ec2a90e629a3` for standalone precondition, ownership, reference, lifecycle, and transaction fixtures.
- `unicode/` pins Unicode 17 NFC, simple C/S folding, the official normalization conformance source, source hashes, and the complete Unicode License V3 notice so portable path identity is host-independent.

These files were Revision 4 contract candidates. Revision 4 was unfrozen for
the Revision 5 canonicalization and validation correction recorded in
`UNFREEZE-REV4-ADR.md`. `CORE-FREEZE.md` is the authoritative status record;
the directory is not a frozen public contract until it explicitly says
Revision 5 is frozen.

## Revision 4 freeze input set

`freeze-inputs.json` is the exact review-bundle input manifest. It contains the
main Default NoC specification, `CONTEXT.md`, Appendices A–F, every ADR whose
status is not superseded, and every regular file below `docs/contracts/` except
`freeze-inputs.json`, `CORE-FREEZE.md`, and `GATE-STATUS.md`. The manifest omits
itself explicitly to avoid a recursive self-digest. Files are sorted by portable
repository-relative path and hashed as their unmodified raw bytes with SHA-256.
Each entry is classified as `normative-artifact`, `generated-fixture`, or
`authoring-tool`; authoring tools are review evidence, not runtime contract data.

The error catalog is sorted by stable `code` and each entry contains exactly
`code`, `owner`, `blocking`, and `messageTemplate`. Runtime/candidate/design
digest identities use the stable code and structured data, never localized or
presentation messages. The review freeze manifest still hashes the complete
catalog file as evidence, so wording changes remain visible to reviewers.
Ownership is independently derived from a closed prefix policy: ordinary codes
are owned by their command/contract/dependency/diagnostic/engine/host/output/
package/patch/pipeline/project/provider/recovery/tool prefix, while attachment,
domain, engine-migration, and package-relation impacts are owned by
`host-side-effects`. The generator never copies an existing `owner` value.

Every relative Markdown link in a frozen document must resolve, after strict URL
decoding and query/anchor removal, to a regular non-symlink file in the same
canonical repository and in the exact freeze-input set. Repository escapes,
missing or omitted files, symlink traversal, absolute paths, and directory links
are rejected. Same-document `#anchors` are allowed without filesystem lookup;
only `http`, `https`, and `mailto` external schemes are exempt from freeze-set
membership.

Reproduce the two generated Gate 0 catalogs before running the executable checks:

```bash
python3 docs/contracts/tools/generate_error_catalog.py --output /tmp/error-codes-v1.json
cmp /tmp/error-codes-v1.json docs/contracts/error-codes-v1.json
python3 docs/contracts/tools/generate_freeze_inputs.py
IPCRAFT_CONTRACT_PYTHON="$(command -v python3)" xmake run -P qt noc_review_bundle_completeness_test
```

`IPCRAFT_CONTRACT_PYTHON` is resolved to an absolute executable by xmake at
configure/build time. The resulting test binary never searches `PATH` at run
time. Delegated verification uses `-B`, removes `PYTHONPATH`, `PYTHONHOME`,
`PYTHONSTARTUP`, and `PYTHONUSERBASE`, disables user-site loading, and sets
`PYTHONDONTWRITEBYTECODE=1`; creating `__pycache__` or `.pyc` anywhere in the
repository fails the executable check. The Python scripts use only the standard
library; the executable identity is authoring-test provenance rather than a
persisted ProjectDesign dependency.

The non-normative fixture authoring generator uses JSON `null` as the deterministic neutral sample for a `true` boolean schema. A `false` boolean schema is unsatisfiable and produces `UnsatisfiableSampleError`; it is never converted into a placeholder fixture.

The standalone schema verifier intentionally supports a closed portable regular-expression subset shared by its Python executor and ECMA-262: literals, explicit character classes/ranges (including `\uXXXX` escapes), anchors, alternation, capturing/non-capturing groups, negative lookahead, and ordinary quantifiers. Wildcard `.` excludes the four ECMA line terminators, and unescaped `$` means strict end-of-input. Shorthand and boundary classes (`\d`, `\s`, `\w`, `\b` and inverses), backreferences, inline flags, named groups, lookbehind, atomic/possessive constructs, and engine-specific set operators are rejected during schema audit. `$id` and `$ref` are likewise restricted to local catalog identifiers, schema basenames, and RFC 6901 fragments; every reference target is resolved during audit even when no fixture visits it.
