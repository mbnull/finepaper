# Hard-Cut Architecture Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the hard-cut boundaries enforceable for P0 public contracts, P3 registry-only plugin context, P4 fail-closed flow command parsing, and P5 architecture gates, while preparing P1/P2 without dual-writing Graph and ProjectDesign.

**Architecture:** `ipcraft.project.v1` becomes only the flat `ProjectDesign` root on write and in examples. Runtime plugins receive registries only and resolve business services from `ServiceRegistry`. FlowRunner parses command JSON into a typed command before any process start. P1/P2 is guarded by structural gates and an explicit design-command migration backlog; do not introduce Graph/design dual-write.

**Tech Stack:** Qt 6 C++23, xmake test targets, `QJsonObject`/`QJsonDocument`, existing `ProjectDesignSerializer`, `ProjectDocumentV1`, `PackageSpecReader`, `PackageCoverageReport`, `FlowRunner`, and architecture scan tests.

---

## Scope Decision

P0, P3, and P4 can be made green in this implementation pass.

P1/P2 has an unavoidable conflict with the current runtime: `NodeEditorWidget` directly creates graph commands, and those commands hold `Graph*` plus `EditorMutationTarget*`. Adding hard gates that forbid this will fail until the old editing path is either removed from runtime or fully migrated to design-level commands. This plan therefore makes P1/P2 a two-stage gate:

- Stage A in this pass: add precise red/blocked gates and design-patch scaffolding tests that document the missing ProjectPatch operations.
- Stage B later: perform the full design-command migration or intentionally disable old durable edit actions until the design path exists.

Do not make P1/P2 green by writing both Graph and ProjectDesign. That is explicitly forbidden.

## File Map

- `schemas/ipcraft.project.v1.schema.json`: already defines flat `ProjectDesign`; keep it authoritative.
- `schemas/ipcraft.package.v1.schema.json`: relax extension declarations so unknown IDs are schema-valid.
- `examples/contracts/*/project.fpproj`: convert wrapper root fixtures to flat `ProjectDesign` root.
- `qt/src/project/projectwriter.cpp`: emit flat `ProjectDesign` through `ProjectDesignSerializer::fromDocument` and `ProjectDocumentV1::writeObject`.
- `qt/src/project/projectreader.cpp`: accept flat `ProjectDesign` as current input; keep wrapper root only as read-only legacy/migration input.
- `qt/test/support/jsonschemavalidator.{h,cpp}`: self-contained test-only schema validator for the public CI gate.
- `qt/test/ipcraft_contract_examples_test.cpp`: validate examples and repo package manifests against schemas.
- `qt/test/ipcraft_project_model_test.cpp` and `qt/test/projectdocument_test.cpp`: update writer expectations from wrapper root to flat root.
- `qt/inc/app/appcontext.h`: registry-only context.
- `qt/src/app/mainwindow.cpp`: register `WorkbenchService` in `ServiceRegistry`; stop filling direct AppContext pointers.
- `qt/src/app/pluginhost.cpp`: require registries and resolve workbench through `ServiceRegistry`.
- `qt/src/project/projectplugin.cpp`, `qt/src/package/packageplugin.cpp`, `qt/src/app/toolpipelineplugin.cpp`: remove fallback service pointers.
- `qt/test/projectplugin_test.cpp`, `qt/test/packageplugin_test.cpp`, `qt/test/toolpipelineplugin_test.cpp`, `qt/test/pluginhost_foundation_test.cpp`, `qt/test/staticplugincatalog_test.cpp`, `qt/test/packagecoverage_test.cpp`, `qt/test/nocplugin_test.cpp`, `qt/test/plugin_registry_test.cpp`: update context setup and missing-service expectations.
- `qt/src/ipcraft/flowrunner.cpp`: strict command parser before `QProcess::start()`.
- `qt/test/ipcraft_flowrunner_test.cpp`: marker-file fail-closed matrix.
- `qt/test/ipcraft_architecture_foundation_scan_test.cpp` or `qt/test/plugin_hard_cutover_scan_test.cpp`: structural gates for P1/P2/P3/P5.
- `qt/xmake.lua`: include any new test support sources.

---

### Task 1: Add Test-Only JSON Schema Validator

**Files:**
- Create: `qt/test/support/jsonschemavalidator.h`
- Create: `qt/test/support/jsonschemavalidator.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing contract test include**

In `qt/test/ipcraft_contract_examples_test.cpp`, add:

```cpp
#include "support/jsonschemavalidator.h"
```

This will fail to compile until the validator exists and is added to the target.

- [ ] **Step 2: Run the compile to verify RED**

Run:

```bash
xmake build -P qt ipcraft_contract_examples_test
```

Expected: FAIL with a missing `support/jsonschemavalidator.h` or missing symbol error.

- [ ] **Step 3: Add `qt/test/support/jsonschemavalidator.h`**

```cpp
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class JsonSchemaValidator {
public:
    explicit JsonSchemaValidator(QJsonObject schema);

    static JsonSchemaValidator fromFile(const QString& path);
    bool validate(const QJsonValue& value, QString* error = nullptr) const;

private:
    bool validateAgainst(const QJsonValue& value,
                         const QJsonValue& schema,
                         const QString& path,
                         QString* error) const;
    QJsonValue resolveRef(const QString& ref) const;

    QJsonObject m_schema;
};
```

- [ ] **Step 4: Add `qt/test/support/jsonschemavalidator.cpp`**

Implement support for the schema keywords already used by repo schemas:

```cpp
#include "support/jsonschemavalidator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <stdexcept>

namespace {

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

QString typeName(const QJsonValue& value) {
    if (value.isObject()) return QStringLiteral("object");
    if (value.isArray()) return QStringLiteral("array");
    if (value.isString()) return QStringLiteral("string");
    if (value.isBool()) return QStringLiteral("boolean");
    if (value.isDouble()) return QStringLiteral("number");
    if (value.isNull()) return QStringLiteral("null");
    return QStringLiteral("undefined");
}

bool matchesType(const QJsonValue& value, const QString& type) {
    if (type == QStringLiteral("object")) return value.isObject();
    if (type == QStringLiteral("array")) return value.isArray();
    if (type == QStringLiteral("string")) return value.isString();
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("number")) return value.isDouble();
    if (type == QStringLiteral("integer")) {
        if (!value.isDouble()) return false;
        const double number = value.toDouble();
        return number == static_cast<double>(static_cast<qint64>(number));
    }
    if (type == QStringLiteral("null")) return value.isNull();
    return true;
}

