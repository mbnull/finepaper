# IP Creation Tool Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert Finepaper into a saved-project-first IP creation tool with instance-owned graphs, project-level validate/generate, and IP core runtime terminology.

**Architecture:** Replace the Qt runtime manifest path from plugin vocabulary to IP core runtime vocabulary, then make project IP instances the unit of ownership for modules, exporter input, deletion, validation, and generation. Keep feature-extension concepts out of the current runtime path; `kind` remains catalog display metadata only.

**Tech Stack:** C++23, Qt Widgets, QSettings, xmake Qt test targets, Ruby `spec_generator`.

---

## File Structure

- Create `qt/inc/ipcore/ipcoreruntimedescriptor.h`: IP core runtime manifest structs, command descriptors, topology preset descriptors, and instance parameter descriptors.
- Create `qt/inc/ipcore/ipcoreruntimeregistry.h`: runtime discovery API and singleton.
- Create `qt/src/ipcore/ipcoreruntimeregistry.cpp`: `ipcore-runtime.json` parser, `FINEPAPER_IPCORE_PATH`, repository `generated/ipcores`, and QSettings path discovery.
- Create `qt/inc/ipcore/ipcorecommandrunner.h`: resolves generator and DRC process commands from `IpCatalogEntry`.
- Create `qt/src/ipcore/ipcorecommandrunner.cpp`: command descriptor substitution and validation.
- Create `qt/inc/ipcore/ipcoreruntimediagnostics.h`: startup diagnostic API for loaded IP core runtimes.
- Create `qt/src/ipcore/ipcoreruntimediagnostics.cpp`: startup log text without plugin terminology.
- Delete `qt/inc/plugins/plugindescriptor.h`: replaced by `ipcoreruntimedescriptor.h`.
- Delete `qt/inc/plugins/pluginregistry.h`: replaced by `ipcoreruntimeregistry.h`.
- Delete `qt/src/plugins/pluginregistry.cpp`: replaced by `ipcoreruntimeregistry.cpp`.
- Delete `qt/inc/plugins/generatorrunner.h`: replaced by `ipcorecommandrunner.h`.
- Delete `qt/src/plugins/generatorrunner.cpp`: replaced by `ipcorecommandrunner.cpp`.
- Delete `qt/inc/plugins/startupdiagnostics.h`: replaced by `ipcoreruntimediagnostics.h`.
- Delete `qt/src/plugins/startupdiagnostics.cpp`: replaced by `ipcoreruntimediagnostics.cpp`.
- Modify `qt/inc/modules/moduleregistry.h`: rename runtime loading entry points from plugin wording to IP core runtime wording.
- Modify `qt/src/modules/moduleregistry.cpp`: load module bundles from `IpCoreRuntimeDescriptor`.
- Modify `qt/inc/ipcore/ipcatalogservice.h`: catalog entries use `IpCoreRuntimeDescriptor`, `IpCoreInstanceParameterDescriptor`, and `IpCoreCommandDescriptor`.
- Modify `qt/src/ipcore/ipcatalogservice.cpp`: construct entries from IP core runtime descriptors.
- Modify `qt/inc/project/ipinstanceparameteradapter.h`: use `IpCoreRuntimeDescriptor`; rename `ManifestIpInstanceParameterAdapter` to `RuntimeIpInstanceParameterAdapter`.
- Create `qt/src/project/runtimeipinstanceparameteradapter.cpp`: renamed adapter implementation.
- Delete `qt/src/project/manifestipinstanceparameteradapter.cpp`: replaced by runtime-named source.
- Modify `qt/inc/app/mainwindow.h`: project-open state, AppSettings dependency, project launcher entry points, project-level generate/delete orchestration.
- Modify `qt/src/app/mainwindow.cpp`: saved-project-first lifecycle, runtime diagnostics, add/remove instance commands, project-level generate and validate wiring.
- Modify `qt/src/app/main.cpp`: show launcher when no `.fpproj` argument is supplied; open provided project paths directly.
- Create `qt/inc/app/appsettings.h`: thin wrapper around `QSettings` for app-local state.
- Create `qt/src/app/appsettings.cpp`: recent project, last directory, IP core path, window geometry, and generation output settings.
- Create `qt/inc/app/projectlauncher.h`: launcher dialog API for New Project, Open Project, and Recent Projects.
- Create `qt/src/app/projectlauncher.cpp`: non-editor startup UI backed by `AppSettings`.
- Modify `qt/inc/graph/module.h`: add `instanceId()` and `setInstanceId()`.
- Modify `qt/src/graph/module.cpp`: store, clone, and expose module instance ownership.
- Modify `qt/inc/project/projectdocument.h`: add `ProjectModuleRecord::instanceId`.
- Modify `qt/src/project/projectreader.cpp`: read graph module `instance` and reject missing malformed ownership.
- Modify `qt/src/project/projectwriter.cpp`: write graph module `instance`.
- Modify `qt/src/project/graphprojectserializer.cpp`: validate `{ipcore, instance}` ownership against `ipcore_state`, preserve module instance ownership on save/load.
- Modify `qt/inc/nodeeditor/nodeeditorentityfactory.h`: pass `instanceId` into module creation.
- Modify `qt/src/nodeeditor/nodeeditorentityfactory.cpp`: stamp created modules with both owner fields.
- Modify `qt/src/nodeeditor/nodeeditorwidget.cpp`: pass scoped payload instance into the factory.
- Modify `qt/inc/commands/addmodulecommand.h`: validate expected instance ownership.
- Modify `qt/src/commands/addmodulecommand.cpp`: reject mismatched instance ownership.
- Modify `qt/inc/topology/topologypresetbuilder.h`: add `TopologyPresetRequest::instanceId`.
- Modify `qt/src/topology/topologypresetbuilder.cpp`: stamp topology modules with instance ownership.
- Modify `qt/src/commands/topologypresetcommand.cpp`: preserve request instance ownership on execute and undo.
- Modify `qt/src/ipcore/ipcoregraphexporter.cpp`: export one `{ipcoreId, instanceId}` graph subset.
- Create `qt/inc/commands/removeipinstancecommand.h`: undoable project-level IP instance deletion.
- Create `qt/src/commands/removeipinstancecommand.cpp`: remove/restore state record, owned modules, incident connections, and active selection.
- Modify `qt/inc/project/projectstateservice.h`: indexed insert/take helpers for undoable state mutations.
- Modify `qt/src/project/projectstateservice.cpp`: implement indexed state helpers.
- Modify `qt/inc/project/projectipservice.h`: replace `ensureInstanceForIpcore` with create/select helpers that allow multiple instances.
- Modify `qt/src/project/projectipservice.cpp`: generate unique instance IDs, remove `kind == noc` project restriction, keep `kind` as metadata.
- Modify `qt/inc/panels/ipcatalogpanel.h`: add remove-instance and workspace-tool intent signals.
- Modify `qt/src/panels/ipcatalogpanel.cpp`: emit remove/tool intents; do not mutate graph, state, run DRC, or run generation directly.
- Modify `qt/src/ipcore/iptoolsmodel.cpp`: return active-instance editing helpers only; remove Generate and DRC entries.
- Create `qt/inc/validation/projectvalidationrunner.h`: project-level validation runner API.
- Create `qt/src/validation/projectvalidationrunner.cpp`: structural validation once, DRC or warning per project IP instance.
- Modify `qt/inc/validation/validationmanager.h`: depend on project-level validation runner instead of active workspace DRC only.
- Modify `qt/src/validation/validationmanager.cpp`: publish project-level results with instance context.
- Create `qt/inc/app/projectgenerationrunner.h`: project-level generation runner API.
- Create `qt/src/app/projectgenerationrunner.cpp`: save required, iterate instances, export JSON per instance, run generator commands, write manifests.
- Modify `qt/inc/app/generationartifacts.h`: add project generation manifest helpers.
- Modify `qt/src/app/generationartifacts.cpp`: write generation manifest and project snapshot under the project output root.
- Modify `qt/inc/connection/connectionruleservice.h`: rename `ConnectionRuleLayer::FeaturePlugin` to `ConnectionRuleLayer::EditorRule`.
- Modify `qt/src/connection/connectionruleservice.cpp`: rename editor-time declarative rule helpers and diagnostics.
- Modify generated runtime artifacts under `generated/ipcores/*`: replace `plugin.json` with `ipcore-runtime.json`, remove `native`.
- Modify `spec_generator/lib/spec_generator.rb`: emit `ipcore-runtime.json`, update generated-output checks.
- Modify `spec_generator/test/spec_generator_test.rb`: assert new manifest filename and absence of `native`.
- Modify `spec_generator/README.md`: document `ipcore-runtime.json` and IP core runtime registry naming.
- Modify `qt/doc/README.md` and `qt/doc/architecture.md`: remove runtime plugin vocabulary for concrete IP cores.
- Modify `qt/xmake.lua`: update renamed source/header paths and add new test targets.
- Rename `qt/test/plugin_test.cpp` to `qt/test/ipcoreruntime_test.cpp`: runtime manifest, command runner, diagnostics, and generated runtime coverage.
- Modify `qt/test/ipcatalogservice_test.cpp`: use `IpCoreRuntimeDescriptor` names.
- Modify `qt/test/projectipservice_test.cpp`: multiple instances, unique instance IDs, selection, and no NoC singleton restriction.
- Modify `qt/test/projectdocument_test.cpp`: graph module instance ownership round-trip and load rejection.
- Modify `qt/test/nodeeditor_geometry_test.cpp`: scoped drop stamps `instanceId`.
- Modify `qt/test/topology_preset_test.cpp`: topology presets stamp `instanceId`.
- Modify `qt/test/ipcoregraphexporter_test.cpp`: instance-scoped export.
- Create `qt/test/removeipinstancecommand_test.cpp`: undoable deletion command behavior.
- Modify `qt/test/ipcatalogpanel_test.cpp`: remove-instance intent, workspace tool intent, and no Generate/DRC tools.
- Modify `qt/test/validation_test.cpp`: project-level validation over all instances.
- Create `qt/test/projectgenerationrunner_test.cpp`: saved project, per-instance output roots, command execution, and manifests.
- Create `qt/test/appsettings_test.cpp`: QSettings wrapper persistence and no project-file coupling.
- Modify `qt/test/v1architecturegate_test.cpp`: end-to-end saved project, all-instance validate/generate, and no stale plugin manifest path.

