# Node 8 Historical Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Use reasoning effort `high` for implementation workers and `xhigh` for final stale-scan review.

**Goal:** Remove pre-v1 duplicate framework content and stale plugin/IP-core terminology from live code, maintained docs, tests, and build files after the Node 7 generator boundary is working.

**Architecture:** Keep the current runtime manifest/native plugin infrastructure names (`PluginRegistry`, `PluginDescriptor`, `plugin.json`, `FINEPAPER_PLUGIN_PATH`) because they still model runtime bundle/native-plugin metadata. Rename stale project/IP-instance state APIs and user-facing text that now refer to IP-core instances. Delete the duplicated root `framework/` generator and the unused `FrameworkPaths` resolver so the only live NoC generator path is `ipcores/finepaper-noc/generator`.

**Tech Stack:** C++23, Qt 6 Widgets, xmake, Ruby `spec_generator`, Ruby NoC/RaveNoC generator tests, repository stale scans with `rg`.

---

## Allowlist Policy

Live-code stale scans must distinguish old concrete-IP-as-plugin vocabulary from the still-existing runtime plugin infrastructure.

Allowed in live code for Node 8:

- `PluginRegistry`, `PluginDescriptor`, `PluginCommandDescriptor`, `PluginInstanceParameterDescriptor`, `plugin.json`, `native plugin`, and `FINEPAPER_PLUGIN_PATH`, when they refer to runtime manifests or future native feature plugins.
- `pluginId` inside `ModuleType` and registry APIs, as an internal runtime owner field. Renaming this is a larger cross-cutting API change and is not part of Node 8.
- Explicit `.fpproj` pre-v1 rejection guards/tests in `ProjectReader` and `projectdocument_test`, including old keys `plugins`, `plugin_state`, `ip_instances`, and module `plugin`.
- Archived historical plans/specs under `docs/superpowers/plans/2026-04-*`, `docs/superpowers/plans/2026-05-02-*`, `docs/superpowers/plans/2026-05-07-*`, `docs/superpowers/plans/2026-05-08-*`, and completed Node 1-7 plans.

Not allowed in live code/docs after Node 8:

- User-facing text such as "plugin graph input" for generation.
- Comments that say project files carry plugin ownership when they now carry IP-core ownership.
- `pluginstate.h`, `SetPluginStateParameterCommand`, or `onPluginStateParameterChanged` as live API/file names.
- `framework/`, `FrameworkPaths`, `FRAMEWORK_PATH`, or root-framework generator references outside historical docs.
- `finepaper-plugin-graph-v1`, `generic_graph_v1`, `GraphJsonFlavor`, or `toJsonDocument(` in live code.

---

## File Structure

- Delete `framework/**`: stale duplicate of the Finepaper NoC generator.
- Delete `qt/inc/common/frameworkpaths.h`: unused framework-root resolver API.
- Delete `qt/src/common/frameworkpaths.cpp`: unused framework-root resolver implementation.
- Modify `qt/xmake.lua`: remove `frameworkpaths.cpp` source entries from tests/targets.
- Move `qt/inc/project/pluginstate.h` to `qt/inc/project/ipinstancestate.h`.
- Move `qt/inc/commands/setpluginstateparametercommand.h` to `qt/inc/commands/setipinstanceparametercommand.h`.
- Move `qt/src/commands/setpluginstateparametercommand.cpp` to `qt/src/commands/setipinstanceparametercommand.cpp`.
- Modify all includes that referenced `project/pluginstate.h` or `commands/setpluginstateparametercommand.h`.
- Modify `qt/inc/panels/propertypanel.h` and `qt/src/panels/propertypanel.cpp`: rename stale slot/command usage to IP-instance terminology.
- Modify `qt/inc/plugins/pluginprojectadapter.h` and `qt/src/plugins/manifestpluginprojectadapter.cpp`: rename comments and section field from stale project-state wording to IP-core instance parameter wording while keeping the runtime manifest adapter concept.
- Modify affected tests: `qt/test/propertypanel_test.cpp`, `qt/test/plugin_test.cpp`, `qt/test/projectdocument_test.cpp`, `qt/test/validation_test.cpp`, and any include fanout found by `rg`.
- Modify `qt/src/app/mainwindow.cpp`: update generation tooltip and load comments.
- Modify `qt/inc/validation/validationmanager.h` and `qt/src/validation/validationmanager.cpp`: replace "framework checks" wording with "structural checks".
- Modify `qt/src/modules/moduleregistry.cpp` and selected tests/docs: replace stale user-facing "plugin-owned module" wording with runtime/IP-core wording where it does not refer to runtime plugin infrastructure.
- Modify `qt/doc/README.md`, `qt/doc/architecture.md`, and `spec_generator/README.md`: align maintained docs with active IP Catalog / active workspace / exporter reality.
- Create/modify no files under ignored `qt/inc/ipcore` or `qt/src/ipcore` in this node. If that changes, force-add new files because `.gitignore` ignores `ipcore/`.

