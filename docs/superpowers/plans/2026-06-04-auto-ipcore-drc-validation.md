# Automatic IP Core DRC Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing Validate action automatically run each unblocked IP instance's package `validate` flow and show the DRC results in the log panel.

**Architecture:** Keep `ProjectValidationRunner` static and side-effect free. Add a separate `ProjectExternalValidationRunner` that executes package `validate` flows through `FlowRunner`, then let `ValidationManager` orchestrate static validation followed by external DRC. Add `ipcraft-generate --validate` and package `validate` flows so bundled packages have a V1 external validation entry point.

**Tech Stack:** Qt 6/C++23, xmake Qt test targets, Ruby/minitest, existing `FlowRunner`, `PackageSpecReader`, `ProjectGenerationRunner`, `spec_generator`, and `ipcraft_generator`.

---

## File Structure

- Modify `ipcraft_generator/test/ipcraft_generator_test.rb`: add CLI tests for validate-only mode and DRC failure behavior.
- Modify `ipcraft_generator/lib/ipcraft_generator.rb`: add `--validate` parsing and `Generator#validate`.
- Modify `spec_generator/test/spec_generator_test.rb`: assert repository package manifests expose `validate` flows.
- Modify `ipcores/finepaper-noc/ipcore.yml`, `ipcores/ravenoc/ipcore.yml`, `ipcores/opennoc/ipcore.yml`: add V1 `validate` flows.
- Regenerate/modify `ipcores/finepaper-noc/ipcraft.json`, `ipcores/ravenoc/ipcraft.json`, `ipcores/opennoc/ipcraft.json`: keep runtime manifests aligned with YAML.
- Create `qt/inc/app/projectflowsupport.h` and `qt/src/app/projectflowsupport.cpp`: shared helpers for project-level flow execution.
- Modify `qt/inc/app/projectgenerationrunner.h` and `qt/src/app/projectgenerationrunner.cpp`: delegate shared flow helpers to `ProjectFlowSupport` without changing generation behavior.
- Create `qt/inc/validation/projectexternalvalidationrunner.h` and `qt/src/validation/projectexternalvalidationrunner.cpp`: run package `validate` flows and translate results to `ValidationResult`.
- Create `qt/test/projectexternalvalidationrunner_test.cpp`: focused tests for the external runner.
- Modify `qt/inc/validation/validationmanager.h` and `qt/src/validation/validationmanager.cpp`: run static validation then external validation.
- Modify `qt/src/app/mainwindow.cpp`: pass project path and design name to `ValidationManager::runValidation`.
- Modify `qt/test/validation_test.cpp`: update integration coverage for default Validate.
- Modify `qt/xmake.lua`: add the new test target and compile new files into affected existing targets.

---

## Task 1: Add `ipcraft-generate --validate`

**Files:**
- Modify: `ipcraft_generator/test/ipcraft_generator_test.rb`
- Modify: `ipcraft_generator/lib/ipcraft_generator.rb`

- [ ] **Step 1: Write the failing Ruby CLI tests**

Insert these tests in `IpcraftGeneratorTest`, after `test_loads_manifest_project_and_writes_output_manifest`:

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

  def test_cli_validate_reports_ravenoc_missing_mesh_link_without_artifacts
    Dir.mktmpdir do |dir|
      project = ravenoc_project
      project.fetch('connections').reject! do |connection|
        connection.fetch('id') == 'rave_0_0_east_to_rave_0_1_west'
      end
      input_path = write_emitted_inputs(dir, project)
      output = File.join(dir, 'out')

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby, CLI,
        '--manifest', File.join(PROJECT_ROOT, 'ipcores/ravenoc/ipcraft.json'),
        '--input', input_path,
        '--validate'
      )

      refute status.success?
      assert_empty stdout
      assert_includes stderr, 'missing mesh link'
      refute_path_exists output
    end
  end
```

- [ ] **Step 2: Run the focused Ruby tests and verify RED**

Run:

```bash
ruby ipcraft_generator/test/ipcraft_generator_test.rb -n '/validate/'
```

Expected: both tests fail because `--validate` is an invalid option.

- [ ] **Step 3: Implement validate-only mode**

In `ipcraft_generator/lib/ipcraft_generator.rb`, update `CLI.run` option parsing and command dispatch:

```ruby
      OptionParser.new do |parser|
        parser.on('--manifest PATH', 'Ipcraft manifest JSON path') { |value| options[:manifest] = value }
        parser.on('--input PATH', 'Ipcraft project input JSON path') { |value| options[:input] = value }
        parser.on('--output DIR', 'Generated output directory') { |value| options[:output] = value }
        parser.on('--validate', 'Validate input without generating artifacts') { options[:validate] = true }
      end.parse!(argv)
```

Replace the output requirement and generator call with:

```ruby
      raise Error, '--manifest is required' unless options[:manifest]
      raise Error, '--input is required' unless options[:input]
      raise Error, '--output is required' unless options[:output] || options[:validate]

      generator = Generator.new(
        manifest: options.fetch(:manifest),
        input: options.fetch(:input),
        output: options[:output]
      )

      if options[:validate]
        generator.validate
        puts "Validated ipcraft input #{options.fetch(:input)}"
      else
        generator.generate
        puts "Generated ipcraft output in #{options.fetch(:output)}"
      end
```

Add this public method to `class Generator`, immediately before `def generate`:

```ruby
    def validate
      manifest = JSON.parse(File.read(@manifest_path))
      manifest = normalize_package_manifest(manifest)
      input = normalize_emitted_inputs(manifest, JSON.parse(File.read(@input_path)))

      validate!(manifest, input)
    end
```

- [ ] **Step 4: Run focused Ruby tests and verify GREEN**

Run:

```bash
ruby ipcraft_generator/test/ipcraft_generator_test.rb -n '/validate/'
```

Expected: 2 runs, 0 failures.

- [ ] **Step 5: Run full generator regression**

Run:

```bash
ruby ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add ipcraft_generator/test/ipcraft_generator_test.rb ipcraft_generator/lib/ipcraft_generator.rb
git commit -m "feat: add ipcraft validate-only generator mode"
```

---

## Task 2: Declare V1 `validate` Flows For Bundled Packages

**Files:**
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `ipcores/finepaper-noc/ipcore.yml`
- Modify: `ipcores/ravenoc/ipcore.yml`
- Modify: `ipcores/opennoc/ipcore.yml`
- Modify: `ipcores/finepaper-noc/ipcraft.json`
- Modify: `ipcores/ravenoc/ipcraft.json`
- Modify: `ipcores/opennoc/ipcraft.json`

- [ ] **Step 1: Write the failing spec-generator contract**

In `assert_repository_ipcraft_manifest_contract`, after the existing generate-flow assertions, add:

```ruby
    validate_flow = manifest.fetch('flows').find { |flow| flow.fetch('id') == 'validate' }
    refute_nil validate_flow
    assert_equal %w[emit_inputs exec],
                 validate_flow.fetch('steps').map { |step| step.fetch('kind') }
    validate_command = validate_flow.fetch('steps').fetch(1).fetch('command')
    assert_equal 'ipcraft-generate', validate_command.fetch('framework_tool')
    assert_includes validate_command.fetch('args'), '{package.manifest}'
    assert_includes validate_command.fetch('args'), '{inputs.manifest}'
    assert_includes validate_command.fetch('args'), '--validate'
    refute_includes validate_flow.fetch('steps').map { |step| step.fetch('kind') }, 'collect_artifacts'
