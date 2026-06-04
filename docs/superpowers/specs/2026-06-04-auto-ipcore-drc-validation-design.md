# Automatic IP Core DRC Validation Design

## Goal

Make the main Validate action automatically run IP-core package DRC after static
project validation, using the V1 `FlowRunner` path instead of reintroducing the
legacy command runner into `ProjectValidationRunner`.

## Problem

The application still has external DRC implementations and direct `DRCRunner`
tests, but the current user-facing Validate path only runs static validation:

1. `MainWindow::runValidation()` calls `ValidationManager::runValidation()`.
2. `ValidationManager` calls `ProjectValidationRunner::validate()`.
3. `ProjectValidationRunner` runs `IpcraftBuiltInValidator` and `BasicValidator`.

This makes IP-specific checks look absent. Missing mesh links, invalid IP-level
parameter combinations, and package-specific topology constraints are not run
from the default UI validation path.

The existing architecture also intentionally keeps `ProjectValidationRunner`
side-effect free. It must not directly reference `QProcess`, `DRCRunner`, or
legacy command execution. External validation belongs behind `FlowRunner`.

## Decision

Use the V1 flow model as the only default external validation path.

The main Validate action will:

1. Run static validation through `ProjectValidationRunner`.
2. Group static blocking errors by project IP instance.
3. For each instance without blocking static errors, execute that package's
   `validate` flow through `FlowRunner`.
4. Convert flow diagnostics and captured process output into `ValidationResult`
   records.
5. Merge static and external results into the existing `LogPanel`.

Packages that do not declare a `validate` flow will produce a clear warning
result instead of silently skipping package DRC.

## Non-Goals

- Do not put `QProcess`, `DRCRunner`, `IpCoreCommandRunner`, or direct external
  command execution into `ProjectValidationRunner`.
- Do not add a separate Run DRC action for this work. The default Validate
  action is the user-facing entry point.
- Do not use the legacy `IpCoreCommandDescriptor` fields as a fallback path.
- Do not remove legacy `DRCRunner` in this change. Existing direct tests can
  remain until a separate cleanup removes the old boundary.
- Do not require perfect source-location mapping for every external diagnostic.
  Unmapped diagnostics must still identify the affected IP instance.

## Architecture

### Static Validation

`ProjectValidationRunner` remains unchanged in responsibility: it validates
package metadata, project records, graph structure, connection contracts, and
basic editor rules without executing external processes.

Its existing side-effect-free architecture gate remains valid.

### External Validation Runner

Add a focused external runner named `ProjectExternalValidationRunner`.

Responsibilities:

- Accept the current `Graph`, catalog entries, project IP instances, project
  path/design name context, and framework tool search paths.
- Find the catalog entry for each instance.
- Read the package spec from the catalog entry's package root.
- Require a package flow with `id: "validate"`.
- Build instance-scoped emitted inputs through the same graph-config projection
  used by project generation.
- Execute `FlowRunner::runFlow()` with `flowId = "validate"`.
- Convert `FlowRunResult` into `ValidationResult` entries.

This runner is allowed to perform external execution through `FlowRunner`.
`ProjectValidationRunner` is not.

### Validation Manager

`ValidationManager::runValidation()` becomes the orchestration boundary:

1. Collect catalog entries and project instances.
2. Run `ProjectValidationRunner`.
3. Build a set of instance IDs with blocking static errors.
4. Run `ProjectExternalValidationRunner` for unblocked instances.
5. Concatenate results and publish to `LogPanel`.

Blocking static errors are errors that already prove an instance cannot be
serialized or validated externally, such as missing catalog entries, invalid
instance records, invalid package metadata, invalid graph-config, or invalid
connection/module ownership for that instance.

If a static error is project-wide rather than instance-scoped, external DRC
should not run for any instance, because the emitted project context may be
invalid.

### Package Validate Flows

Each bundled IP package should declare a public V1 `validate` flow in its
authoring source and generated runtime JSON.

The preferred validate flow shape is:

```json
{
  "id": "validate",
  "label": "Validate",
  "scope": "instance",
  "steps": [
    { "kind": "emit_inputs" },
    {
      "kind": "exec",
      "command": {
        "framework_tool": "ipcraft-generate",
        "args": [
          "--manifest",
          "{package.manifest}",
          "--input",
          "{inputs.manifest}",
          "--validate"
        ],
        "cwd": "run_dir",
        "timeout_ms": 300000,
        "env": { "allow": [] },
        "capture": {
          "stdout": "stdout.log",
          "stderr": "stderr.log",
          "max_bytes": 1048576
        }
      }
    }
  ]
}
```

`ipcraft-generate --validate` should parse the package manifest and emitted
input, run the same validation checks that generation already runs, print a
short success message on success, and exit nonzero with a clear error message on
failure. It must not write generated RTL artifacts.

This keeps generation and DRC semantics aligned while avoiding output churn from
validation.

## Data Flow

For each unblocked instance:

1. `ProjectExternalValidationRunner` computes a run directory under a temporary
   validation root.