---

## Task 8.1: Baseline Stale Inventory

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-8-historical-cleanup.md`

- [x] **Step 1: Gather read-only stale-term findings**

Use explorers or local scans to classify current stale hits into:

- must-clean live code/tests;
- explicit pre-v1 project rejection tests;
- historical docs allowlist;
- runtime plugin infrastructure allowed for Node 8.

Required scans:

```bash
rg -n "plugin_state|finepaper-plugin-graph-v1|generic_graph_v1|finepaper\\.extension\\.v1|ip_instance|plugins/ravenoc|plugins/noc/generator|plugin graph|plugin ownership|FrameworkPaths|FRAMEWORK_PATH|framework/" \
  qt spec_generator ipcores generated docs \
  -g '!**/*.svg'

rg -n "pluginstate|SetPluginStateParameterCommand|onPluginStateParameterChanged|plugin-owned project state" \
  qt/inc qt/src qt/test \
  -g '!**/*.svg'
```

Expected before cleanup: hits exist in `framework/**`, `FrameworkPaths`, project-state file/command names, maintained docs, and explicit project rejection tests.

---

## Task 8.2: Remove Duplicate Root Framework

**Files:**

- Delete: `framework/**`
- Delete: `qt/inc/common/frameworkpaths.h`
- Delete: `qt/src/common/frameworkpaths.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Confirm active generators do not use root framework**

Run:

```bash
rg -n "framework/|FRAMEWORK_PATH|resolveFrameworkPath|resolveTemplatePath|bin/generate|generator/template" \
  qt spec_generator ipcores generated \
  -g '!**/*.svg'
```

Expected before deletion:

- `generated/ipcores/*/plugin.json` and `ipcores/*/ipcore.yml` point to `generator/bin/generate` under each IP-core source package.
- `qt/src/common/frameworkpaths.cpp` is the only active source probing root `framework/`.
- No test or production code invokes `framework/bin/generate`.

- [x] **Step 2: Delete stale framework tree**

Run:

```bash
git rm -r framework
```

Expected: staged deletions for `framework/bin`, `framework/src`, `framework/template`, `framework/test`, `framework/examples`, and `framework/output`.

- [x] **Step 3: Delete unused FrameworkPaths API**

Run:

```bash
git rm qt/inc/common/frameworkpaths.h qt/src/common/frameworkpaths.cpp
```

Expected: both files staged for deletion.

- [x] **Step 4: Remove frameworkpaths sources from xmake**

In `qt/xmake.lua`, remove every occurrence of:

```lua
"src/**/frameworkpaths.cpp",
```

and every explicit occurrence of:

```lua
add_files("src/common/frameworkpaths.cpp")
```

Do not remove unrelated source entries.

- [x] **Step 5: Verify no live FrameworkPaths references remain**

Run:

```bash
rg -n "FrameworkPaths|resolveFrameworkPath|resolveTemplatePath|FRAMEWORK_PATH|framework/" \
  qt spec_generator ipcores generated \
  -g '!**/*.svg'