```

- [ ] **Step 2: Run the spec-generator contract and verify RED**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb -n '/repository_ipcraft_manifest_contract|repository_ipcraft_package_source_schema/'
```

Expected: failure because repository manifests do not expose `validate` flows.

- [ ] **Step 3: Add `validate` flow to each package YAML**

In each of `ipcores/finepaper-noc/ipcore.yml`, `ipcores/ravenoc/ipcore.yml`, and `ipcores/opennoc/ipcore.yml`, add this flow immediately after the existing `generate` flow:

```yaml
  - id: validate
    label: Validate
    scope: instance
    steps:
      - kind: emit_inputs
      - kind: exec
        command:
          framework_tool: ipcraft-generate
          args: ["--manifest", "{package.manifest}", "--input", "{inputs.manifest}", "--validate"]
          cwd: run_dir
          timeout_ms: 300000
          env: { allow: [] }
          capture:
            stdout: stdout.log
            stderr: stderr.log
            max_bytes: 1048576
```

- [ ] **Step 4: Regenerate runtime manifests**

Run:

```bash
ruby spec_generator/bin/spec-gen
```

Expected: `ipcores/finepaper-noc/ipcraft.json`, `ipcores/ravenoc/ipcraft.json`, and `ipcores/opennoc/ipcraft.json` are updated with matching `validate` flows.

- [ ] **Step 5: Run spec-generator regression**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: all tests pass.

- [ ] **Step 6: Run validate-only CLI against each bundled package**

Run:

```bash
ruby ipcraft_generator/test/ipcraft_generator_test.rb -n '/validate|generates_finepaper_noc|generates_ravenoc|generates_opennoc/'
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit**

```bash
git add spec_generator/test/spec_generator_test.rb ipcores/finepaper-noc/ipcore.yml ipcores/ravenoc/ipcore.yml ipcores/opennoc/ipcore.yml ipcores/finepaper-noc/ipcraft.json ipcores/ravenoc/ipcraft.json ipcores/opennoc/ipcraft.json
git commit -m "feat: declare package validate flows"
```

---

## Task 3: Add External Validation Runner Tests

**Files:**
- Create: `qt/test/projectexternalvalidationrunner_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Create the failing external-runner test file**

Create `qt/test/projectexternalvalidationrunner_test.cpp` with this content:

```cpp
// ProjectExternalValidationRunner tests package validate flows.
#include "validation/projectexternalvalidationrunner.h"

#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

void makeExecutable(const QString& path) {
    QFile file(path);
    require(file.setPermissions(QFile::ReadOwner |
                                QFile::WriteOwner |
                                QFile::ExeOwner |
                                QFile::ReadGroup |
                                QFile::ExeGroup |
                                QFile::ReadOther |
                                QFile::ExeOther),
            "failed to make test script executable");
}

QJsonArray strings(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject flowCapture() {
    return QJsonObject{
        {QStringLiteral("stdout"), QStringLiteral("stdout.log")},
        {QStringLiteral("stderr"), QStringLiteral("stderr.log")},
        {QStringLiteral("max_bytes"), 1048576}
    };
}

QJsonArray packageEmitters() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("graph_config")},
            {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
            {QStringLiteral("path"), QStringLiteral("graph_config.json")}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("parameters")},
            {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
            {QStringLiteral("path"), QStringLiteral("parameters.json")}
        }
    };
}

QJsonObject validateCommand(const QString& executable, const QStringList& args) {
    return QJsonObject{
        {QStringLiteral("executable"), executable},
        {QStringLiteral("args"), strings(args)},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("timeout_ms"), 300000},
        {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
        {QStringLiteral("capture"), flowCapture()}
    };
}

QJsonArray validateFlows(const QJsonObject& command) {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("validate")},
            {QStringLiteral("label"), QStringLiteral("Validate")},
            {QStringLiteral("scope"), QStringLiteral("instance")},
            {QStringLiteral("steps"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}},
                 QJsonObject{
                     {QStringLiteral("kind"), QStringLiteral("exec")},
                     {QStringLiteral("command"), command}
                 }
             }}
        }
    };
}

void writePackageSpec(const QString& packageRoot,
                      const QString& packageId,
                      const QJsonArray& flows) {
    require(QDir().mkpath(packageRoot), "package root should be created");
    const QJsonObject spec{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), packageId},
        {QStringLiteral("name"), packageId},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), strings({
            QStringLiteral("ipcraft.config.params"),
            QStringLiteral("ipcraft.graph_config"),
            QStringLiteral("ipcraft.emitters"),
            QStringLiteral("ipcraft.flows")
        })},
        {QStringLiteral("graph_config"),
         QJsonObject{
             {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
             {QStringLiteral("objects"), QJsonArray{}},
             {QStringLiteral("relationships"), QJsonArray{}},
             {QStringLiteral("properties"), QJsonObject{}},
             {QStringLiteral("native"), QJsonObject{}}
         }},
        {QStringLiteral("emitters"), packageEmitters()},
        {QStringLiteral("flows"), flows}
    };
    writeFile(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")),
              QJsonDocument(spec).toJson(QJsonDocument::Indented));
}

QString writeScript(const QString& packageRoot,
                    const QString& fileName,
                    const QByteArray& content) {
    const QDir packageDir(packageRoot);
    require(QDir().mkpath(packageDir.filePath(QStringLiteral("tools"))),
            "package tools directory should be created");
    const QString path = packageDir.filePath(QStringLiteral("tools/%1").arg(fileName));
    writeFile(path, content);
    makeExecutable(path);
    return QStringLiteral("tools/%1").arg(fileName);
}

IpCatalogEntry catalogEntry(const QString& ipcoreId, const QString& packageRoot) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.packageId = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0.0");
    entry.kind = QStringLiteral("ipcraft");
    entry.runtimeRootPath = packageRoot;
    entry.sourceRootPath = packageRoot;
    entry.packageManifest.schema = ipcraft::schemaids::packageV1;
    entry.packageManifest.id = ipcoreId;
    entry.packageManifest.name = ipcoreId;
    entry.packageManifest.version = QStringLiteral("1.0.0");
    entry.packageManifest.packageRootPath = packageRoot;
    return entry;
}

ProjectIpInstanceRecord instanceRecord(const QString& ipcoreId, const QString& instanceId) {
    ProjectIpInstanceRecord record;
    record.id = instanceId;
    record.package = ProjectPackageRef{ipcoreId, QStringLiteral("1.0.0")};
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.config = QJsonObject{{QStringLiteral("parameters"), QJsonObject{}}};
    return record;
}

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& ipcoreId,
                                   const QString& instanceId) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Tile"));
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    module->setParameter(QStringLiteral("external_id"), id);
    module->addPort(Port(QStringLiteral("link"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("Link")));
    return module;
}

bool hasMessageContaining(const QList<ValidationResult>& results, const QString& text) {
    for (const ValidationResult& result : results) {
        if (result.message().contains(text)) {
            return true;
        }
    }
    return false;
}

ProjectExternalValidationRequest requestFor(const Graph& graph,
                                            const IpCatalogEntry& entry,
                                            const ProjectIpInstanceRecord& instance) {
    ProjectExternalValidationRequest request;
    request.graph = &graph;
    request.projectPath = QStringLiteral("/tmp/external-validation-test.fpproj");
    request.designName = QStringLiteral("external_validation_test");
    request.catalogEntries = {entry};
    request.instances = {instance};
    return request;
}

void testSuccessfulValidateFlowReturnsNoFindings() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString packageRoot = QDir(tempDir.path()).filePath(QStringLiteral("package"));
    const QString tool = writeScript(
        packageRoot,
        QStringLiteral("validate-ok.sh"),
        QByteArrayLiteral("#!/bin/sh\n"
                          "set -eu\n"
                          "test -f \"$1\"\n"
                          "test -f \"inputs/graph_config.json\"\n"
                          "test -f \"inputs/parameters.json\"\n"
                          "exit 0\n"));
    writePackageSpec(packageRoot,
                     QStringLiteral("finepaper.external"),
                     validateFlows(validateCommand(tool, {QStringLiteral("{inputs.manifest}")})));

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("tile_0"),
                                       QStringLiteral("finepaper.external"),
                                       QStringLiteral("external_0"))),
            "module should add");
    const ProjectIpInstanceRecord instance =
        instanceRecord(QStringLiteral("finepaper.external"), QStringLiteral("external_0"));

    const QList<ValidationResult> results =
        ProjectExternalValidationRunner().validate(
            requestFor(graph, catalogEntry(QStringLiteral("finepaper.external"), packageRoot), instance));

    require(results.isEmpty(), "successful validate flow should not emit findings");
}

void testFailingValidateFlowReturnsCapturedOutput() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString packageRoot = QDir(tempDir.path()).filePath(QStringLiteral("package"));
    const QString tool = writeScript(
        packageRoot,
        QStringLiteral("validate-fail.sh"),
        QByteArrayLiteral("#!/bin/sh\n"
                          "echo 'ERROR tile_0: scripted DRC violation' >&2\n"
                          "exit 7\n"));
    writePackageSpec(packageRoot,
                     QStringLiteral("finepaper.external"),
                     validateFlows(validateCommand(tool, {QStringLiteral("{inputs.manifest}")})));

    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("tile_0"),
                                       QStringLiteral("finepaper.external"),
                                       QStringLiteral("external_0"))),
            "module should add");
    const ProjectIpInstanceRecord instance =
        instanceRecord(QStringLiteral("finepaper.external"), QStringLiteral("external_0"));

    const QList<ValidationResult> results =
        ProjectExternalValidationRunner().validate(
            requestFor(graph, catalogEntry(QStringLiteral("finepaper.external"), packageRoot), instance));

    require(hasMessageContaining(results, QStringLiteral("external_0")),
            "external DRC result should include the instance id");
    require(hasMessageContaining(results, QStringLiteral("scripted DRC violation")),
            "external DRC result should include captured stderr");
}

void testMissingValidateFlowWarns() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString packageRoot = QDir(tempDir.path()).filePath(QStringLiteral("package"));
    writePackageSpec(packageRoot, QStringLiteral("finepaper.external"), QJsonArray{});

    Graph graph;
    const ProjectIpInstanceRecord instance =
        instanceRecord(QStringLiteral("finepaper.external"), QStringLiteral("external_0"));

    const QList<ValidationResult> results =
        ProjectExternalValidationRunner().validate(
            requestFor(graph, catalogEntry(QStringLiteral("finepaper.external"), packageRoot), instance));

    require(results.size() == 1, "missing validate flow should produce one warning");
    require(results.first().severity() == ValidationSeverity::Warning,
            "missing validate flow should be a warning");
    require(results.first().message().contains(QStringLiteral("does not declare a validate flow")),
            "missing validate flow warning should be explicit");
}

void testStaticProjectWideErrorSkipsExternalValidation() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString packageRoot = QDir(tempDir.path()).filePath(QStringLiteral("package"));
    const QString marker = QDir(tempDir.path()).filePath(QStringLiteral("validate-ran"));
    const QString tool = writeScript(
        packageRoot,
        QStringLiteral("validate-marker.sh"),
        QString("#!/bin/sh\nprintf ran > '%1'\nexit 0\n").arg(marker).toUtf8());
    writePackageSpec(packageRoot,
                     QStringLiteral("finepaper.external"),
                     validateFlows(validateCommand(tool, {QStringLiteral("{inputs.manifest}")})));

    Graph graph;
    const ProjectIpInstanceRecord instance =
        instanceRecord(QStringLiteral("finepaper.external"), QStringLiteral("external_0"));
    ProjectExternalValidationRequest request =
        requestFor(graph, catalogEntry(QStringLiteral("finepaper.external"), packageRoot), instance);
    request.staticResults = {
        ValidationResult(ValidationSeverity::Error,
                         QStringLiteral("Graph is not available."),
                         QString(),
                         QStringLiteral("built_in_project"))
    };

    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.isEmpty(), "project-wide static error should skip external validation");
    require(!QFileInfo::exists(marker), "external validate script should not run");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testSuccessfulValidateFlowReturnsNoFindings();
        testFailingValidateFlowReturnsCapturedOutput();
        testMissingValidateFlowWarns();
        testStaticProjectWideErrorSkipsExternalValidation();
    } catch (const std::exception& error) {
        std::cerr << "projectexternalvalidationrunner_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "projectexternalvalidationrunner_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add a new xmake test target**

Add this target to `qt/xmake.lua` near `projectgenerationrunner_test`:

```lua
add_qt_test_target("projectexternalvalidationrunner_test", "test/projectexternalvalidationrunner_test.cpp", {
    "src/validation/projectexternalvalidationrunner.cpp",
    "src/app/projectflowsupport.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/ipcraft/flowrunner.cpp",
    "src/ipcraft/artifactmodel.cpp",
    "src/ipcraft/emitter.cpp",
    "src/ipcraft/configschema.cpp",
    "src/ipcraft/value.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcraft/packagespec.cpp",
    "src/ipcraft/diagnostics.cpp",
    "src/ipcraft/jsonhelpers.cpp",
    "src/ipcraft/ipcraftmanifest.cpp",
    "src/ipcraft/ipcraftmanifestreader.cpp",
    "src/ipcraft/ipcraftregistry.cpp",
    "src/project/graphprojectserializer.cpp",
    "src/project/projectstateservice.cpp",
    "src/validation/validationresult.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "inc/validation/projectexternalvalidationrunner.h",
    "inc/app/projectflowsupport.h",
    "inc/ipcraft/flowrunner.h",
    "inc/ipcraft/artifactmodel.h",
    "inc/ipcraft/emitter.h",
    "inc/ipcraft/configschema.h",
    "inc/ipcraft/value.h",
    "inc/ipcraft/compositionmodel.h",
    "inc/ipcraft/packagespec.h",
    "inc/ipcraft/diagnostics.h",
    "inc/ipcraft/jsonhelpers.h",
    "inc/ipcraft/schemaids.h",
    "inc/project/graphprojectserializer.h",
    "inc/project/projectstateservice.h",
    "inc/validation/validationresult.h",
    "inc/graph/graph.h",
    "inc/graph/module.h"
})
```

- [ ] **Step 3: Run the new test target and verify RED**

Run:

```bash
xmake run -P qt projectexternalvalidationrunner_test
```

Expected: build fails because `validation/projectexternalvalidationrunner.h` and `app/projectflowsupport.h` do not exist.

- [ ] **Step 4: Commit the failing tests**

```bash
git add qt/test/projectexternalvalidationrunner_test.cpp qt/xmake.lua
git commit -m "test: cover external ipcore validate runner"
```

---

## Task 4: Implement Shared Flow Support And External Runner

**Files:**
- Create: `qt/inc/app/projectflowsupport.h`
- Create: `qt/src/app/projectflowsupport.cpp`
- Create: `qt/inc/validation/projectexternalvalidationrunner.h`
- Create: `qt/src/validation/projectexternalvalidationrunner.cpp`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Create `ProjectFlowSupport` header**

Create `qt/inc/app/projectflowsupport.h`:

```cpp
// Shared helpers for project-level package FlowRunner execution.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "ipcraft/packagespec.h"
#include "project/ipinstancestate.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class Graph;