2. It reads the package spec from the package root.
3. It calls the same projected graph-config helper used by generation.
4. It builds a `FlowRunRequest`:
   - `projectId`: design name or project basename.
   - `instanceId`: project IP instance ID.
   - `flowId`: `validate`.
   - `runId`: instance ID.
   - `runRoot`: per-instance temporary validation directory.
   - `outputRoot`: same per-instance temporary validation directory.
   - `packageRoot`: package root.
   - `package`: parsed package spec.
   - `config`: instance config bundle.
   - `graphConfig`: projected graph-config for that instance.
   - `frameworkToolSearchPaths`: same defaults as generation.
5. `FlowRunner` emits inputs and executes the validate command.
6. The external runner reads captured `stdout.log` and `stderr.log`.
7. The external runner converts flow diagnostics and captured text into
   `ValidationResult`.

Temporary validation run directories may be deleted after the run. If the run
fails before captured output is read, diagnostics must still include the failure
message produced by `FlowRunner`.

## Diagnostic Mapping

External validation results should be usable even when exact element mapping is
not possible.

Mapping rules:

1. Every external result must include rule name `DRC` or a stable
   `flow.<rule_id>`-derived rule name.
2. Every message must include the affected instance ID unless the message already
   contains it.
3. Flow policy errors, missing executable errors, timeout errors, and nonzero
   process exits become `ValidationSeverity::Error`.
4. Missing `validate` flow becomes `ValidationSeverity::Warning` with a message
   like `Instance 'ravenoc_0' package 'finepaper.ravenoc' does not declare a validate flow.`
5. Captured stderr/stdout lines from failed DRC are preserved in validation
   messages, with duplicate flow messages collapsed.
6. If a line follows a known element pattern, map the external ID back to the
   graph element where possible. Otherwise leave `elementId` empty and rely on
   the instance-prefixed message.

Known element patterns can initially match the legacy DRC output forms already
handled by `DRCRunner`, such as:

- `ERROR element: message`
- `WARNING element: message`
- `XP <id>: message`
- `Endpoint <id>: message`
- `Duplicate XP id: <id>`
- `Duplicate endpoint id: <id>`
- `missing mesh link ...`

The first implementation can keep mapping conservative. It is better to show an
instance-scoped DRC message than to guess the wrong graph element.

## UI Behavior

The user triggers the existing Validate action.

The log panel shows a single merged result set:

- static validation results;
- package DRC warnings for missing `validate` flows;
- package DRC errors from failed validate flows;
- no extra modal on success.

If there are no findings, the existing success logging remains valid.

Validation may take longer because it now executes package flows. The first
implementation can run synchronously like current generation and validation
paths. A future change can move external validation to a worker if UI blocking
becomes a problem.

## Package Changes

Update the authoring specs for bundled packages so generated runtime manifests
include `validate` flows:

- `ipcores/finepaper-noc/ipcore.yml`
- `ipcores/ravenoc/ipcore.yml`
- `ipcores/opennoc/ipcore.yml`

Update the generated runtime `ipcraft.json` files in the same change, using the
repository's established package-generation workflow where practical and keeping
the runtime JSON aligned with the authoring YAML.

The validate flow should use the V1 emitted-input manifest and the shared
`ipcraft-generate --validate` command. Package-local legacy Ruby DRC scripts may
remain for compatibility tests until separately removed.

## Testing Strategy

Use TDD for implementation.

Add focused tests before production changes:

1. `ProjectExternalValidationRunner` executes a package `validate` flow and
   returns no results for a successful validator.
2. A failing validate flow produces an error `ValidationResult` containing the
   instance ID and captured process output.
3. Missing validate flow produces one warning result and does not silently pass.
4. Blocking static validation prevents external validation for the blocked
   instance.
5. `ValidationManager::runValidation()` publishes merged static and external
   results to `LogPanel`.
6. `ProjectValidationRunner` remains side-effect free and still passes the
   architecture gate.
7. `ipcraft-generate --validate` runs validation without writing generation
   artifacts.
8. Bundled `finepaper.noc`, `finepaper.ravenoc`, and `finepaper.opennoc`
   manifests expose `validate` flows.

Run the relevant Qt validation, flow, generation, package spec, and architecture
tests after each task, and run the broader `validation_test` and
`v1architecturegate_test` before completion.

## Risks

- External validation can slow down the Validate action. This is acceptable for
  the first complete fix because correctness is the priority.
- Existing package DRC scripts and `ipcraft-generate` validation may not produce
  identical messages. The V1 flow path should prefer `ipcraft-generate
  --validate` as the source of truth.
- Static errors may not all have instance IDs today. The implementation should
  conservatively skip all external DRC when an error cannot be scoped safely.
- Generated package manifests must stay synchronized with authoring YAML.

## Acceptance Criteria

- Pressing Validate in the Qt app runs static validation and package `validate`
  flows automatically.
- `ProjectValidationRunner` remains static and side-effect free.
- Packages without a validate flow produce visible warnings.
- External validate failures appear in the log panel with the affected instance
  ID.
- Bundled packages declare validate flows.
- `ipcraft-generate --validate` validates inputs without producing generated RTL
  artifacts.
- Existing architecture gate tests pass.
