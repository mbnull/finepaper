# Black-Box Audit Guide

Third-party audit agents validate the public contract without implementation unit tests.

Public audit inputs:

- `docs/architecture/v1-core-architecture.md`
- `schemas/`
- `examples/contracts/`
- `ipcraft-cli`
- public package/project files
- public headless API, if published

Workflow:

1. Build or obtain `ipcraft-cli`.
2. Read the architecture contract, schemas, examples, and CLI contract.
3. Write independent tests for valid, malformed, boundary, and fuzz inputs.
4. Run tests only through public CLI/API surfaces.
5. Report failures using `docs/audit/failure-report-format.md`.
6. If behavior is ambiguous, request a public contract update before relying on a hidden expectation.

Hidden audit tests may cover malformed project/package JSON, duplicate ids, unsupported schema rejection, explicit migration, extension enforcement, native namespace preservation, config/table/document/file validation, composition validation, graph-config validation, emitter path security, emitted-input manifests, flow failures, missing executables, timeouts, artifact glob confinement, diagnostic stability, and deterministic writing.

Audit checklist:

- CLI JSON shape
- exit code behavior
- schema validation
- deterministic writing
- duplicate IDs
- extension enforcement
- native preservation
- config, table, document, and file validation
- composition validation
- graph-config validation
- path security
- flow security
- diagnostics stability
- artifact collection
- migration behavior
- old schema rejection
- package cutover

Implementation feedback policy:

- Codex receives failure summaries only.
- Hidden test source, fuzz corpus, and private fixture names are not shared.
- Failure summaries include contract section, expected behavior, actual behavior, and minimal redacted input when necessary.
- If the contract is ambiguous, update the public contract before changing hidden tests or implementation.

Hard-cutover integration policy:

The hard-cutover branch may be internally breaking, but no release/main integration is allowed until the full cutover acceptance criteria pass. Headless model, CLI, schemas, and examples are the first audit surface. Qt editor integration must conform to that model, not define it.