namespace ProjectFlowSupport {

struct PackageFlowContext {
    enum class ErrorKind {
        None,
        MissingPackageRoot,
        SpecReadFailed,
        MissingFlow,
    };

    bool ok = false;
    ErrorKind errorKind = ErrorKind::None;
    ipcraft::PackageSpec package;
    QString packageRoot;
    QString error;
};

QStringList defaultFrameworkToolSearchPaths();
QString designNameForProject(const QString& projectPath, const QString& explicitDesignName);
QString withInstanceContext(const ProjectIpInstanceRecord& instance, const QString& message);
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

} // namespace ProjectFlowSupport
```

- [ ] **Step 2: Implement `ProjectFlowSupport` by moving existing generation helpers**

Create `qt/src/app/projectflowsupport.cpp` by moving these helper implementations out of `qt/src/app/projectgenerationrunner.cpp`:

```cpp
// Shared helpers for project-level package FlowRunner execution.
#include "app/projectflowsupport.h"

#include "graph/graph.h"
#include "ipcraft/emitter.h"
#include "project/graphprojectserializer.h"
#include "project/projectdocument.h"
#include "project/projectstateservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QSet>
#include <algorithm>

namespace {

void appendUniquePath(QStringList& paths, const QString& path) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return;
    }
    const QString absolutePath = QFileInfo(trimmedPath).absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

bool isSourceTreeWithFrameworkTool(const QDir& dir) {
    return QFileInfo(dir.filePath(QStringLiteral("qt/xmake.lua"))).isFile()
           && QFileInfo(dir.filePath(QStringLiteral("ipcraft_generator/bin/ipcraft-generate"))).isFile();
}

void appendSourceTreeFrameworkToolPath(QStringList& paths, const QDir& applicationDir) {
    QDir candidate = applicationDir;
    while (true) {
        if (isSourceTreeWithFrameworkTool(candidate)) {
            appendUniquePath(paths, candidate.filePath(QStringLiteral("ipcraft_generator/bin")));
            return;
        }
        if (!candidate.cdUp()) {
            return;
        }
    }
}

QString normalizedInstanceOutputKey(const QString& instanceId) {
    return instanceId.toCaseFolded();
}

} // namespace

namespace ProjectFlowSupport {

QStringList defaultFrameworkToolSearchPaths() {
    QStringList paths;

    const QDir application(QCoreApplication::applicationDirPath());
    appendSourceTreeFrameworkToolPath(paths, application);
    appendUniquePath(paths, application.filePath(QStringLiteral("ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../../ipcraft_generator/bin")));

    appendUniquePath(paths, QStringLiteral("/usr/local/libexec/finepaper"));
    appendUniquePath(paths, QStringLiteral("/usr/local/bin"));
    return paths;
}

QString designNameForProject(const QString& projectPath, const QString& explicitDesignName) {
    const QString requested = explicitDesignName.trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }
    const QString fromProjectPath = QFileInfo(projectPath).completeBaseName().trimmed();
    return fromProjectPath.isEmpty() ? QStringLiteral("design") : fromProjectPath;
}

QString withInstanceContext(const ProjectIpInstanceRecord& instance, const QString& message) {
    return QStringLiteral("Instance '%1' (%2): %3")
        .arg(instance.instanceId, instance.ipcoreId, message);
}

const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId) {
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const IpCatalogEntry& entry) {
        return entry.id == ipcoreId;
    });
    return it == entries.cend() ? nullptr : &(*it);
}

