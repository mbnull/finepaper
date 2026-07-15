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

These files are Revision 4 contract candidates, not a completed Gate 0 freeze. The schema, fixture, vector, and error catalogs are populated; Gate 0 still requires the Qt executable checks, freeze-input manifest, review bundle, and `CORE-FREEZE.md` digests listed in Appendix E.

The non-normative fixture authoring generator uses JSON `null` as the deterministic neutral sample for a `true` boolean schema. A `false` boolean schema is unsatisfiable and produces `UnsatisfiableSampleError`; it is never converted into a placeholder fixture.

The standalone schema verifier intentionally supports a closed portable regular-expression subset shared by its Python executor and ECMA-262: literals, explicit character classes/ranges (including `\uXXXX` escapes), anchors, alternation, capturing/non-capturing groups, negative lookahead, and ordinary quantifiers. Wildcard `.` excludes the four ECMA line terminators, and unescaped `$` means strict end-of-input. Shorthand and boundary classes (`\d`, `\s`, `\w`, `\b` and inverses), backreferences, inline flags, named groups, lookbehind, atomic/possessive constructs, and engine-specific set operators are rejected during schema audit. `$id` and `$ref` are likewise restricted to local catalog identifiers, schema basenames, and RFC 6901 fragments; every reference target is resolved during audit even when no fixture visits it.