```

Expected: no hits except generator argument strings such as `generator/template` that do not reference root `framework/`.

---

## Task 8.3: Rename IP-Instance State APIs

**Files:**

- Move: `qt/inc/project/pluginstate.h` -> `qt/inc/project/ipinstancestate.h`
- Move: `qt/inc/commands/setpluginstateparametercommand.h` -> `qt/inc/commands/setipinstanceparametercommand.h`
- Move: `qt/src/commands/setpluginstateparametercommand.cpp` -> `qt/src/commands/setipinstanceparametercommand.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/inc/project/projectstateservice.h`
- Modify: `qt/inc/project/projectipservice.h`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/inc/app/generationartifacts.h`
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/inc/ipcore/ipcoregraphexporter.h`
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: any additional include fanout found by `rg`

- [x] **Step 1: Move project IP-instance state header**

Run:

```bash
git mv qt/inc/project/pluginstate.h qt/inc/project/ipinstancestate.h
```

Then replace includes:

```bash
perl -0pi -e 's#"project/pluginstate.h"#"project/ipinstancestate.h"#g' \
  $(rg -l '"project/pluginstate.h"' qt/inc qt/src qt/test)
```

Expected:

```bash
rg -n "project/pluginstate\\.h|pluginstate\\.h" qt/inc qt/src qt/test
```

prints no results.

- [x] **Step 2: Move and rename undo command**

Run:

```bash
git mv qt/inc/commands/setpluginstateparametercommand.h qt/inc/commands/setipinstanceparametercommand.h
git mv qt/src/commands/setpluginstateparametercommand.cpp qt/src/commands/setipinstanceparametercommand.cpp
```

In the moved header, rename the class to:

```cpp
// SetIpInstanceParameterCommand changes one IP-instance parameter with undo support.
#pragma once

#include "commands/command.h"
#include "project/projectstateservice.h"

#include <QJsonValue>
#include <QString>

class SetIpInstanceParameterCommand final : public Command {
public:
    SetIpInstanceParameterCommand(ProjectStateService* stateService,
                                  QString ipcoreId,
                                  QString instanceId,
                                  QString section,
                                  QString name,
                                  QJsonValue newValue);
    void execute() override;
    void undo() override;

private:
    ProjectStateService* m_stateService;
    QString m_ipcoreId;
    QString m_instanceId;
    QString m_section;
    QString m_name;
    QJsonValue m_newValue;
    QJsonValue m_oldValue;
};
```

In the moved `.cpp`, include the new header and rename all definitions from `SetPluginStateParameterCommand::` to `SetIpInstanceParameterCommand::`.

- [x] **Step 3: Update property panel command and slot names**

In `qt/inc/panels/propertypanel.h`, replace:

```cpp
void onPluginStateParameterChanged(const QString& pluginId,
                                   const QString& instanceId,
                                   const QString& section,
                                   const QString& name);
```

with:

```cpp
void onIpInstanceParameterChanged(const QString& ipcoreId,
                                  const QString& instanceId,
                                  const QString& section,
                                  const QString& name);
```

In `qt/src/panels/propertypanel.cpp`:

- replace include `commands/setpluginstateparametercommand.h` with `commands/setipinstanceparametercommand.h`;
- connect `ProjectStateService::parameterChanged` to `PropertyPanel::onIpInstanceParameterChanged`;
- replace `SetPluginStateParameterCommand` with `SetIpInstanceParameterCommand`;
- rename local variables in the IP instance section renderer from `pluginId` to `ipcoreId` where they represent `section.ipcoreId`.

- [x] **Step 4: Verify stale command names are gone**

Run:

```bash
rg -n "pluginstate|SetPluginStateParameterCommand|onPluginStateParameterChanged|setpluginstateparametercommand" \
  qt/inc qt/src qt/test qt/xmake.lua \
  -g '!**/*.svg'
