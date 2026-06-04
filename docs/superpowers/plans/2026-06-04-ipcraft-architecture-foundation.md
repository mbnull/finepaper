# IpCraft Architecture Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first independently testable foundation slice from `.specify/specs/001-ipcraft-architecture`: architecture audit gates, public schema coverage tracking, `ProjectDesign` core records, `ipcraft.project.v1` structural read/write, and `ipcraft.patch.v1` mutation envelope.

**Architecture:** This is a hard-cutover foundation, not a compatibility layer. New source-of-truth types live under `ipcraft-core`; existing graph/editor/runtime code remains untouched except for build wiring and public scan tests that document future deletion targets. Project loading is structural and package-registry-free in this slice; package resolution, topology expansion, UI, NoC capability, and tool protocol execution are separate follow-up plans.

**Tech Stack:** C++23, Qt Core JSON APIs, xmake targets under `qt/`, Markdown audit docs under `docs/`, public JSON schema constants under `qt/inc/ipcraft/schemaids.h`.

---

## Spec Readiness Decision

No preliminary `docs/superpowers/specs/...` file is required before this plan.

The `.specify` package is already a complete implementation spec input:

- `.specify/specs/001-ipcraft-architecture/spec.md` defines users, goals, non-goals, functional requirements, anti-requirements, and success criteria.
- `.specify/specs/001-ipcraft-architecture/contracts.md` defines public schema contracts and examples.
- `.specify/specs/001-ipcraft-architecture/data-model.md` defines canonical records.
- `.specify/specs/001-ipcraft-architecture/test-strategy.md` defines public, hidden, golden, architecture, and phase gates.
- `.specify/specs/001-ipcraft-architecture/migration-strategy.md` defines hard-cutover and deletion policy.
- `.specify/specs/001-ipcraft-architecture/tasks.md` splits the work into phases T0001 through T1002.

Writing a second Superpowers spec now would duplicate the source of truth and create drift. If a reviewer later wants a Superpowers-native spec index, make it a thin pointer to `.specify/specs/001-ipcraft-architecture/*`, not a rewritten requirement source.

## Scope Check

The `.specify` content spans multiple independent subsystems. Per `superpowers:writing-plans`, this must be split into separate implementation plans so each one can ship working, testable software.

This plan covers:

- Phase 0: T0001 and T0002 architecture audit and deletion map.
- Phase 1: T0100, T0101, T0102, T0103, and T0104 core IR foundation.

Follow-up plans should be created separately after this foundation lands:

- `ipcraft-package-capability`: T0201 through T0203.
- `ipcraft-topology-ir`: T0301 through T0303.
- `ipcraft-resolution-tool-protocol`: T0401 through T0403.
- `ipcraft-domain-services`: T0501.
- `ipcraft-ui-view-host`: T0601 through T0603.
- `ipcraft-noc-capability`: T0701 through T0702.
- `ipcraft-vendor-meshnoc-package`: T0801 through T0802.
- `ipcraft-package-authoring-cli`: T0901 through T0902.
- `ipcraft-final-cleanup-gates`: T1001 through T1002.

## File Structure

Architecture docs:

- Create `docs/architecture/ipcraft-architecture-deletion-map.md`: auditable classification of old graph, registry, NoC, command, loader, generator, and UI paths.
- Create `docs/audit/ipcraft-public-schema-matrix.md`: public schema coverage matrix used by the foundation scan.

Foundation tests:

- Create `qt/test/ipcraft_architecture_foundation_scan_test.cpp`: scan docs and schema constants for foundation coverage.
- Create `qt/test/ipcraft_project_design_foundation_test.cpp`: core value model and validation tests.
- Create `qt/test/ipcraft_project_document_v1_foundation_test.cpp`: structural reader/writer contract tests.
- Create `qt/test/ipcraft_patch_foundation_test.cpp`: patch parse/validation/apply contract tests.
- Modify `qt/xmake.lua`: add four `qt.console` test targets.

Core source:

- Modify `qt/inc/ipcraft/schemaids.h`: add constants for component, interface, connection rules, topology graph, topology parametric, view, view descriptor, tool input, tool result, artifact, patch, and NoC capability schemas.
- Create `qt/inc/ipcraft/core/project_design.h`: source-of-truth value records and structural validation API.
- Create `qt/src/ipcraft/core/project_design.cpp`: validation and JSON helpers for the core records.
- Create `qt/inc/ipcraft/core/project_document_v1.h`: structural reader/writer API for `ipcraft.project.v1`.
- Create `qt/src/ipcraft/core/project_document_v1.cpp`: deterministic structural parse/write implementation.
- Create `qt/inc/ipcraft/core/project_patch.h`: patch operation and apply API.
- Create `qt/src/ipcraft/core/project_patch.cpp`: patch parse/validation/apply implementation for the foundation operations.

This plan intentionally does not modify existing `qt/inc/project/projectdocument.h`, `qt/src/project/projectreader.cpp`, or existing graph/node editor code. Those are migration targets for later cleanup plans after the new core contract exists and passes tests.

## Task 1: Architecture Audit Gate And Schema Matrix

**Files:**
- Create: `qt/test/ipcraft_architecture_foundation_scan_test.cpp`
- Create: `docs/architecture/ipcraft-architecture-deletion-map.md`
- Create: `docs/audit/ipcraft-public-schema-matrix.md`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing architecture foundation scan test**

Create `qt/test/ipcraft_architecture_foundation_scan_test.cpp`:

```cpp
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

QString readText(const QString& path) {
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + path);
    if (!file.exists()) {
        QFile sourceTreeFile(path);
        if (!sourceTreeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw std::runtime_error(("missing file: " + path).toStdString());
        }
        return QString::fromUtf8(sourceTreeFile.readAll());
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read file: " + path).toStdString());
    }
    return QString::fromUtf8(file.readAll());
}

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" must contain ") + needle);
}

void testDeletionMapCoversHardCutoverTargets() {
    const QString deletionMap = readText(QStringLiteral("docs/architecture/ipcraft-architecture-deletion-map.md"));
    const QStringList requiredTerms = {
        QStringLiteral("Graph / Module / Connection"),
        QStringLiteral("NodeEditorWidget"),
        QStringLiteral("ModuleRegistry"),
        QStringLiteral("ipcraft.noc.project.v1"),
        QStringLiteral("ProjectPatchCommand"),
        QStringLiteral("ToolInputBuilder"),
        QStringLiteral("packages/vendor-meshnoc"),
        QStringLiteral("delete"),
        QStringLiteral("replace"),
        QStringLiteral("adapter only")
    };
    for (const QString& term : requiredTerms) {
        requireContains(deletionMap, term, QStringLiteral("deletion map"));
    }
}

void testSchemaMatrixListsAllPublicContracts() {
    const QString matrix = readText(QStringLiteral("docs/audit/ipcraft-public-schema-matrix.md"));
    const QStringList schemas = {
        QStringLiteral("ipcraft.project.v1"),
        QStringLiteral("ipcraft.package.v1"),
        QStringLiteral("ipcraft.component.v1"),
        QStringLiteral("ipcraft.interface.v1"),
        QStringLiteral("ipcraft.connection_rules.v1"),
        QStringLiteral("ipcraft.topology.graph.v1"),
        QStringLiteral("ipcraft.topology.parametric.v1"),
        QStringLiteral("ipcraft.view.v1"),
        QStringLiteral("ipcraft.view.descriptor.v1"),
        QStringLiteral("ipcraft.tool.input.v1"),
        QStringLiteral("ipcraft.tool.result.v1"),
        QStringLiteral("ipcraft.diagnostic.v1"),
        QStringLiteral("ipcraft.artifact.v1"),
        QStringLiteral("ipcraft.patch.v1"),
        QStringLiteral("ipcraft.capability.noc.v1"),
        QStringLiteral("ipcraft.capability.noc.extension.v1")
    };
    for (const QString& schema : schemas) {
        requireContains(matrix, schema, QStringLiteral("schema matrix"));
    }
    requireContains(matrix, QStringLiteral("parser"), QStringLiteral("schema matrix"));
    requireContains(matrix, QStringLiteral("writer"), QStringLiteral("schema matrix"));
    requireContains(matrix, QStringLiteral("roundtrip"), QStringLiteral("schema matrix"));
    requireContains(matrix, QStringLiteral("negative"), QStringLiteral("schema matrix"));
    requireContains(matrix, QStringLiteral("golden"), QStringLiteral("schema matrix"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testDeletionMapCoversHardCutoverTargets();
    testSchemaMatrixListsAllPublicContracts();
    std::cout << "ipcraft_architecture_foundation_scan_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the xmake target and verify the test fails**

Add this target near the other standalone test targets in `qt/xmake.lua`:

```lua
target("ipcraft_architecture_foundation_scan_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_architecture_foundation_scan_test.cpp")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_architecture_foundation_scan_test passed"
    })
```

Run:

```bash
xmake -P qt run ipcraft_architecture_foundation_scan_test
```

Expected: FAIL with `missing file: docs/architecture/ipcraft-architecture-deletion-map.md`.

- [ ] **Step 3: Create the deletion map**

Create `docs/architecture/ipcraft-architecture-deletion-map.md`:

```markdown
# IpCraft Architecture Deletion Map