QString childPath(const QString& base, const QString& key) {
    return base == QStringLiteral("$")
        ? QStringLiteral("$.") + key
        : base + QStringLiteral(".") + key;
}

QString indexPath(const QString& base, qsizetype index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

} // namespace

JsonSchemaValidator::JsonSchemaValidator(QJsonObject schema)
    : m_schema(std::move(schema)) {}

JsonSchemaValidator JsonSchemaValidator::fromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("cannot read schema: %1").arg(path).toStdString());
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error(QStringLiteral("invalid schema JSON: %1").arg(path).toStdString());
    }
    return JsonSchemaValidator(document.object());
}

bool JsonSchemaValidator::validate(const QJsonValue& value, QString* error) const {
    return validateAgainst(value, m_schema, QStringLiteral("$"), error);
}
```

Continue the same file with `validateAgainst(...)` and `resolveRef(...)` implementing:

- `$ref` with local `#/$defs/name`
- `const`
- `enum`
- `type`
- `required`
- `properties`
- `additionalProperties: false`
- `items`
- `minItems`
- `minLength`
- `minimum`
- `allOf`
- `oneOf`
- `not`
- `if` + `then`
- `contains`

Use recursive calls and return the first precise path-bearing error, for example `$.components[0].packageRef: expected string`.

- [ ] **Step 5: Add support source to `ipcraft_contract_examples_test` target**

In `qt/xmake.lua`, add to target `ipcraft_contract_examples_test`:

```lua
    add_files("test/support/jsonschemavalidator.cpp")
    add_files("test/support/jsonschemavalidator.h")
```

- [ ] **Step 6: Run GREEN for validator compile**

Run:

```bash
xmake build -P qt ipcraft_contract_examples_test
```

Expected: PASS build.

- [ ] **Step 7: Commit**

```bash
git add qt/test/support/jsonschemavalidator.h qt/test/support/jsonschemavalidator.cpp qt/xmake.lua qt/test/ipcraft_contract_examples_test.cpp
git commit -m "test: add public schema validator support"
```

---

### Task 2: Add P0 Public Schema Gates

**Files:**
- Modify: `qt/test/ipcraft_contract_examples_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing tests for examples and ipcores**

Add helpers to `qt/test/ipcraft_contract_examples_test.cpp`:

```cpp
QString repositoryRoot() {
    QDir dir(QFileInfo(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")))
                 .absoluteDir());
    require(dir.cdUp(), "repository root should be reachable from schemas directory");
    return dir.absolutePath();
}

QString relativeToRepository(const QString& absolutePath) {
    return QDir(repositoryRoot()).relativeFilePath(absolutePath);
}

QVector<QFileInfo> filesMatching(const QString& relativeRoot,
                                 const QString& fileName) {
    QVector<QFileInfo> files;
    const QFileInfo root(repositoryPath(relativeRoot));
    require(root.isDir(), "scan root should exist");
    QDirIterator iterator(root.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().fileName() == fileName) {
            files.append(iterator.fileInfo());
        }
    }
    return files;
}

void requireJsonMatchesSchema(const QString& jsonPath,
                              const JsonSchemaValidator& schema) {
    QString error;
    const QJsonObject object = readJsonObject(jsonPath);
    require(schema.validate(object, &error),
            QStringLiteral("%1 must match schema: %2")
                .arg(relativeToRepository(jsonPath), error)
                .toUtf8()
                .constData());
}
```

Add tests:

```cpp
void testAllContractProjectsMatchPublicProjectSchema() {
    const JsonSchemaValidator projectSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")));
    const QVector<QFileInfo> projects =
        filesMatching(QStringLiteral("examples/contracts"), QStringLiteral("project.fpproj"));
    require(!projects.isEmpty(), "contract projects should exist");
    for (const QFileInfo& project : projects) {
        requireJsonMatchesSchema(project.absoluteFilePath(), projectSchema);
    }
}

void testAllRepositoryRuntimePackagesMatchPublicPackageSchema() {
    const JsonSchemaValidator packageSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.package.v1.schema.json")));
    const QVector<QFileInfo> packages =
        filesMatching(QStringLiteral("ipcores"), QStringLiteral("ipcraft.json"));
    require(!packages.isEmpty(), "runtime package manifests should exist");
    for (const QFileInfo& package : packages) {
        requireJsonMatchesSchema(package.absoluteFilePath(), packageSchema);
    }
}
```

Call both tests from `main()`.

- [ ] **Step 2: Run RED**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
```

Expected: FAIL because current `examples/contracts/*/project.fpproj` contain wrapper fields such as `project`, `instances`, `composition`, `layout`, `migration`, and `native`.

- [ ] **Step 3: Keep the failure text precise**

If the failure does not name the offending path and schema path, adjust `JsonSchemaValidator` error strings before implementation work.

- [ ] **Step 4: Commit tests**

```bash
git add qt/test/ipcraft_contract_examples_test.cpp qt/test/support/jsonschemavalidator.cpp qt/test/support/jsonschemavalidator.h qt/xmake.lua
git commit -m "test: gate public project and package schemas"
```

---

### Task 3: Make ProjectWriter Emit Flat ProjectDesign

**Files:**
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/xmake.lua`
- Modify tests that assert wrapper output:
  - `qt/test/ipcraft_project_model_test.cpp`
  - `qt/test/projectdocument_test.cpp`
  - `qt/test/projectservice_test.cpp`
  - `qt/test/ipcraft_migration_test.cpp`

- [ ] **Step 1: Write failing writer schema test**

In `qt/test/ipcraft_contract_examples_test.cpp`, add:

```cpp
#include "project/projectwriter.h"
#include "project/projectreader.h"
```

Add:

```cpp
void testProjectWriterOutputMatchesPublicProjectSchema() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("writer_flat_project");
    document.projectName = QStringLiteral("Writer Flat Project");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.example.writer"), QStringLiteral("1.0.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("writer0");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.example.writer"), QStringLiteral("1.0.0")};
    instance.native.insert(QStringLiteral("componentType"), QStringLiteral("WriterComponent"));
    instance.config.insert(QStringLiteral("parameters"), QJsonObject{{QStringLiteral("width"), 32}});
    document.instances.append(instance);

    const QJsonObject written = ProjectWriter::toJsonObject(document);
    require(!written.contains(QStringLiteral("project")), "writer must not emit wrapper project root");
    require(!written.contains(QStringLiteral("instances")), "writer must not emit wrapper instances root");
    require(written.contains(QStringLiteral("components")), "writer must emit flat components");

    const JsonSchemaValidator projectSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")));
    QString error;
    require(projectSchema.validate(written, &error),
            QStringLiteral("ProjectWriter output must match project schema: %1").arg(error).toUtf8().constData());
}
```

Call it from `main()`.

- [ ] **Step 2: Run RED**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
```