## Execution Rules

- Keep OpenNoC source and generated content compatible with the existing OpenNoC adaptation work. Do not revert the OpenNoC submodule or vendor smoke changes.
- Commit after every task. Each task below ends with an exact commit command.
- Do not rename `ipcores/finepaper-noc/generator/src/ruby/plugin`; that is the generator-internal extension pipeline, not the Qt IP core runtime loader.
- Use `xmake build <target>` and `xmake run <target>` from repository root unless a step states another working directory.

## Tasks

### Task 1: Runtime Vocabulary and Manifest Cutover

**Files:**
- Create: `qt/inc/ipcore/ipcoreruntimedescriptor.h`
- Create: `qt/inc/ipcore/ipcoreruntimeregistry.h`
- Create: `qt/src/ipcore/ipcoreruntimeregistry.cpp`
- Create: `qt/inc/ipcore/ipcorecommandrunner.h`
- Create: `qt/src/ipcore/ipcorecommandrunner.cpp`
- Create: `qt/inc/ipcore/ipcoreruntimediagnostics.h`
- Create: `qt/src/ipcore/ipcoreruntimediagnostics.cpp`
- Delete: `qt/inc/plugins/plugindescriptor.h`
- Delete: `qt/inc/plugins/pluginregistry.h`
- Delete: `qt/src/plugins/pluginregistry.cpp`
- Delete: `qt/inc/plugins/generatorrunner.h`
- Delete: `qt/src/plugins/generatorrunner.cpp`
- Delete: `qt/inc/plugins/startupdiagnostics.h`
- Delete: `qt/src/plugins/startupdiagnostics.cpp`
- Rename: `qt/test/plugin_test.cpp` -> `qt/test/ipcoreruntime_test.cpp`
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/inc/ipcore/ipcatalogservice.h`
- Modify: `qt/src/ipcore/ipcatalogservice.cpp`
- Modify: `qt/inc/project/ipinstanceparameteradapter.h`
- Rename: `qt/src/project/manifestipinstanceparameteradapter.cpp` -> `qt/src/project/runtimeipinstanceparameteradapter.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Rename the test file and write failing runtime manifest tests**

Use `git mv qt/test/plugin_test.cpp qt/test/ipcoreruntime_test.cpp`, then replace the helper and first manifest test names with IP core runtime names:

```cpp
#include "ipcore/ipcorecommandrunner.h"
#include "ipcore/ipcoreruntimeregistry.h"
#include "ipcore/ipcoreruntimediagnostics.h"

IpCatalogEntry entryFromRuntime(const IpCoreRuntimeDescriptor& runtime) {
    IpCatalogEntry entry;
    entry.id = runtime.id;
    entry.name = runtime.name;
    entry.version = runtime.version;
    entry.kind = runtime.kind;
    entry.runtimeRootPath = runtime.runtimeRootPath;
    entry.sourceRootPath = runtime.sourceRootPath;
    entry.modulesPath = runtime.modulesPath;
    entry.graphicsPath = runtime.graphicsPath;
    entry.generator = runtime.generator;
    entry.drc = runtime.drc;
    entry.topologyPresets = runtime.topologyPresets;
    return entry;
}

void testIpCoreRuntimeManifestLoadsRuntimeAndSourcePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("generated/finepaper.demo/graphics")),
            "failed to create runtime dirs");
    require(root.mkpath(QStringLiteral("ipcores/demo")),
            "failed to create source dirs");
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/modules.xml")),
              QByteArrayLiteral("<module-bundle/>"));
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/plugin.json")),
              QByteArrayLiteral(R"json({"id":"stale.plugin","source_root":"."})json"));
    writeFile(root.filePath(QStringLiteral("generated/finepaper.demo/ipcore-runtime.json")),
              QByteArrayLiteral(R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "kind": "noc",
      "source_root": "../../ipcores/demo",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {
        "command": "ruby",
        "input_format": "ipcore_graph_v1",
        "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]
      },
      "drc": {
        "command": "ruby",
        "input_format": "ipcore_graph_v1",
        "args": ["generator/bin/drc", "-i", "{input}"]
      },
      "topology_presets": []
    })json"));

    const QList<IpCoreRuntimeDescriptor> runtimes =
        IpCoreRuntimeRegistry::discover({root.filePath(QStringLiteral("generated"))});

    require(runtimes.size() == 1, "expected one IP core runtime");
    require(runtimes.first().id == QStringLiteral("finepaper.demo"),
            "runtime id should come from ipcore-runtime.json");
    require(runtimes.first().runtimeRootPath.endsWith(QStringLiteral("generated/finepaper.demo")),
            "runtime root should be manifest directory");
    require(QFileInfo(runtimes.first().modulesPath).isAbsolute(),
            "modules path should be absolute");
    require(runtimes.first().generator.hasCommand(),
            "generator command should be retained");
    require(runtimes.first().drc.hasCommand(),
            "DRC command should be retained");
}
```

- [ ] **Step 2: Add a failing default-root and environment variable test**

Add this test to `qt/test/ipcoreruntime_test.cpp`:

```cpp
void testDefaultDiscoveryUsesIpcoreRuntimeRootsOnly() {
    const QByteArray previous = qgetenv("FINEPAPER_IPCORE_PATH");
    QTemporaryDir extra;
    require(extra.isValid(), "temporary runtime root should be valid");
    qputenv("FINEPAPER_IPCORE_PATH", extra.path().toLocal8Bit());

    const QStringList roots = IpCoreRuntimeRegistry::defaultRuntimeRoots();
    const QString generatedRoot = repositoryRuntimePath(QStringLiteral("generated/ipcores"));

    require(roots.contains(QFileInfo(extra.path()).absoluteFilePath()),
            "FINEPAPER_IPCORE_PATH should be included");
    require(roots.contains(generatedRoot),
            "default roots should include generated ipcores");
    for (const QString& root : roots) {
        require(!root.endsWith(QStringLiteral("/plugins")),
                "runtime roots should not scan plugins directories");
    }

    if (previous.isEmpty()) {
        qunsetenv("FINEPAPER_IPCORE_PATH");
    } else {
        qputenv("FINEPAPER_IPCORE_PATH", previous);
    }
}
```

- [ ] **Step 3: Run the failing runtime test**

Run:

```bash
xmake build ipcoreruntime_test
```

Expected: build fails because `IpCoreRuntimeRegistry`, `IpCoreRuntimeDescriptor`, and `IpCoreCommandRunner` do not exist yet.

- [ ] **Step 4: Add the IP core runtime descriptor header**

Create `qt/inc/ipcore/ipcoreruntimedescriptor.h` with these public names:

```cpp
#pragma once

#include "graph/parameter.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct IpCoreCommandDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList args;

    bool hasCommand() const { return !command.trimmed().isEmpty(); }
    bool usesIpcoreGraphInput() const {
        return inputFormat == QStringLiteral("ipcore_graph_v1");
    }
    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const;
};

struct TopologyPresetParameterDescriptor {
    QString label;
    int defaultValue = 0;
    int minimumValue = 0;
    int maximumValue = 0;
};

struct TopologyPresetDescriptor {
    QString id;
    QString label;
    QString kind;
    QString routerModule;
    QString idPattern;
    QHash<QString, QString> ports;
    QHash<QString, TopologyPresetParameterDescriptor> parameters;
};

struct IpCoreInstanceParameterChoice {
    QString value;
    QString label;
};

struct IpCoreInstanceParameterDescriptor {
    QString name;
    QString type;
    Parameter::Value defaultValue = QString();
    QString label;
    QString description;
    QVector<IpCoreInstanceParameterChoice> choices;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
    bool configurable = true;
};

struct IpCoreRuntimeDescriptor {
    QString id;
    QString name;
    QString version;
    QString kind;
    QString runtimeRootPath;
    QString sourceRootPath;
    QString rootPath;
    QString modulesPath;
    QString graphicsPath;
    QHash<QString, IpCoreInstanceParameterDescriptor> instanceParameters;
    IpCoreCommandDescriptor generator;
    IpCoreCommandDescriptor drc;
    QVector<TopologyPresetDescriptor> topologyPresets;

    bool hasModules() const { return !modulesPath.isEmpty(); }
};
```

- [ ] **Step 5: Add runtime registry and command runner declarations**

Create `qt/inc/ipcore/ipcoreruntimeregistry.h`:

```cpp
#pragma once

#include "ipcore/ipcoreruntimedescriptor.h"

#include <QList>
#include <QString>
#include <QStringList>

class IpCoreRuntimeRegistry {
public:
    static IpCoreRuntimeRegistry& instance();

    static QList<IpCoreRuntimeDescriptor> discover(const QStringList& roots);
    static QStringList defaultRuntimeRoots();

    const QList<IpCoreRuntimeDescriptor>& runtimes() const;
    const IpCoreRuntimeDescriptor* runtime(const QString& ipcoreId) const;

private:
    IpCoreRuntimeRegistry();

    QList<IpCoreRuntimeDescriptor> m_runtimes;
};
```

Create `qt/inc/ipcore/ipcorecommandrunner.h`:

```cpp
#pragma once

#include "ipcore/ipcatalogservice.h"

#include <QString>
#include <QStringList>

struct IpCoreResolvedCommand {
    bool valid = false;
    QString errorMessage;
    QString ipcoreId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList arguments;
};

class IpCoreCommandRunner {
public:
    static IpCoreResolvedCommand resolveGenerator(const IpCatalogEntry& entry,
                                                  const QString& inputPath,
                                                  const QString& outputDirectory);
    static IpCoreResolvedCommand resolveDrc(const IpCatalogEntry& entry,
                                            const QString& inputPath,
                                            const QString& outputDirectory);
};
```

- [ ] **Step 6: Implement `ipcore-runtime.json` discovery**

In `qt/src/ipcore/ipcoreruntimeregistry.cpp`, port the current parser and make these concrete changes:

```cpp
QFileInfo manifestInfo(QDir(runtimeDirectory).filePath(QStringLiteral("ipcore-runtime.json")));
if (!manifestInfo.isFile()) {
    return std::nullopt;
}

const QString envPath = qEnvironmentVariable("FINEPAPER_IPCORE_PATH");
for (const QString& path : envPath.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
    appendUniquePath(roots, path);
}

const QString generatedIpcores = dir.filePath(QStringLiteral("generated/ipcores"));
if (QFileInfo(generatedIpcores).isDir()) {
    appendUniquePath(roots, generatedIpcores);
}
```

Do not parse or retain a `native` object. Do not append ancestor `plugins` directories.

- [ ] **Step 7: Implement command descriptor substitution**

Add this implementation to `qt/src/ipcore/ipcoreruntimeregistry.cpp` or a small companion section in `qt/src/ipcore/ipcorecommandrunner.cpp`:

```cpp
QStringList IpCoreCommandDescriptor::arguments(const QString& inputPath,
                                               const QString& outputDirectory) const {
    QStringList resolved;
    resolved.reserve(args.size());
    for (QString arg : args) {
        arg.replace(QStringLiteral("{input}"), inputPath);
        arg.replace(QStringLiteral("{output}"), outputDirectory);
        resolved.append(arg);
    }
    return resolved;
}
```

Implement `IpCoreCommandRunner::resolveGenerator()` and `resolveDrc()` using `IpCoreCommandDescriptor IpCatalogEntry::*` member pointers and error messages:

```cpp
QStringLiteral("IP core '%1' does not declare a generator.")
QStringLiteral("IP core '%1' does not declare a DRC command.")
```

- [ ] **Step 8: Rename runtime consumers**

Update `ModuleRegistry`, `IpCatalogService`, the parameter adapter, startup diagnostics, and tests to use these names:

```cpp
bool ModuleRegistry::loadIpCoreRuntimes(const QList<IpCoreRuntimeDescriptor>& runtimes);
IpCatalogService::IpCatalogService(QList<IpCoreRuntimeDescriptor> descriptors,
                                   const ModuleRegistry* moduleRegistry);
RuntimeIpInstanceParameterAdapter::RuntimeIpInstanceParameterAdapter(IpCoreRuntimeDescriptor runtime);
IpCoreRuntimeDiagnostics::logLines(IpCoreRuntimeRegistry::instance().runtimes(),
                                   ModuleRegistry::instance());
```

- [ ] **Step 9: Update `qt/xmake.lua` for renamed files**

Replace runtime source/header patterns with explicit new paths where the old plugin files were referenced:

```lua
add_files("src/ipcore/ipcoreruntimeregistry.cpp")
add_files("src/ipcore/ipcorecommandrunner.cpp")
add_files("src/ipcore/ipcoreruntimediagnostics.cpp")
add_files("inc/ipcore/ipcoreruntimeregistry.h")
add_files("inc/ipcore/ipcoreruntimedescriptor.h")
add_files("inc/ipcore/ipcorecommandrunner.h")
add_files("inc/ipcore/ipcoreruntimediagnostics.h")
```

Rename the test target:

```lua
add_qt_test_target("ipcoreruntime_test", "test/ipcoreruntime_test.cpp", {
    "src/ipcore/ipcorecommandrunner.cpp",
    "src/ipcore/ipcoreruntimeregistry.cpp",
    "src/ipcore/ipcoreruntimediagnostics.cpp",
    "src/project/runtimeipinstanceparameteradapter.cpp",
    "src/graph/graph.cpp",
    "src/graph/module.cpp",
    "src/graph/connection.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "src/modules/moduleprovider.cpp",
    "src/modules/moduleregistry.cpp",
    "inc/ipcore/ipcorecommandrunner.h",
    "inc/ipcore/ipcoreruntimeregistry.h",
    "inc/ipcore/ipcoreruntimedescriptor.h",
    "inc/ipcore/ipcoreruntimediagnostics.h",
    "inc/project/ipinstanceparameteradapter.h",
    "inc/graph/graph.h",
    "inc/graph/module.h",
    "inc/modules/moduleregistry.h",
    "inc/modules/moduleprovider.h"
})
```

- [ ] **Step 10: Regenerate and rename runtime artifacts**

Update `spec_generator/lib/spec_generator.rb` so generated output roots use `ipcore-runtime.json`:

```ruby
GENERATED_OUTPUT_ROOTS = [
  ['generated/ipcores/finepaper.noc/ipcore-runtime.json', :file],
  ['generated/ipcores/finepaper.noc/modules.xml', :file],
  ['generated/ipcores/finepaper.noc/graphics', :directory],
  ['ipcores/finepaper-noc/generator/src/ruby/model', :generated_files],
  ['generated/ipcores/finepaper.ravenoc/ipcore-runtime.json', :file],
  ['generated/ipcores/finepaper.ravenoc/modules.xml', :file],
  ['generated/ipcores/finepaper.ravenoc/graphics', :directory],
  ['generated/ipcores/finepaper.opennoc/ipcore-runtime.json', :file],
  ['generated/ipcores/finepaper.opennoc/modules.xml', :file],
  ['generated/ipcores/finepaper.opennoc/graphics', :directory]
].freeze
```

Change the emitter write path:

```ruby
File.write(File.join(bundle_dir, 'ipcore-runtime.json'), runtime_json)
```

Remove the `native` key from the emitted JSON object.

- [ ] **Step 11: Run runtime and generator tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
xmake build ipcoreruntime_test ipcatalogservice_test propertypanel_test
xmake run ipcoreruntime_test
xmake run ipcatalogservice_test
xmake run propertypanel_test
```

Expected: Ruby test output ends with zero failures; each xmake run prints its target's `passed` line.

- [ ] **Step 12: Commit runtime cutover**

Run:

```bash
git add qt spec_generator generated docs
git commit -m "refactor: rename plugin runtime to IP core runtime"
```

### Task 2: AppSettings and Saved-Project-First Lifecycle

**Files:**
- Create: `qt/inc/app/appsettings.h`
- Create: `qt/src/app/appsettings.cpp`
- Create: `qt/inc/app/projectlauncher.h`
- Create: `qt/src/app/projectlauncher.cpp`
- Create: `qt/test/appsettings_test.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/src/app/main.cpp`
- Modify: `qt/src/ipcore/ipcoreruntimeregistry.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing AppSettings tests**

Create `qt/test/appsettings_test.cpp`:

```cpp
#include "app/appsettings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testRecentProjectsAreAbsoluteUniqueAndNewestFirst() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));

    AppSettings settings(settingsPath, QSettings::IniFormat);
    const QString first = temp.filePath(QStringLiteral("a.fpproj"));
    const QString second = temp.filePath(QStringLiteral("b.fpproj"));
    settings.addRecentProject(first);
    settings.addRecentProject(second);
    settings.addRecentProject(first);

    const QStringList recent = settings.recentProjects();
    require(recent.size() == 2, "recent projects should be unique");
    require(recent.first() == QFileInfo(first).absoluteFilePath(),
            "most recent project should be first");
    require(recent.last() == QFileInfo(second).absoluteFilePath(),
            "older project should remain second");
}

void testIpcorePathsPersistAsAppLocalState() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));

    AppSettings settings(settingsPath, QSettings::IniFormat);
    settings.setIpcorePaths({QStringLiteral("/tmp/ipcores/a"), QStringLiteral("/tmp/ipcores/b")});

    AppSettings reloaded(settingsPath, QSettings::IniFormat);
    require(reloaded.ipcorePaths() == QStringList({QStringLiteral("/tmp/ipcores/a"),
                                                   QStringLiteral("/tmp/ipcores/b")}),
            "IP core paths should persist through QSettings");
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRecentProjectsAreAbsoluteUniqueAndNewestFirst();
        testIpcorePathsPersistAsAppLocalState();
    } catch (const std::exception& error) {
        std::cerr << "appsettings_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "appsettings_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run the failing AppSettings test**

Run:

```bash
xmake build appsettings_test
```

Expected: build fails because `app/appsettings.h` does not exist.

- [ ] **Step 3: Implement AppSettings**

Create `qt/inc/app/appsettings.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>

class AppSettings {
public:
    AppSettings();
    AppSettings(const QString& fileName, QSettings::Format format);

    QStringList recentProjects() const;
    void addRecentProject(const QString& path);
    QString lastDirectory() const;
    void setLastDirectory(const QString& path);

    QStringList ipcorePaths() const;
    void setIpcorePaths(const QStringList& paths);

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray& geometry);
    QByteArray mainWindowState() const;
    void setMainWindowState(const QByteArray& state);

    QString lastOutputRoot() const;
    void setLastOutputRoot(const QString& path);

private:
    QStringList absoluteUniquePaths(const QStringList& paths) const;

    QSettings m_settings;
};
```

Implement keys in `qt/src/app/appsettings.cpp`:

```cpp
namespace {
constexpr auto RecentProjectsKey = "projects/recent";
constexpr auto LastDirectoryKey = "projects/lastDirectory";
constexpr auto IpcorePathsKey = "ipcores/paths";
constexpr auto MainWindowGeometryKey = "ui/mainWindowGeometry";
constexpr auto MainWindowStateKey = "ui/mainWindowState";
constexpr auto LastOutputRootKey = "generation/lastOutputRoot";
}
```

`addRecentProject()` must normalize to `QFileInfo(path).absoluteFilePath()`, remove duplicates, prepend, and keep at most 10 entries.

- [ ] **Step 4: Add QSettings IP core paths to runtime roots**

In `qt/src/ipcore/ipcoreruntimeregistry.cpp`, append AppSettings IP core paths after environment roots and before repository roots:

```cpp
const AppSettings settings;
for (const QString& path : settings.ipcorePaths()) {
    appendUniquePath(roots, path);
}
```

- [ ] **Step 5: Write a failing MainWindow lifecycle test**

Add to `qt/test/ipcatalogpanel_test.cpp`:

```cpp
void testMainWindowStartsWithoutEditableUnsavedProject() {
    MainWindow window;
    require(!window.hasOpenProject(), "MainWindow should start without an open project");
    require(window.findChild<QAction*>(QStringLiteral("generateAction"))->isEnabled() == false,
            "Generate should be disabled without a saved project");
    require(window.findChild<QAction*>(QStringLiteral("validateAction"))->isEnabled() == false,
            "Validate should be disabled without a saved project");
    require(window.findChild<IpCatalogPanel*>(QStringLiteral("ipCatalogPanel"))->isEnabled() == false,
            "IP catalog editing should be disabled without a saved project");
}
```

Expose `bool hasOpenProject() const` in `MainWindow` and object names on generate/validate actions.

- [ ] **Step 6: Implement saved-project-first MainWindow state**

Add these members and methods to `qt/inc/app/mainwindow.h`:

```cpp
bool hasOpenProject() const;
bool createProjectAt(const QString& path);

