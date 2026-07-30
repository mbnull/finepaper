# Runtime NoC Packages

Each immediate child directory is one runtime-loaded NoC Package. Finepaper
reads only `package.json` for generic information:

- identity and Mesh bounds;
- global parameters and Endpoint types;
- Package-defined Domain schemas and the runtime's explicit Domain data-plane
  consumption contract;
- Endpoint-to-Router attachment capacity, slot mode, and optional named explicit positions;
- executable paths for the required Generator and optional IP Engine.

A Generator is an executable process with these commands:

```text
generator validate --design NORMALIZED_DESIGN --result RESULT_JSON
generator generate --design NORMALIZED_DESIGN --output ARTIFACT_DIRECTORY --result RESULT_JSON
```

`validate` is called only when `generator.supportsValidate` is true and no
validation-providing Engine is declared. `generate` is always called through
the Generator. A successful `result.json` has `success: true` and relative
artifact paths below the output directory.

## Domain runtime contract

Every Package V2/V3 manifest (which already must explicitly declare
`domainTypes`, including an empty array) must also declare the following strict
object. All five booleans are required; unknown fields and implicit defaults
are rejected.

```json
{
  "runtimeCapabilities": {
    "domainConfiguration": {
      "domains": true,
      "memberships": true,
      "relations": true,
      "crossingPolicies": true,
      "edgeOverrides": true
    }
  }
}
```

Each value describes whether the Package runtime pipeline semantically consumes
the corresponding normalized Design data plane. `memberships` maps to
`domainMemberships`, and `relations` maps to `domainRelations`; the other names
match their Design arrays. `false` is a supported, explicit declaration of a
partial runtime, not a parser default. Package V1 has no Domain runtime contract.

Consumption dependencies follow the references in the Design model:

- `memberships`, `relations`, and `crossingPolicies` require `domains`;
- `edgeOverrides` requires `domains`, `memberships`, and `crossingPolicies`
  because an override can only resolve against a crossing derived from element
  assignments.

The contract covers the Package execution pipeline as a whole, including any
validation-providing Engine and the required Generator. Declaring a capability
`true` is therefore a compatibility promise: a successful runtime must validate
and materialize that data plane in a declared output or implementation stage.
It does not, by itself, claim that the primary RTL artifact already implements
the constraints; Package documentation must identify that implementation or
the constraints artifact consumed by a downstream stage.

Finepaper regards locally installed Packages as trusted tools. Process
separation prevents ABI and GUI-process coupling; it is not a security
sandbox.