Expected: FAIL because `ProjectWriter::toJsonObject()` currently emits wrapper fields.

- [ ] **Step 3: Change writer to flat output**

In `qt/src/project/projectwriter.cpp`, include:

```cpp
#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/core/project_design.h"
#include "project/projectdesignserializer.h"
```

Replace `ProjectWriter::toJsonObject` with:

```cpp
QJsonObject ProjectWriter::toJsonObject(const ProjectDocument& document) {
    const ipcraft::core::ProjectDesign design =
        ProjectDesignSerializer::fromDocument(document);
    return ipcraft::core::ProjectDocumentV1::writeObject(design);
}
```

Change `ProjectWriter::writeFile` to validate the flat design before writing:

```cpp
ProjectWriteResult ProjectWriter::writeFile(const QString& path, const ProjectDocument& document) {
    const ProjectWriteResult validation = validateDocument(document);
    if (!validation.success) {
        return validation;
    }

    const ipcraft::core::ProjectDesign design =
        ProjectDesignSerializer::fromDocument(document);
    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(design);
    if (!issues.isEmpty()) {
        return writeFailure(QStringLiteral("Project design is invalid: %1 at %2")
                                .arg(issues.first().code, issues.first().path));
    }

    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return writeFailure(QStringLiteral("Could not create project directory"));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return writeFailure(QStringLiteral("Could not open project file for writing"));
    }

    const QByteArray bytes =
        ipcraft::toDeterministicJson(ProjectWriter::toJsonObject(document));
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return writeFailure(QStringLiteral("Could not write project file"));
    }
    return {true, {}};
}
```

- [ ] **Step 4: Add `projectdesignserializer` dependencies to contract target**

In `qt/xmake.lua`, add these files to `ipcraft_contract_examples_test`:

```lua
    add_files("src/project/projectwriter.cpp")
    add_files("src/project/projectdesignserializer.cpp")
    add_files("src/ipcraft/core/project_document_v1.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
```

- [ ] **Step 5: Make ProjectReader accept flat current input**

In `qt/src/project/projectreader.cpp`, before the wrapper-root parsing branch, add:

```cpp
    if (!root.contains(QStringLiteral("project"))) {
        const ipcraft::core::ProjectDocumentReadResult flatResult =
            ipcraft::core::ProjectDocumentV1::readObject(root);
        if (!flatResult.success) {
            ProjectReadResult result;
            result.success = false;
            result.error = QStringLiteral("Project design is invalid.");
            for (const ipcraft::core::ValidationIssue& issue : flatResult.issues) {
                result.diagnostics.append(IpcraftDiagnostic{
                    QStringLiteral("error"),
                    QStringLiteral("project"),
                    issue.code,
                    QStringLiteral("project"),
                    issue.message,
                    {},
                    issue.path,
                    {},
                    {}
                });
            }
            return result;
        }
        ProjectReadResult result;
        result.success = true;
        result.document = ProjectDesignSerializer::toDocument(flatResult.project);
        return result;
    }
```

Keep the existing wrapper branch as legacy read-only compatibility. Add a source comment immediately above the wrapper branch:

```cpp
    // Legacy wrapper compatibility: accepted for read/migration only.
    // ProjectWriter never emits this shape for ipcraft.project.v1.
```

- [ ] **Step 6: Run targeted tests and update wrapper expectations**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
xmake run -P qt ipcraft_project_model_test
xmake run -P qt projectdocument_test
xmake run -P qt projectservice_test
xmake run -P qt ipcraft_migration_test
```

Expected before test edits: failures where tests expect `project`, `instances`, `composition`, or `layout` at root.

Update those tests to assert flat fields:

```cpp
require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1,
        "writer should emit ipcraft.project.v1");
require(written.value(QStringLiteral("id")).toString() == document.projectId,
        "writer should emit flat project id");
require(written.value(QStringLiteral("components")).isArray(),
        "writer should emit flat components");
require(!written.contains(QStringLiteral("project")),
        "writer should not emit legacy wrapper root");
```

- [ ] **Step 7: Verify GREEN**

Run the same five tests. Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add qt/src/project/projectwriter.cpp qt/src/project/projectreader.cpp qt/xmake.lua qt/test/ipcraft_contract_examples_test.cpp qt/test/ipcraft_project_model_test.cpp qt/test/projectdocument_test.cpp qt/test/projectservice_test.cpp qt/test/ipcraft_migration_test.cpp
git commit -m "refactor: emit flat project design contract"
```

---

### Task 4: Convert Contract Project Fixtures to Flat ProjectDesign

**Files:**
- Modify: `examples/contracts/*/project.fpproj`