void setProjectOpen(bool open);
bool requireOpenProject(const QString& actionName);

std::unique_ptr<AppSettings> m_appSettings;
bool m_projectOpen = false;
```

In `MainWindow::MainWindow`, construct `m_appSettings`, restore geometry/state if present, call `setProjectOpen(false)`, and do not create a dirty `Untitled` document.

`newGraph()` becomes a saved project creation flow:

```cpp
void MainWindow::newGraph() {
    if (!maybeSaveChanges(QStringLiteral("creating a new project"))) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this,
                                                      "New Project",
                                                      defaultDocumentPath(),
                                                      projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }
    createProjectAt(pathWithProjectExtension(path));
}
```

`createProjectAt()` must clear graph/state, save an empty `.fpproj`, set current path, set clean command state, add recent project, and call `setProjectOpen(true)`.

- [ ] **Step 7: Add ProjectLauncher**

Create `ProjectLauncherDialog` with this result shape:

```cpp
struct ProjectLauncherResult {
    enum class Action {
        Cancel,
        NewProject,
        OpenProject
    };

    Action action = Action::Cancel;
    QString path;
};
```

The dialog shows three paths into the same project lifecycle:

```cpp
ProjectLauncherResult ProjectLauncherDialog::result() const;
```

Buttons: New Project, Open Project, and recent project rows. New/Open use QFileDialog and `AppSettings::lastDirectory()`.

- [ ] **Step 8: Wire launcher in main.cpp**

Update `qt/src/app/main.cpp`:

```cpp
MainWindow w;
const QStringList args = QCoreApplication::arguments();
if (args.size() > 1) {
    w.loadGraph(args.at(1));
    w.show();
    return a.exec();
}

ProjectLauncherDialog launcher;
const int launcherResult = launcher.exec();
if (launcherResult != QDialog::Accepted) {
    return 0;
}

const ProjectLauncherResult result = launcher.result();
if (result.action == ProjectLauncherResult::Action::NewProject) {
    w.createProjectAt(result.path);
} else if (result.action == ProjectLauncherResult::Action::OpenProject) {
    w.loadGraph(result.path);
}
w.show();
return a.exec();
```

- [ ] **Step 9: Run lifecycle tests**

Run:

```bash
xmake build appsettings_test ipcatalogpanel_test qt
xmake run appsettings_test
xmake run ipcatalogpanel_test
```

Expected: `appsettings_test passed`, `ipcatalogpanel_test passed`, and the app target builds.

- [ ] **Step 10: Commit app settings and lifecycle**

Run:

```bash
git add qt
git commit -m "feat: require saved project before editing"
```

### Task 3: Module Instance Ownership in Graph, Project Files, and Creation Paths

**Files:**
- Modify: `qt/inc/graph/module.h`
- Modify: `qt/src/graph/module.cpp`
- Modify: `qt/inc/project/projectdocument.h`
- Modify: `qt/src/project/projectreader.cpp`
- Modify: `qt/src/project/projectwriter.cpp`
- Modify: `qt/src/project/graphprojectserializer.cpp`
- Modify: `qt/inc/nodeeditor/nodeeditorentityfactory.h`
- Modify: `qt/src/nodeeditor/nodeeditorentityfactory.cpp`
- Modify: `qt/src/nodeeditor/nodeeditorwidget.cpp`
- Modify: `qt/inc/commands/addmodulecommand.h`
- Modify: `qt/src/commands/addmodulecommand.cpp`
- Modify: `qt/inc/topology/topologypresetbuilder.h`
- Modify: `qt/src/topology/topologypresetbuilder.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/nodeeditor_geometry_test.cpp`
- Modify: `qt/test/topology_preset_test.cpp`

- [ ] **Step 1: Add failing project document tests**

In `qt/test/projectdocument_test.cpp`, extend `validProjectDocument()` records:

```cpp
document.ipcoreState.push_back(ProjectIpInstanceRecord{
    QStringLiteral("finepaper.test"),
    QStringLiteral("test_0"),
    QStringLiteral("finepaper.test-project-state-v1"),
    QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}}
});
xp.instanceId = QStringLiteral("test_0");
endpoint.instanceId = QStringLiteral("test_0");
```

Add a direct load rejection test:

```cpp
void testLoadRejectsModuleWithoutMatchingIpcoreInstanceState() {
    ProjectDocument document = validProjectDocument();
    document.modules.first().instanceId = QStringLiteral("missing_0");

    Graph graph;
    const GraphProjectLoadResult result =
        GraphProjectSerializer::loadProject(document, graph);

    require(!result.success,
            "module whose ipcore/instance lacks ipcore_state should be rejected");
    require(result.error.contains(QStringLiteral("missing_0")),
            "ownership error should mention missing instance");
}
```

Add a writer assertion:

```cpp
require(firstModule.value(QStringLiteral("instance")).toString() == QStringLiteral("ravenoc_0"),
        "module should emit instance owner");
```

- [ ] **Step 2: Add failing creation tests**

In `qt/test/nodeeditor_geometry_test.cpp`, extend `testScopedDropCreatesOwnedModule()`:

```cpp
require(module->instanceId() == QStringLiteral("ravenoc_0"),
        "created module should keep active instance ownership");
```

In `qt/test/topology_preset_test.cpp`, extend the mesh request setup:

```cpp
request.instanceId = QStringLiteral("ravenoc_0");
```

Then assert every created module has the instance:

```cpp
for (const auto& module : graph.modules()) {
    require(module->instanceId() == QStringLiteral("ravenoc_0"),
            "topology preset should stamp active instance ownership");
}
```

- [ ] **Step 3: Run failing tests**

Run:

```bash
xmake build projectdocument_test nodeeditor_geometry_test topology_preset_test
```

Expected: build fails because `Module::instanceId()` and `ProjectModuleRecord::instanceId` do not exist.

- [ ] **Step 4: Add instance ownership to Module**

Update `qt/inc/graph/module.h`:

```cpp
QString instanceId() const { return m_instanceId; }
void setInstanceId(const QString& instanceId);
```

Add private storage:

```cpp
QString m_instanceId;
```

Update `qt/src/graph/module.cpp`:

```cpp
void Module::setInstanceId(const QString& instanceId) {
    m_instanceId = instanceId;
}

std::unique_ptr<Module> Module::clone() const {
    auto cloned = std::make_unique<Module>(m_id, m_type);
    cloned->m_ipcoreId = m_ipcoreId;
    cloned->m_instanceId = m_instanceId;
    cloned->m_ports = m_ports;
    cloned->m_parameters = m_parameters;
    return cloned;
}
```

- [ ] **Step 5: Persist module instance ownership**

Update `ProjectModuleRecord`:

```cpp
struct ProjectModuleRecord {
    QString id;
    QString ipcoreId;
    QString instanceId;
    QString type;
    QJsonObject parameters;
};
```

Reader:

```cpp
module.instanceId = object.value(QStringLiteral("instance")).toString();
```

Writer:

```cpp
object.insert(QStringLiteral("instance"), module.instanceId);
```

Serializer save:

```cpp
record.instanceId = module->instanceId();
```

Serializer load:

```cpp
module->setIpcoreId(record.ipcoreId);
module->setInstanceId(record.instanceId);
```

- [ ] **Step 6: Validate project ownership against `ipcore_state`**

In `GraphProjectSerializer::loadProject`, build valid ownership keys before module validation:

```cpp
QSet<QString> validInstances;
for (const ProjectIpInstanceRecord& state : document.ipcoreState) {
    validInstances.insert(state.ipcoreId + QLatin1Char('\n') + state.instanceId);
}
```

For every module:

```cpp
if (record.instanceId.isEmpty()) {
    return failure(QStringLiteral("Module %1 is missing instance").arg(record.id));
}
const QString ownerKey = record.ipcoreId + QLatin1Char('\n') + record.instanceId;
if (!validInstances.contains(ownerKey)) {
    return failure(QStringLiteral("Module %1 references missing IP instance %2/%3")
                       .arg(record.id, record.ipcoreId, record.instanceId));
}
```

- [ ] **Step 7: Stamp instance ownership on drag/drop and create menu modules**

Change factory signature:

```cpp
std::unique_ptr<Module> createModule(Graph* graph,
                                     const QString& moduleId,
                                     const QString& moduleType,
                                     const QString& ipcoreId,
                                     const QString& instanceId);
```

Factory implementation:

```cpp
if (!type || ipcoreId.trimmed().isEmpty() || instanceId.trimmed().isEmpty() ||
    type->ipcoreId != ipcoreId) {
    return {};
}
module->setIpcoreId(ipcoreId);
module->setInstanceId(instanceId);
```

Call site in `NodeEditorWidget::createModuleAt()`:

```cpp
auto module = NodeEditorEntityFactory::createModule(m_graph,
                                                    moduleId,
                                                    payload.moduleType,
                                                    payload.ipcoreId,
                                                    payload.instanceId);
```

- [ ] **Step 8: Validate instance in AddModuleCommand**

Update constructor and members:

```cpp
AddModuleCommand(Graph* graph,
                 std::unique_ptr<Module> module,
                 QString expectedIpcoreId,
                 QString expectedInstanceId);