Source spec: `.specify/specs/001-ipcraft-architecture`.

This document classifies legacy runtime concepts for the hard cutover to `ProjectDesign`.

| Legacy concept | Classification | Replacement | Removal condition |
| --- | --- | --- | --- |
| Graph / Module / Connection as project source of truth | replace | `ProjectDesign`, semantic `Connection`, `TopologyGraph`, `ViewDocument` | Project load/save/generation no longer reads graph records as the aggregate root. |
| NodeEditorWidget owning domain mutation | adapter only | `ViewHost`, `BlockDiagramViewProvider`, `TopologyGraphViewProvider`, `DesignEditingService` | UI emits `ProjectPatch` for every durable semantic and layout mutation. |
| ModuleRegistry global mutable singleton | replace | injected `PackageRegistry`, `ComponentTypeRegistry`, `InterfaceTypeRegistry`, `ConnectionRuleRegistry` | no runtime path gets component/package capabilities from a global mutable registry. |
| `ipcraft.noc.project.v1` normal generator input | delete | `ipcraft.tool.input.v1` projection produced by `ToolInputBuilder` | generation and validation tools consume tool input only. |
| NoC implementation package ids in core | delete | ordinary package data under `packages/vendor-meshnoc` and capability data under `ipcraft-capability-noc` | source scans find no core package-id checks for `meshnoc`, `ravenoc`, `opennoc`, or `vendor.meshnoc`. |
| UI protocol compatibility hardcode | replace | `ConnectionCompatibilityService` backed by package connection rules | UI has no AXI, UART, NoC, direction-port, or mesh-router connection rules. |
| Raw graph commands for durable mutation | replace | `ProjectPatchCommand`, transaction groups, `PatchApplier` | undo/redo applies validated patch transactions. |
| Graph project serializer as document writer | adapter only | `ProjectDocumentV1` writer plus view layout projection adapters | graph serializer cannot write semantic project state. |
| Old NoC exporter | delete | `ProjectionService`, `ToolInputBuilder`, package generator tools | no normal runtime exporter emits `ipcraft.noc.project.v1`. |
| View XML/YAML drawing primitive descriptors | replace | `ipcraft.view.descriptor.v1` package descriptors | descriptor validation rejects Qt paint commands and business logic. |
| Explicit import/conversion of old projects | adapter only | explicit import command outside normal loader | normal loader rejects old schemas and reports `project.unsupported_schema`. |
```

- [ ] **Step 4: Create the public schema matrix**

Create `docs/audit/ipcraft-public-schema-matrix.md`:

```markdown
# IpCraft Public Schema Matrix

Source spec: `.specify/specs/001-ipcraft-architecture/contracts.md` and `.specify/specs/001-ipcraft-architecture/test-strategy.md`.

Every public schema needs parser, writer, roundtrip, negative, and golden coverage before a phase claims contract completion.

| Schema | Owner phase | Parser | Writer | Roundtrip | Negative | Golden | Public test target |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ipcraft.project.v1` | Foundation | required | required | required | required | required | `ipcraft_project_document_v1_foundation_test` |
| `ipcraft.package.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.component.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.interface.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.connection_rules.v1` | Package capability | required | required | required | required | required | `ipcraft_package_contract_test` |
| `ipcraft.topology.graph.v1` | Topology IR | required | required | required | required | required | `ipcraft_topology_graph_contract_test` |
| `ipcraft.topology.parametric.v1` | Topology IR | required | required | required | required | required | `ipcraft_topology_graph_contract_test` |
| `ipcraft.view.v1` | UI view host | required | required | required | required | required | `ipcraft_view_contract_test` |
| `ipcraft.view.descriptor.v1` | UI view host | required | required | required | required | required | `ipcraft_view_contract_test` |
| `ipcraft.tool.input.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.tool.result.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.diagnostic.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.artifact.v1` | Resolution and tool protocol | required | required | required | required | required | `ipcraft_tool_protocol_contract_test` |
| `ipcraft.patch.v1` | Foundation | required | required | required | required | required | `ipcraft_patch_foundation_test` |
| `ipcraft.capability.noc.v1` | NoC capability | required | required | required | required | required | `noc_contract_test` |
| `ipcraft.capability.noc.extension.v1` | NoC capability | required | required | required | required | required | `noc_contract_test` |
```

- [ ] **Step 5: Run the scan test and commit**

Run:

```bash
xmake -P qt run ipcraft_architecture_foundation_scan_test
```

Expected: PASS with `ipcraft_architecture_foundation_scan_test passed`.

Commit:

```bash
git add docs/architecture/ipcraft-architecture-deletion-map.md docs/audit/ipcraft-public-schema-matrix.md qt/test/ipcraft_architecture_foundation_scan_test.cpp qt/xmake.lua
git commit -m "test: add ipcraft architecture foundation scan"
```

## Task 2: Schema Ids And ProjectDesign Value Model

**Files:**
- Create: `qt/test/ipcraft_project_design_foundation_test.cpp`
- Modify: `qt/inc/ipcraft/schemaids.h`
- Create: `qt/inc/ipcraft/core/project_design.h`
- Create: `qt/src/ipcraft/core/project_design.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing ProjectDesign foundation test**

Create `qt/test/ipcraft_project_design_foundation_test.cpp`:

```cpp
#include "ipcraft/core/project_design.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasCode(const QVector<ipcraft::core::ValidationIssue>& issues, const QString& code) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

ipcraft::core::ProjectDesign minimalProject() {
    ipcraft::core::ProjectDesign project;
    project.id = QStringLiteral("proj_uart_min");
    project.name = QStringLiteral("Minimal UART");
    project.packages.append({QStringLiteral("vendor.uart16550"), QStringLiteral("1.0.0")});

    ipcraft::core::ComponentInstance uart;
    uart.id = QStringLiteral("uart0");
    uart.type = QStringLiteral("uart16550");
    uart.packageRef = QStringLiteral("vendor.uart16550@1.0.0");
    uart.identity.insert(QStringLiteral("label"), QStringLiteral("UART 0"));
    uart.config.insert(QStringLiteral("baud"), 115200);
    project.components.append(uart);
    return project;
}

void testSchemaConstantsCoverFoundationAndFollowupContracts() {
    require(ipcraft::schemaids::projectV1 == QStringLiteral("ipcraft.project.v1"),
            "project schema id should remain stable");
    require(ipcraft::schemaids::patchV1 == QStringLiteral("ipcraft.patch.v1"),
            "patch schema id should exist");
    require(ipcraft::schemaids::topologyGraphV1 == QStringLiteral("ipcraft.topology.graph.v1"),
            "topology graph schema id should exist");
    require(ipcraft::schemaids::toolInputV1 == QStringLiteral("ipcraft.tool.input.v1"),
            "tool input schema id should exist");
    require(ipcraft::schemaids::nocCapabilityV1 == QStringLiteral("ipcraft.capability.noc.v1"),
            "NoC capability schema id should exist");
}

void testMinimalProjectDesignValidates() {
    const ipcraft::core::ProjectDesign project = minimalProject();
    const QVector<ipcraft::core::ValidationIssue> issues = ipcraft::core::validateProjectDesign(project);
    require(issues.isEmpty(), "minimal structural project should validate");
}

void testDuplicateComponentIdsRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components.append(project.components.first());
    const QVector<ipcraft::core::ValidationIssue> issues = ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.duplicate_component_id")),
            "duplicate component ids should be rejected");
}

void testLayoutFieldsRejectedFromComponentConfig() {
    ipcraft::core::ProjectDesign project = minimalProject();
    project.components[0].config.insert(QStringLiteral("x"), 80);
    project.components[0].config.insert(QStringLiteral("waypoints"), QJsonArray{80, 120});
    const QVector<ipcraft::core::ValidationIssue> issues = ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.layout_in_component_config")),
            "layout fields in component config should be rejected");
}

void testAttachmentConnectionKindRejected() {
    ipcraft::core::ProjectDesign project = minimalProject();
    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("c_bad_attachment");
    connection.from = {QStringLiteral("uart0"), QStringLiteral("serial")};
    connection.to = {QStringLiteral("uart0"), QStringLiteral("axi_s")};
    connection.kind = QStringLiteral("attachment");
    project.connections.append(connection);

    const QVector<ipcraft::core::ValidationIssue> issues = ipcraft::core::validateProjectDesign(project);
    require(hasCode(issues, QStringLiteral("project.attachment_connection_forbidden")),
            "connections kind attachment should be rejected");
}