- [ ] **Step 1: Run RED schema gate**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
```

Expected: FAIL listing each wrapper fixture.

- [ ] **Step 2: Convert each fixture**

For each `examples/contracts/*/project.fpproj`, map:

```json
{
  "schema": "ipcraft.project.v1",
  "project": { "id": "p", "name": "P" },
  "instances": [
    {
      "id": "i0",
      "package": { "id": "vendor.pkg", "version": "1.0.0" },
      "config": { "parameters": { "width": 64 } },
      "native": { "componentType": "ExampleComponent" }
    }
  ]
}
```

to:

```json
{
  "schema": "ipcraft.project.v1",
  "id": "p",
  "name": "P",
  "packages": [
    { "id": "vendor.pkg", "version": "1.0.0" }
  ],
  "components": [
    {
      "id": "i0",
      "type": "ExampleComponent",
      "packageRef": "vendor.pkg@1.0.0",
      "config": { "width": 64 }
    }
  ],
  "interfaces": [],
  "connections": [],
  "topologies": [],
  "views": [],
  "diagnostics": [],
  "artifacts": [],
  "extensions": [],
  "metadata": {}
}
```

Use package-specific component type names already present in `native.componentType`, package views, or package module descriptors. If the old fixture did not declare a component type, use the package's primary module type from `package/ipcraft.json`.

- [ ] **Step 3: Preserve graph_config and layout correctly**

If an instance has `graph_config`, put it under `components[].extensionData.graph_config`.

If old `layout.views[].canvas.nodes` carried `x/y`, do not put `x/y` under `components[].config`. Store it under a flat project `views[]` entry:

```json
{
  "id": "graph",
  "schema": "ipcraft.view.v1",
  "kind": "canvas",
  "targetRef": "project",
  "providerRef": "finepaper.editor",
  "layout": {
    "canvas": {
      "nodes": {
        "tile0": { "x": 0, "y": 0 }
      },
      "connections": {}
    }
  }
}
```

- [ ] **Step 4: Run GREEN**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
```

Expected: PASS for examples schema gate, except package-negative examples may still fail package parsing only where the test explicitly expects a negative package diagnostic.

- [ ] **Step 5: Commit**

```bash
git add examples/contracts/*/project.fpproj
git commit -m "test: convert contract projects to flat design schema"
```

---

### Task 5: Relax Package Schema for Unknown Extension IDs

**Files:**
- Modify: `schemas/ipcraft.package.v1.schema.json`
- Modify: `qt/test/ipcraft_contract_examples_test.cpp`
- Existing behavioral tests: `qt/test/packageservice_test.cpp`, `qt/test/packagecoverage_test.cpp`

- [ ] **Step 1: Add schema-valid unknown extension test**

In `qt/test/ipcraft_contract_examples_test.cpp`, add:

```cpp
void testUnknownOptionalPackageExtensionIsSchemaValid() {
    const JsonSchemaValidator packageSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.package.v1.schema.json")));
    const QJsonObject package{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), QStringLiteral("vendor.experimental.schema")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("name"), QStringLiteral("Experimental Schema")},
        {QStringLiteral("extensions"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("vendor.experimental.v1")},
                {QStringLiteral("required"), false},
                {QStringLiteral("version"), QStringLiteral("0.1.0")}
            }
        }}
    };

    QString error;
    require(packageSchema.validate(package, &error),
            QStringLiteral("unknown optional extension should be schema valid: %1")
                .arg(error)
                .toUtf8()
                .constData());
}
```

Call it from `main()`.

- [ ] **Step 2: Run RED**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
```

Expected: FAIL because `known_extension_id` currently rejects `vendor.experimental.v1`.

- [ ] **Step 3: Relax schema extension declaration**

In `schemas/ipcraft.package.v1.schema.json`, replace:

```json
"id": { "$ref": "#/$defs/known_extension_id" }
```

with:

```json
"id": { "$ref": "#/$defs/extension_id" }
```

and replace `known_extension_id` string use in `extension_decl` with `extension_id`:

```json
"extension_id": {
  "type": "string",
  "minLength": 1
}
```

Keep all `has_ipcraft.*` definitions for known section gating. Unknown extension IDs become schema-valid declarations; they do not satisfy known extension requirements unless the exact known ID is present.

- [ ] **Step 4: Verify behavioral coverage still decides support**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
xmake run -P qt packageservice_test
xmake run -P qt packagecoverage_test
```

Expected:

- unknown optional extension schema valid
- existing `packageservice_test` optional unknown coverage is `Unsupported` and non-blocking
- existing required unknown coverage is `Blocking` with `package.capability_missing_handler`

- [ ] **Step 5: Commit**

```bash
git add schemas/ipcraft.package.v1.schema.json qt/test/ipcraft_contract_examples_test.cpp
git commit -m "schema: allow unknown extension declarations"
```

---

### Task 6: Make AppContext Registry-Only

**Files:**
- Modify: `qt/inc/app/appcontext.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/app/pluginhost.cpp`
- Modify: `qt/src/project/projectplugin.cpp`
- Modify: `qt/src/package/packageplugin.cpp`
- Modify: `qt/src/app/toolpipelineplugin.cpp`
- Modify tests listed in the file map.

- [ ] **Step 1: Add failing scan gate**

In `qt/test/plugin_hard_cutover_scan_test.cpp`, add:

```cpp
void testAppContextIsRegistryOnly() {
    const QString header = readText(QStringLiteral("qt/inc/app/appcontext.h"));
    const QStringList forbidden{
        QStringLiteral("WorkbenchService* workbench"),
        QStringLiteral("ProjectService* projectService"),
        QStringLiteral("PackageService* packageService"),
        QStringLiteral("ToolPipelineService* toolPipelineService")
    };
    for (const QString& token : forbidden) {
        requireNotContains(header, token, QStringLiteral("AppContext"));
    }
}

void testPluginsDoNotFallbackToDirectContextServices() {
    const QStringList pluginSources{
        QStringLiteral("qt/src/project/projectplugin.cpp"),
        QStringLiteral("qt/src/package/packageplugin.cpp"),
        QStringLiteral("qt/src/app/toolpipelineplugin.cpp"),
        QStringLiteral("qt/src/noc/nocplugin.cpp"),
        QStringLiteral("qt/src/app/pluginhost.cpp")
    };
    const QStringList forbidden{
        QStringLiteral("context.workbench"),
        QStringLiteral("context.projectService"),
        QStringLiteral("context.packageService"),
        QStringLiteral("context.toolPipelineService")
    };
    for (const QString& path : pluginSources) {
        const QString source = readText(path);
        for (const QString& token : forbidden) {
            requireNotContains(source, token, path);
        }
    }
}
```

Call both from `main()`.

- [ ] **Step 2: Run RED**

Run:

```bash
xmake run -P qt plugin_hard_cutover_scan_test
```

Expected: FAIL with direct AppContext pointers and fallback usages.

- [ ] **Step 3: Remove direct pointers from AppContext**

Change `qt/inc/app/appcontext.h` to:

```cpp
#pragma once