```

Expected: no results.

---

## Task 8.4: Rename IP-Instance Parameter Adapter Types

**Files:**

- Move: `qt/inc/plugins/pluginprojectadapter.h` -> `qt/inc/project/ipinstanceparameteradapter.h`
- Move: `qt/src/plugins/manifestpluginprojectadapter.cpp` -> `qt/src/project/manifestipinstanceparameteradapter.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/inc/panels/propertypanel.h`
- Modify: `qt/src/panels/propertypanel.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/test/propertypanel_test.cpp`
- Modify: `qt/xmake.lua`

- [x] **Step 1: Move adapter files**

Run:

```bash
git mv qt/inc/plugins/pluginprojectadapter.h qt/inc/project/ipinstanceparameteradapter.h
git mv qt/src/plugins/manifestpluginprojectadapter.cpp qt/src/project/manifestipinstanceparameteradapter.cpp
```

- [x] **Step 2: Replace adapter declarations**

In `qt/inc/project/ipinstanceparameteradapter.h`, define:

```cpp
// IP instance parameter adapters expose manifest-declared project state to the core UI.
#pragma once

#include "graph/parameter.h"
#include "plugins/plugindescriptor.h"

#include <QString>
#include <QVector>

struct IpInstanceParameterField {
    QString name;
    QString label;
    QString description;
    QString type;
    Parameter::Value defaultValue = QString();
    QVector<PluginInstanceParameterChoice> choices;
    bool configurable = true;
};

struct IpInstanceParameterSection {
    QString ipcoreId;
    QString instanceId;
    QString id;
    QString label;
    bool expandedByDefault = true;
    QVector<IpInstanceParameterField> fields;
};

class IIpInstanceParameterAdapter {
public:
    virtual ~IIpInstanceParameterAdapter() = default;
    virtual QVector<IpInstanceParameterSection> parameterSections() const = 0;
};

class ManifestIpInstanceParameterAdapter final : public IIpInstanceParameterAdapter {
public:
    explicit ManifestIpInstanceParameterAdapter(PluginDescriptor plugin);
    QVector<IpInstanceParameterSection> parameterSections() const override;

private:
    PluginDescriptor m_plugin;
};
```

- [x] **Step 3: Update implementation**

In `qt/src/project/manifestipinstanceparameteradapter.cpp`, use:

```cpp
// ManifestIpInstanceParameterAdapter exposes plugin.json instance_parameters as IP instance parameters.
#include "project/ipinstanceparameteradapter.h"
```

Rename implementation symbols:

- `ManifestPluginProjectAdapter` -> `ManifestIpInstanceParameterAdapter`
- `PluginParameterSection` -> `IpInstanceParameterSection`
- `PluginParameterField` -> `IpInstanceParameterField`
- `section.pluginId` -> `section.ipcoreId`

- [x] **Step 4: Update app, property panel, and tests**

Replace includes:

```bash
perl -0pi -e 's#"plugins/pluginprojectadapter.h"#"project/ipinstanceparameteradapter.h"#g' \
  $(rg -l '"plugins/pluginprojectadapter.h"' qt/inc qt/src qt/test)
```

Then update type names in `qt/inc`, `qt/src`, and `qt/test`:

```bash
perl -0pi -e 's/IPluginProjectAdapter/IIpInstanceParameterAdapter/g;
              s/ManifestPluginProjectAdapter/ManifestIpInstanceParameterAdapter/g;
              s/PluginParameterSection/IpInstanceParameterSection/g;
              s/PluginParameterField/IpInstanceParameterField/g;
              s/\\.pluginId/.ipcoreId/g' \
  $(rg -l "IPluginProjectAdapter|ManifestPluginProjectAdapter|PluginParameterSection|PluginParameterField|\\.pluginId" qt/inc qt/src qt/test)
```

Manually review the replacements. Revert accidental `.pluginId` replacements in `ModuleType`, `PluginDescriptor`, `PluginRegistry`, tests for runtime owner fields, and runtime manifest code. Only `IpInstanceParameterSection` should use `.ipcoreId`.

- [x] **Step 5: Update xmake adapter paths**

In `qt/xmake.lua`, replace:

```lua
add_files("src/plugins/manifestpluginprojectadapter.cpp")
```

with:

```lua
add_files("src/project/manifestipinstanceparameteradapter.cpp")
```

Replace explicit header entries:

```lua
"inc/**/pluginprojectadapter.h",
```

with:

```lua
"inc/project/ipinstanceparameteradapter.h",
```

- [x] **Step 6: Verify adapter stale names are gone**

Run:

```bash
rg -n "pluginprojectadapter|PluginProjectAdapter|PluginParameterField|PluginParameterSection|ManifestPluginProjectAdapter|IPluginProjectAdapter|plugin-owned project state" \
  qt/inc qt/src qt/test qt/xmake.lua \
  -g '!**/*.svg'