void testExtensionBlockEnvelopeIsPreserved() {
    ipcraft::core::ExtensionBlock extension;
    extension.ownerPackageId = QStringLiteral("vendor.meshnoc");
    extension.schemaId = QStringLiteral("vendor.meshnoc.project.v1");
    extension.version = 1;
    extension.data.insert(QStringLiteral("schema"), QStringLiteral("vendor.meshnoc.project.v1"));
    extension.data.insert(QStringLiteral("routing_algorithm"), QStringLiteral("xy"));

    const QJsonObject json = ipcraft::core::extensionBlockToJson(extension);
    require(json.value(QStringLiteral("ownerPackageId")).toString() == QStringLiteral("vendor.meshnoc"),
            "extension owner should serialize");
    require(json.value(QStringLiteral("data")).toObject().value(QStringLiteral("routing_algorithm")).toString() == QStringLiteral("xy"),
            "extension data should serialize");

    const ipcraft::core::ExtensionBlock parsed = ipcraft::core::extensionBlockFromJson(json);
    require(parsed.ownerPackageId == extension.ownerPackageId, "extension owner should parse");
    require(parsed.schemaId == extension.schemaId, "extension schema should parse");
    require(parsed.version == 1, "extension version should parse");
}

} // namespace

int main() {
    testSchemaConstantsCoverFoundationAndFollowupContracts();
    testMinimalProjectDesignValidates();
    testDuplicateComponentIdsRejected();
    testLayoutFieldsRejectedFromComponentConfig();
    testAttachmentConnectionKindRejected();
    testExtensionBlockEnvelopeIsPreserved();
    std::cout << "ipcraft_project_design_foundation_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the xmake target and verify it fails**

Add this target to `qt/xmake.lua`:

```lua
target("ipcraft_project_design_foundation_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_project_design_foundation_test.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
    add_files("inc/ipcraft/core/project_design.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_project_design_foundation_test passed"
    })
```

Run:

```bash
xmake -P qt run ipcraft_project_design_foundation_test
```

Expected: FAIL because `ipcraft/core/project_design.h` does not exist.

- [ ] **Step 3: Extend schema id constants**

Replace `qt/inc/ipcraft/schemaids.h` with:

```cpp
#pragma once

#include <QString>

namespace ipcraft::schemaids {

inline const QString projectV1 = QStringLiteral("ipcraft.project.v1");
inline const QString packageV1 = QStringLiteral("ipcraft.package.v1");
inline const QString componentV1 = QStringLiteral("ipcraft.component.v1");
inline const QString interfaceV1 = QStringLiteral("ipcraft.interface.v1");
inline const QString connectionRulesV1 = QStringLiteral("ipcraft.connection_rules.v1");
inline const QString topologyGraphV1 = QStringLiteral("ipcraft.topology.graph.v1");
inline const QString topologyParametricV1 = QStringLiteral("ipcraft.topology.parametric.v1");
inline const QString viewV1 = QStringLiteral("ipcraft.view.v1");
inline const QString viewDescriptorV1 = QStringLiteral("ipcraft.view.descriptor.v1");
inline const QString toolInputV1 = QStringLiteral("ipcraft.tool.input.v1");
inline const QString toolResultV1 = QStringLiteral("ipcraft.tool.result.v1");
inline const QString diagnosticsV1 = QStringLiteral("ipcraft.diagnostics.v1");
inline const QString diagnosticV1 = QStringLiteral("ipcraft.diagnostic.v1");
inline const QString artifactV1 = QStringLiteral("ipcraft.artifact.v1");
inline const QString patchV1 = QStringLiteral("ipcraft.patch.v1");
inline const QString graphConfigV1 = QStringLiteral("ipcraft.graph-config.v1");
inline const QString emittedInputsV1 = QStringLiteral("ipcraft.emitted-inputs.v1");
inline const QString cliResultV1 = QStringLiteral("ipcraft.cli.result.v1");
inline const QString nocCapabilityV1 = QStringLiteral("ipcraft.capability.noc.v1");
inline const QString nocExtensionV1 = QStringLiteral("ipcraft.capability.noc.extension.v1");

} // namespace ipcraft::schemaids

namespace IpcraftSchemaIds {
inline const QString& projectV1 = ipcraft::schemaids::projectV1;
inline const QString& packageV1 = ipcraft::schemaids::packageV1;
inline const QString& componentV1 = ipcraft::schemaids::componentV1;
inline const QString& interfaceV1 = ipcraft::schemaids::interfaceV1;
inline const QString& connectionRulesV1 = ipcraft::schemaids::connectionRulesV1;
inline const QString& topologyGraphV1 = ipcraft::schemaids::topologyGraphV1;
inline const QString& topologyParametricV1 = ipcraft::schemaids::topologyParametricV1;
inline const QString& viewV1 = ipcraft::schemaids::viewV1;
inline const QString& viewDescriptorV1 = ipcraft::schemaids::viewDescriptorV1;
inline const QString& toolInputV1 = ipcraft::schemaids::toolInputV1;
inline const QString& toolResultV1 = ipcraft::schemaids::toolResultV1;
inline const QString& diagnosticsV1 = ipcraft::schemaids::diagnosticsV1;
inline const QString& diagnosticV1 = ipcraft::schemaids::diagnosticV1;
inline const QString& artifactV1 = ipcraft::schemaids::artifactV1;
inline const QString& patchV1 = ipcraft::schemaids::patchV1;
inline const QString& graphConfigV1 = ipcraft::schemaids::graphConfigV1;
inline const QString& emittedInputsV1 = ipcraft::schemaids::emittedInputsV1;
inline const QString& cliResultV1 = ipcraft::schemaids::cliResultV1;
inline const QString& nocCapabilityV1 = ipcraft::schemaids::nocCapabilityV1;
inline const QString& nocExtensionV1 = ipcraft::schemaids::nocExtensionV1;
}
```

- [ ] **Step 4: Add the ProjectDesign value model**

Create `qt/inc/ipcraft/core/project_design.h`:

```cpp
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ipcraft::core {

struct ValidationIssue {
    QString code;
    QString message;
    QString path;
};

struct PackageRef {
    QString id;
    QString version;
};

struct EndpointRef {
    QString component;
    QString interface;
};

struct ComponentInstance {
    QString id;
    QString type;
    QString packageRef;
    QJsonObject config;
    QJsonObject identity;
    QJsonObject metadata;
    QJsonObject extensionData;
};

struct InterfaceInstance {
    QString id;
    QString ownerComponentId;
    QString type;
    QString role;
    QString direction;
    QString protocol;
    QString clockRef;
    QString resetRef;
    QJsonObject config;
    QJsonObject metadata;
};

struct Connection {
    QString id;
    EndpointRef from;
    EndpointRef to;
    QString kind = QStringLiteral("interface");
    QJsonObject config;
    QJsonObject constraints;
    QJsonObject metadata;
};

struct TopologyAttachment {
    QString id;
    QString topologyId;
    QJsonObject attachmentPoint;
    QString componentRef;
    QString interfaceRef;
    QString adapterRef;
    QJsonObject config;
};

struct TopologyGraph {
    QString id;
    QString schema;
    QString ownerComponentId;
    QString kind;
    QVector<QJsonObject> nodes;
    QVector<QJsonObject> links;
    QVector<TopologyAttachment> attachments;
    QJsonObject routing;
    QJsonObject metadata;
};

struct ViewDocument {
    QString id;
    QString schema;
    QString kind;
    QString targetRef;
    QString providerRef;
    QString sourceRef;
    QJsonObject layout;
    QJsonObject presentationState;
    QJsonObject metadata;
};

struct ExtensionBlock {
    QString ownerPackageId;
    QString schemaId;
    int version = 0;
    QJsonObject data;
    QJsonObject validationState;
};

struct ProjectDesign {
    QString schema;
    QString id;
    QString name;
    QVector<PackageRef> packages;
    QVector<ComponentInstance> components;
    QVector<InterfaceInstance> interfaces;
    QVector<Connection> connections;
    QVector<TopologyGraph> topologies;
    QJsonObject constraints;
    QVector<ViewDocument> views;
    QVector<QJsonObject> diagnostics;
    QVector<QJsonObject> artifacts;
    QVector<ExtensionBlock> extensions;
    QJsonObject metadata;
};

QVector<ValidationIssue> validateProjectDesign(const ProjectDesign& project);
QJsonObject extensionBlockToJson(const ExtensionBlock& extension);
ExtensionBlock extensionBlockFromJson(const QJsonObject& object);

} // namespace ipcraft::core
```

Create `qt/src/ipcraft/core/project_design.cpp`:

```cpp
#include "ipcraft/core/project_design.h"

#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QSet>

namespace ipcraft::core {
namespace {

bool isNonEmpty(const QString& value) {
    return !value.trimmed().isEmpty();
}

bool hasForbiddenLayoutKey(const QJsonObject& object) {
    static const QSet<QString> forbidden = {
        QStringLiteral("x"),
        QStringLiteral("y"),
        QStringLiteral("node_width"),
        QStringLiteral("node_height"),
        QStringLiteral("collapsed"),
        QStringLiteral("waypoints"),
        QStringLiteral("zoom"),
        QStringLiteral("pan")
    };
    for (const QString& key : object.keys()) {
        if (forbidden.contains(key)) {
            return true;
        }
        if (object.value(key).isObject() && hasForbiddenLayoutKey(object.value(key).toObject())) {
            return true;
        }
    }
    return false;
}

void addIssue(QVector<ValidationIssue>& issues,
              const QString& code,
              const QString& message,
              const QString& path) {
    issues.append({code, message, path});
}

} // namespace

QVector<ValidationIssue> validateProjectDesign(const ProjectDesign& project) {
    QVector<ValidationIssue> issues;
    if (!project.schema.isEmpty() && project.schema != schemaids::projectV1) {
        addIssue(issues,
                 QStringLiteral("project.unsupported_schema"),
                 QStringLiteral("ProjectDesign schema must be ipcraft.project.v1"),
                 QStringLiteral("/schema"));
    }
    if (!isNonEmpty(project.id)) {
        addIssue(issues,
                 QStringLiteral("project.missing_id"),
                 QStringLiteral("Project id is required"),
                 QStringLiteral("/id"));
    }
    if (!isNonEmpty(project.name)) {
        addIssue(issues,
                 QStringLiteral("project.missing_name"),
                 QStringLiteral("Project name is required"),
                 QStringLiteral("/name"));
    }

    QSet<QString> componentIds;
    for (int index = 0; index < project.components.size(); ++index) {
        const ComponentInstance& component = project.components.at(index);
        const QString path = QStringLiteral("/components/%1").arg(index);
        if (!isNonEmpty(component.id)) {
            addIssue(issues,
                     QStringLiteral("project.missing_component_id"),
                     QStringLiteral("Component id is required"),
                     path + QStringLiteral("/id"));
        } else if (componentIds.contains(component.id)) {
            addIssue(issues,
                     QStringLiteral("project.duplicate_component_id"),
                     QStringLiteral("Component ids must be unique"),
                     path + QStringLiteral("/id"));
        }
        componentIds.insert(component.id);

        if (hasForbiddenLayoutKey(component.config)) {
            addIssue(issues,
                     QStringLiteral("project.layout_in_component_config"),
                     QStringLiteral("Component config must not contain view layout fields"),
                     path + QStringLiteral("/config"));
        }
    }

    QSet<QString> connectionIds;
    for (int index = 0; index < project.connections.size(); ++index) {
        const Connection& connection = project.connections.at(index);
        const QString path = QStringLiteral("/connections/%1").arg(index);
        if (!isNonEmpty(connection.id)) {
            addIssue(issues,
                     QStringLiteral("project.missing_connection_id"),
                     QStringLiteral("Connection id is required"),
                     path + QStringLiteral("/id"));
        } else if (connectionIds.contains(connection.id)) {
            addIssue(issues,
                     QStringLiteral("project.duplicate_connection_id"),
                     QStringLiteral("Connection ids must be unique"),
                     path + QStringLiteral("/id"));
        }
        connectionIds.insert(connection.id);

        if (connection.kind == QStringLiteral("attachment")) {
            addIssue(issues,
                     QStringLiteral("project.attachment_connection_forbidden"),
                     QStringLiteral("Topology attachments are represented by topology attachments, not connections"),
                     path + QStringLiteral("/kind"));
        }
    }
    return issues;
}

QJsonObject extensionBlockToJson(const ExtensionBlock& extension) {
    QJsonObject object;
    object.insert(QStringLiteral("ownerPackageId"), extension.ownerPackageId);
    object.insert(QStringLiteral("schemaId"), extension.schemaId);
    object.insert(QStringLiteral("version"), extension.version);
    object.insert(QStringLiteral("data"), extension.data);
    if (!extension.validationState.isEmpty()) {
        object.insert(QStringLiteral("validationState"), extension.validationState);
    }
    return object;
}

ExtensionBlock extensionBlockFromJson(const QJsonObject& object) {
    ExtensionBlock extension;
    extension.ownerPackageId = object.value(QStringLiteral("ownerPackageId")).toString();
    extension.schemaId = object.value(QStringLiteral("schemaId")).toString();
    extension.version = object.value(QStringLiteral("version")).toInt();
    extension.data = object.value(QStringLiteral("data")).toObject();
    extension.validationState = object.value(QStringLiteral("validationState")).toObject();
    return extension;
}

} // namespace ipcraft::core
```

- [ ] **Step 5: Run the ProjectDesign test and commit**

Run:

```bash
xmake -P qt run ipcraft_project_design_foundation_test
```

Expected: PASS with `ipcraft_project_design_foundation_test passed`.

Commit:

```bash
git add qt/inc/ipcraft/schemaids.h qt/inc/ipcraft/core/project_design.h qt/src/ipcraft/core/project_design.cpp qt/test/ipcraft_project_design_foundation_test.cpp qt/xmake.lua
git commit -m "feat: add ipcraft project design foundation"
```

## Task 3: ProjectDocumentV1 Structural Reader And Writer

**Files:**
- Create: `qt/test/ipcraft_project_document_v1_foundation_test.cpp`
- Create: `qt/inc/ipcraft/core/project_document_v1.h`
- Create: `qt/src/ipcraft/core/project_document_v1.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing project document contract test**

Create `qt/test/ipcraft_project_document_v1_foundation_test.cpp`:

```cpp
#include "ipcraft/core/project_document_v1.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasCode(const QVector<ipcraft::core::ValidationIssue>& issues, const QString& code) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

QJsonObject minimalUartProject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::projectV1},
        {QStringLiteral("id"), QStringLiteral("proj_uart_min")},
        {QStringLiteral("name"), QStringLiteral("Minimal UART")},
        {QStringLiteral("packages"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.uart16550")},
                        {QStringLiteral("version"), QStringLiteral("1.0.0")}}
        }},
        {QStringLiteral("components"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("uart0")},
                {QStringLiteral("type"), QStringLiteral("uart16550")},
                {QStringLiteral("packageRef"), QStringLiteral("vendor.uart16550@1.0.0")},
                {QStringLiteral("identity"), QJsonObject{{QStringLiteral("label"), QStringLiteral("UART 0")}}},
                {QStringLiteral("config"), QJsonObject{{QStringLiteral("baud"), 115200}}}
            }
        }},
        {QStringLiteral("connections"), QJsonArray{}},
        {QStringLiteral("topologies"), QJsonArray{}},
        {QStringLiteral("views"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("block.main")},
                {QStringLiteral("schema"), ipcraft::schemaids::viewV1},
                {QStringLiteral("kind"), QStringLiteral("block_diagram")},
                {QStringLiteral("targetRef"), QStringLiteral("project:proj_uart_min")},
                {QStringLiteral("providerRef"), QStringLiteral("ipcraft.ui.block_diagram")},
                {QStringLiteral("layout"), QJsonObject{
                    {QStringLiteral("nodes"), QJsonObject{
                        {QStringLiteral("uart0"), QJsonObject{{QStringLiteral("x"), 96}, {QStringLiteral("y"), 128}}}
                    }},
                    {QStringLiteral("edges"), QJsonObject{}}
                }}
            }
        }},
        {QStringLiteral("extensions"), QJsonArray{}}
    };
}