QString m_expectedInstanceId;
```

Execution guard:

```cpp
if (!m_expectedInstanceId.isEmpty() && m_module->instanceId() != m_expectedInstanceId) {
    return;
}
```

- [ ] **Step 9: Stamp topology preset ownership**

Update `TopologyPresetRequest`:

```cpp
struct TopologyPresetRequest {
    QString ipcoreId;
    QString instanceId;
    TopologyPresetDescriptor preset;
    QHash<QString, int> parameters;
};
```

Update `instantiateModule()`:

```cpp
std::unique_ptr<Module> instantiateModule(const ModuleType& type,
                                          const QString& id,
                                          const QString& ipcoreId,
                                          const QString& instanceId,
                                          int row,
                                          int col) {
    auto module = std::make_unique<Module>(id, type.name);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    return module;
}
```

Reject empty instance IDs at the top of `TopologyPresetBuilder::apply()`:

```cpp
if (request.instanceId.trimmed().isEmpty()) {
    return failure(QStringLiteral("IP instance is required for topology preset"));
}
```

- [ ] **Step 10: Run ownership tests**

Run:

```bash
xmake build projectdocument_test nodeeditor_geometry_test topology_preset_test
xmake run projectdocument_test
xmake run nodeeditor_geometry_test
xmake run topology_preset_test
```

Expected: all three targets print `passed`.

- [ ] **Step 11: Commit instance ownership**

Run:

```bash
git add qt
git commit -m "feat: persist IP instance ownership on modules"
```

### Task 4: Instance-Scoped Export

**Files:**
- Modify: `qt/src/ipcore/ipcoregraphexporter.cpp`
- Modify: `qt/test/ipcoregraphexporter_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/v1architecturegate_test.cpp`

- [ ] **Step 1: Write failing exporter tests**

In `qt/test/ipcoregraphexporter_test.cpp`, update `makeModule()` to accept instance:

```cpp
std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   const QString& ipcoreId,
                                   const QString& instanceId,
                                   std::vector<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : ports) {
        module->addPort(port);
    }
    return module;
}
```

Add:

```cpp
void testExporterFiltersSameIpcoreOtherInstanceModules() {
    registerOwnedType(QStringLiteral("Tile"), QStringLiteral("finepaper.ravenoc"));
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("a"),
                                       QStringLiteral("Tile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("ravenoc_0"),
                                       {})),
            "first instance module should add");
    require(graph.addModule(makeModule(QStringLiteral("b"),
                                       QStringLiteral("Tile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("ravenoc_1"),
                                       {})),
            "second instance module should add");

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            catalogEntry(QStringLiteral("finepaper.ravenoc")),
            instanceRecord(QStringLiteral("finepaper.ravenoc"), QStringLiteral("ravenoc_0")),
            QStringLiteral("design"),
            nullptr
        });

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.document.object().value(QStringLiteral("modules")).toArray().size() == 1,
            "export should include only selected instance modules");
}

void testExporterRejectsCrossInstanceConnectionTouchingSelectedInstance() {
    registerOwnedType(QStringLiteral("SourceTile"), QStringLiteral("finepaper.ravenoc"));
    registerOwnedType(QStringLiteral("TargetTile"), QStringLiteral("finepaper.ravenoc"));
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("a"),
                                       QStringLiteral("SourceTile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("ravenoc_0"),
                                       {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Out"))})),
            "selected instance module should add");
    require(graph.addModule(makeModule(QStringLiteral("b"),
                                       QStringLiteral("TargetTile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("ravenoc_1"),
                                       {Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("In"))})),
            "other instance module should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("cross"),
        PortRef{QStringLiteral("a"), QStringLiteral("out")},
        PortRef{QStringLiteral("b"), QStringLiteral("in")}));

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            catalogEntry(QStringLiteral("finepaper.ravenoc")),
            instanceRecord(QStringLiteral("finepaper.ravenoc"), QStringLiteral("ravenoc_0")),
            QStringLiteral("design"),
            nullptr
        });

    require(!result.success,
            "export should reject a connection crossing the selected instance boundary");
    require(result.error.contains(QStringLiteral("ravenoc_0")),
            "cross-instance error should mention selected instance");
}
```

- [ ] **Step 2: Run failing exporter test**

Run:

```bash
xmake build ipcoregraphexporter_test
xmake run ipcoregraphexporter_test
```

Expected: test fails because export still treats all same-IP modules as selected.

- [ ] **Step 3: Update exporter module filtering**

In `IpCoreGraphExporter::exportGraph()`:

```cpp
const auto belongsToSelectedInstance = [&](const Module* module) {
    return module &&
           module->ipcoreId() == request.ipcore.id &&
           module->instanceId() == request.instance.instanceId;
};

for (const auto& module : request.graph->modules()) {
    if (!belongsToSelectedInstance(module.get())) {
        continue;
    }
    const QString artifactId = moduleArtifactId(module.get(), usedModuleIds);
    runtimeToArtifactIds.insert(module->id(), artifactId);
}
```

When iterating connections:

```cpp
const bool sourceInside = runtimeToArtifactIds.contains(connection->source().moduleId);
const bool targetInside = runtimeToArtifactIds.contains(connection->target().moduleId);
if (!sourceInside && !targetInside) {
    continue;
}
if (sourceInside != targetInside) {
    return {false, {},
            QStringLiteral("Connection '%1' crosses IP instance '%2'.")
                .arg(connection->id(), request.instance.instanceId)};
}
```

- [ ] **Step 4: Run exporter and project tests**

Run:

```bash
xmake build ipcoregraphexporter_test projectdocument_test v1architecturegate_test
xmake run ipcoregraphexporter_test
xmake run projectdocument_test
```

Expected: exporter and project document tests pass. `v1architecturegate_test` builds for the next tasks.

- [ ] **Step 5: Commit instance-scoped export**

Run:

```bash
git add qt
git commit -m "feat: export IP graphs by instance"
```

### Task 5: Undoable IP Instance Deletion

**Files:**
- Create: `qt/inc/commands/removeipinstancecommand.h`
- Create: `qt/src/commands/removeipinstancecommand.cpp`
- Create: `qt/test/removeipinstancecommand_test.cpp`
- Modify: `qt/inc/project/projectstateservice.h`
- Modify: `qt/src/project/projectstateservice.cpp`
- Modify: `qt/inc/project/projectipservice.h`
- Modify: `qt/src/project/projectipservice.cpp`
- Modify: `qt/inc/panels/ipcatalogpanel.h`
- Modify: `qt/src/panels/ipcatalogpanel.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/projectipservice_test.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing multiple-instance service tests**

In `qt/test/projectipservice_test.cpp`, replace the NoC singleton rejection test with:

```cpp
void testProjectIpServiceCreatesMultipleInstancesWithUniqueIds() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult first = service.createInstanceForIpcore(ravenocEntry());
    const ProjectIpServiceResult second = service.createInstanceForIpcore(ravenocEntry());

    require(first.success, first.error.toLocal8Bit().constData());
    require(second.success, second.error.toLocal8Bit().constData());
    require(stateService.ipInstanceRecords().size() == 2,
            "two project IP instances should be stored");
    require(stateService.ipInstanceRecords().at(0).instanceId == QStringLiteral("ravenoc_0"),
            "first instance should use suffix 0");
    require(stateService.ipInstanceRecords().at(1).instanceId == QStringLiteral("ravenoc_1"),
            "second instance should use suffix 1");
}
```

Keep `selectInstance()` coverage for selecting existing records from the list.

- [ ] **Step 2: Write failing deletion command test**

Create `qt/test/removeipinstancecommand_test.cpp`:

```cpp
#include "commands/commandmanager.h"
#include "commands/removeipinstancecommand.h"
#include "graph/graph.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ProjectIpInstanceRecord record(const QString& ipcoreId, const QString& instanceId) {
    ProjectIpInstanceRecord result;
    result.ipcoreId = ipcoreId;
    result.instanceId = instanceId;
    result.schema = ipcoreId + QStringLiteral("-project-state-v1");
    result.state = QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}};
    return result;
}

std::unique_ptr<Module> module(const QString& id,
                               const QString& ipcoreId,
                               const QString& instanceId) {
    auto result = std::make_unique<Module>(id, QStringLiteral("Tile"));
    result->setIpcoreId(ipcoreId);
    result->setInstanceId(instanceId);
    result->addPort(Port(QStringLiteral("p"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("P")));
    return result;
}

void testRemoveIpInstanceCommandDeletesGraphStateAndRestoresOnUndo() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService ipService(&stateService);
    CommandManager commandManager;

    require(stateService.ensureIpInstanceRecord(record(QStringLiteral("finepaper.ravenoc"),
                                                       QStringLiteral("ravenoc_0"))),
            "first state should insert");
    require(stateService.ensureIpInstanceRecord(record(QStringLiteral("finepaper.ravenoc"),
                                                       QStringLiteral("ravenoc_1"))),
            "second state should insert");
    require(ipService.selectInstance(QStringLiteral("finepaper.ravenoc"), QStringLiteral("ravenoc_0")),
            "initial selection should set");

    require(graph.addModule(module(QStringLiteral("owned"),
                                   QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"))),
            "owned module should add");
    require(graph.addModule(module(QStringLiteral("other"),
                                   QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_1"))),
            "other module should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("incident"),
        PortRef{QStringLiteral("owned"), QStringLiteral("p")},
        PortRef{QStringLiteral("other"), QStringLiteral("p")}));

    commandManager.executeCommand(std::make_unique<RemoveIpInstanceCommand>(
        &graph,
        &stateService,
        &ipService,
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("ravenoc_0")));

    require(graph.getModule(QStringLiteral("owned")) == nullptr,
            "owned module should be removed");
    require(graph.getModule(QStringLiteral("other")) != nullptr,
            "other instance module should remain");
    require(graph.connections().empty(),
            "incident connection should be removed");
    require(stateService.ipInstanceRecords().size() == 1,
            "state record should be removed");
    require(ipService.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_1"),
            "selection should move to remaining instance");

    commandManager.undo();

    require(graph.getModule(QStringLiteral("owned")) != nullptr,
            "undo should restore owned module");
    require(graph.connections().size() == 1,
            "undo should restore incident connection");
    require(stateService.ipInstanceRecords().size() == 2,
            "undo should restore state record");
    require(ipService.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_0"),
            "undo should restore previous selection");
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRemoveIpInstanceCommandDeletesGraphStateAndRestoresOnUndo();
    } catch (const std::exception& error) {
        std::cerr << "removeipinstancecommand_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "removeipinstancecommand_test passed\n";
    return 0;
}
```

- [ ] **Step 3: Run failing deletion tests**

Run:

```bash
xmake build projectipservice_test removeipinstancecommand_test
```

Expected: build fails because `createInstanceForIpcore` and `RemoveIpInstanceCommand` do not exist.

- [ ] **Step 4: Make ProjectIpService create unique instances**

Replace `ensureInstanceForIpcore()` with:

```cpp
ProjectIpServiceResult ProjectIpService::createInstanceForIpcore(const IpCatalogEntry& entry);
```

Add a unique ID helper:

```cpp
QString uniqueIpInstanceId(const QString& ipcoreId,
                           const QVector<ProjectIpInstanceRecord>& records) {
    QString token = ipcoreId.section(QLatin1Char('.'), -1).trimmed().toLower();
    token.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    token.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (token.isEmpty()) {
        token = QStringLiteral("ip");
    }
    int index = 0;
    while (true) {
        const QString candidate = QStringLiteral("%1_%2").arg(token).arg(index);
        const auto exists = std::any_of(records.cbegin(), records.cend(), [&](const ProjectIpInstanceRecord& record) {
            return record.instanceId == candidate;
        });
        if (!exists) {
            return candidate;
        }
        ++index;
    }
}
```

Remove `isNocKind()` and the "Project already contains a NoC IP instance" branch.

- [ ] **Step 5: Add indexed state helpers**

Add to `ProjectStateService`:

```cpp
int indexOfIpInstanceRecord(const QString& ipcoreId, const QString& instanceId) const;
std::optional<ProjectIpInstanceRecord> ipInstanceRecord(const QString& ipcoreId,
                                                        const QString& instanceId) const;
bool insertIpInstanceRecord(int index, const ProjectIpInstanceRecord& record);
std::optional<ProjectIpInstanceRecord> takeIpInstanceRecord(const QString& ipcoreId,
                                                           const QString& instanceId);
```

`insertIpInstanceRecord()` must reject duplicate `{ipcoreId, instanceId}` and clamp index into `[0, size]`.

- [ ] **Step 6: Implement RemoveIpInstanceCommand**

Header:

```cpp
class RemoveIpInstanceCommand : public Command {
public:
    RemoveIpInstanceCommand(Graph* graph,
                            ProjectStateService* stateService,
                            ProjectIpService* ipService,
                            QString ipcoreId,
                            QString instanceId);

    void execute() override;
    void undo() override;

private:
    bool ownsModule(const Module* module) const;

    Graph* m_graph = nullptr;
    ProjectStateService* m_stateService = nullptr;
    ProjectIpService* m_ipService = nullptr;
    QString m_ipcoreId;
    QString m_instanceId;
    std::optional<ProjectIpInstanceRef> m_previousSelection;
    int m_recordIndex = -1;
    ProjectIpInstanceRecord m_record;
    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
```

Execute order:

```cpp
m_previousSelection = m_ipService->selectedIpInstance();
m_recordIndex = m_stateService->indexOfIpInstanceRecord(m_ipcoreId, m_instanceId);
const std::optional<ProjectIpInstanceRecord> record =
    m_stateService->ipInstanceRecord(m_ipcoreId, m_instanceId);
m_record = *record;

QStringList ownedModuleIds;
for (const auto& module : m_graph->modules()) {
    if (ownsModule(module.get())) {
        ownedModuleIds.append(module->id());
    }
}

QStringList incidentConnectionIds;
for (const auto& connection : m_graph->connections()) {
    if (ownedModuleIds.contains(connection->source().moduleId) ||
        ownedModuleIds.contains(connection->target().moduleId)) {
        incidentConnectionIds.append(connection->id());
    }
}

for (const QString& id : incidentConnectionIds) {
    if (auto connection = m_graph->takeConnection(id)) {
        m_connections.push_back(std::move(connection));
    }
}
for (const QString& id : ownedModuleIds) {
    if (auto module = m_graph->takeModule(id)) {
        m_modules.push_back(std::move(module));
    }
}
m_ipService->removeInstance(m_ipcoreId, m_instanceId);
m_executed = true;
```

Undo order:

```cpp
m_stateService->insertIpInstanceRecord(m_recordIndex, m_record);
for (auto& module : m_modules) {
    m_graph->insertModule(std::move(module));
}
for (auto& connection : m_connections) {
    m_graph->insertConnection(std::move(connection));
}
m_modules.clear();
m_connections.clear();
if (m_previousSelection.has_value()) {
    m_ipService->selectInstance(m_previousSelection->ipcoreId,
                                m_previousSelection->instanceId);
}
```

- [ ] **Step 7: Add panel remove intent**

Add signal:

```cpp
void removeIpInstanceRequested(const QString& ipcoreId, const QString& instanceId);
```

Add a context menu and Delete-key path on `m_projectIpList` that only emits the signal:

```cpp
void IpCatalogPanel::emitRemoveRequest(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const QString ipcoreId = item->data(Qt::UserRole).toString();
    const QString instanceId = item->data(InstanceIdRole).toString();
    if (!ipcoreId.trimmed().isEmpty() && !instanceId.trimmed().isEmpty()) {
        emit removeIpInstanceRequested(ipcoreId, instanceId);
    }
}
```

- [ ] **Step 8: Wire deletion through MainWindow command stack**

In `MainWindow::setupConnections()`:

```cpp
connect(m_ipCatalogPanel,
        &IpCatalogPanel::removeIpInstanceRequested,
        this,
        [this](const QString& ipcoreId, const QString& instanceId) {
            auto rejected = m_commandManager->executeCommand(std::make_unique<RemoveIpInstanceCommand>(
                m_graph,
                m_projectStateService.get(),
                m_projectIpService.get(),
                ipcoreId,
                instanceId));
            if (rejected) {
                QMessageBox::warning(this, "Remove IP Instance", "IP instance could not be removed.");
            }
            syncDocumentStateFromHistory();
        });
```

Update catalog add request to call `createInstanceForIpcore()`.

- [ ] **Step 9: Run deletion tests**

Run:

```bash
xmake build projectipservice_test removeipinstancecommand_test ipcatalogpanel_test
xmake run projectipservice_test
xmake run removeipinstancecommand_test
xmake run ipcatalogpanel_test
```

Expected: all three targets print `passed`.

- [ ] **Step 10: Commit undoable deletion**

Run:

```bash
git add qt
git commit -m "feat: delete IP instances through undoable command"
```

### Task 6: Workspace Tools Become Active-Instance Editing Helpers Only

**Files:**
- Modify: `qt/inc/ipcore/iptoolsmodel.h`
- Modify: `qt/src/ipcore/iptoolsmodel.cpp`
- Modify: `qt/inc/panels/ipcatalogpanel.h`
- Modify: `qt/src/panels/ipcatalogpanel.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/ipcatalogpanel_test.cpp`

- [ ] **Step 1: Write failing Workspace Tools tests**

In `qt/test/ipcatalogpanel_test.cpp`, update `testSelectingIpInstanceUpdatesActiveModuleAndToolLists()`:

```cpp
require(toolList->count() == 1,
        "active tool list should include topology helpers only");
require(toolList->item(0)->text() == QStringLiteral("Mesh"),
        "active tool list should expose topology preset label");
for (int row = 0; row < toolList->count(); ++row) {
    const QString id = toolList->item(row)->data(Qt::UserRole).toString();
    require(id != QStringLiteral("generate"),
            "Workspace Tools should not include Generate");
    require(id != QStringLiteral("drc"),
            "Workspace Tools should not include DRC");
}
```

Add a tool intent test:

```cpp
void testPanelEmitsWorkspaceToolIntentWithActiveInstance() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);
    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");

    QString toolId;
    QString ipcoreId;
    QString instanceId;
    QObject::connect(&panel, &IpCatalogPanel::workspaceToolRequested, &panel,
                     [&](const QString& requestedToolId,
                         const QString& requestedIpcoreId,
                         const QString& requestedInstanceId) {
                         toolId = requestedToolId;
                         ipcoreId = requestedIpcoreId;
                         instanceId = requestedInstanceId;
                     });

    auto* toolList = panel.findChild<QListWidget*>(QStringLiteral("activeToolList"));
    require(toolList->count() == 1, "tool list should contain topology tool");
    QMetaObject::invokeMethod(toolList,
                              "itemActivated",
                              Qt::DirectConnection,
                              Q_ARG(QListWidgetItem*, toolList->item(0)));

    require(toolId == QStringLiteral("topology:mesh"),
            "tool intent should include topology tool id");
    require(ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "tool intent should include active ipcore");
    require(instanceId == QStringLiteral("ravenoc_0"),
            "tool intent should include active instance");
}
```

- [ ] **Step 2: Run failing panel test**

Run:

```bash
xmake build ipcatalogpanel_test
xmake run ipcatalogpanel_test
```

Expected: test fails because Generate/DRC are still listed and tool activation emits no intent.

- [ ] **Step 3: Remove Generate and DRC from IpToolsModel**

Update `entriesForWorkspace()`:

```cpp
entries.reserve(entry.topologyPresets.size());
for (const TopologyPresetDescriptor& preset : entry.topologyPresets) {
    entries.push_back(IpToolEntry{
        QStringLiteral("topology:") + preset.id,
        preset.label,
        QStringLiteral("topology")
    });
}
```

Remove the generator and DRC branches.

- [ ] **Step 4: Emit workspace tool intent from panel**

Add signal:

```cpp
void workspaceToolRequested(const QString& toolId,
                            const QString& ipcoreId,
                            const QString& instanceId);
```

When creating tool items, store active owner data:

```cpp
item->setData(Qt::UserRole, tool.id);
item->setData(IpcoreIdRole, state.ipcoreId);
item->setData(ActiveInstanceIdRole, state.instanceId);
```

Connect `itemActivated`:

```cpp
connect(m_activeToolList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    if (!item) {
        return;
    }
    emit workspaceToolRequested(item->data(Qt::UserRole).toString(),
                                item->data(IpcoreIdRole).toString(),
                                item->data(ActiveInstanceIdRole).toString());
});
```

- [ ] **Step 5: Handle topology tools in MainWindow**

Move topology execution into a helper:

```cpp
void MainWindow::createTopologyPresetFor(const QString& ipcoreId,
                                         const QString& instanceId,
                                         const QString& presetId);
```

Both toolbar topology menu and Workspace Tools call this helper. The request must set instance ownership:

```cpp
TopologyPresetRequest request;
request.ipcoreId = ipcoreId;
request.instanceId = instanceId;
request.preset = *presetIt;
```

- [ ] **Step 6: Run workspace tool tests**

Run:

```bash
xmake build ipcatalogpanel_test topology_preset_test
xmake run ipcatalogpanel_test
xmake run topology_preset_test
```

