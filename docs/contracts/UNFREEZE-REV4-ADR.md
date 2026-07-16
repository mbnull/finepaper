# ADR — Unfreeze Revision 4 for Canonicalization and Validation Correction

- Status: Accepted
- Date: 2026-07-16
- Supersedes: Gate 0 Revision 4 freeze record

## Decision

Revision 4 is unfrozen. Its archive integrity, schema inventory, fixture
inventory, and existing behavior vectors remain useful evidence, but Revision 4
is not a valid cross-language digest contract because its Python and Qt
canonicalizers do not implement the RFC 8785 numeric and property-order model
declared by the specification.

Revision 5 retains the existing NoC architecture, Pending Topology Group, exact
Default Engine lock, single Structure Authority, and versioned Host
Side-effect Contract. It revises the canonical input boundary, canonical
serialization, validation modes, schema resolution, and their derived evidence.

## Canonical JSON decision

All digestable JSON uses RFC 8785 canonical JSON after the declared Appendix F
collection projections. The input model is strict UTF-8 JSON with:

- no duplicate object members after JSON string decoding;
- no malformed UTF-8 or unpaired surrogate code points;
- finite IEEE-754 binary64 JSON numbers only;
- no NaN or Infinity;
- exact large integers and exact decimal values represented as strings when
  binary64 cannot preserve their intended value.

The field declaration, not the JSON number token spelling, determines whether a
value is an `int` or `double`. `1`, `1.0`, and `1e0` are the same numeric value
for semantic validation and canonicalize to the same RFC 8785 bytes.

The Host performs strict byte-level admission before constructing a Qt JSON
value. A standard Qt JSON parse is never the only duplicate-key, surrogate, or
number validation boundary.

## Validation-mode decision

The contract distinguishes three predicates over one authoritative working
design:

- `ProjectDesignWellFormed`: UTF-8/JSON/schema/reference structure is readable.
  Recovery and inspect mode may contain a disconnected non-empty Domain.
- `ProjectDesignCommitValid`: an atomic Application transaction may become the
  authoritative working design. A disconnected Domain is allowed only when the
  same commit produces the stable blocking `domain.disconnected` diagnostic.
- `ProjectDesignSaveEligible`: the design may be written to formal `.nocproj`,
  validated, or generated. It requires connected Domains, resolved
  Attachments, current dependencies/Derived State, and no blocking diagnostics.

Recovery stores a WellFormed authoritative working design and re-runs semantic
validation on open. It does not turn a blocking diagnostic into a second source
of truth.

## Consequences

All canonical vectors, fixtures, catalogs, freeze inputs, and the deterministic
review archive must be regenerated as Revision 5. Gate A remains blocked until
the new Core evidence is complete. A later stable release may add migration
from Revision 4, but Revision 4 is not treated as a compatible digest baseline.