QJsonObject cpuNicNocProject() {
    QJsonObject project = minimalUartProject();
    project.insert(QStringLiteral("id"), QStringLiteral("proj_cpu_nic_noc"));
    project.insert(QStringLiteral("name"), QStringLiteral("CPU NIC NoC Demo"));
    project.insert(QStringLiteral("packages"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.riscv")},
                    {QStringLiteral("version"), QStringLiteral("2.1.0")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.meshnoc")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")}}
    });
    project.insert(QStringLiteral("components"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cpu0")},
                    {QStringLiteral("type"), QStringLiteral("core")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.riscv@2.1.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("xlen"), 64}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("nic_cpu0")},
                    {QStringLiteral("type"), QStringLiteral("axi_nic")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc@1.0.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("axi_data_width"), 64}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("noc0")},
                    {QStringLiteral("type"), QStringLiteral("meshnoc")},
                    {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc@1.0.0")},
                    {QStringLiteral("config"), QJsonObject{{QStringLiteral("name"), QStringLiteral("noc0")}}}}
    });
    project.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("c_cpu_to_nic")},
                    {QStringLiteral("from"), QJsonObject{{QStringLiteral("component"), QStringLiteral("cpu0")},
                                                         {QStringLiteral("interface"), QStringLiteral("axi_m")}}},
                    {QStringLiteral("to"), QJsonObject{{QStringLiteral("component"), QStringLiteral("nic_cpu0")},
                                                       {QStringLiteral("interface"), QStringLiteral("axi_s")}}},
                    {QStringLiteral("kind"), QStringLiteral("interface")}}
    });
    project.insert(QStringLiteral("topologies"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("noc0.fabric")},
            {QStringLiteral("schema"), ipcraft::schemaids::topologyParametricV1},
            {QStringLiteral("ownerComponentId"), QStringLiteral("noc0")},
            {QStringLiteral("kind"), QStringLiteral("parametric")},
            {QStringLiteral("family"), QStringLiteral("mesh")},
            {QStringLiteral("providerRef"), QStringLiteral("ipcraft.capability.noc.topology.mesh")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("dimensions"), QJsonArray{2, 2}},
                                                       {QStringLiteral("routing"), QStringLiteral("xy")}}},
            {QStringLiteral("attachments"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("a_cpu")},
                            {QStringLiteral("attachmentPoint"), QJsonObject{{QStringLiteral("tile"), QJsonArray{0, 0}},
                                                                             {QStringLiteral("slot"), QStringLiteral("local0")}}},
                            {QStringLiteral("componentRef"), QStringLiteral("nic_cpu0")},
                            {QStringLiteral("interfaceRef"), QStringLiteral("noc_ep")}}
            }}
        }
    });
    project.insert(QStringLiteral("extensions"), QJsonArray{
        QJsonObject{{QStringLiteral("ownerPackageId"), QStringLiteral("ipcraft.capability.noc")},
                    {QStringLiteral("schemaId"), ipcraft::schemaids::nocExtensionV1},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("data"), QJsonObject{{QStringLiteral("schema"), ipcraft::schemaids::nocExtensionV1}}}},
        QJsonObject{{QStringLiteral("ownerPackageId"), QStringLiteral("vendor.meshnoc")},
                    {QStringLiteral("schemaId"), QStringLiteral("vendor.meshnoc.project.v1")},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("data"), QJsonObject{{QStringLiteral("schema"), QStringLiteral("vendor.meshnoc.project.v1")},
                                                         {QStringLiteral("routing_algorithm"), QStringLiteral("xy")}}}}
    });
    return project;
}

