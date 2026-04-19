# Future Plan

## Goal

Keep the framework module catalog simple enough to edit by hand, while making it authoritative for:

- frontend module metadata
- framework-side validation
- secondary IP-XACT exchange

## Near Term

1. Add a catalog validator.
   Check `framework/src/ruby/model/modules/` for duplicate parameter names, duplicate port IDs, unsupported generators, broken inheritance, and broken template references before export.

2. Add round-trip tests.
   Verify:
   `model/modules/*.json -> frontend bundle`
   `model/modules/*.json -> IP-XACT`
   `IP-XACT -> split bundle`
   This should protect the metadata from silent drift.

3. Reduce remaining repetition in metadata.
   If more module types are added, introduce compact presets for repeated choices, identity blocks, and presentation snippets, but only where it keeps the file readable.

## Medium Term

1. Move more connection rules into the catalog.
   Encode attachment counts, allowed bus families, and directional semantics as data where possible, then have DRC consume that data instead of hardcoding per-type assumptions.

2. Promote runtime objects beyond `xps` and `endpoints`.
   The catalog is now extendable within those families, but the in-memory model and template pipeline still revolve around `XP` and `Endpoint` style roles. The next step is a generic node/attachment runtime so new frontend model families do not need dedicated Ruby classes.

3. Share the same catalog with Qt more directly.
   Today Qt consumes generated XML. The next step is to decide whether Qt should keep using generated XML or read the canonical JSON model through a small converter step during build/startup.

4. Add an export/import command contract.
   Define stable CLI behavior for:
   `bin/export_modules`
   `tools/convert_module_bundle.py`
   so external tooling can rely on them.

## IP-XACT Track

1. Keep IP-XACT secondary, not primary.
   The hand-authored source remains `framework/src/ruby/model/modules/`.

2. Improve fidelity of the IP-XACT mapping.
   The current component export is enough for metadata exchange, but it does not yet model richer IP-XACT structures such as bus definitions, abstraction definitions, or stronger typed relationships.

3. Align with the 1685-2022 document incrementally.
   Use the current `vendorExtensions` approach for frontend-only metadata, and only promote fields into standard IP-XACT structures when there is a clear interoperability benefit.

## Long Term

1. Support more module families.
   As new IP blocks appear, the file-per-model catalog should stay compact and generator-driven instead of growing into large copied blocks.

2. Add schema publication.
   Publish a JSON Schema for the model folder so Ruby, Python, C++, and external tools can validate the catalog consistently.

3. Separate authored data from generated artifacts cleanly.
   Generated frontend bundles and IP-XACT files should remain reproducible outputs and never become the edited source again.
