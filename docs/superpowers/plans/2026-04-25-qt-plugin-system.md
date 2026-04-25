# Qt Plugin System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build startup-loaded directory plugins for the Qt editor, migrate current NoC support into a bundled plugin, reserve native plugin metadata, and upgrade the Qt build to C++23.

**Architecture:** Add a focused `plugins` layer that discovers `plugin.json` manifests and exposes `PluginDescriptor` metadata. `ModuleRegistry` imports module bundles from discovered plugins and annotates each `ModuleType` with its owning plugin ID. Generation and external DRC use the generator command declared by the single plugin used by the current graph.

**Tech Stack:** Qt 6, C++23, xmake, existing XML/JSON module bundle loaders, existing Ruby NoC generator.

---

## File Structure

- Create `qt/inc/plugins/plugindescriptor.h`: value types for plugin manifest, generator, and native metadata.
- Create `qt/inc/plugins/pluginregistry.h` and `qt/src/plugins/pluginregistry.cpp`: startup plugin discovery, manifest parsing, path resolution, duplicate plugin ID handling.
- Modify `qt/inc/modules/moduleregistry.h` and `qt/src/modules/moduleregistry.cpp`: store `pluginId` on module types, import modules from plugins, skip duplicate module type names.
- Create `qt/inc/plugins/generatorrunner.h` and `qt/src/plugins/generatorrunner.cpp`: select a graph plugin, substitute generator args, and expose command data for `QProcess`.
- Modify `qt/src/app/mainwindow.cpp`: run plugin generator instead of hard-coded `../framework`.
- Modify `qt/src/validation/drcrunner.cpp`: run plugin generator for DRC instead of hard-coded `../framework`.
- Create `qt/test/plugin_test.cpp`: tests plugin discovery, module import ownership, duplicate handling, and generator argument substitution.
- Modify `qt/xmake.lua`: upgrade app/tests to C++23 and add `plugin_test`.
- Create `plugins/noc/**`: bundled NoC plugin using existing `qt/bundles` and `framework` files.
- Modify `qt/doc/README.md` and `qt/doc/architecture.md`: document plugin discovery and bundled NoC plugin.

---

### Task 1: C++23 Build Configuration

**Files:**
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Update build language standard**

Replace both `set_languages("c++20")` calls with:

```lua
set_languages("c++23")
```

- [ ] **Step 2: Verify graph test still builds**

Run: `xmake build graph_test`

Expected: build exits 0.

- [ ] **Step 3: Commit**

```bash
git add qt/xmake.lua
git commit -m "build: use c++23 for qt editor"
```

---

### Task 2: Add Plugin Manifest Types and Failing Tests

**Files:**
- Create: `qt/inc/plugins/plugindescriptor.h`
- Create: `qt/inc/plugins/pluginregistry.h`
- Create: `qt/src/plugins/pluginregistry.cpp`
- Create: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write the failing plugin discovery test**

Create `qt/test/plugin_test.cpp` with test helpers that create temporary plugin directories and assert manifest loading:

```cpp
#include "plugins/pluginregistry.h"
#include "modules/moduleprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

void testPluginManifestLoadsRelativePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    require(root.mkpath("demo/graphics"), "failed to create plugin dirs");
    writeFile(root.filePath("demo/modules.xml"), "<module-bundle/>");
    writeFile(root.filePath("demo/plugin.json"), R"json({
      "id": "finepaper.demo",
      "name": "Demo",
      "version": "1.0",
      "modules": "modules.xml",
      "graphics": "graphics",
      "generator": {"command": "ruby", "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}"]},
      "native": {"enabled": true, "library": "libdemo.so"}
    })json");

    const QList<PluginDescriptor> plugins = PluginRegistry::discover({temp.path()});
    require(plugins.size() == 1, "expected one plugin");
    require(plugins.first().id == "finepaper.demo", "plugin id should load");
    require(QFileInfo(plugins.first().modulesPath).isAbsolute(), "modules path should be absolute");
    require(QFileInfo(plugins.first().graphicsPath).isAbsolute(), "graphics path should be absolute");
    require(plugins.first().native.enabled, "native metadata should be retained");
    require(plugins.first().generator.hasGenerator(), "generator should be retained");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testPluginManifestLoadsRelativePaths();
    } catch (const std::exception& error) {
        std::cerr << "plugin_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "plugin_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target**

Add an `add_qt_test_target("plugin_test", ...)` block to `qt/xmake.lua` with `qt/test/plugin_test.cpp`, `src/plugins/pluginregistry.cpp`, and existing module provider dependencies.

- [ ] **Step 3: Run the failing test build**

Run: `xmake build plugin_test`

Expected: FAIL because `plugins/pluginregistry.h` does not exist yet.

- [ ] **Step 4: Implement manifest value types and registry**

Add `PluginDescriptor`, `PluginGeneratorDescriptor`, `PluginNativeDescriptor`, and `PluginRegistry::discover(const QStringList&)`. Manifest paths resolve relative to the plugin directory. Duplicate plugin IDs keep the first loaded descriptor.

- [ ] **Step 5: Run plugin test**

Run: `xmake run plugin_test`

Expected: `plugin_test passed`.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/plugins qt/src/plugins qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat(qt): discover directory plugins"
```