class CapabilityRegistry;
class ExtensionPointRegistry;
class PluginInteractionRegistry;
class ServiceRegistry;

struct AppContext {
    ServiceRegistry* services = nullptr;
    ExtensionPointRegistry* extensionPoints = nullptr;
    CapabilityRegistry* capabilities = nullptr;
    PluginInteractionRegistry* interactions = nullptr;
};
```

- [ ] **Step 4: Register WorkbenchService through ServiceRegistry**

In `qt/src/app/mainwindow.cpp`, after creating `m_serviceRegistry`, register:

```cpp
    m_serviceRegistry->registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                       m_workbenchService.get());
```

Remove:

```cpp
    context.workbench = m_workbenchService.get();
    context.projectService = m_projectService.get();
    context.packageService = m_packageService.get();
    context.toolPipelineService = m_toolPipelineService.get();
```

- [ ] **Step 5: Resolve required services from registry**

In `qt/src/project/projectplugin.cpp`, replace `hasProjectService` with:

```cpp
ProjectService* requiredProjectService(AppContext& context) {
    return context.services
        ? context.services->service<ProjectService>(ServiceKey::fromLiteral("finepaper.project"))
        : nullptr;
}
```

Activation:

```cpp
    if (!requiredProjectService(context)) {
        throw std::runtime_error("ProjectService is required before activating ProjectPlugin.");
    }
```

In `qt/src/package/packageplugin.cpp`, replace `packageService` with:

```cpp
PackageService* requiredPackageService(AppContext& context) {
    return context.services
        ? context.services->service<PackageService>(ServiceKey::fromLiteral("finepaper.package"))
        : nullptr;
}
```

Use `requiredPackageService(context)` and throw the existing error if null.

In `qt/src/app/toolpipelineplugin.cpp`, replace `hasToolPipelineService` with a registry-only lookup for `finepaper.tool-pipeline`.

- [ ] **Step 6: Make PluginHost require workbench via registry**

In `qt/src/app/pluginhost.cpp`, replace:

```cpp
    if (!m_context.workbench) {
        result.success = false;
        result.error = QStringLiteral("WorkbenchService is required before activating plugins.");
        return result;
    }
```

with:

```cpp
    if (!m_context.services->service<WorkbenchService>(
            ServiceKey::fromLiteral("finepaper.workbench"))) {
        result.success = false;
        result.error = QStringLiteral("WorkbenchService is required before activating plugins.");
        return result;
    }
```

Add includes:

```cpp
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"
```

- [ ] **Step 7: Update tests to use ServiceRegistry**

Where tests currently set direct fields, replace:

```cpp
AppContext context;
context.workbench = &workbench;
context.projectService = &projectService;
```

with:

```cpp
ServiceRegistry services;
services.registerService(ServiceKey::fromLiteral("finepaper.workbench"), &workbench);
services.registerService(ServiceKey::fromLiteral("finepaper.project"), &projectService);

AppContext context;
context.services = &services;
```

Apply equivalent keys:

- `finepaper.package`
- `finepaper.tool-pipeline`
- `finepaper.workbench`

- [ ] **Step 8: Add missing-service activation tests**

In `qt/test/projectplugin_test.cpp`, keep the missing service test:

```cpp
void testProjectPluginRequiresRegistryProjectService() {
    ServiceRegistry services;
    WorkbenchService workbench;
    services.registerService(ServiceKey::fromLiteral("finepaper.workbench"), &workbench);
    AppContext context;
    context.services = &services;

    PluginHost host(context);
    require(host.registerPlugin(createProjectPlugin()), "project plugin should register");
    const PluginActivationResult result = host.activatePlugins();
    require(!result.success, "missing ProjectService should fail activation");
    require(result.error.contains(QStringLiteral("ProjectService is required")),
            "activation error should name ProjectService");
}
```

Add equivalent tests for PackagePlugin and ToolPipelinePlugin.

- [ ] **Step 9: Verify GREEN**

Run:

```bash
xmake run -P qt plugin_hard_cutover_scan_test
xmake run -P qt pluginhost_foundation_test
xmake run -P qt projectplugin_test
xmake run -P qt packageplugin_test
xmake run -P qt toolpipelineplugin_test
xmake run -P qt staticplugincatalog_test
xmake run -P qt packagecoverage_test
xmake run -P qt nocplugin_test
xmake run -P qt plugin_registry_test
```

Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add qt/inc/app/appcontext.h qt/src/app/mainwindow.cpp qt/src/app/pluginhost.cpp qt/src/project/projectplugin.cpp qt/src/package/packageplugin.cpp qt/src/app/toolpipelineplugin.cpp qt/test
git commit -m "refactor: make app context registry only"
```

---

### Task 7: Strict FlowRunner Command Parsing

**Files:**
- Modify: `qt/src/ipcraft/flowrunner.cpp`
- Modify: `qt/test/ipcraft_flowrunner_test.cpp`

- [ ] **Step 1: Add marker-file malformed command test helper**

In `qt/test/ipcraft_flowrunner_test.cpp`, add:

```cpp
void requireMalformedCommandFailsBeforeStart(const QJsonValue& commandValue,
                                             const QString& markerName) {
    QTemporaryDir runRoot;
    QTemporaryDir packageRoot;
    require(runRoot.isValid(), "run root should be valid");
    require(packageRoot.isValid(), "package root should be valid");

    const QString markerPath = QDir(runRoot.path()).filePath(markerName);
    writeExecutable(QDir(packageRoot.path()).filePath(QStringLiteral("tools/mark.sh")),
                    "#!/bin/sh\n"
                    "printf started > \"$1\"\n");

    QJsonObject step{{QStringLiteral("kind"), QStringLiteral("exec")}};
    step.insert(QStringLiteral("command"), commandValue);
    const QJsonObject flow{
        {QStringLiteral("id"), QStringLiteral("generate")},
        {QStringLiteral("steps"), QJsonArray{step}}
    };

    const ipcraft::FlowRunResult result =
        ipcraft::FlowRunner::runFlow(requestFor(runRoot, packageRoot, flow));

    require(!result.ok, "malformed command should fail flow run");
    require(hasRule(result.diagnostics, QStringLiteral("flow.command_policy_violation")),
            "malformed command should emit flow.command_policy_violation");
    require(!QFileInfo::exists(markerPath),
            "malformed command must be rejected before process starts");
}
```

