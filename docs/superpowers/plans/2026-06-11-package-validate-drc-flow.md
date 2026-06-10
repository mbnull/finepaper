# Package Validate DRC Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Qt default validation run package-declared `validate` flows so IP DRC failures are surfaced instead of silently passing.

**Architecture:** Keep the current plugin/package/FlowRunner architecture. Port the validated behavior from `auto-ipcore-drc-validation`, but do not overwrite the hardened `ProjectGenerationRunner`; validation gets its own external runner that invokes package `validate` flows through `ipcraft::FlowRunner` after static validation determines which instances are safe to run.

**Tech Stack:** Qt 6/C++23, Ruby generator CLI, xmake Qt tests, existing `ipcraft.package.v1` flows and `ipcraft.emitted-inputs.v1` input emission.

---

### Task 1: Add Validate-Only Generator Mode And Package Flow Declarations

**Files:**
- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`
- Modify: `ipcraft_generator/test/ipcraft_generator_test.rb`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/finepaper-noc/ipcraft.json`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcraft.json`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcraft.json`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add failing generator CLI tests**

Add tests equivalent to:

```ruby
def test_cli_validate_runs_checks_without_output_directory
  Dir.mktmpdir do |dir|
    manifest_path = File.join(dir, 'ipcraft.json')
    File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
    input_path = write_emitted_inputs(dir, minimal_project)
    output = File.join(dir, 'out')

    stdout, stderr, status = Open3.capture3(
      RbConfig.ruby, CLI,
      '--manifest', manifest_path,
      '--input', input_path,
      '--validate'
    )

    assert status.success?, stderr
    assert_includes stdout, "Validated ipcraft input #{input_path}"
    refute_path_exists output
  end
end
```

Run: `ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb`
Expected: FAIL because `--validate` is not accepted.

- [ ] **Step 2: Implement validate-only mode**

Add `--validate` to the CLI, allow missing `--output` only in validate mode, and add `Generator#validate` that normalizes manifest/input and runs existing validation plus package-specific projection checks.

- [ ] **Step 3: Add package `validate` flows**

Add a `validate` flow to each anchor IP package using:

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
        "args": ["--manifest", "{package.manifest}", "--input", "{inputs.manifest}", "--validate"],
        "cwd": "run_dir",
        "timeout_ms": 300000,
        "env": { "allow": [] },
        "capture": { "stdout": "stdout.log", "stderr": "stderr.log", "max_bytes": 1048576 }
      }
    }
  ]
}
```

- [ ] **Step 4: Verify and commit**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
ruby spec_generator/test/spec_generator_test.rb
ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb
```

Commit:

```bash
git add ipcraft_generator/lib/ipcraft_generator.rb ipcraft_generator/test/ipcraft_generator_test.rb ipcores spec_generator/test/spec_generator_test.rb
git commit -m "feat: add package validate generator flow"
```

### Task 2: Add Shared Package Flow Support For Validation

**Files:**
- Create: `qt/inc/app/projectflowsupport.h`
- Create: `qt/src/app/projectflowsupport.cpp`

- [ ] **Step 1: Add helper API**

Create helpers for:

```cpp
QStringList defaultFrameworkToolSearchPaths();
QString designNameForProject(const QString& projectPath, const QString& explicitDesignName);
const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId);
PackageFlowContext packageFlowContextForEntry(const IpCatalogEntry& entry, const QString& flowId);
std::optional<ipcraft::GraphConfig> projectedGraphConfigForInstance(
    const Graph* graph,
    const QVector<ProjectIpInstanceRecord>& instances,
    const QString& designName,
    const ProjectIpInstanceRecord& instance);
QString readTextFileIfPresent(const QString& path);
bool isSafeInstanceOutputKey(const QString& instanceId);
bool isReservedInstanceOutputKey(const QString& instanceId);
```

- [ ] **Step 2: Keep generation runner intact**

Do not replace the current hardened `ProjectGenerationRunner` implementation in this task. The new helper is consumed first by external validation; generation de-duplication can happen later only after equivalent tests pass.

- [ ] **Step 3: Commit**

Run: `git diff --check`

Commit:

```bash
git add qt/inc/app/projectflowsupport.h qt/src/app/projectflowsupport.cpp
git commit -m "feat: add package flow support helpers"
```

### Task 3: Run Package Validate Flows From Qt Validation