void testMinimalProjectRoundTripsStructurally() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(minimalUartProject());
    require(result.success, "minimal UART project should parse");
    require(result.project.id == QStringLiteral("proj_uart_min"), "project id should parse");
    require(result.project.components.size() == 1, "components should parse");
    require(result.project.views.size() == 1, "views should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1,
            "writer should emit project schema");
    require(written.value(QStringLiteral("components")).toArray().first().toObject()
                .value(QStringLiteral("config")).toObject().value(QStringLiteral("baud")).toInt() == 115200,
            "writer should preserve authored config");
}

void testCpuNicNocRoundTripsWithExtensionsAndAttachments() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(cpuNicNocProject());
    require(result.success, "CPU NIC NoC project should parse structurally");
    require(result.project.topologies.size() == 1, "topology should parse");
    require(result.project.topologies.first().attachments.size() == 1, "attachment should parse");
    require(result.project.extensions.size() == 2, "extension blocks should parse");

    const QJsonObject written = ipcraft::core::ProjectDocumentV1::writeObject(result.project);
    require(written.value(QStringLiteral("extensions")).toArray().size() == 2,
            "writer should preserve extension blocks");
}

void testReaderRejectsOldSchemaAndAttachmentConnections() {
    QJsonObject oldSchema = minimalUartProject();
    oldSchema.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.noc.project.v1"));
    const ipcraft::core::ProjectDocumentReadResult oldResult =
        ipcraft::core::ProjectDocumentV1::readObject(oldSchema);
    require(!oldResult.success, "old NoC schema should be rejected");
    require(hasCode(oldResult.issues, QStringLiteral("project.unsupported_schema")),
            "old schema should report project.unsupported_schema");

    QJsonObject badConnection = minimalUartProject();
    badConnection.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("c_bad")},
                    {QStringLiteral("from"), QJsonObject{{QStringLiteral("component"), QStringLiteral("uart0")},
                                                         {QStringLiteral("interface"), QStringLiteral("serial")}}},
                    {QStringLiteral("to"), QJsonObject{{QStringLiteral("component"), QStringLiteral("uart0")},
                                                       {QStringLiteral("interface"), QStringLiteral("axi_s")}}},
                    {QStringLiteral("kind"), QStringLiteral("attachment")}}
    });
    const ipcraft::core::ProjectDocumentReadResult badConnectionResult =
        ipcraft::core::ProjectDocumentV1::readObject(badConnection);
    require(!badConnectionResult.success, "attachment connection should be rejected");
    require(hasCode(badConnectionResult.issues, QStringLiteral("project.attachment_connection_forbidden")),
            "attachment connection should report stable issue code");
}

void testWriterIsDeterministic() {
    const ipcraft::core::ProjectDocumentReadResult result =
        ipcraft::core::ProjectDocumentV1::readObject(cpuNicNocProject());
    require(result.success, "input should parse");

    const QByteArray first = QJsonDocument(ipcraft::core::ProjectDocumentV1::writeObject(result.project))
                                 .toJson(QJsonDocument::Indented);
    const QByteArray second = QJsonDocument(ipcraft::core::ProjectDocumentV1::writeObject(result.project))
                                  .toJson(QJsonDocument::Indented);
    require(first == second, "writer output should be deterministic");
}

} // namespace