- [ ] **Step 2: Add malformed field matrix**

Add:

```cpp
QJsonObject markerCommand(const QString& markerPath) {
    return commandFor(QStringLiteral("tools/mark.sh"), QJsonArray{markerPath});
}

void testMalformedExecCommandFieldsFailBeforeProcessStart() {
    requireMalformedCommandFailsBeforeStart(QStringLiteral("not-object"),
                                            QStringLiteral("malformed-command"));

    QTemporaryDir runRoot;
    require(runRoot.isValid(), "run root for command templates should be valid");
    const QString marker = QDir(runRoot.path()).filePath(QStringLiteral("unused"));

    QJsonObject command = markerCommand(marker);
    command.insert(QStringLiteral("executable"), 7);
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-executable"));

    command = markerCommand(marker);
    command.remove(QStringLiteral("executable"));
    command.insert(QStringLiteral("framework_tool"), 7);
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-framework-tool"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("cwd"), 7);
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-cwd"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("args"), QStringLiteral("not-array"));
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-args"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("args"), QJsonArray{QStringLiteral("ok"), 42});
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-args-item"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("env"), QStringLiteral("not-object"));
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-env"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("env"),
                   QJsonObject{{QStringLiteral("allow"), QStringLiteral("not-array")}});
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-env-allow"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("env"),
                   QJsonObject{{QStringLiteral("allow"), QJsonArray{QStringLiteral("PATH"), 9}}});
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-env-allow-item"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("capture"), QStringLiteral("not-object"));
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-capture"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("capture"),
                   QJsonObject{{QStringLiteral("stdout"), 9},
                               {QStringLiteral("stderr"), QStringLiteral("stderr.log")}});
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-capture-stdout"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("capture"),
                   QJsonObject{{QStringLiteral("stdout"), QStringLiteral("stdout.log")},
                               {QStringLiteral("stderr"), 9}});
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-capture-stderr"));

    command = markerCommand(marker);
    command.insert(QStringLiteral("timeout_ms"), QStringLiteral("60000"));
    requireMalformedCommandFailsBeforeStart(command, QStringLiteral("bad-timeout"));
}
```

Call `testMalformedExecCommandFieldsFailBeforeProcessStart()` from `main()`.

- [ ] **Step 3: Run RED**

Run:

```bash
xmake run -P qt ipcraft_flowrunner_test
```

Expected: FAIL because some malformed fields are silently converted to empty/default values and process can start.

- [ ] **Step 4: Introduce typed parsed command**

In `qt/src/ipcraft/flowrunner.cpp`, add:

```cpp
struct ParsedExecCommand {
    QString executable;
    QString frameworkTool;
    QString cwd;
    QStringList args;
    QSet<QString> envAllow;
    CapturePolicy capture;
    std::chrono::milliseconds deadline = kDefaultCommandDeadline;
    bool hasNative = false;
};
```

Add strict helpers:

```cpp
bool optionalStringStrict(const QJsonObject& object,
                          const QString& key,
                          const QString& path,
                          ipcraft::DiagnosticStore& diagnostics,
                          QString* output) {
    if (!object.contains(key)) {
        output->clear();
        return true;
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow command field must be a string."),
                          path);
        return false;
    }
    *output = value.toString().trimmed();
    return true;
}
```

Add strict array/object helpers for `args`, `env`, `env.allow`, and `capture`.

- [ ] **Step 5: Parse before resolving executable**

Add:

```cpp
std::optional<ParsedExecCommand> parseExecCommandStrict(const QJsonValue& commandValue,
                                                        const QString& stepPath,
                                                        ipcraft::DiagnosticStore& diagnostics) {
    if (!commandValue.isObject()) {
        addFlowDiagnostic(diagnostics,
                          QStringLiteral("flow.command_policy_violation"),
                          QStringLiteral("Flow exec command must be an object."),
                          childPath(stepPath, QStringLiteral("command")));
        return std::nullopt;
    }

    const QJsonObject command = commandValue.toObject();
    ParsedExecCommand parsed;
    parsed.hasNative = command.contains(QStringLiteral("native"));

    if (!optionalStringStrict(command, QStringLiteral("executable"),
                              childPath(stepPath, QStringLiteral("command.executable")),
                              diagnostics, &parsed.executable)) {
        return std::nullopt;
    }
    if (!optionalStringStrict(command, QStringLiteral("framework_tool"),
                              childPath(stepPath, QStringLiteral("command.framework_tool")),
                              diagnostics, &parsed.frameworkTool)) {
        return std::nullopt;
    }
    if (!optionalStringStrict(command, QStringLiteral("cwd"),
                              childPath(stepPath, QStringLiteral("command.cwd")),
                              diagnostics, &parsed.cwd)) {
        return std::nullopt;
    }

    if (!parseStringArrayStrict(command.value(QStringLiteral("args")),
                                command.contains(QStringLiteral("args")),
                                childPath(stepPath, QStringLiteral("command.args")),
                                diagnostics,
                                &parsed.args)) {
        return std::nullopt;
    }

    if (!parseEnvironmentStrict(command.value(QStringLiteral("env")),
                                command.contains(QStringLiteral("env")),
                                stepPath,
                                diagnostics,
                                &parsed.envAllow)) {
        return std::nullopt;
    }

    const std::optional<CapturePolicy> capture =
        parseCapturePolicyStrict(command.value(QStringLiteral("capture")),
                                 command.contains(QStringLiteral("capture")),
                                 stepPath,
                                 diagnostics);
    if (!capture.has_value()) {
        return std::nullopt;
    }
    parsed.capture = *capture;

    const std::optional<std::chrono::milliseconds> deadline =
        parseCommandDeadlineStrict(command, stepPath, diagnostics);
    if (!deadline.has_value()) {
        return std::nullopt;
    }
    parsed.deadline = *deadline;
    return parsed;
}
```

- [ ] **Step 6: Use parsed fields in runExecStep**

At the top of `runExecStep`, replace permissive extraction with:

```cpp
    const std::optional<ParsedExecCommand> parsed =
        parseExecCommandStrict(step.value(QStringLiteral("command")), stepPath, result.diagnostics);
    if (!parsed.has_value()) {
        return false;
    }
    if (parsed->hasNative) {
        addFlowDiagnostic(...);
        return false;
    }
```