**Files:**
- Create: `qt/inc/validation/projectexternalvalidationrunner.h`
- Create: `qt/src/validation/projectexternalvalidationrunner.cpp`
- Modify: `qt/inc/validation/projectvalidationrunner.h`
- Modify: `qt/src/validation/projectvalidationrunner.cpp`
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/src/app/mainwindow.cpp`

- [ ] **Step 1: Add failing Qt validation tests**

Port tests that require:

```cpp
manager.runValidation(QStringLiteral("/tmp/default-validation.fpproj"),
                      QStringLiteral("default_validation"));
```

Expected behavior:
- built-in/static diagnostics are still reported;
- valid instances run package `validate`;
- instances touched by static blocking errors skip package `validate`;
- BasicValidator blockers only skip touched instances, not the whole project.

Run: `xmake run -P qt validation_test`
Expected: FAIL because current default validation does not run package validate diagnostics.

- [ ] **Step 2: Add external validation runner**

Implement `ProjectExternalValidationRunner::validate()` to:
- return no diagnostics when graph is null, instances are empty, or `blockAllExternalValidation` is true;
- skip `blockingInstanceIds`;
- find each instance's package entry;
- resolve package `validate` flow through `PackageSpecReader`;
- run `ipcraft::FlowRunner::runFlow`;
- parse structured `ERROR element: message` and `WARNING element: message` lines from captured stdout/stderr;
- append flow diagnostics and generic flow failure diagnostics when needed.

- [ ] **Step 3: Make static validation return blocker scope**

Add `ProjectValidationReport` with:

```cpp
QList<ValidationResult> diagnostics;
QSet<QString> blockingInstanceIds;
bool blockAllExternalValidation = false;
bool hasErrors() const;
```

Make `validate()` delegate to `validateDetailed().diagnostics`.

- [ ] **Step 4: Wire ValidationManager and MainWindow**

Change `ValidationManager::runValidation()` to accept optional project path/design name, run static validation first, then run `ProjectExternalValidationRunner` with blocker scope. Change `MainWindow::runValidation()` to pass `m_currentDocumentPath` and the document basename.

- [ ] **Step 5: Commit**

Run:

```bash
xmake run -P qt validation_test
```

Commit:

```bash
git add qt/inc/validation qt/src/validation qt/src/app/mainwindow.cpp qt/test/validation_test.cpp
git commit -m "feat: run package validate flows during validation"
```

### Task 4: Add Dedicated External Validation Runner Coverage

**Files:**
- Create: `qt/test/projectexternalvalidationrunner_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add dedicated runner tests**

Cover:
- missing validate flow emits a warning, not a silent pass;
- failed validate flow becomes an error;
- structured stdout/stderr diagnostics become `ValidationResult`;
- `blockAllExternalValidation` suppresses external DRC;
- `blockingInstanceIds` skips only matching instances.

Run: `xmake run -P qt projectexternalvalidationrunner_test`
Expected: PASS after Task 3 implementation and target registration.

- [ ] **Step 2: Register xmake target**

Register `projectexternalvalidationrunner_test` with `src/validation/projectexternalvalidationrunner.cpp`, `src/app/projectflowsupport.cpp`, `src/ipcraft/flowrunner.cpp`, `src/ipcraft/emitter.cpp`, `src/project/graphprojectserializer.cpp`, and required dependencies.

- [ ] **Step 3: Commit**

Run:

```bash
xmake run -P qt projectexternalvalidationrunner_test
```

Commit:

```bash
git add qt/test/projectexternalvalidationrunner_test.cpp qt/xmake.lua
git commit -m "test: cover external package validation runner"
```

### Task 5: Full Regression And Architecture Gate

**Files:**
- Modify only if verification exposes missing registrations or stale assertions.

- [ ] **Step 1: Run focused verification**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
ruby spec_generator/test/spec_generator_test.rb
ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb
xmake run -P qt validation_test
xmake run -P qt projectexternalvalidationrunner_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt commercial_noc_mvp_test
xmake run -P qt plugin_architecture_phase10_scan_test
xmake build -P qt qt
```

- [ ] **Step 2: Run Qt review linter on changed C++ files**

Run:

```bash
python3 /home/bnl/dev/finepaper/.agents/skills/qt-cpp-review/references/lint-scripts/qt_review_lint.py <changed-qt-cpp-files>
```

- [ ] **Step 3: Commit any verification fixes**

If no fixes are needed, skip this commit. If stale scan/xmake assertions are discovered, fix them with the smallest patch and commit:

```bash
git add <files>
git commit -m "fix: align package validation integration checks"
```