PackageFlowContext packageFlowContextForEntry(const IpCatalogEntry& entry, const QString& flowId) {
    PackageFlowContext context;
    const QString packageRoot = !entry.packageManifest.packageRootPath.trimmed().isEmpty()
        ? entry.packageManifest.packageRootPath
        : entry.runtimeRootPath;
    if (packageRoot.trimmed().isEmpty()) {
        context.errorKind = PackageFlowContext::ErrorKind::MissingPackageRoot;
        context.error = QStringLiteral("Package root is not available.");
        return context;
    }

    const ipcraft::PackageSpecReadResult specResult =
        ipcraft::PackageSpecReader().readPackageRoot(packageRoot);
    if (!specResult.ok) {
        QStringList messages;
        for (const ipcraft::Diagnostic& diagnostic : specResult.diagnostics.records) {
            messages.append(diagnostic.message);
        }
        context.errorKind = PackageFlowContext::ErrorKind::SpecReadFailed;
        context.error = messages.isEmpty()
            ? QStringLiteral("Package spec could not be read.")
            : messages.join(QStringLiteral("\n"));
        return context;
    }

    const bool hasFlow =
        std::any_of(specResult.spec.flows.constBegin(),
                    specResult.spec.flows.constEnd(),
                    [&](const QJsonValue& flowValue) {
                        return flowValue.isObject() &&
                               flowValue.toObject().value(QStringLiteral("id")).toString() == flowId;
                    });
    if (!hasFlow) {
        context.errorKind = PackageFlowContext::ErrorKind::MissingFlow;
        context.error = QStringLiteral("Package does not declare a %1 flow.").arg(flowId);
        return context;
    }

    context.ok = true;
    context.errorKind = PackageFlowContext::ErrorKind::None;
    context.package = specResult.spec;
    context.packageRoot = packageRoot;
    return context;
}