int main() {
    testMinimalProjectRoundTripsStructurally();
    testCpuNicNocRoundTripsWithExtensionsAndAttachments();
    testReaderRejectsOldSchemaAndAttachmentConnections();
    testWriterIsDeterministic();
    std::cout << "ipcraft_project_document_v1_foundation_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the xmake target and verify it fails**

Add this target to `qt/xmake.lua`:

```lua
target("ipcraft_project_document_v1_foundation_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_project_document_v1_foundation_test.cpp")
    add_files("src/ipcraft/core/project_document_v1.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
    add_files("inc/ipcraft/core/project_document_v1.h")
    add_files("inc/ipcraft/core/project_design.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_project_document_v1_foundation_test passed"
    })
```

Run:

```bash
xmake -P qt run ipcraft_project_document_v1_foundation_test
```

Expected: FAIL because `ipcraft/core/project_document_v1.h` does not exist.

- [ ] **Step 3: Add the structural reader/writer API**

Create `qt/inc/ipcraft/core/project_document_v1.h`:

```cpp
#pragma once

#include "ipcraft/core/project_design.h"

#include <QJsonObject>

namespace ipcraft::core {

struct ProjectDocumentReadResult {
    bool success = false;
    ProjectDesign project;
    QVector<ValidationIssue> issues;
};

class ProjectDocumentV1 {
public:
    static ProjectDocumentReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectDesign& project);
};

} // namespace ipcraft::core
```

Create `qt/src/ipcraft/core/project_document_v1.cpp` with structural parsing and writing helpers. Preserve unknown package-owned extension `data` objects without inspection, reject old schemas, and call `validateProjectDesign()` before returning success.

```cpp
#include "ipcraft/core/project_document_v1.h"

#include "ipcraft/schemaids.h"

#include <QJsonArray>

namespace ipcraft::core {
namespace {

ValidationIssue issue(const QString& code, const QString& message, const QString& path) {
    return {code, message, path};
}

QString stringValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toString();
}

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toObject();
}

QJsonArray arrayValue(const QJsonObject& object, const QString& key) {
    return object.value(key).isArray() ? object.value(key).toArray() : QJsonArray{};
}

EndpointRef endpointFromJson(const QJsonObject& object) {
    return {stringValue(object, QStringLiteral("component")),
            stringValue(object, QStringLiteral("interface"))};
}

QJsonObject endpointToJson(const EndpointRef& endpoint) {
    return QJsonObject{{QStringLiteral("component"), endpoint.component},
                       {QStringLiteral("interface"), endpoint.interface}};
}

PackageRef packageRefFromJson(const QJsonObject& object) {
    return {stringValue(object, QStringLiteral("id")),
            stringValue(object, QStringLiteral("version"))};
}

QJsonObject packageRefToJson(const PackageRef& package) {
    return QJsonObject{{QStringLiteral("id"), package.id},
                       {QStringLiteral("version"), package.version}};
}

ComponentInstance componentFromJson(const QJsonObject& object) {
    ComponentInstance component;
    component.id = stringValue(object, QStringLiteral("id"));
    component.type = stringValue(object, QStringLiteral("type"));
    component.packageRef = stringValue(object, QStringLiteral("packageRef"));
    component.config = objectValue(object, QStringLiteral("config"));
    component.identity = objectValue(object, QStringLiteral("identity"));
    component.metadata = objectValue(object, QStringLiteral("metadata"));
    component.extensionData = objectValue(object, QStringLiteral("extensionData"));
    return component;
}

QJsonObject componentToJson(const ComponentInstance& component) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), component.id);
    object.insert(QStringLiteral("type"), component.type);
    object.insert(QStringLiteral("packageRef"), component.packageRef);
    if (!component.config.isEmpty()) {
        object.insert(QStringLiteral("config"), component.config);
    }
    if (!component.identity.isEmpty()) {
        object.insert(QStringLiteral("identity"), component.identity);
    }
    if (!component.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), component.metadata);
    }
    if (!component.extensionData.isEmpty()) {
        object.insert(QStringLiteral("extensionData"), component.extensionData);
    }
    return object;
}

Connection connectionFromJson(const QJsonObject& object) {
    Connection connection;
    connection.id = stringValue(object, QStringLiteral("id"));
    connection.from = endpointFromJson(objectValue(object, QStringLiteral("from")));
    connection.to = endpointFromJson(objectValue(object, QStringLiteral("to")));
    connection.kind = stringValue(object, QStringLiteral("kind"));
    if (connection.kind.isEmpty()) {
        connection.kind = QStringLiteral("interface");
    }
    connection.config = objectValue(object, QStringLiteral("config"));
    connection.constraints = objectValue(object, QStringLiteral("constraints"));
    connection.metadata = objectValue(object, QStringLiteral("metadata"));
    return connection;
}

QJsonObject connectionToJson(const Connection& connection) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), connection.id);
    object.insert(QStringLiteral("from"), endpointToJson(connection.from));
    object.insert(QStringLiteral("to"), endpointToJson(connection.to));
    object.insert(QStringLiteral("kind"), connection.kind);
    if (!connection.config.isEmpty()) {
        object.insert(QStringLiteral("config"), connection.config);
    }
    if (!connection.constraints.isEmpty()) {
        object.insert(QStringLiteral("constraints"), connection.constraints);
    }
    if (!connection.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), connection.metadata);
    }
    return object;
}

TopologyAttachment attachmentFromJson(const QJsonObject& object, const QString& topologyId) {
    TopologyAttachment attachment;
    attachment.id = stringValue(object, QStringLiteral("id"));
    attachment.topologyId = stringValue(object, QStringLiteral("topologyId"));
    if (attachment.topologyId.isEmpty()) {
        attachment.topologyId = topologyId;
    }
    attachment.attachmentPoint = objectValue(object, QStringLiteral("attachmentPoint"));
    attachment.componentRef = stringValue(object, QStringLiteral("componentRef"));
    attachment.interfaceRef = stringValue(object, QStringLiteral("interfaceRef"));
    attachment.adapterRef = stringValue(object, QStringLiteral("adapterRef"));
    attachment.config = objectValue(object, QStringLiteral("config"));
    return attachment;
}

QJsonObject attachmentToJson(const TopologyAttachment& attachment, const QString& containingTopologyId) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), attachment.id);
    if (!attachment.topologyId.isEmpty() && attachment.topologyId != containingTopologyId) {
        object.insert(QStringLiteral("topologyId"), attachment.topologyId);
    }
    object.insert(QStringLiteral("attachmentPoint"), attachment.attachmentPoint);
    object.insert(QStringLiteral("componentRef"), attachment.componentRef);
    object.insert(QStringLiteral("interfaceRef"), attachment.interfaceRef);
    if (!attachment.adapterRef.isEmpty()) {
        object.insert(QStringLiteral("adapterRef"), attachment.adapterRef);
    }
    if (!attachment.config.isEmpty()) {
        object.insert(QStringLiteral("config"), attachment.config);
    }
    return object;
}

TopologyGraph topologyFromJson(const QJsonObject& object) {
    TopologyGraph topology;
    topology.id = stringValue(object, QStringLiteral("id"));
    topology.schema = stringValue(object, QStringLiteral("schema"));
    topology.ownerComponentId = stringValue(object, QStringLiteral("ownerComponentId"));
    topology.kind = stringValue(object, QStringLiteral("kind"));
    for (const QJsonValue& node : arrayValue(object, QStringLiteral("nodes"))) {
        if (node.isObject()) {
            topology.nodes.append(node.toObject());
        }
    }
    for (const QJsonValue& link : arrayValue(object, QStringLiteral("links"))) {
        if (link.isObject()) {
            topology.links.append(link.toObject());
        }
    }
    for (const QJsonValue& attachment : arrayValue(object, QStringLiteral("attachments"))) {
        if (attachment.isObject()) {
            topology.attachments.append(attachmentFromJson(attachment.toObject(), topology.id));
        }
    }
    topology.routing = objectValue(object, QStringLiteral("routing"));
    topology.metadata = objectValue(object, QStringLiteral("metadata"));
    return topology;
}

QJsonObject topologyToJson(const TopologyGraph& topology) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), topology.id);
    object.insert(QStringLiteral("schema"), topology.schema);
    if (!topology.ownerComponentId.isEmpty()) {
        object.insert(QStringLiteral("ownerComponentId"), topology.ownerComponentId);
    }
    object.insert(QStringLiteral("kind"), topology.kind);
    if (!topology.nodes.isEmpty()) {
        QJsonArray nodes;
        for (const QJsonObject& node : topology.nodes) {
            nodes.append(node);
        }
        object.insert(QStringLiteral("nodes"), nodes);
    }
    if (!topology.links.isEmpty()) {
        QJsonArray links;
        for (const QJsonObject& link : topology.links) {
            links.append(link);
        }
        object.insert(QStringLiteral("links"), links);
    }
    if (!topology.attachments.isEmpty()) {
        QJsonArray attachments;
        for (const TopologyAttachment& attachment : topology.attachments) {
            attachments.append(attachmentToJson(attachment, topology.id));
        }
        object.insert(QStringLiteral("attachments"), attachments);
    }
    if (!topology.routing.isEmpty()) {
        object.insert(QStringLiteral("routing"), topology.routing);
    }
    if (!topology.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), topology.metadata);
    }
    return object;
}

ViewDocument viewFromJson(const QJsonObject& object) {
    ViewDocument view;
    view.id = stringValue(object, QStringLiteral("id"));
    view.schema = stringValue(object, QStringLiteral("schema"));
    view.kind = stringValue(object, QStringLiteral("kind"));
    view.targetRef = stringValue(object, QStringLiteral("targetRef"));
    view.providerRef = stringValue(object, QStringLiteral("providerRef"));
    view.sourceRef = stringValue(object, QStringLiteral("sourceRef"));
    view.layout = objectValue(object, QStringLiteral("layout"));
    view.presentationState = objectValue(object, QStringLiteral("presentationState"));
    view.metadata = objectValue(object, QStringLiteral("metadata"));
    return view;
}

QJsonObject viewToJson(const ViewDocument& view) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), view.id);
    object.insert(QStringLiteral("schema"), view.schema);
    object.insert(QStringLiteral("kind"), view.kind);
    object.insert(QStringLiteral("targetRef"), view.targetRef);
    object.insert(QStringLiteral("providerRef"), view.providerRef);
    if (!view.sourceRef.isEmpty()) {
        object.insert(QStringLiteral("sourceRef"), view.sourceRef);
    }
    if (!view.layout.isEmpty()) {
        object.insert(QStringLiteral("layout"), view.layout);
    }
    if (!view.presentationState.isEmpty()) {
        object.insert(QStringLiteral("presentationState"), view.presentationState);
    }
    if (!view.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), view.metadata);
    }
    return object;
}

} // namespace

ProjectDocumentReadResult ProjectDocumentV1::readObject(const QJsonObject& object) {
    ProjectDocumentReadResult result;
    const QString schema = object.value(QStringLiteral("schema")).toString();
    if (schema != schemaids::projectV1) {
        result.issues.append(issue(QStringLiteral("project.unsupported_schema"),
                                   QStringLiteral("Only ipcraft.project.v1 is accepted by the normal loader"),
                                   QStringLiteral("/schema")));
        return result;
    }

    ProjectDesign project;
    project.schema = schema;
    project.id = stringValue(object, QStringLiteral("id"));
    project.name = stringValue(object, QStringLiteral("name"));
    project.constraints = objectValue(object, QStringLiteral("constraints"));
    project.metadata = objectValue(object, QStringLiteral("metadata"));

    for (const QJsonValue& package : arrayValue(object, QStringLiteral("packages"))) {
        if (package.isObject()) {
            project.packages.append(packageRefFromJson(package.toObject()));
        }
    }
    for (const QJsonValue& component : arrayValue(object, QStringLiteral("components"))) {
        if (component.isObject()) {
            project.components.append(componentFromJson(component.toObject()));
        }
    }
    for (const QJsonValue& connection : arrayValue(object, QStringLiteral("connections"))) {
        if (connection.isObject()) {
            project.connections.append(connectionFromJson(connection.toObject()));
        }
    }
    for (const QJsonValue& topology : arrayValue(object, QStringLiteral("topologies"))) {
        if (topology.isObject()) {
            project.topologies.append(topologyFromJson(topology.toObject()));
        }
    }
    for (const QJsonValue& view : arrayValue(object, QStringLiteral("views"))) {
        if (view.isObject()) {
            project.views.append(viewFromJson(view.toObject()));
        }
    }
    for (const QJsonValue& diagnostic : arrayValue(object, QStringLiteral("diagnostics"))) {
        if (diagnostic.isObject()) {
            project.diagnostics.append(diagnostic.toObject());
        }
    }
    for (const QJsonValue& artifact : arrayValue(object, QStringLiteral("artifacts"))) {
        if (artifact.isObject()) {
            project.artifacts.append(artifact.toObject());
        }
    }
    for (const QJsonValue& extension : arrayValue(object, QStringLiteral("extensions"))) {
        if (extension.isObject()) {
            project.extensions.append(extensionBlockFromJson(extension.toObject()));
        }
    }

    result.project = project;
    result.issues = validateProjectDesign(project);
    result.success = result.issues.isEmpty();
    return result;
}

QJsonObject ProjectDocumentV1::writeObject(const ProjectDesign& project) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::projectV1);
    object.insert(QStringLiteral("id"), project.id);
    object.insert(QStringLiteral("name"), project.name);

    QJsonArray packages;
    for (const PackageRef& package : project.packages) {
        packages.append(packageRefToJson(package));
    }
    object.insert(QStringLiteral("packages"), packages);

    QJsonArray components;
    for (const ComponentInstance& component : project.components) {
        components.append(componentToJson(component));
    }
    object.insert(QStringLiteral("components"), components);

    QJsonArray connections;
    for (const Connection& connection : project.connections) {
        connections.append(connectionToJson(connection));
    }
    object.insert(QStringLiteral("connections"), connections);

    QJsonArray topologies;
    for (const TopologyGraph& topology : project.topologies) {
        topologies.append(topologyToJson(topology));
    }
    object.insert(QStringLiteral("topologies"), topologies);

    if (!project.constraints.isEmpty()) {
        object.insert(QStringLiteral("constraints"), project.constraints);
    }

    QJsonArray views;
    for (const ViewDocument& view : project.views) {
        views.append(viewToJson(view));
    }
    object.insert(QStringLiteral("views"), views);

    QJsonArray diagnostics;
    for (const QJsonObject& diagnostic : project.diagnostics) {
        diagnostics.append(diagnostic);
    }
    object.insert(QStringLiteral("diagnostics"), diagnostics);

    QJsonArray artifacts;
    for (const QJsonObject& artifact : project.artifacts) {
        artifacts.append(artifact);
    }
    object.insert(QStringLiteral("artifacts"), artifacts);

    QJsonArray extensions;
    for (const ExtensionBlock& extension : project.extensions) {
        extensions.append(extensionBlockToJson(extension));
    }
    object.insert(QStringLiteral("extensions"), extensions);

    if (!project.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), project.metadata);
    }
    return object;
}

} // namespace ipcraft::core
```

- [ ] **Step 4: Run the project document test and commit**

Run:

```bash
xmake -P qt run ipcraft_project_document_v1_foundation_test
```

Expected: PASS with `ipcraft_project_document_v1_foundation_test passed`.

Commit:

```bash
git add qt/inc/ipcraft/core/project_document_v1.h qt/src/ipcraft/core/project_document_v1.cpp qt/test/ipcraft_project_document_v1_foundation_test.cpp qt/xmake.lua
git commit -m "feat: add ipcraft project v1 structural document"
```

## Task 4: ProjectPatch Foundation

**Files:**
- Create: `qt/test/ipcraft_patch_foundation_test.cpp`
- Create: `qt/inc/ipcraft/core/project_patch.h`
- Create: `qt/src/ipcraft/core/project_patch.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing patch foundation test**

Create `qt/test/ipcraft_patch_foundation_test.cpp`:

```cpp
#include "ipcraft/core/project_patch.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasCode(const QVector<ipcraft::core::ValidationIssue>& issues, const QString& code) {
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

ipcraft::core::ProjectDesign minimalProject() {
    ipcraft::core::ProjectDesign project;
    project.schema = ipcraft::schemaids::projectV1;
    project.id = QStringLiteral("proj_uart_min");
    project.name = QStringLiteral("Minimal UART");
    project.packages.append({QStringLiteral("vendor.uart16550"), QStringLiteral("1.0.0")});
    ipcraft::core::ComponentInstance uart;
    uart.id = QStringLiteral("uart0");
    uart.type = QStringLiteral("uart16550");
    uart.packageRef = QStringLiteral("vendor.uart16550@1.0.0");
    uart.config.insert(QStringLiteral("baud"), 115200);
    project.components.append(uart);
    return project;
}

QJsonObject setBaudPatchJson() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::patchV1},
        {QStringLiteral("id"), QStringLiteral("patch_set_uart_baud")},
        {QStringLiteral("description"), QStringLiteral("Set UART baud rate")},
        {QStringLiteral("ops"), QJsonArray{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("set_config")},
                        {QStringLiteral("target"), QStringLiteral("component:uart0")},
                        {QStringLiteral("path"), QStringLiteral("/baud")},
                        {QStringLiteral("value"), 921600}}
        }}
    };
}