Expected: both targets print `passed`.

- [ ] **Step 7: Commit Workspace Tools cleanup**

Run:

```bash
git add qt
git commit -m "fix: scope workspace tools to active instance editing"
```

### Task 7: Project-Level Validation

**Files:**
- Create: `qt/inc/validation/projectvalidationrunner.h`
- Create: `qt/src/validation/projectvalidationrunner.cpp`
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing validation runner tests**

Add to `qt/test/validation_test.cpp`:

```cpp
#include "validation/projectvalidationrunner.h"

void testProjectValidationWarnsForEveryInstanceWithoutDrc() {
    Graph graph;
    IpCatalogEntry first;
    first.id = QStringLiteral("finepaper.first");
    first.name = QStringLiteral("First");
    IpCatalogEntry second;
    second.id = QStringLiteral("finepaper.second");
    second.name = QStringLiteral("Second");
    IpCatalogService catalog(QList<IpCoreRuntimeDescriptor>{}, nullptr);
    ProjectValidationRunner runner;

    ProjectIpInstanceRecord a;
    a.ipcoreId = QStringLiteral("finepaper.first");
    a.instanceId = QStringLiteral("first_0");
    a.schema = QStringLiteral("finepaper.first-project-state-v1");
    ProjectIpInstanceRecord b;
    b.ipcoreId = QStringLiteral("finepaper.second");
    b.instanceId = QStringLiteral("second_0");
    b.schema = QStringLiteral("finepaper.second-project-state-v1");

    QList<IpCatalogEntry> entries{first, second};
    const QList<ValidationResult> results =
        runner.validate(&graph, entries, QVector<ProjectIpInstanceRecord>{a, b});

    require(results.size() == 2,
            "project validation should produce one missing-DRC warning per instance");
    require(results.at(0).message().contains(QStringLiteral("first_0")),
            "first warning should include first instance id");
    require(results.at(1).message().contains(QStringLiteral("second_0")),
            "second warning should include second instance id");
}
```

Use a `QList<IpCatalogEntry>` API on the runner to keep the test independent of catalog construction details.

- [ ] **Step 2: Run failing validation test**

Run:

```bash
xmake build validation_test
```

Expected: build fails because `ProjectValidationRunner` does not exist.

- [ ] **Step 3: Implement ProjectValidationRunner API**

Create header:

```cpp
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QVector>

class Graph;

class ProjectValidationRunner {
public:
    QList<ValidationResult> validate(const Graph* graph,
                                     const QList<IpCatalogEntry>& entries,
                                     const QVector<ProjectIpInstanceRecord>& instances) const;
};
```

Implementation shape:

```cpp
QList<ValidationResult> ProjectValidationRunner::validate(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    QList<ValidationResult> results = BasicValidator().validate(graph);
    DRCRunner drcRunner;
    for (const ProjectIpInstanceRecord& instance : instances) {
        const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const IpCatalogEntry& entry) {
            return entry.id == instance.ipcoreId;
        });
        if (it == entries.cend()) {
            results.append(ValidationResult(ValidationSeverity::Error,
                                            QStringLiteral("%1: IP core runtime '%2' is not loaded.")
                                                .arg(instance.instanceId, instance.ipcoreId),
                                            QString(),
                                            QStringLiteral("DRC")));
            continue;
        }
        if (!it->drc.hasCommand()) {
            results.append(ValidationResult(ValidationSeverity::Warning,
                                            QStringLiteral("%1: IP core '%2' does not declare a DRC command.")
                                                .arg(instance.instanceId, instance.ipcoreId),
                                            QString(),
                                            QStringLiteral("DRC")));
            continue;
        }
        QList<ValidationResult> drcResults = drcRunner.validate(graph, *it, instance);
        for (const ValidationResult& result : drcResults) {
            results.append(ValidationResult(result.severity(),
                                            QStringLiteral("%1: %2").arg(instance.instanceId, result.message()),
                                            result.elementId(),
                                            result.ruleName()));
        }
    }
    return results;
}
```

- [ ] **Step 4: Update ValidationManager**

Remove active-workspace-only DRC logic. `runValidation()` should call:

```cpp
const QList<ValidationResult> results =
    m_projectValidationRunner->validate(m_graph,
                                        m_catalogService ? m_catalogService->entries() : QList<IpCatalogEntry>{},
                                        m_projectStateService ? m_projectStateService->ipInstanceRecords()
                                                              : QVector<ProjectIpInstanceRecord>{});
```

The manager still logs counts and sends results to `LogPanel`.

- [ ] **Step 5: Run validation tests**

Run:

```bash
xmake build validation_test ipcatalogpanel_test
xmake run validation_test
xmake run ipcatalogpanel_test
```

Expected: both targets print `passed`.

- [ ] **Step 6: Commit validation orchestration**

Run:

```bash
git add qt
git commit -m "feat: validate all project IP instances"
```

### Task 8: Project-Level Generation

**Files:**
- Create: `qt/inc/app/projectgenerationrunner.h`
- Create: `qt/src/app/projectgenerationrunner.cpp`
- Create: `qt/test/projectgenerationrunner_test.cpp`
- Modify: `qt/inc/app/generationartifacts.h`
- Modify: `qt/src/app/generationartifacts.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/projectdocument_test.cpp`
- Modify: `qt/test/v1architecturegate_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing project generation runner test**

Create `qt/test/projectgenerationrunner_test.cpp`:

```cpp
#include "app/projectgenerationrunner.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
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

ProjectIpInstanceRecord record(const QString& ipcoreId, const QString& instanceId) {
    ProjectIpInstanceRecord result;
    result.ipcoreId = ipcoreId;
    result.instanceId = instanceId;
    result.schema = ipcoreId + QStringLiteral("-project-state-v1");
    result.state = QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}};
    return result;
}

void registerGenerationType() {
    ModuleType type;
    type.name = QStringLiteral("GenerationTile");
    type.ipcoreId = QStringLiteral("finepaper.gen");
    ModuleRegistry::instance().registerType(type);
}

std::unique_ptr<Module> module(const QString& id, const QString& instanceId) {
    auto result = std::make_unique<Module>(id, QStringLiteral("GenerationTile"));
    result->setIpcoreId(QStringLiteral("finepaper.gen"));
    result->setInstanceId(instanceId);
    return result;
}