std::optional<ipcraft::GraphConfig> projectedGraphConfigForInstance(
    const Graph* graph,
    const QVector<ProjectIpInstanceRecord>& instances,
    const QString& designName,
    const ProjectIpInstanceRecord& instance) {
    if (!graph) {
        return std::nullopt;
    }

    ProjectDocument document = GraphProjectSerializer::toProject(*graph, designName);
    ProjectStateService stateService;
    for (const ProjectIpInstanceRecord& record : instances) {
        stateService.ensureIpInstanceRecord(record);
    }
    stateService.writeToDocument(document);
    for (const ProjectIpInstanceRecord& projected : document.instances) {
        if (projected.id != instance.instanceId && projected.id != instance.id) {
            continue;
        }
        if (!projected.hasGraphConfig || projected.graphConfigIsNull) {
            return std::nullopt;
        }
        const ipcraft::GraphConfigReadResult graphConfig =
            ipcraft::GraphConfig::fromJson(projected.graphConfig);
        if (graphConfig.ok) {
            return graphConfig.config;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

QString readTextFileIfPresent(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool isSafeInstanceOutputKey(const QString& instanceId) {
    const QString trimmed = instanceId.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

bool isReservedInstanceOutputKey(const QString& instanceId) {
    return normalizedInstanceOutputKey(instanceId)
        == QStringLiteral("project-snapshot.fpproj");
}

} // namespace ProjectFlowSupport
```

- [ ] **Step 3: Update `ProjectGenerationRunner` to use `ProjectFlowSupport`**

In `qt/src/app/projectgenerationrunner.cpp`:

1. Add `#include "app/projectflowsupport.h"`.
2. Delete the moved helper definitions from the anonymous namespace.
3. Replace helper calls:

```cpp
return ProjectFlowSupport::defaultFrameworkToolSearchPaths();
```

```cpp
const QString designName = ProjectFlowSupport::designNameForProject(request.projectPath, request.designName);
```

```cpp
const IpCatalogEntry* entry =
    ProjectFlowSupport::findCatalogEntry(request.catalogEntries, instance.ipcoreId);
```

```cpp
const ProjectFlowSupport::PackageFlowContext flowContext =
    ProjectFlowSupport::packageFlowContextForEntry(*entry, QStringLiteral("generate"));
```

```cpp
flowRequest.graphConfig =
    ProjectFlowSupport::projectedGraphConfigForInstance(request.graph,
                                                        request.instances,
                                                        designName,
                                                        instance);
```

```cpp
result.standardOutput =
    ProjectFlowSupport::readTextFileIfPresent(QDir(result.outputDirectory).filePath(QStringLiteral("stdout.log")));
result.standardError =
    ProjectFlowSupport::readTextFileIfPresent(QDir(result.outputDirectory).filePath(QStringLiteral("stderr.log")));
```

Also replace `withInstanceContext`, `isSafeInstanceOutputKey`, and `isReservedInstanceOutputKey` call sites with `ProjectFlowSupport::...`.

- [ ] **Step 4: Create external runner header**

Create `qt/inc/validation/projectexternalvalidationrunner.h`:

```cpp
// ProjectExternalValidationRunner executes package validate flows through FlowRunner.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class Graph;

struct ProjectExternalValidationRequest {
    const Graph* graph = nullptr;
    QString projectPath;
    QString designName;
    QList<IpCatalogEntry> catalogEntries;
    QVector<ProjectIpInstanceRecord> instances;
    QList<ValidationResult> staticResults;
    QStringList frameworkToolSearchPaths;
};

class ProjectExternalValidationRunner {
public:
    ProjectExternalValidationRunner();
    explicit ProjectExternalValidationRunner(QStringList frameworkToolSearchPaths);

    QStringList frameworkToolSearchPaths() const;
    void setFrameworkToolSearchPaths(QStringList searchPaths);

    QList<ValidationResult> validate(const ProjectExternalValidationRequest& request) const;

private:
    QStringList m_frameworkToolSearchPaths;
};
```

- [ ] **Step 5: Implement external runner**

Create `qt/src/validation/projectexternalvalidationrunner.cpp` with these functions and behavior:

```cpp
// ProjectExternalValidationRunner implementation.
#include "validation/projectexternalvalidationrunner.h"

#include "app/projectflowsupport.h"
#include "graph/graph.h"
#include "ipcraft/flowrunner.h"

#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <utility>

namespace {

struct BlockedScopes {
    bool blockAll = false;
    QSet<QString> instanceIds;
};

QString instanceIdFor(const ProjectIpInstanceRecord& instance) {
    return instance.instanceId.isEmpty() ? instance.id : instance.instanceId;
}

QString prefixedMessage(const ProjectIpInstanceRecord& instance, const QString& message) {
    const QString instanceId = instanceIdFor(instance);
    if (message.contains(instanceId)) {
        return message;
    }
    return QStringLiteral("Instance '%1': %2").arg(instanceId, message);
}

BlockedScopes blockedScopesFor(const QList<ValidationResult>& staticResults,
                               const QVector<ProjectIpInstanceRecord>& instances) {
    BlockedScopes scopes;
    for (const ValidationResult& result : staticResults) {
        if (result.severity() != ValidationSeverity::Error) {
            continue;
        }
        bool matchedInstance = false;
        for (const ProjectIpInstanceRecord& instance : instances) {
            const QString id = instanceIdFor(instance);
            if (result.message().contains(id) || result.elementId() == id) {
                scopes.instanceIds.insert(id);
                matchedInstance = true;
            }
        }
        if (!matchedInstance) {
            scopes.blockAll = true;
        }
    }
    return scopes;
}

QList<ValidationResult> outputDiagnostics(const ProjectIpInstanceRecord& instance,
                                          const QString& output) {
    QList<ValidationResult> results;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QRegularExpression structured(
        QStringLiteral("^(ERROR|WARNING|error|warning)\\s+(.+?):\\s+(.+)$"));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QRegularExpressionMatch match = structured.match(line);
        if (match.hasMatch()) {
            const bool warning = match.captured(1).toLower().startsWith(QStringLiteral("warn"));
            results.append(ValidationResult(warning ? ValidationSeverity::Warning : ValidationSeverity::Error,
                                            prefixedMessage(instance, match.captured(3)),
                                            match.captured(2),
                                            QStringLiteral("DRC")));
            continue;
        }
        results.append(ValidationResult(ValidationSeverity::Error,
                                        prefixedMessage(instance, line),
                                        QString(),
                                        QStringLiteral("DRC")));
    }
    return results;
}

QList<ValidationResult> flowDiagnostics(const ProjectIpInstanceRecord& instance,
                                        const ipcraft::FlowRunResult& flowResult) {
    QList<ValidationResult> results;
    for (const ipcraft::Diagnostic& diagnostic : flowResult.diagnostics.records) {
        const QString rule = diagnostic.ruleId.trimmed().isEmpty()
            ? QStringLiteral("flow")
            : diagnostic.ruleId;
        results.append(ValidationResult(ValidationSeverity::Error,
                                        prefixedMessage(instance, diagnostic.message),
                                        QString(),
                                        rule));
    }
    return results;
}

} // namespace

ProjectExternalValidationRunner::ProjectExternalValidationRunner()
    : m_frameworkToolSearchPaths(ProjectFlowSupport::defaultFrameworkToolSearchPaths()) {}

ProjectExternalValidationRunner::ProjectExternalValidationRunner(QStringList frameworkToolSearchPaths)
    : m_frameworkToolSearchPaths(std::move(frameworkToolSearchPaths)) {}

QStringList ProjectExternalValidationRunner::frameworkToolSearchPaths() const {
    return m_frameworkToolSearchPaths;
}

void ProjectExternalValidationRunner::setFrameworkToolSearchPaths(QStringList searchPaths) {
    m_frameworkToolSearchPaths = std::move(searchPaths);
}

QList<ValidationResult> ProjectExternalValidationRunner::validate(
    const ProjectExternalValidationRequest& request) const {
    QList<ValidationResult> results;
    if (!request.graph || request.instances.isEmpty()) {
        return results;
    }

    const BlockedScopes blocked = blockedScopesFor(request.staticResults, request.instances);
    if (blocked.blockAll) {
        return results;
    }

    QTemporaryDir validationRoot;
    if (!validationRoot.isValid()) {
        return {ValidationResult(ValidationSeverity::Error,
                                 QStringLiteral("External DRC validation failed: could not create temporary run directory."),
                                 QString(),
                                 QStringLiteral("DRC"))};
    }

    const QString designName =
        ProjectFlowSupport::designNameForProject(request.projectPath, request.designName);
    const QStringList toolSearchPaths = request.frameworkToolSearchPaths.isEmpty()
        ? m_frameworkToolSearchPaths
        : request.frameworkToolSearchPaths;

    for (const ProjectIpInstanceRecord& instance : request.instances) {
        const QString instanceId = instanceIdFor(instance);
        if (blocked.instanceIds.contains(instanceId)) {
            continue;
        }

        const IpCatalogEntry* entry =
            ProjectFlowSupport::findCatalogEntry(request.catalogEntries, instance.ipcoreId);
        if (!entry) {
            continue;
        }

        const ProjectFlowSupport::PackageFlowContext flowContext =
            ProjectFlowSupport::packageFlowContextForEntry(*entry, QStringLiteral("validate"));
        if (!flowContext.ok) {
            const ValidationSeverity severity =
                flowContext.errorKind == ProjectFlowSupport::PackageFlowContext::ErrorKind::MissingFlow
                    ? ValidationSeverity::Warning
                    : ValidationSeverity::Error;
            const QString message =
                flowContext.errorKind == ProjectFlowSupport::PackageFlowContext::ErrorKind::MissingFlow
                    ? QStringLiteral("package '%1' does not declare a validate flow.").arg(entry->id)
                    : flowContext.error;
            results.append(ValidationResult(severity,
                                            prefixedMessage(instance, message),
                                            QString(),
                                            QStringLiteral("DRC")));
            continue;
        }

        const QString runRoot = QDir(validationRoot.path()).filePath(instanceId);
        ipcraft::FlowRunRequest flowRequest;
        flowRequest.projectId = designName;
        flowRequest.instanceId = instanceId;
        flowRequest.flowId = QStringLiteral("validate");
        flowRequest.runId = instanceId;
        flowRequest.runRoot = runRoot;
        flowRequest.outputRoot = runRoot;
        flowRequest.packageRoot = flowContext.packageRoot;
        flowRequest.package = flowContext.package;
        flowRequest.config = ipcraft::ConfigBundle::fromJson(instance.config);
        flowRequest.graphConfig =
            ProjectFlowSupport::projectedGraphConfigForInstance(request.graph,
                                                                request.instances,
                                                                designName,
                                                                instance);
        flowRequest.frameworkToolSearchPaths = toolSearchPaths;

        const ipcraft::FlowRunResult flowResult = ipcraft::FlowRunner::runFlow(flowRequest);
        const QString stdoutText =
            ProjectFlowSupport::readTextFileIfPresent(QDir(runRoot).filePath(QStringLiteral("stdout.log")));
        const QString stderrText =
            ProjectFlowSupport::readTextFileIfPresent(QDir(runRoot).filePath(QStringLiteral("stderr.log")));
        const QList<ValidationResult> captured = outputDiagnostics(instance, stderrText + stdoutText);

        if (!flowResult.ok) {
            if (!captured.isEmpty()) {
                results += captured;
            } else {
                results += flowDiagnostics(instance, flowResult);
            }
        } else {
            for (const ValidationResult& capturedResult : captured) {
                if (capturedResult.severity() == ValidationSeverity::Warning) {
                    results.append(capturedResult);
                }
            }
        }
    }

    return results;
}
```

- [ ] **Step 6: Update xmake targets with new implementation files**

Add `src/app/projectflowsupport.cpp` and `inc/app/projectflowsupport.h` to `projectgenerationrunner_test`.

Add `src/validation/projectexternalvalidationrunner.cpp`, `src/app/projectflowsupport.cpp`, `src/ipcraft/flowrunner.cpp`, `src/ipcraft/artifactmodel.cpp`, `src/ipcraft/emitter.cpp`, `src/ipcraft/configschema.cpp`, `src/ipcraft/value.cpp`, `src/ipcraft/packagespec.cpp`, `src/ipcraft/diagnostics.cpp`, `src/ipcraft/jsonhelpers.cpp`, `src/ipcraft/ipcraftmanifest.cpp`, `src/ipcraft/ipcraftmanifestreader.cpp`, and `src/ipcraft/ipcraftregistry.cpp` to `validation_test` because `ValidationManager` will link the external runner in Task 5.

- [ ] **Step 7: Run new test and verify GREEN**

Run:

```bash
xmake run -P qt projectexternalvalidationrunner_test
```

Expected: `projectexternalvalidationrunner_test passed`.

- [ ] **Step 8: Run generation regression after helper extraction**

Run:

```bash
xmake run -P qt projectgenerationrunner_test
```

Expected: `projectgenerationrunner_test passed`.

- [ ] **Step 9: Commit**

```bash
git add qt/inc/app/projectflowsupport.h qt/src/app/projectflowsupport.cpp qt/inc/validation/projectexternalvalidationrunner.h qt/src/validation/projectexternalvalidationrunner.cpp qt/inc/app/projectgenerationrunner.h qt/src/app/projectgenerationrunner.cpp qt/xmake.lua
git commit -m "feat: run package validate flows through external runner"
```

---

## Task 5: Wire External DRC Into Default Validate

**Files:**
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Rewrite the existing integration test expectation**

In `qt/test/validation_test.cpp`, replace the body of `testValidateRunsBuiltInThenPackageValidate()` with a FlowRunner-based package and a `ValidationManager` call:

```cpp
void testValidateRunsBuiltInThenPackageValidate() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary DRC directory");
    const QString packageRoot = tempDir.filePath(QStringLiteral("scripted-package"));
    require(QDir().mkpath(QDir(packageRoot).filePath(QStringLiteral("tools"))),
            "package tools directory should be created");
    const QString scriptPath = QDir(packageRoot).filePath(QStringLiteral("tools/validate.sh"));
    QFile script(scriptPath);
    require(script.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create validate script");
    script.write("#!/bin/sh\n");
    script.write("echo \"ERROR design: scripted DRC violation\" >&2\n");
    script.write("exit 7\n");
    script.close();
    require(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
            "failed to mark validate script executable");

    const QString ipcoreId = QStringLiteral("finepaper.scripted");
    QJsonObject packageSpec{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), ipcoreId},
        {QStringLiteral("name"), ipcoreId},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), QJsonArray{
            QStringLiteral("ipcraft.emitters"),
            QStringLiteral("ipcraft.flows"),
            QStringLiteral("ipcraft.graph_config")
        }},
        {QStringLiteral("graph_config"), QJsonObject{
            {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
            {QStringLiteral("objects"), QJsonArray{}},
            {QStringLiteral("relationships"), QJsonArray{}},
            {QStringLiteral("properties"), QJsonObject{}},
            {QStringLiteral("native"), QJsonObject{}}
        }},
        {QStringLiteral("emitters"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("graph_config")},
                {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
                {QStringLiteral("path"), QStringLiteral("graph_config.json")}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("parameters")},
                {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                {QStringLiteral("path"), QStringLiteral("parameters.json")}
            }
        }},
        {QStringLiteral("flows"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("validate")},
                {QStringLiteral("label"), QStringLiteral("Validate")},
                {QStringLiteral("scope"), QStringLiteral("instance")},
                {QStringLiteral("steps"), QJsonArray{
                    QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}},
                    QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("exec")},
                        {QStringLiteral("command"), QJsonObject{
                            {QStringLiteral("executable"), QStringLiteral("tools/validate.sh")},
                            {QStringLiteral("args"), QJsonArray{QStringLiteral("{inputs.manifest}")}},
                            {QStringLiteral("cwd"), QStringLiteral("run_dir")},
                            {QStringLiteral("timeout_ms"), 300000},
                            {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
                            {QStringLiteral("capture"), QJsonObject{
                                {QStringLiteral("stdout"), QStringLiteral("stdout.log")},
                                {QStringLiteral("stderr"), QStringLiteral("stderr.log")},
                                {QStringLiteral("max_bytes"), 1048576}
                            }}
                        }}
                    }
                }}
            }
        }}
    };
    writeJsonFile(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")), packageSpec);

    Graph graph;
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("bad_module"),
                                                    ipcoreId,
                                                    QStringLiteral("bad_0"),
                                                    QStringLiteral("MissingTile"))),
            "bad module should add");
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("good_module"),
                                                    ipcoreId,
                                                    QStringLiteral("good_0"))),
            "good module should add");

    IpcraftPackageManifest manifest = packageManifest(ipcoreId);
    manifest.packageRootPath = packageRoot;
    IpCatalogService catalog(QVector<IpcraftPackageManifest>{manifest}, nullptr);

    ProjectStateService stateService;
    stateService.ensureIpInstanceRecord(projectInstanceRecord(ipcoreId, QStringLiteral("bad_0")));
    stateService.ensureIpInstanceRecord(projectInstanceRecord(ipcoreId, QStringLiteral("good_0")));

    LogPanel logPanel;
    ValidationManager manager(&graph, &stateService, &catalog, nullptr, &logPanel);
    manager.runValidation(tempDir.filePath(QStringLiteral("demo.fpproj")), QStringLiteral("demo"));

    auto* logList = logPanel.findChild<QListWidget*>();
    const int builtInIndex = indexOfLogItemContaining(logList, QStringLiteral("MissingTile"));
    const int packageIndex =
        indexOfLogItemContaining(logList, QStringLiteral("good_0"));
    require(builtInIndex >= 0,
            "validation log should include the built-in diagnostic for the invalid instance");
    require(packageIndex >= 0,
            "default validation log should include package validate diagnostics for the valid instance");
    require(indexOfLogItemContaining(logList, QStringLiteral("bad_0: scripted DRC violation")) < 0,
            "package validate should not run for the instance with a blocking built-in error");
}
```

If `writeJsonFile` is not already present in `validation_test.cpp`, add this helper near the other file helpers:

```cpp
void writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to write JSON test file");
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(content) == content.size(), "failed to write JSON test content");
}
```

- [ ] **Step 2: Run validation test and verify RED**

Run:

```bash
xmake run -P qt validation_test
```

Expected: integration test fails because `ValidationManager` still only publishes static results.

- [ ] **Step 3: Update `ValidationManager` API and ownership**

In `qt/inc/validation/validationmanager.h`:

```cpp
class ProjectExternalValidationRunner;
```

Change the slot signature:

```cpp
    void runValidation(const QString& projectPath = QString(),
                       const QString& designName = QString());
```

Add the member:

```cpp
    ProjectExternalValidationRunner* m_projectExternalValidationRunner;
```

In `qt/src/validation/validationmanager.cpp`, include the runner:

```cpp
#include "validation/projectexternalvalidationrunner.h"
```

Initialize and delete it:

```cpp
      m_logPanel(logPanel),
      m_projectValidationRunner(new ProjectValidationRunner()),
      m_projectExternalValidationRunner(new ProjectExternalValidationRunner()) {
```

```cpp
    delete m_projectExternalValidationRunner;
    delete m_projectValidationRunner;
```

- [ ] **Step 4: Run external validation from `ValidationManager`**

Replace `ValidationManager::runValidation()` with:

```cpp
void ValidationManager::runValidation(const QString& projectPath, const QString& designName) {
    qInfo() << "Running validation"
            << "modules" << (m_graph ? m_graph->modules().size() : 0)
            << "connections" << (m_graph ? m_graph->connections().size() : 0)
            << "ipInstances" << (m_projectStateService ? m_projectStateService->ipInstanceRecords().size() : 0);
    const QList<IpCatalogEntry> entries =
        m_catalogService ? m_catalogService->entries() : QList<IpCatalogEntry>{};
    const QVector<ProjectIpInstanceRecord> instances =
        m_projectStateService ? m_projectStateService->ipInstanceRecords() : QVector<ProjectIpInstanceRecord>{};

    QList<ValidationResult> results = m_projectValidationRunner->validate(m_graph, entries, instances);
    if (m_projectExternalValidationRunner) {
        ProjectExternalValidationRequest externalRequest;
        externalRequest.graph = m_graph;
        externalRequest.projectPath = projectPath;
        externalRequest.designName = designName;
        externalRequest.catalogEntries = entries;
        externalRequest.instances = instances;
        externalRequest.staticResults = results;
        results += m_projectExternalValidationRunner->validate(externalRequest);
    }

    if (m_logPanel) {
        m_logPanel->setResults(results);
    }

    int errorCount = 0;
    int warningCount = 0;
    for (const auto& result : results) {
        if (result.severity() == ValidationSeverity::Error) {
            ++errorCount;
            qCritical().noquote() << QString("Validation error [%1] element=%2 message=%3")
                                         .arg(result.ruleName(),
                                              result.elementId().isEmpty() ? QStringLiteral("-") : result.elementId(),
                                              result.message());
        } else {
            ++warningCount;
            qWarning().noquote() << QString("Validation warning [%1] element=%2 message=%3")
                                        .arg(result.ruleName(),
                                             result.elementId().isEmpty() ? QStringLiteral("-") : result.elementId(),
                                             result.message());
        }
    }

    if (results.isEmpty()) {
        qInfo() << "Validation passed with no findings";
    }

    qInfo() << "Validation complete"
            << "results" << results.size()
            << "errors" << errorCount
            << "warnings" << warningCount;
}
```

- [ ] **Step 5: Pass project context from MainWindow**

In `qt/src/app/mainwindow.cpp`, update `MainWindow::runValidation()`:

```cpp
    qInfo() << "Validation requested by user";
    m_validationManager->runValidation(m_currentDocumentPath,
                                       QFileInfo(m_currentDocumentPath).completeBaseName());
```

- [ ] **Step 6: Run integration test and verify GREEN**

Run:

```bash
xmake run -P qt validation_test
```

Expected: `validation_test passed`.

- [ ] **Step 7: Run architecture gate**

Run:

```bash
xmake run -P qt v1architecturegate_test
```

Expected: `v1architecturegate_test passed`. This proves `ProjectValidationRunner` remains side-effect free.

- [ ] **Step 8: Commit**

```bash
git add qt/inc/validation/validationmanager.h qt/src/validation/validationmanager.cpp qt/src/app/mainwindow.cpp qt/test/validation_test.cpp qt/xmake.lua
git commit -m "feat: run ipcore drc from default validation"
```

---

## Task 6: Final Verification

**Files:**
- No production edits unless verification exposes a defect.

- [ ] **Step 1: Run Ruby generator tests**

```bash
ruby ipcraft_generator/test/ipcraft_generator_test.rb
```

Expected: all tests pass.

- [ ] **Step 2: Run spec-generator tests**

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: all tests pass.

- [ ] **Step 3: Run focused Qt tests**

```bash
xmake run -P qt projectexternalvalidationrunner_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt validation_test
xmake run -P qt ipcraft_flowrunner_test
xmake run -P qt v1architecturegate_test
```

Expected: every command prints its `... passed` line.

- [ ] **Step 4: Run build**

```bash
xmake build -P qt validation_test projectexternalvalidationrunner_test projectgenerationrunner_test v1architecturegate_test
```

Expected: build succeeds with exit code 0.

- [ ] **Step 5: Inspect diff for scope**

```bash
git status --short
git diff --stat HEAD
```

Expected: only files named in this plan are modified, plus generated manifest updates from Task 2.

- [ ] **Step 6: Commit any verification fixes**

If verification required fixes:

```bash
git add <changed-files>
git commit -m "fix: stabilize automatic ipcore drc validation"
```

If no fixes were needed, do not create an empty commit.

---

## Self-Review

- Spec coverage: the plan covers validate-only CLI mode, package validate flows, external runner execution, ValidationManager orchestration, diagnostic visibility, and the side-effect-free architecture gate.
- Red-flag scan: no incomplete-marker terms or unspecified test commands remain.
- Type consistency: `ProjectExternalValidationRequest`, `ProjectExternalValidationRunner`, and `ProjectFlowSupport` names are consistent across tasks. The manager keeps `ProjectValidationRunner` static and uses the new external runner only from the orchestration boundary.