void testPatchParsesAndSerializes() {
    const ipcraft::core::ProjectPatchReadResult result =
        ipcraft::core::ProjectPatchApi::readObject(setBaudPatchJson());
    require(result.success, "patch should parse");
    require(result.patch.ops.size() == 1, "patch op should parse");
    require(result.patch.ops.first().op == QStringLiteral("set_config"), "op name should parse");

    const QJsonObject written = ipcraft::core::ProjectPatchApi::writeObject(result.patch);
    require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::patchV1,
            "patch writer should emit schema");
    require(written.value(QStringLiteral("ops")).toArray().size() == 1,
            "patch writer should emit ops");
}

void testPatchAppliesSetConfigTransactionally() {
    ipcraft::core::ProjectDesign project = minimalProject();
    const ipcraft::core::ProjectPatch patch =
        ipcraft::core::ProjectPatchApi::readObject(setBaudPatchJson()).patch;

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(result.success, "set_config patch should apply");
    require(result.project.components.first().config.value(QStringLiteral("baud")).toInt() == 921600,
            "component config should change");
}

void testPatchRejectsInvalidTargetWithoutMutation() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("target"), QStringLiteral("component:missing"));
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::ProjectPatch patch =
        ipcraft::core::ProjectPatchApi::readObject(patchJson).patch;
    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, patch);
    require(!result.success, "invalid target should fail");
    require(hasCode(result.issues, QStringLiteral("patch.target_not_found")),
            "invalid target should report stable issue code");
    require(result.project.components.first().config.value(QStringLiteral("baud")).toInt() == 115200,
            "failed patch should not mutate project");
}

void testPatchRejectsLayoutFieldInsideComponentConfig() {
    ipcraft::core::ProjectDesign project = minimalProject();
    QJsonObject patchJson = setBaudPatchJson();
    QJsonArray ops = patchJson.value(QStringLiteral("ops")).toArray();
    QJsonObject op = ops.first().toObject();
    op.insert(QStringLiteral("path"), QStringLiteral("/x"));
    op.insert(QStringLiteral("value"), 100);
    ops.replace(0, op);
    patchJson.insert(QStringLiteral("ops"), ops);

    const ipcraft::core::PatchApplyResult result =
        ipcraft::core::applyPatch(project, ipcraft::core::ProjectPatchApi::readObject(patchJson).patch);
    require(!result.success, "layout key in component config should fail");
    require(hasCode(result.issues, QStringLiteral("patch.layout_in_component_config")),
            "layout key should report stable issue code");
}

} // namespace