Change resolve helpers to accept parsed strings:

```cpp
bool resolveExecutable(const ipcraft::FlowRunRequest& request,
                       const ParsedExecCommand& command,
                       const QString& stepPath,
                       ipcraft::DiagnosticStore& diagnostics,
                       QString* executablePath);
```

Use:

```cpp
process.setArguments(expandedArguments(parsed->args, request, result));
process.setProcessEnvironment(sanitizedEnvironment(parsed->envAllow));
```

Replace `expandedArguments(const QJsonValue& ...)` with:

```cpp
QStringList expandedArguments(const QStringList& values,
                              const ipcraft::FlowRunRequest& request,
                              const ipcraft::FlowRunResult& result)
```

Replace `sanitizedEnvironment(const QJsonObject&)` with:

```cpp
QProcessEnvironment sanitizedEnvironment(const QSet<QString>& envAllow)
```

- [ ] **Step 7: Run GREEN**

Run:

```bash
xmake run -P qt ipcraft_flowrunner_test
```

Expected: PASS. The marker files are not created in every malformed field case.

- [ ] **Step 8: Commit**

```bash
git add qt/src/ipcraft/flowrunner.cpp qt/test/ipcraft_flowrunner_test.cpp
git commit -m "fix: fail closed on malformed flow commands"
```

---

### Task 8: Add P1/P2 Structural Gates Without Dual-Write

**Files:**
- Modify: `qt/test/plugin_hard_cutover_scan_test.cpp`
- Modify: `qt/test/ipcraft_patch_foundation_test.cpp`
- Optional create: `qt/inc/commands/projectpatchcommand.h`
- Optional create: `qt/src/commands/projectpatchcommand.cpp`

- [ ] **Step 1: Add architecture gates as red tests**

Add these scan functions to `qt/test/plugin_hard_cutover_scan_test.cpp`:

```cpp
void testNodeEditorDoesNotConstructDurableGraphCommands() {
    const QStringList nodeEditorFiles{
        QStringLiteral("qt/src/nodeeditor/nodeeditorwidget.cpp"),
        QStringLiteral("qt/src/nodeeditor/events/nodeeditorwidget_events.cpp")
    };
    const QStringList forbidden{
        QStringLiteral("std::make_unique<AddModuleCommand>"),
        QStringLiteral("std::make_unique<AddConnectionCommand>"),
        QStringLiteral("std::make_unique<RemoveModuleCommand>"),
        QStringLiteral("std::make_unique<RemoveConnectionCommand>"),
        QStringLiteral("std::make_unique<SetParameterCommand>"),
        QStringLiteral("std::make_unique<ArrangeCommand>")
    };
    for (const QString& path : nodeEditorFiles) {
        const QString source = readText(path);
        for (const QString& token : forbidden) {
            requireNotContains(source, token, path);
        }
    }
}

void testDurableCommandsDoNotHoldGraphOrEditorMutationTarget() {
    const QStringList commandHeaders{
        QStringLiteral("qt/inc/commands/addmodulecommand.h"),
        QStringLiteral("qt/inc/commands/addconnectioncommand.h"),
        QStringLiteral("qt/inc/commands/removemodulecommand.h"),
        QStringLiteral("qt/inc/commands/removeconnectioncommand.h"),
        QStringLiteral("qt/inc/commands/setparametercommand.h"),
        QStringLiteral("qt/inc/commands/setconnectionclasscommand.h"),
        QStringLiteral("qt/inc/commands/arrangecommand.h"),
        QStringLiteral("qt/inc/commands/topologypresetcommand.h")
    };
    const QStringList forbidden{
        QStringLiteral("Graph*"),
        QStringLiteral("Graph *"),
        QStringLiteral("EditorMutationTarget")
    };
    for (const QString& path : commandHeaders) {
        const QString source = readText(path);
        for (const QString& token : forbidden) {
            requireNotContains(source, token, path);
        }
    }
}

void testTopologyPresetInteractionHandlerDoesNotConstructGraphCommand() {
    const QString source = readText(QStringLiteral("qt/src/app/topologypresetinteractionhandler.cpp"));
    requireNotContains(source, QStringLiteral("commands/topologypresetcommand.h"),
                       QStringLiteral("TopologyPresetInteractionHandler"));
    requireNotContains(source, QStringLiteral("TopologyPresetCommand"),
                       QStringLiteral("TopologyPresetInteractionHandler"));
}
```

Call them from `main()`.

- [ ] **Step 2: Run RED and capture expected failures**

Run:

```bash
xmake run -P qt plugin_hard_cutover_scan_test
```

Expected: FAIL on current NodeEditor graph-command construction, command header `Graph*`, `EditorMutationTarget`, and topology preset command construction.

- [ ] **Step 3: Do not make these gates green by dual-writing**

For this pass, do not change commands to both mutate Graph and mutate ProjectDesign.

The green path is one of these later Stage B options:

1. Full migration to `ProjectPatchCommand` and `DesignEditingService`.
2. Hard-disable old durable edit actions until the design command path is ready.

- [ ] **Step 4: Add ProjectPatch missing-operation tests**

In `qt/test/ipcraft_patch_foundation_test.cpp`, add red tests for missing operations:

```cpp
void testPatchRejectsUnsupportedRemoveComponentUntilImplemented() {
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    ipcraft::core::PatchOperation operation;
    operation.op = QStringLiteral("remove");
    operation.target = QStringLiteral("component:uart0");
    operation.path = QStringLiteral("/components/uart0");
    patch.ops.append(operation);

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(minimalProject(), patch);
    require(!result.success, "remove component is not implemented in this tranche");
    requireIssueCode(result.issues,
                     QStringLiteral("patch.unsupported_op"),
                     "unsupported remove component should be explicit");
}
```

Add equivalent explicit unsupported tests for:

- add connection
- remove connection
- set connection metadata/class
- layout/view state
- topology preset output

These tests preserve the boundary by documenting missing patch vocabulary without pretending the migration is done.

- [ ] **Step 5: Commit red gate only if the team agrees to keep it red**