---

### Task 3: Import Module Types From Plugins

**Files:**
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/src/modules/moduleregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Add failing module ownership and duplicate tests**

Extend `plugin_test.cpp` to create two plugin directories with module XML. Assert loaded module types receive `pluginId`, and duplicate type names keep the first plugin owner.

```cpp
void testModuleTypesKeepPluginOwnershipAndSkipDuplicates() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    require(root.mkpath("first"), "failed to create first plugin");
    require(root.mkpath("second"), "failed to create second plugin");
    const QByteArray moduleXml = R"xml(<module-bundle>
      <module name="Shared" palette_label="Shared" graph_group="demo">
        <ports><port id="in" direction="input" type="bus" name="IN"/></ports>
        <parameters><parameter name="width" type="int" default="32"/></parameters>
      </module>
    </module-bundle>)xml";
    writeFile(root.filePath("first/modules.xml"), moduleXml);
    writeFile(root.filePath("second/modules.xml"), moduleXml);
    writeFile(root.filePath("first/plugin.json"), R"json({"id":"first","name":"First","version":"1","modules":"modules.xml"})json");
    writeFile(root.filePath("second/plugin.json"), R"json({"id":"second","name":"Second","version":"1","modules":"modules.xml"})json");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(PluginRegistry::discover({temp.path()}));
    const ModuleType* type = registry.getType("Shared");
    require(type != nullptr, "Shared type should load");
    require(type->pluginId == "first", "duplicate type should keep first plugin owner");
    require(registry.availableTypes().size() == 1, "duplicate type name should be skipped");
}
```

- [ ] **Step 2: Run the failing test**

Run: `xmake run plugin_test`

Expected: FAIL because `ModuleType::pluginId`, test constructor, or `loadPlugins()` is missing.

- [ ] **Step 3: Implement module registry plugin import**

Add `QString pluginId` to `ModuleType`. Add a testable `ModuleRegistry(LoadMode)` constructor and `loadPlugins(const QList<PluginDescriptor>&)`. Default singleton construction loads discovered plugins first, then legacy bundle fallback if no plugin modules were loaded. `registerType()` skips duplicate names.

- [ ] **Step 4: Run plugin and graph tests**

Run:

```bash
xmake run plugin_test
xmake run graph_test
```

Expected: both tests print passed.

- [ ] **Step 5: Commit**

```bash
git add qt/inc/modules/moduleregistry.h qt/src/modules/moduleregistry.cpp qt/test/plugin_test.cpp
git commit -m "feat(qt): load module types from plugins"
```

---

### Task 4: Migrate Current NoC Support Into a Built-In Plugin

**Files:**
- Create: `plugins/noc/plugin.json`
- Copy: `qt/bundles/modules.xml` to `plugins/noc/modules.xml`
- Copy: `qt/bundles/graphics/*` to `plugins/noc/graphics/`
- Copy: `framework/*` to `plugins/noc/generator/`

- [ ] **Step 1: Create bundled plugin files**

Copy existing NoC bundle and generator files into `plugins/noc/`. Create `plugin.json`:

```json
{
  "id": "finepaper.noc",
  "name": "NoC",
  "version": "1.0",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "args": [
      "generator/bin/generate",
      "-i",
      "{input}",
      "-o",
      "{output}",
      "-t",
      "generator/template"
    ]
  },
  "native": {
    "enabled": false,
    "library": ""
  }
}
```

- [ ] **Step 2: Run graph test**

Run: `xmake run graph_test`

Expected: `graph_test passed`, with XP and Endpoint loaded from `plugins/noc`.

- [ ] **Step 3: Run moved Ruby generator tests**

Run: `ruby test/test_generator.rb` from `plugins/noc/generator`.

Expected: 0 failures.

- [ ] **Step 4: Commit**

```bash
git add plugins/noc
git commit -m "feat: bundle noc plugin"
```

---

### Task 5: Plugin-Aware Generator Selection

**Files:**
- Create: `qt/inc/plugins/generatorrunner.h`
- Create: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing generator command tests**

Extend `plugin_test.cpp` with a test that builds generator arguments from a descriptor and substitutes `{input}` and `{output}`.

```cpp
void testGeneratorArgumentsSubstituteInputAndOutput() {
    PluginGeneratorDescriptor generator;
    generator.command = "ruby";
    generator.args = {"generator/bin/generate", "-i", "{input}", "-o", "{output}", "-t", "generator/template"};
    const QStringList args = generator.arguments("/tmp/design.json", "/tmp/out");
    require(args.contains("/tmp/design.json"), "input placeholder should be substituted");
    require(args.contains("/tmp/out"), "output placeholder should be substituted");
    require(args.first() == "generator/bin/generate", "literal relative args should be preserved");
}
```

- [ ] **Step 2: Run failing test**

Run: `xmake run plugin_test`

Expected: FAIL because `PluginGeneratorDescriptor::arguments()` is missing.

- [ ] **Step 3: Implement generator runner**

Implement `PluginGeneratorDescriptor::arguments()`. Add `GeneratorRunner::resolveForGraph(const Graph*)`, returning a success command only when exactly one plugin with a generator is used by graph modules. Use `ModuleRegistry` to map module type names to plugin IDs.

- [ ] **Step 4: Wire generation and DRC**

Update `MainWindow::generateVerilog()` and `DRCRunner::validate()` to use `GeneratorRunner` and run `QProcess` with the plugin root as working directory. Preserve existing log panel and error behavior.

- [ ] **Step 5: Run tests**

Run:

```bash
xmake run plugin_test
xmake run graph_test
xmake run validation_test
```

Expected: all print passed.

- [ ] **Step 6: Commit**

```bash
git add qt/inc/plugins/generatorrunner.h qt/src/plugins/generatorrunner.cpp qt/src/app/mainwindow.cpp qt/inc/validation/drcrunner.h qt/src/validation/drcrunner.cpp qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat(qt): run plugin generators"
```

---

### Task 6: Documentation and Final Verification

**Files:**
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`

- [ ] **Step 1: Update docs**

Document `FINEPAPER_PLUGIN_PATH`, repository-local `plugins/`, `plugin.json`, and the bundled NoC plugin.

- [ ] **Step 2: Run full verification**

Run:

```bash
ruby test/test_generator.rb
ruby test/test_generator.rb
xmake build graph_test
xmake run graph_test
xmake build commandmanager_test
xmake run commandmanager_test
xmake build validation_test
xmake run validation_test
xmake build plugin_test
xmake run plugin_test
```

Run the first Ruby command from `framework/` and the second from `plugins/noc/generator/`.

Expected: Ruby tests report 0 failures; Qt tests print passed.

- [ ] **Step 3: Commit docs**

```bash
git add qt/doc/README.md qt/doc/architecture.md
git commit -m "docs: describe qt plugin loading"
```

---

## Self-Review

- Spec coverage: startup directory plugins, native metadata reservation, NoC plugin migration, UI layout ownership, generator command selection, and C++23 are covered.
- Placeholder scan: the plan intentionally uses concrete file paths, commands, and expected outputs.
- Type consistency: plugin descriptor names are used consistently across tests and implementation tasks.