```

Expected: no results.

---

## Task 8.5: Maintained Docs And User-Facing Text

**Files:**

- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`
- Modify: `spec_generator/README.md`

- [x] **Step 1: Update user-facing generation text**

In `qt/src/app/mainwindow.cpp`, replace:

```cpp
m_generateAction->setToolTip("Write plugin graph input and a project snapshot, then generate Verilog in a selected folder.");
```

with:

```cpp
m_generateAction->setToolTip("Write IP-core graph input and a project snapshot, then generate Verilog in a selected folder.");
```

Replace the load comment:

```cpp
// Project files carry plugin ownership and typed parameters, so load
```

with:

```cpp
// Project files carry IP-core ownership and typed parameters, so load
```

- [x] **Step 2: Update validation comments**

In `qt/inc/validation/validationmanager.h`, replace:

```cpp
// Runs framework checks and IP-provided DRC, then publishes merged results to the log panel.
```

with:

```cpp
// Runs structural checks and IP-provided DRC, then publishes merged results to the log panel.
```

In `qt/src/validation/validationmanager.cpp`, replace file header:

```cpp
// ValidationManager orchestrates framework checks and IP-provided DRC.
```

with:

```cpp
// ValidationManager orchestrates structural checks and IP-provided DRC.
```

- [x] **Step 3: Update runtime bundle wording**

In `qt/src/modules/moduleregistry.cpp`, replace warning strings:

```cpp
"No plugin-owned module definitions found."
"from plugin"
```

with:

```cpp
"No runtime-owned module definitions found."
"from runtime bundle"
```

In `qt/test/graph_test.cpp`, replace assertion messages that say "bundled NoC plugin" with "bundled NoC IP core".

- [x] **Step 4: Update maintained docs**

In `qt/doc/README.md`:

- replace the test count with current xmake test coverage;
- move JSON export wording from `Graph`/`graph_test` to `IpCoreGraphExporter`;
- replace `Palette` workflow with IP Catalog/internal module library wording;
- state that generation writes `finepaper-ipcore-graph-v1` JSON plus a `.fpproj` snapshot.

In `qt/doc/architecture.md`:

- replace `Palette` references with IP Catalog and active workspace model;
- update validation/DRC section to say `IpCoreGraphExporter` writes DRC input and DRC resolves from active workspace/catalog entry;
- update generation section to say it targets the active IP instance and rejects mismatched module ownership through the exporter.

In `spec_generator/README.md`, replace "Ruby framework" with "Ruby IP-core generators".

- [x] **Step 5: Maintained docs stale scan**

Run:

```bash
rg -n "plugin graph|plugin ownership|plugin-owned|Ruby framework|Palette|Graph::toJsonDocument|framework/" \
  qt/doc spec_generator/README.md qt/src/app/mainwindow.cpp qt/inc/validation qt/src/validation qt/src/modules qt/test/graph_test.cpp \
  -g '!**/*.svg'
```

Expected: no stale hits except allowed runtime plugin infrastructure names in docs.

---

## Task 8.6: Live Stale Scan

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-8-historical-cleanup.md`

- [x] **Step 1: Run hard live-code stale scan**

Run:

```bash
rg -n "finepaper-plugin-graph-v1|generic_graph_v1|GraphJsonFlavor|toJsonDocument\\(|FrameworkPaths|FRAMEWORK_PATH|framework/|pluginstate|SetPluginStateParameterCommand|onPluginStateParameterChanged|pluginprojectadapter|PluginProjectAdapter|PluginParameterField|PluginParameterSection|ManifestPluginProjectAdapter|IPluginProjectAdapter" \
  qt/inc qt/src qt/test spec_generator ipcores generated/ipcores \
  -g '!**/*.svg'
