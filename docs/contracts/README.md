# Gate 0 Contract Artifacts

This directory contains machine-readable companions to the approved architecture specification.

- `schemas/ipcraft.core-canonical-models.v1.schema.json` closes the Appendix F Core model envelopes.
- `schemas/ipcraft.engine-bundle.v1.schema.json` closes the independently installable Default Engine Bundle manifest.
- `vectors/core-canonical-projection-v1.json` records canonical JSON/digest and set-order examples.
- `error-codes-v1.json` is the machine-readable stable Core/runtime error catalog candidate.
- `fixture-error-policy-v1.json` closes the standalone fixture `(schemaId, validationPhase)` failure classification.
- `unicode/` pins Unicode 17 NFC, simple C/S folding, the official normalization conformance source, source hashes, and the complete Unicode License V3 notice so portable path identity is host-independent.

These files are Revision 4 contract candidates, not a completed Gate 0 freeze. Gate 0 still requires the remaining schemas/fixtures, full vector catalog, error catalog, automated validators, and `CORE-FREEZE.md` digests listed in Appendix E.
