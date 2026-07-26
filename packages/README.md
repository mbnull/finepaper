# Runtime NoC Packages

Each immediate child directory is one runtime-loaded NoC Package. Finepaper
reads only `package.json` for generic information:

- identity and Mesh bounds;
- global parameters and Endpoint types;
- Endpoint-to-Router attachment capacity and slot mode;
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

Finepaper regards locally installed Packages as trusted tools. Process
separation prevents ABI and GUI-process coupling; it is not a security
sandbox.