void testProjectGenerationRunsEveryInstanceUnderProjectRoot() {
    registerGenerationType();
    QTemporaryDir temp;
    require(temp.isValid(), "temporary project directory should be valid");

    Graph graph;
    require(graph.addModule(module(QStringLiteral("a"), QStringLiteral("gen_0"))),
            "first module should add");
    require(graph.addModule(module(QStringLiteral("b"), QStringLiteral("gen_1"))),
            "second module should add");

    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.gen");
    entry.name = QStringLiteral("Gen");
    entry.sourceRootPath = temp.path();
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.args = {
        QStringLiteral("-e"),
        QStringLiteral("File.write(File.join(ARGV[1], 'artifact.sv'), File.read(ARGV[0]))"),
        QStringLiteral("{input}"),
        QStringLiteral("{output}")
    };

    ProjectGenerationRequest request;
    request.graph = &graph;
    request.projectPath = QDir(temp.path()).filePath(QStringLiteral("design.fpproj"));
    request.designName = QStringLiteral("design");
    request.catalogEntries = QList<IpCatalogEntry>{entry};
    request.instances = QVector<ProjectIpInstanceRecord>{
        record(QStringLiteral("finepaper.gen"), QStringLiteral("gen_0")),
        record(QStringLiteral("finepaper.gen"), QStringLiteral("gen_1"))
    };

    const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);

    require(result.success, result.error.toLocal8Bit().constData());
    require(result.instances.size() == 2,
            "generation should run both project instances");
    require(QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("generated/gen_0/artifact.sv"))),
            "first instance artifact should be under project generated root");
    require(QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("generated/gen_1/artifact.sv"))),
            "second instance artifact should be under project generated root");
    require(QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("generated/project-snapshot.fpproj"))),
            "project snapshot should be written beside generated outputs");
}

}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testProjectGenerationRunsEveryInstanceUnderProjectRoot();
    } catch (const std::exception& error) {
        std::cerr << "projectgenerationrunner_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "projectgenerationrunner_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing generation test**

Run:

```bash
xmake build projectgenerationrunner_test
```

Expected: build fails because `ProjectGenerationRunner` does not exist.

- [ ] **Step 3: Define project generation API**

Create `qt/inc/app/projectgenerationrunner.h`:

```cpp
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

class Graph;

struct ProjectGenerationInstanceResult {
    bool success = false;
    QString ipcoreId;
    QString instanceId;
    QString outputDirectory;
    QString inputJsonPath;
    QString manifestPath;
    QString standardOutput;
    QString standardError;
    QString error;
};

struct ProjectGenerationRequest {
    const Graph* graph = nullptr;
    QString projectPath;
    QString designName;
    QString outputRoot;
    QList<IpCatalogEntry> catalogEntries;
    QVector<ProjectIpInstanceRecord> instances;
};

struct ProjectGenerationResult {
    bool success = false;
    QString outputRoot;
    QString snapshotPath;
    QString error;
    QVector<ProjectGenerationInstanceResult> instances;
};

class ProjectGenerationRunner {
public:
    ProjectGenerationResult generate(const ProjectGenerationRequest& request) const;
};
```

- [ ] **Step 4: Implement saved-project guard and output roots**

In `ProjectGenerationRunner::generate()`:

```cpp
if (!request.graph) {
    return {false, {}, {}, QStringLiteral("Graph is not available."), {}};
}
if (request.projectPath.trimmed().isEmpty()) {
    return {false, {}, {}, QStringLiteral("Save the project before generation."), {}};
}
const QFileInfo projectInfo(request.projectPath);
const QString outputRoot = request.outputRoot.trimmed().isEmpty()
    ? QDir(projectInfo.absolutePath()).filePath(QStringLiteral("generated"))
    : request.outputRoot;
```

Per instance:

```cpp
const QString instanceOutput = QDir(outputRoot).filePath(instance.instanceId);
QDir().mkpath(instanceOutput);
const QString jsonPath = QDir(instanceOutput).filePath(QStringLiteral("ipcore-graph.json"));
```

- [ ] **Step 5: Export and run every instance**

Find the catalog entry by `instance.ipcoreId`, export via `IpCoreGraphExporter`, write JSON, resolve command via `IpCoreCommandRunner::resolveGenerator()`, and run `QProcess` with `waitForFinished(-1)`.

Use this manifest object:

```cpp
QJsonObject manifest;
manifest.insert(QStringLiteral("schema"), QStringLiteral("finepaper-generation-manifest-v1"));
manifest.insert(QStringLiteral("project"), request.projectPath);
manifest.insert(QStringLiteral("ipcore"), instance.ipcoreId);
manifest.insert(QStringLiteral("instance"), instance.instanceId);
manifest.insert(QStringLiteral("input"), jsonPath);
manifest.insert(QStringLiteral("output"), instanceOutput);
```

Write it to:

```cpp
const QString manifestPath = QDir(instanceOutput).filePath(QStringLiteral("generation-manifest.json"));
```

- [ ] **Step 6: Write project snapshot under output root**

Add helper in `generationartifacts`:

```cpp
GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotToPath(
    const Graph& graph,
    const QString& projectPath,
    const QString& designName,
    const QVector<ProjectIpInstanceRecord>& ipcoreState);
```

`ProjectGenerationRunner` writes:

```cpp
const QString snapshotPath = QDir(outputRoot).filePath(QStringLiteral("project-snapshot.fpproj"));
```

- [ ] **Step 7: Replace MainWindow active-only generation**

`MainWindow::generateVerilog()` must no longer ask for an arbitrary output folder or use active workspace context. It must require an open saved project:

```cpp
if (!requireOpenProject(QStringLiteral("Generate"))) {
    return;
}
ProjectGenerationRequest request;
request.graph = m_graph;
request.projectPath = m_currentDocumentPath;
request.designName = QFileInfo(m_currentDocumentPath).completeBaseName();
request.catalogEntries = m_ipCatalogService->entries();
request.instances = m_projectStateService->ipInstanceRecords();
const ProjectGenerationResult result = ProjectGenerationRunner().generate(request);
```

Log each `ProjectGenerationInstanceResult` with `[Generate][<instanceId>]` prefixes.

- [ ] **Step 8: Run generation tests**

Run:

```bash
xmake build projectgenerationrunner_test projectdocument_test v1architecturegate_test qt
xmake run projectgenerationrunner_test
xmake run projectdocument_test
```

Expected: new generation runner and project document tests print `passed`; app target builds.

- [ ] **Step 9: Commit project-level generation**

Run:

```bash
git add qt
git commit -m "feat: generate all project IP instances"
```

### Task 9: Legacy Product-Model Cleanup

**Files:**
- Modify: `qt/inc/connection/connectionruleservice.h`
- Modify: `qt/src/connection/connectionruleservice.cpp`
- Modify: `qt/test/connectionruleservice_test.cpp`
- Modify: `qt/test/projectipservice_test.cpp`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`
- Modify: `spec_generator/README.md`
- Modify: `docs/superpowers/specs/2026-05-12-ip-creation-tool-architecture-design.md` only if implementation naming differs from the approved spec.

- [ ] **Step 1: Write failing cleanup assertions**

Add a scan gate to `qt/test/v1architecturegate_test.cpp`:

```cpp
void testRuntimeVocabularyHasNoQtPluginManifestPath() {
    const QString root = repositoryPath(QStringLiteral("qt"));
    const QStringList forbidden = {
        QStringLiteral("PluginRegistry"),
        QStringLiteral("PluginDescriptor"),
        QStringLiteral("PluginCommandDescriptor"),
        QStringLiteral("PluginInstanceParameterDescriptor"),
        QStringLiteral("FINEPAPER_PLUGIN_PATH"),
        QStringLiteral("plugin.json"),
        QStringLiteral("ConnectionRuleLayer::FeaturePlugin")
    };
    for (const QString& token : forbidden) {
        QProcess process;
        process.start(QStringLiteral("rg"), QStringList{QStringLiteral("-n"), token, root});
        process.waitForFinished();
        require(process.exitCode() != 0,
                QString("forbidden runtime token remains: %1").arg(token).toLocal8Bit().constData());
    }
}
```

This gate scans `qt/` only so historical docs under `docs/superpowers/` remain auditable.

- [ ] **Step 2: Rename connection rule layer**

In `qt/inc/connection/connectionruleservice.h`:

```cpp
enum class ConnectionRuleLayer {
    Structural,
    EditorRule,
    Ipcore
};
```

In `qt/src/connection/connectionruleservice.cpp`, rename `checkFeatureDeclarativeRules()` to:

```cpp
CandidateEvaluation checkEditorDeclarativeRules(const Graph* graph,
                                                const PortRef& source,
                                                const PortRef& target,
                                                const ModuleInterfaceMetadata& sourceEndpoint,
                                                const ModuleInterfaceMetadata& targetEndpoint)
```

Use `ConnectionRuleLayer::EditorRule` for editor-time declarative rule results.

- [ ] **Step 3: Remove stale ProjectType behavior**

Confirm `ProjectIpService` has no `isNocKind()` function and no error message:

```cpp
QStringLiteral("Project already contains a NoC IP instance.")
```

Keep `record.state.insert(QStringLiteral("kind"), entry.kind);` as category/display metadata.

- [ ] **Step 4: Update maintained docs**

Update `qt/doc/README.md`, `qt/doc/architecture.md`, and `spec_generator/README.md` so concrete IP core runtime docs use:

```text
IpCoreRuntimeRegistry
IpCoreRuntimeDescriptor
ipcore-runtime.json
FINEPAPER_IPCORE_PATH
generated/ipcores/<ipcore-id>/
```

The docs must state that `plugins/` is reserved for future feature extensions, not concrete IP core runtimes.

- [ ] **Step 5: Run cleanup gates**

Run:

```bash
xmake build connectionruleservice_test projectipservice_test v1architecturegate_test
xmake run connectionruleservice_test
xmake run projectipservice_test
xmake run v1architecturegate_test
```

Expected: all three targets print `passed`.

- [ ] **Step 6: Commit cleanup**

Run:

```bash
git add qt spec_generator docs
git commit -m "refactor: remove legacy product model vocabulary"
```

### Task 10: Final V1 Gate and Full Verification

**Files:**
- Modify: `qt/test/v1architecturegate_test.cpp`
- Modify: `qt/xmake.lua`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`

- [ ] **Step 1: Update final architecture gate flow**

Change the gate to:

```cpp
const QList<IpCoreRuntimeDescriptor> runtimes =
    IpCoreRuntimeRegistry::discover({repositoryPath(QStringLiteral("generated/ipcores"))});
ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
require(registry.loadIpCoreRuntimes(runtimes),
        "repository IP modules should load");
IpCatalogService catalog(runtimes, &registry);
```

Create at least two project instances, generate topology for the first, add one manual module for the second, save/load the project, run `ProjectValidationRunner`, and run `ProjectGenerationRunner`.

- [ ] **Step 2: Add stale artifact checks**

In the gate, assert current generated artifacts:

```cpp
require(QFileInfo::exists(repositoryPath(QStringLiteral("generated/ipcores/finepaper.noc/ipcore-runtime.json"))),
        "NoC runtime manifest should use current filename");
require(!QFileInfo::exists(repositoryPath(QStringLiteral("generated/ipcores/finepaper.noc/plugin.json"))),
        "NoC runtime manifest should not use stale plugin filename");
```

Repeat for `finepaper.ravenoc` and `finepaper.opennoc`.

- [ ] **Step 3: Run targeted tests**

Run:

```bash
ruby -I spec_generator/test spec_generator/test/spec_generator_test.rb
xmake build ipcoreruntime_test ipcatalogservice_test projectipservice_test projectdocument_test ipcoregraphexporter_test removeipinstancecommand_test ipcatalogpanel_test validation_test projectgenerationrunner_test connectionruleservice_test v1architecturegate_test
xmake run ipcoreruntime_test
xmake run ipcatalogservice_test
xmake run projectipservice_test
xmake run projectdocument_test
xmake run ipcoregraphexporter_test
xmake run removeipinstancecommand_test
xmake run ipcatalogpanel_test
xmake run validation_test
xmake run projectgenerationrunner_test
xmake run connectionruleservice_test
xmake run v1architecturegate_test
```

Expected: Ruby test output has zero failures and every xmake test prints its `passed` line.

- [ ] **Step 4: Run app build**

Run:

```bash
xmake build qt
```

Expected: app target builds without errors.

- [ ] **Step 5: Inspect stale runtime tokens**

Run:

```bash
rg -n "PluginRegistry|PluginDescriptor|PluginCommandDescriptor|PluginInstanceParameterDescriptor|FINEPAPER_PLUGIN_PATH|plugin\\.json|ConnectionRuleLayer::FeaturePlugin" qt spec_generator generated
```

Expected: no matches. If matches remain inside `ipcores/finepaper-noc/generator/src/ruby/plugin`, leave them because that path is generator-internal and outside the scan roots above.

- [ ] **Step 6: Commit final gate**

Run:

```bash
git add qt spec_generator generated docs
git commit -m "test: gate IP creation tool architecture"
```

## Self-Review

- Spec coverage: runtime vocabulary and manifest cutover are Task 1; saved-project-first lifecycle and QSettings are Task 2; module `{ipcoreId, instanceId}` ownership is Task 3; instance-scoped export is Task 4; undoable deletion is Task 5; Workspace Tools cleanup is Task 6; project-level validation is Task 7; project-level generation is Task 8; legacy cleanup is Task 9; final gate is Task 10.
- Placeholder scan: the plan uses concrete file paths, concrete API names, exact commands, and expected outputs.
- Type consistency: `IpCoreRuntimeDescriptor`, `IpCoreCommandDescriptor`, `IpCoreInstanceParameterDescriptor`, `IpCoreRuntimeRegistry`, `IpCoreCommandRunner`, `ProjectGenerationRunner`, and `ProjectValidationRunner` are introduced before later tasks use them.
- Risk boundary: OpenNoC generated artifacts are included in runtime manifest rename, but OpenNoC source/submodule work is not reverted.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-12-ip-creation-tool-architecture.md`. Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** - execute tasks in this session using executing-plans, batch execution with checkpoints.