int main() {
    testPatchParsesAndSerializes();
    testPatchAppliesSetConfigTransactionally();
    testPatchRejectsInvalidTargetWithoutMutation();
    testPatchRejectsLayoutFieldInsideComponentConfig();
    std::cout << "ipcraft_patch_foundation_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the xmake target and verify it fails**

Add this target to `qt/xmake.lua`:

```lua
target("ipcraft_patch_foundation_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_patch_foundation_test.cpp")
    add_files("src/ipcraft/core/project_patch.cpp")
    add_files("src/ipcraft/core/project_design.cpp")
    add_files("inc/ipcraft/core/project_patch.h")
    add_files("inc/ipcraft/core/project_design.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_patch_foundation_test passed"
    })
```

Run:

```bash
xmake -P qt run ipcraft_patch_foundation_test
```

Expected: FAIL because `ipcraft/core/project_patch.h` does not exist.

- [ ] **Step 3: Add the patch API**

Create `qt/inc/ipcraft/core/project_patch.h`:

```cpp
#pragma once

#include "ipcraft/core/project_design.h"

#include <QJsonValue>

namespace ipcraft::core {

struct PatchOperation {
    QString op;
    QString target;
    QString path;
    QJsonValue value;
    QJsonObject payload;
};

struct ProjectPatch {
    QString schema;
    QString id;
    QString description;
    QVector<PatchOperation> ops;
    QJsonObject metadata;
};

struct ProjectPatchReadResult {
    bool success = false;
    ProjectPatch patch;
    QVector<ValidationIssue> issues;
};

struct PatchApplyResult {
    bool success = false;
    ProjectDesign project;
    QVector<ValidationIssue> issues;
};

class ProjectPatchCodec {
public:
    static ProjectPatchReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectPatch& patch);
};

class ProjectPatchApi {
public:
    static ProjectPatchReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectPatch& patch);
};

PatchApplyResult applyPatch(const ProjectDesign& project, const ProjectPatch& patch);

} // namespace ipcraft::core
```

Create `qt/src/ipcraft/core/project_patch.cpp`:

```cpp
#include "ipcraft/core/project_patch.h"

#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QSet>

namespace ipcraft::core {
namespace {

ValidationIssue issue(const QString& code, const QString& message, const QString& path) {
    return {code, message, path};
}

bool isForbiddenComponentConfigPath(const QString& path) {
    static const QSet<QString> forbidden = {
        QStringLiteral("/x"),
        QStringLiteral("/y"),
        QStringLiteral("/node_width"),
        QStringLiteral("/node_height"),
        QStringLiteral("/collapsed"),
        QStringLiteral("/waypoints"),
        QStringLiteral("/zoom"),
        QStringLiteral("/pan")
    };
    return forbidden.contains(path);
}

int componentIndexById(const ProjectDesign& project, const QString& componentId) {
    for (int index = 0; index < project.components.size(); ++index) {
        if (project.components.at(index).id == componentId) {
            return index;
        }
    }
    return -1;
}

QString componentIdFromTarget(const QString& target) {
    const QString prefix = QStringLiteral("component:");
    return target.startsWith(prefix) ? target.mid(prefix.size()) : QString();
}

PatchOperation operationFromJson(const QJsonObject& object) {
    PatchOperation operation;
    operation.op = object.value(QStringLiteral("op")).toString();
    operation.target = object.value(QStringLiteral("target")).toString();
    operation.path = object.value(QStringLiteral("path")).toString();
    operation.value = object.value(QStringLiteral("value"));
    operation.payload = object;
    return operation;
}

QJsonObject operationToJson(const PatchOperation& operation) {
    QJsonObject object = operation.payload;
    object.insert(QStringLiteral("op"), operation.op);
    if (!operation.target.isEmpty()) {
        object.insert(QStringLiteral("target"), operation.target);
    }
    if (!operation.path.isEmpty()) {
        object.insert(QStringLiteral("path"), operation.path);
    }
    if (!operation.value.isUndefined()) {
        object.insert(QStringLiteral("value"), operation.value);
    }
    return object;
}

} // namespace

ProjectPatchReadResult ProjectPatchCodec::readObject(const QJsonObject& object) {
    ProjectPatchReadResult result;
    if (object.value(QStringLiteral("schema")).toString() != schemaids::patchV1) {
        result.issues.append(issue(QStringLiteral("patch.unsupported_schema"),
                                   QStringLiteral("Patch schema must be ipcraft.patch.v1"),
                                   QStringLiteral("/schema")));
        return result;
    }
    result.patch.schema = schemaids::patchV1;
    result.patch.id = object.value(QStringLiteral("id")).toString();
    result.patch.description = object.value(QStringLiteral("description")).toString();
    result.patch.metadata = object.value(QStringLiteral("metadata")).toObject();
    const QJsonArray ops = object.value(QStringLiteral("ops")).toArray();
    if (ops.isEmpty()) {
        result.issues.append(issue(QStringLiteral("patch.empty_ops"),
                                   QStringLiteral("Patch must contain at least one op"),
                                   QStringLiteral("/ops")));
        return result;
    }
    for (const QJsonValue& value : ops) {
        if (!value.isObject()) {
            result.issues.append(issue(QStringLiteral("patch.invalid_op"),
                                       QStringLiteral("Patch op must be an object"),
                                       QStringLiteral("/ops")));
            return result;
        }
        result.patch.ops.append(operationFromJson(value.toObject()));
    }
    result.success = true;
    return result;
}

QJsonObject ProjectPatchCodec::writeObject(const ProjectPatch& patch) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::patchV1);
    if (!patch.id.isEmpty()) {
        object.insert(QStringLiteral("id"), patch.id);
    }
    if (!patch.description.isEmpty()) {
        object.insert(QStringLiteral("description"), patch.description);
    }
    QJsonArray ops;
    for (const PatchOperation& operation : patch.ops) {
        ops.append(operationToJson(operation));
    }
    object.insert(QStringLiteral("ops"), ops);
    if (!patch.metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), patch.metadata);
    }
    return object;
}

ProjectPatchReadResult ProjectPatchApi::readObject(const QJsonObject& object) {
    return ProjectPatchCodec::readObject(object);
}

QJsonObject ProjectPatchApi::writeObject(const ProjectPatch& patch) {
    return ProjectPatchCodec::writeObject(patch);
}

PatchApplyResult applyPatch(const ProjectDesign& project, const ProjectPatch& patch) {
    PatchApplyResult result;
    result.project = project;
    ProjectDesign candidate = project;

    for (int index = 0; index < patch.ops.size(); ++index) {
        const PatchOperation& operation = patch.ops.at(index);
        const QString path = QStringLiteral("/ops/%1").arg(index);
        if (operation.op != QStringLiteral("set_config")) {
            result.issues.append(issue(QStringLiteral("patch.unsupported_op"),
                                       QStringLiteral("Foundation patch applier supports set_config"),
                                       path + QStringLiteral("/op")));
            return result;
        }
        const QString componentId = componentIdFromTarget(operation.target);
        if (componentId.isEmpty()) {
            result.issues.append(issue(QStringLiteral("patch.invalid_target"),
                                       QStringLiteral("set_config target must be component:<id>"),
                                       path + QStringLiteral("/target")));
            return result;
        }
        const int componentIndex = componentIndexById(candidate, componentId);
        if (componentIndex < 0) {
            result.issues.append(issue(QStringLiteral("patch.target_not_found"),
                                       QStringLiteral("Patch target component was not found"),
                                       path + QStringLiteral("/target")));
            return result;
        }
        if (isForbiddenComponentConfigPath(operation.path)) {
            result.issues.append(issue(QStringLiteral("patch.layout_in_component_config"),
                                       QStringLiteral("Layout keys cannot be written to component config"),
                                       path + QStringLiteral("/path")));
            return result;
        }
        if (!operation.path.startsWith(QLatin1Char('/')) || operation.path.count(QLatin1Char('/')) != 1) {
            result.issues.append(issue(QStringLiteral("patch.unsupported_path"),
                                       QStringLiteral("Foundation set_config supports one-level JSON Pointer paths"),
                                       path + QStringLiteral("/path")));
            return result;
        }
        const QString key = operation.path.mid(1);
        candidate.components[componentIndex].config.insert(key, operation.value);
    }

    const QVector<ValidationIssue> validationIssues = validateProjectDesign(candidate);
    if (!validationIssues.isEmpty()) {
        result.issues = validationIssues;
        return result;
    }
    result.project = candidate;
    result.success = true;
    return result;
}

} // namespace ipcraft::core
```

- [ ] **Step 4: Run the patch test and commit**

Run:

```bash
xmake -P qt run ipcraft_patch_foundation_test
```

Expected: PASS with `ipcraft_patch_foundation_test passed`.

Commit:

```bash
git add qt/inc/ipcraft/core/project_patch.h qt/src/ipcraft/core/project_patch.cpp qt/test/ipcraft_patch_foundation_test.cpp qt/xmake.lua
git commit -m "feat: add ipcraft patch foundation"
```

## Task 5: Foundation Verification Bundle

**Files:**
- Modify only if a previous task missed build wiring: `qt/xmake.lua`

- [ ] **Step 1: Run all foundation tests**

Run:

```bash
xmake -P qt run ipcraft_architecture_foundation_scan_test
xmake -P qt run ipcraft_project_design_foundation_test
xmake -P qt run ipcraft_project_document_v1_foundation_test
xmake -P qt run ipcraft_patch_foundation_test
```

Expected:

```text
ipcraft_architecture_foundation_scan_test passed
ipcraft_project_design_foundation_test passed
ipcraft_project_document_v1_foundation_test passed
ipcraft_patch_foundation_test passed
```

- [ ] **Step 2: Run existing nearby contract tests**

Run:

```bash
xmake -P qt run ipcraft_diagnostics_test
xmake -P qt run ipcraft_project_model_test
xmake -P qt run ipcraft_contract_examples_test
```

Expected:

```text
ipcraft_diagnostics_test passed
ipcraft_project_model_test passed
ipcraft_contract_examples_test passed
```

- [ ] **Step 3: Scan for forbidden foundation patterns in new core**

Run:

```bash
rg -n "QWidget|QGraphics|NodeEditorWidget|ModuleRegistry|mesh_router|ipcraft\\.noc\\.project\\.v1|vendor\\.meshnoc" qt/inc/ipcraft/core qt/src/ipcraft/core
```

Expected: no output.

- [ ] **Step 4: Commit final build-wiring correction if needed**

If `qt/xmake.lua` required a correction during verification, commit only that correction:

```bash
git add qt/xmake.lua
git commit -m "build: wire ipcraft architecture foundation tests"
```

If no correction was required, do not create an empty commit.

## Self-Review

Spec coverage:

- T0001 is covered by Task 1 deletion map and scan.
- T0002 is covered by Task 1 deletion map entries for NoC schema, NoC package ids, and ordinary package relocation.
- T0100 is covered by Task 1 public schema matrix.
- T0101 is covered by Task 3 minimal UART, CPU -> NIC -> NoC, old schema rejection, attachment-connection rejection, extension preservation, and deterministic writer tests.
- T0102 is covered by Task 2 `ProjectDesign`, `ComponentInstance`, `InterfaceInstance`, `Connection`, `TopologyGraph`, `ViewDocument`, `ExtensionBlock`, diagnostics/artifacts JSON placeholders as data records, and validation helpers.
- T0103 is covered by Task 3 `ProjectDocumentV1`.
- T0104 is covered by Task 4 `ProjectPatch` and foundation `set_config` application.

Known follow-up scope:

- Package registry, component/interface/rule schemas, topology expansion, resolution/provenance, tool protocol, domain services, UI, NoC capability, example package conversion, package CLI, and final cleanup are intentionally outside this foundation plan and listed under Scope Check.

Placeholder scan:

- No task depends on missing product decisions.
- Every new file path is exact.
- Every command has expected output.
- Every code-changing step includes the concrete code or concrete patch content for that step.

Type consistency:

- `ProjectDesign`, `ComponentInstance`, `Connection`, `TopologyGraph`, `ExtensionBlock`, `ProjectDocumentV1`, `ProjectPatchApi`, and `applyPatch` names are consistent across tests and implementation steps.
- `ipcraft::schemaids::*` constants are used consistently with the schema names in `.specify`.
- Patch target grammar for this foundation uses `component:<componentId>`, matching `.specify` ref grammar.