Do not commit a permanently failing CI gate to `master` unless the team wants a blocking branch. For a normal implementation branch, keep P1/P2 gates in a separate commit after the migration implementation. If committing now, use:

```bash
git add qt/test/plugin_hard_cutover_scan_test.cpp qt/test/ipcraft_patch_foundation_test.cpp
git commit -m "test: define design-editing hard-cut gates"
```

Expected state: branch is intentionally red until P1/P2 Stage B.

---

### Task 9: P5 Green Gates for P0/P3/P4

**Files:**
- Modify: `qt/test/plugin_hard_cutover_scan_test.cpp`
- Modify: `qt/test/ipcraft_architecture_foundation_scan_test.cpp`
- Modify: `qt/test/ipcraft_contract_examples_test.cpp`
- Modify: `qt/test/ipcraft_flowrunner_test.cpp`

- [ ] **Step 1: Ensure public schema gate is in CI**

Confirm `qt/xmake.lua` target `ipcraft_contract_examples_test` includes:

```lua
add_tests("default", {
    trim_output = true,
    pass_outputs = "ipcraft_contract_examples_test passed"
})
```

- [ ] **Step 2: Ensure FlowRunner malformed gate is in CI**

Confirm `ipcraft_flowrunner_test` has the malformed field matrix from Task 7 and the xmake test target already runs it.

- [ ] **Step 3: Ensure AppContext scan gate is in CI**

Confirm `plugin_hard_cutover_scan_test` includes the P3 scan functions and its xmake target is in group `test`.

- [ ] **Step 4: Add writer-shape scan**

In `qt/test/ipcraft_architecture_foundation_scan_test.cpp`, add:

```cpp
void testProjectWriterDoesNotEmitLegacyWrapperRoot() {
    const QString source = readText(QStringLiteral("qt/src/project/projectwriter.cpp"));
    require(!source.contains(QStringLiteral("root.insert(QStringLiteral(\"project\")")),
            QStringLiteral("ProjectWriter must not emit wrapper project root"));
    require(!source.contains(QStringLiteral("root.insert(QStringLiteral(\"instances\")")),
            QStringLiteral("ProjectWriter must not emit wrapper instances root"));
    require(source.contains(QStringLiteral("ProjectDocumentV1::writeObject")),
            QStringLiteral("ProjectWriter should emit through flat ProjectDocumentV1 writer"));
}
```

Call from `main()`.

- [ ] **Step 5: Run GREEN gates**

Run:

```bash
xmake run -P qt ipcraft_contract_examples_test
xmake run -P qt ipcraft_flowrunner_test
xmake run -P qt plugin_hard_cutover_scan_test
xmake run -P qt ipcraft_architecture_foundation_scan_test
```

Expected: PASS for P0/P3/P4/P5 green gates. P1/P2 gates pass only after Stage B migration or if they are not enabled in CI yet.

- [ ] **Step 6: Commit**

```bash
git add qt/test/plugin_hard_cutover_scan_test.cpp qt/test/ipcraft_architecture_foundation_scan_test.cpp
git commit -m "test: harden architecture boundary gates"
```

---

### Task 10: Final Verification Matrix

**Files:**
- No source changes.

- [ ] **Step 1: Run targeted P0 tests**

```bash
xmake run -P qt ipcraft_contract_examples_test
xmake run -P qt ipcraft_project_model_test
xmake run -P qt projectdocument_test
xmake run -P qt projectservice_test
xmake run -P qt ipcraft_migration_test
```

Expected: PASS.

- [ ] **Step 2: Run targeted P3 tests**

```bash
xmake run -P qt pluginhost_foundation_test
xmake run -P qt projectplugin_test
xmake run -P qt packageplugin_test
xmake run -P qt toolpipelineplugin_test
xmake run -P qt staticplugincatalog_test
xmake run -P qt packagecoverage_test
xmake run -P qt nocplugin_test
xmake run -P qt plugin_registry_test
```

Expected: PASS.

- [ ] **Step 3: Run targeted P4 tests**

```bash
xmake run -P qt ipcraft_flowrunner_test
```

Expected: PASS.

- [ ] **Step 4: Run P5 gates**

```bash
xmake run -P qt plugin_hard_cutover_scan_test
xmake run -P qt ipcraft_architecture_foundation_scan_test
```

Expected: PASS for enabled gates. If P1/P2 red gates are present, the expected state is FAIL until Stage B; do not report the branch as complete.

- [ ] **Step 5: Optional broader sweep**

If time allows:

```bash
xmake build -P qt
xmake run -P qt ipcraft_cli_contract_test
xmake run -P qt packageservice_test
xmake run -P qt packagecoverage_test
xmake run -P qt vendor_meshnoc_onboarding_test
```

Expected: PASS. If not run, list them in the final report.

---

## Remaining P1/P2 Stage B Work

The later migration must make these gates green without dual-write:

1. Extend `ProjectPatch` to support:
   - remove component
   - add/remove connection
   - connection class/metadata/config
   - layout/view state
   - topology preset output
2. Add `ProjectPatchCommand` or `DesignCommand` that holds `DesignEditingService*`, not `Graph*`.
3. Route `NodeEditorWidget` and `PropertyPanel` user intent to design commands.
4. Add a projection refresh service:
   - ProjectDesign update is authoritative.
   - Graph projection refresh is best-effort view update.
   - projection failure emits `editor.projection_failed`.
   - projection failure marks the view stale and never makes Graph authoritative.
5. Replace `TopologyPresetInteractionHandler` command creation with a design-level planner.
6. Remove or quarantine graph-mutating command classes from runtime durable edit paths.

Do not mark P1/P2 complete while `NodeEditorWidget` still constructs graph commands or command headers still hold `Graph*`/`EditorMutationTarget`.

## Plan Self-Review

- P0 coverage: Tasks 1-5 cover schema validator, examples, package manifests, writer output, package unknown extension boundary.
- P3 coverage: Task 6 covers registry-only AppContext, ServiceRegistry publication, plugin activation failure, and scan gates.
- P4 coverage: Task 7 covers strict typed parsing and marker-file fail-closed tests before `QProcess::start()`.
- P5 coverage: Tasks 8-9 cover structural gates, with explicit P1/P2 red/Stage B handling to avoid unsafe dual-write.
- No placeholders: every task names exact files, commands, and expected outcomes.