```

Expected: no results.

- [x] **Step 2: Run legacy project-field scan with explicit allowlist**

Run:

```bash
rg -n "plugin_state|ip_instances|\\\"plugin\\\"|QStringLiteral\\(\"plugin\"\\)" \
  qt/inc qt/src qt/test spec_generator ipcores generated/ipcores \
  -g '!**/*.svg'
```

Expected allowed hits only:

- `qt/src/project/projectreader.cpp`: explicit pre-v1 project rejection guards.
- `qt/test/projectdocument_test.cpp`: explicit writer-not-emitting and pre-v1 project rejection tests.

No generator/DRC input code may match this scan.

- [x] **Step 3: Run current-doc stale scan**

Run:

```bash
rg -n "finepaper-plugin-graph-v1|generic_graph_v1|finepaper\\.extension\\.v1|plugins/ravenoc|plugins/noc/generator|plugin_state|ip_instance|framework/" \
  qt/doc spec_generator/README.md docs/superpowers/plans/2026-05-09-ipcore-plugin-catalog-architecture.md docs/superpowers/specs/2026-05-09-ipcore-plugin-catalog-architecture-design.md \
  -g '!**/*.svg'
```

Expected:

- no hits in `qt/doc` or `spec_generator/README.md`;
- hits in the current umbrella plan/spec are acceptable only when they describe migration targets or completed historical cleanup requirements.

---

## Task 8.7: Verification

**Files:**

- Modify: none expected, unless verification reveals failures.

- [x] **Step 1: Run focused Qt tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt graph_test
```

Expected: all pass.

- [x] **Step 2: Build Qt app**

Run:

```bash
CCACHE_DISABLE=1 xmake build -P qt qt
```

Expected: build succeeds.

- [x] **Step 3: Run Ruby/spec checks**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --check
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected: all pass.

- [x] **Step 4: Run repository hygiene checks**

Run:

```bash
git diff --check
git status --short
```

Expected:

- `git diff --check` prints no output.
- `git status --short` does not show staged or modified `.codex`, `.superpowers/`, or `image.png`.

---

## Task 8.8: Review, Supervisor Preflight, And Archive

**Files:**

- Modify: `docs/superpowers/plans/2026-05-09-node-8-historical-cleanup.md`

- [x] **Step 1: Architecture/stale review**

Ask an `xhigh` reviewer to inspect:

- root `framework/**` is deleted and no live code references `FrameworkPaths`;
- active NoC/RaveNoC generation still resolves through `ipcores/*/generator`;
- IP-instance project state files/commands use IP-instance names;
- runtime plugin infrastructure names are either still meaningful or explicitly allowed;
- stale scans match the allowlist above;
- maintained docs reflect active IP Catalog/workspace/generation reality.

- [x] **Step 2: Supervisor preflight**

Send the standing supervisor:

- verification command results;
- stale scan output and allowlist assessment;
- `git diff --stat`;
- confirmation that `.codex/`, `.superpowers/`, and `image.png` are not staged.

- [x] **Step 3: Archive Node 8**

Run:

```bash
git status --short
git add docs/superpowers/plans/2026-05-09-node-8-historical-cleanup.md qt spec_generator
git add -u framework
git commit -m "archive: complete node-8 historical cleanup"
```

Expected:

- commit succeeds;
- `.codex`, `.superpowers/`, and `image.png` remain untracked and uncommitted;
- no new files under ignored `qt/inc/ipcore` or `qt/src/ipcore` are missed.

---

## Self-Review

- Spec coverage: This plan removes duplicate root `framework/`, removes unused framework path resolution, cleans live project/IP-instance stale names, updates maintained docs, and defines stale scan allowlists for historical docs and runtime plugin infrastructure.
- Scope control: This plan does not rename `PluginRegistry`, `PluginDescriptor`, `plugin.json`, `FINEPAPER_PLUGIN_PATH`, or `ModuleType::pluginId`; those remain runtime manifest/native plugin infrastructure and can be reconsidered in a later plugin-system pass.
- Test coverage: The plan runs affected Qt tests, builds the Qt app, runs Ruby generator/spec tests, checks generated artifact drift, performs stale scans, and requires xhigh review plus supervisor preflight before archive.
