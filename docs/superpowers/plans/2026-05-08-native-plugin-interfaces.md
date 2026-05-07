# Native Plugin Interfaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. When dispatching subagents, set reasoning effort to `high`; use `xhigh` for review or architecture-critical tasks.

**Goal:** Introduce native C++ plugin libraries so plugins can own project-state parsing, parameter metadata, validation, and generation without expanding `Graph` into a universal IP model.

**Architecture:** Keep directory manifests as discovery and compatibility metadata. Add a Qt `QPluginLoader` boundary for native plugin libraries that expose stable Finepaper interfaces; when a native plugin is unavailable, the current manifest adapter and external command paths continue to work. The host passes `Graph + ProjectPluginStateRecord` to plugin interfaces and keeps undo, dirty tracking, project file IO, and missing-plugin state preservation in core.

**Tech Stack:** C++23, Qt 6 plugin interfaces, Qt Widgets, xmake, existing `PluginRegistry`, `ProjectStateService`, `GeneratorRunner`, `DRCRunner`, and Qt test binaries.

---

## Spec Validation

Validated previous spec: `docs/superpowers/specs/2026-05-07-plugin-owned-project-state-design.md`.

Already implemented on `master`:

- `ProjectDocument::pluginStates` persists opaque plugin-owned state.
- `ProjectStateService` owns editable plugin state outside `Graph`.
- `PropertyPanel` renders global parameters through `IPluginProjectAdapter` metadata.
- Plugin parameter edits are undoable and mark the document dirty.
- Generation and validation receive graph plus plugin state.
- Generated Verilog output folders receive a `.fpproj` snapshot through `writeGeneratedProjectSnapshot()`.

This plan implements the remaining architecture item from that spec: native C++ plugin interfaces. It does not redesign project file format or graph storage.

## File Structure

- Create `qt/inc/plugins/nativeplugininterface.h`: stable Qt plugin interface and optional service interfaces.
- Create `qt/inc/plugins/nativepluginloader.h`: load result, loaded plugin handle, and loader API.
- Create `qt/src/plugins/nativepluginloader.cpp`: `QPluginLoader` integration and manifest-library path resolution.
- Modify `qt/inc/plugins/pluginprojectadapter.h`: expand adapter interface with state validation/migration hooks that native plugins can override.
- Modify `qt/src/plugins/manifestpluginprojectadapter.cpp`: keep manifest fallback behavior and default no-op validation/migration.
- Modify `qt/inc/plugins/plugindescriptor.h`: store resolved native library path and load status text.
- Modify `qt/src/plugins/pluginregistry.cpp`: resolve native library paths during manifest discovery.
- Modify `qt/inc/app/mainwindow.h` and `qt/src/app/mainwindow.cpp`: own loaded native plugin handles and prefer native project adapters when available.
- Modify `qt/inc/plugins/generatorrunner.h` and `qt/src/plugins/generatorrunner.cpp`: allow native generators before manifest command fallback.
- Modify `qt/inc/validation/drcrunner.h` and `qt/src/validation/drcrunner.cpp`: allow native validators before manifest command fallback.
- Modify `qt/test/plugin_test.cpp`: native interface, loader, fallback, and adapter-selection tests.
- Modify `qt/test/validation_test.cpp`: native validator dispatch test.
- Modify `qt/test/projectdocument_test.cpp`: native adapter migration test.
- Modify `qt/xmake.lua`: add new plugin loader sources to app and affected test targets.

---

### Task 1: Define Native Plugin API And Loader

**Files:**
- Create: `qt/inc/plugins/nativeplugininterface.h`
- Create: `qt/inc/plugins/nativepluginloader.h`
- Create: `qt/src/plugins/nativepluginloader.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing interface/loader test**

Add includes to `qt/test/plugin_test.cpp`:

```cpp
#include "plugins/nativeplugininterface.h"
#include "plugins/nativepluginloader.h"
```

Add this concrete test plugin in the anonymous namespace:

```cpp
class TestNativePlugin final : public IFinepaperNativePlugin {
public:
    QString pluginId() const override {
        return QStringLiteral("finepaper.native-test");
    }

    std::unique_ptr<IPluginProjectAdapter> createProjectAdapter(const PluginDescriptor& plugin) override {
        return std::make_unique<ManifestPluginProjectAdapter>(plugin);
    }
};
```

Add this test:

```cpp
void testNativePluginHandleWrapsInterfaceForTests() {
    auto plugin = std::make_unique<TestNativePlugin>();
    NativePluginHandle handle = NativePluginHandle::fromOwnedInstanceForTest(
        QStringLiteral("/tmp/libfinepaper_native_test.so"),
        std::move(plugin));

    require(handle.isLoaded(), "test native plugin handle should be loaded");
    require(handle.libraryPath() == QStringLiteral("/tmp/libfinepaper_native_test.so"),
            "test native plugin handle should retain library path");
    require(handle.plugin()->pluginId() == QStringLiteral("finepaper.native-test"),
            "test native plugin should expose plugin id");
}
```

Call it from `main()` before `testStartupDiagnosticsListLoadedPluginsAndIpTypes()`:

```cpp
testNativePluginHandleWrapsInterfaceForTests();
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: compile fails because `nativeplugininterface.h`, `nativepluginloader.h`, and `NativePluginHandle` do not exist.

- [ ] **Step 3: Add native plugin interface**

Create `qt/inc/plugins/nativeplugininterface.h`:

```cpp
// Native plugin interfaces loaded through Qt's plugin system.
#pragma once

#include "project/pluginstate.h"
#include "plugins/pluginprojectadapter.h"
#include "validation/validationresult.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>

class Graph;
struct PluginDescriptor;

struct NativeGenerationResult {
    bool handled = false;
    bool success = false;
    QString error;
    QStringList artifacts;
};

class IPluginGenerator {
public:
    virtual ~IPluginGenerator() = default;
    virtual NativeGenerationResult generate(const Graph& graph,
                                            const QVector<ProjectPluginStateRecord>& pluginStates,
                                            const QString& outputDirectory,
                                            const QString& designName) = 0;
};

class IPluginValidator {
public:
    virtual ~IPluginValidator() = default;
    virtual QList<ValidationResult> validate(const Graph& graph,
                                             const QVector<ProjectPluginStateRecord>& pluginStates) = 0;
};

class IFinepaperNativePlugin {
public:
    virtual ~IFinepaperNativePlugin() = default;
    virtual QString pluginId() const = 0;
    virtual std::unique_ptr<IPluginProjectAdapter> createProjectAdapter(const PluginDescriptor& plugin) = 0;
    virtual std::unique_ptr<IPluginGenerator> createGenerator(const PluginDescriptor& plugin) {
        Q_UNUSED(plugin);
        return nullptr;
    }
    virtual std::unique_ptr<IPluginValidator> createValidator(const PluginDescriptor& plugin) {
        Q_UNUSED(plugin);
        return nullptr;
    }
};

#define FinepaperNativePlugin_iid "org.finepaper.IFinepaperNativePlugin/1.0"
Q_DECLARE_INTERFACE(IFinepaperNativePlugin, FinepaperNativePlugin_iid)
```

- [ ] **Step 4: Add loader handle API**

Create `qt/inc/plugins/nativepluginloader.h`:

```cpp
// Loads native Finepaper plugin libraries and owns their Qt loader lifetime.
#pragma once

#include "plugins/nativeplugininterface.h"
#include "plugins/plugindescriptor.h"

#include <QHash>
#include <QList>
#include <QString>
#include <memory>

class QPluginLoader;

class NativePluginHandle {
public:
    NativePluginHandle();
    ~NativePluginHandle();
    NativePluginHandle(NativePluginHandle&&) noexcept;
    NativePluginHandle& operator=(NativePluginHandle&&) noexcept;
    NativePluginHandle(const NativePluginHandle&) = delete;
    NativePluginHandle& operator=(const NativePluginHandle&) = delete;

    static NativePluginHandle fromOwnedInstanceForTest(QString libraryPath,
                                                       std::unique_ptr<IFinepaperNativePlugin> plugin);

    bool isLoaded() const;
    QString libraryPath() const;
    IFinepaperNativePlugin* plugin() const;

private:
    friend class NativePluginLoader;
    explicit NativePluginHandle(QString libraryPath,
                                std::unique_ptr<QPluginLoader> loader,
                                IFinepaperNativePlugin* plugin);

    QString m_libraryPath;
    std::unique_ptr<QPluginLoader> m_loader;
    std::unique_ptr<IFinepaperNativePlugin> m_ownedPluginForTest;
    IFinepaperNativePlugin* m_plugin = nullptr;
};

struct NativePluginLoadResult {
    bool success = false;
    QString pluginId;
    QString libraryPath;
    QString error;
    NativePluginHandle handle;
};

class NativePluginLoader {
public:
    static NativePluginLoadResult load(const PluginDescriptor& descriptor);
    static QHash<QString, NativePluginHandle> loadAll(const QList<PluginDescriptor>& descriptors,
                                                      QStringList* diagnostics = nullptr);
};
```

- [ ] **Step 5: Implement loader**

Create `qt/src/plugins/nativepluginloader.cpp`:

```cpp
#include "plugins/nativepluginloader.h"

#include <QFileInfo>
#include <QPluginLoader>
#include <QObject>

NativePluginHandle::NativePluginHandle() = default;
NativePluginHandle::~NativePluginHandle() = default;
NativePluginHandle::NativePluginHandle(NativePluginHandle&&) noexcept = default;
NativePluginHandle& NativePluginHandle::operator=(NativePluginHandle&&) noexcept = default;

NativePluginHandle::NativePluginHandle(QString libraryPath,
                                       std::unique_ptr<QPluginLoader> loader,
                                       IFinepaperNativePlugin* plugin)
    : m_libraryPath(std::move(libraryPath)),
      m_loader(std::move(loader)),
      m_plugin(plugin) {}

NativePluginHandle NativePluginHandle::fromOwnedInstanceForTest(
    QString libraryPath,
    std::unique_ptr<IFinepaperNativePlugin> plugin) {
    NativePluginHandle handle;
    handle.m_libraryPath = std::move(libraryPath);
    handle.m_plugin = plugin.get();
    handle.m_ownedPluginForTest = std::move(plugin);
    return handle;
}

bool NativePluginHandle::isLoaded() const {
    return m_plugin != nullptr;
}

QString NativePluginHandle::libraryPath() const {
    return m_libraryPath;
}

IFinepaperNativePlugin* NativePluginHandle::plugin() const {
    return m_plugin;
}

NativePluginLoadResult NativePluginLoader::load(const PluginDescriptor& descriptor) {
    NativePluginLoadResult result;
    result.pluginId = descriptor.id;
    result.libraryPath = descriptor.native.library;

    if (!descriptor.native.enabled) {
        result.error = QStringLiteral("Native plugin is disabled for '%1'.").arg(descriptor.id);
        return result;
    }
    if (descriptor.native.library.isEmpty()) {
        result.error = QStringLiteral("Native plugin '%1' has no library path.").arg(descriptor.id);
        return result;
    }
    if (!QFileInfo::exists(descriptor.native.library)) {
        result.error = QStringLiteral("Native plugin library does not exist: %1")
                           .arg(descriptor.native.library);
        return result;
    }

    auto loader = std::make_unique<QPluginLoader>(descriptor.native.library);
    QObject* instance = loader->instance();
    if (!instance) {
        result.error = loader->errorString();
        return result;
    }

    auto* plugin = qobject_cast<IFinepaperNativePlugin*>(instance);
    if (!plugin) {
        result.error = QStringLiteral("Library '%1' does not implement %2.")
                           .arg(descriptor.native.library, QStringLiteral(FinepaperNativePlugin_iid));
        loader->unload();
        return result;
    }
    if (plugin->pluginId() != descriptor.id) {
        result.error = QStringLiteral("Native plugin id mismatch: manifest '%1', library '%2'.")
                           .arg(descriptor.id, plugin->pluginId());
        loader->unload();
        return result;
    }

    result.success = true;
    result.handle = NativePluginHandle(descriptor.native.library, std::move(loader), plugin);
    return result;
}

QHash<QString, NativePluginHandle> NativePluginLoader::loadAll(
    const QList<PluginDescriptor>& descriptors,
    QStringList* diagnostics) {
    QHash<QString, NativePluginHandle> handles;
    for (const PluginDescriptor& descriptor : descriptors) {
        if (!descriptor.native.enabled) {
            continue;
        }
        NativePluginLoadResult result = load(descriptor);
        if (!result.success) {
            if (diagnostics) {
                diagnostics->append(QStringLiteral("[Plugin] %1").arg(result.error));
            }
            continue;
        }
        handles.insert(result.pluginId, std::move(result.handle));
    }
    return handles;
}
```

- [ ] **Step 6: Add xmake entries**

In `qt/xmake.lua`, add `src/**/nativepluginloader.cpp` and `inc/**/nativeplugininterface.h`, `inc/**/nativepluginloader.h` to the app and `plugin_test` target file lists. The app target already includes `src/**.cpp` and `inc/**.h`; only `plugin_test` needs explicit entries:

```lua
"src/**/nativepluginloader.cpp",
"inc/**/nativeplugininterface.h",
"inc/**/nativepluginloader.h",
```

- [ ] **Step 7: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: `plugin_test passed`.

Commit:

```bash
git add qt/inc/plugins/nativeplugininterface.h qt/inc/plugins/nativepluginloader.h qt/src/plugins/nativepluginloader.cpp qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat: add native plugin loader"
```

---

### Task 2: Resolve Native Library Paths From Manifests

**Files:**
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Write failing path-resolution test**

Extend `testPluginManifestLoadsRelativePaths()` in `qt/test/plugin_test.cpp` after the existing native assertions:

```cpp
    require(QFileInfo(plugins.first().native.library).isAbsolute(),
            "native library path should be resolved relative to plugin root");
    require(plugins.first().native.library.endsWith(QStringLiteral("demo/libdemo.so")),
            "native library path should keep manifest library filename");
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: fail with `native library path should be resolved relative to plugin root`.

- [ ] **Step 3: Resolve native library path**

In `qt/src/plugins/pluginregistry.cpp`, replace the native library assignment in `loadManifest()` with:

```cpp
    descriptor.native.library = resolvePath(descriptor.rootPath,
                                            native.value(QStringLiteral("library")).toString());
```

- [ ] **Step 4: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: `plugin_test passed`.

Commit:

```bash
git add qt/src/plugins/pluginregistry.cpp qt/test/plugin_test.cpp
git commit -m "fix: resolve native plugin library paths"
```

---

### Task 3: Prefer Native Project Adapters With Manifest Fallback

**Files:**
- Modify: `qt/inc/plugins/pluginprojectadapter.h`
- Modify: `qt/src/plugins/manifestpluginprojectadapter.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/plugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing adapter-selection test**

Add this test plugin adapter in `qt/test/plugin_test.cpp`:

```cpp
class NativeLabelAdapter final : public IPluginProjectAdapter {
public:
    QVector<PluginParameterSection> parameterSections() const override {
        PluginParameterField field;
        field.name = QStringLiteral("native_width");
        field.label = QStringLiteral("Native Width");
        field.type = QStringLiteral("int");
        field.defaultValue = 128;

        PluginParameterSection section;
        section.pluginId = QStringLiteral("finepaper.native-adapter");
        section.instanceId = QStringLiteral("native_0");
        section.id = QStringLiteral("global_parameters");
        section.label = QStringLiteral("Native Adapter");
        section.fields = {field};
        return {section};
    }
};

class NativeAdapterPlugin final : public IFinepaperNativePlugin {
public:
    QString pluginId() const override {
        return QStringLiteral("finepaper.native-adapter");
    }

    std::unique_ptr<IPluginProjectAdapter> createProjectAdapter(const PluginDescriptor&) override {
        return std::make_unique<NativeLabelAdapter>();
    }
};
```

Add this helper declaration to the production header in the implementation step, then test it now:

```cpp
void testNativeProjectAdapterOverridesManifestAdapter() {
    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.native-adapter");
    plugin.name = QStringLiteral("Manifest Label");

    PluginInstanceParameterDescriptor manifestField;
    manifestField.name = QStringLiteral("manifest_width");
    manifestField.type = QStringLiteral("int");
    manifestField.defaultValue = 32;
    plugin.instanceParameters.insert(manifestField.name, manifestField);

    QHash<QString, NativePluginHandle> handles;
    handles.insert(plugin.id,
                   NativePluginHandle::fromOwnedInstanceForTest(
                       QStringLiteral("/tmp/libnative_adapter.so"),
                       std::make_unique<NativeAdapterPlugin>()));

    std::vector<std::unique_ptr<IPluginProjectAdapter>> adapters =
        createProjectAdaptersForPlugins({plugin}, handles);

    require(adapters.size() == 1, "one project adapter should be created");
    const QVector<PluginParameterSection> sections = adapters.front()->parameterSections();
    require(sections.size() == 1, "native adapter should provide one section");
    require(sections.first().label == QStringLiteral("Native Adapter"),
            "native adapter should override manifest adapter");
    require(sections.first().fields.first().name == QStringLiteral("native_width"),
            "native adapter field should be visible");
}
```

Call it from `main()` after `testNativePluginHandleWrapsInterfaceForTests()`.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: compile fails because `createProjectAdaptersForPlugins()` does not exist.

- [ ] **Step 3: Add adapter factory API**

Create this declaration in `qt/inc/plugins/pluginprojectadapter.h` after `ManifestPluginProjectAdapter`:

```cpp
class NativePluginHandle;

std::vector<std::unique_ptr<IPluginProjectAdapter>> createProjectAdaptersForPlugins(
    const QList<PluginDescriptor>& plugins,
    const QHash<QString, NativePluginHandle>& nativePlugins);
```

Add missing includes to the same header:

```cpp
#include <QHash>
#include <QList>
#include <memory>
#include <vector>
```

- [ ] **Step 4: Implement native-first factory**

Add to `qt/src/plugins/manifestpluginprojectadapter.cpp`:

```cpp
#include "plugins/nativepluginloader.h"

std::vector<std::unique_ptr<IPluginProjectAdapter>> createProjectAdaptersForPlugins(
    const QList<PluginDescriptor>& plugins,
    const QHash<QString, NativePluginHandle>& nativePlugins) {
    std::vector<std::unique_ptr<IPluginProjectAdapter>> adapters;
    adapters.reserve(static_cast<size_t>(plugins.size()));

    for (const PluginDescriptor& plugin : plugins) {
        auto nativeIt = nativePlugins.constFind(plugin.id);
        if (nativeIt != nativePlugins.constEnd() && nativeIt.value().plugin()) {
            std::unique_ptr<IPluginProjectAdapter> adapter =
                nativeIt.value().plugin()->createProjectAdapter(plugin);
            if (adapter) {
                adapters.push_back(std::move(adapter));
                continue;
            }
        }
        adapters.push_back(std::make_unique<ManifestPluginProjectAdapter>(plugin));
    }

    return adapters;
}
```

- [ ] **Step 5: Wire MainWindow to native loader**

In `qt/inc/app/mainwindow.h`, include the native loader header:

```cpp
#include "plugins/nativepluginloader.h"
```

Add this member:

```cpp
QHash<QString, NativePluginHandle> m_nativePluginHandles;
```

In `MainWindow::setupPanels()` in `qt/src/app/mainwindow.cpp`, replace direct manifest adapter construction with:

```cpp
    QStringList nativeDiagnostics;
    m_nativePluginHandles = NativePluginLoader::loadAll(PluginRegistry::instance().plugins(),
                                                        &nativeDiagnostics);
    m_pluginProjectAdapters = createProjectAdaptersForPlugins(PluginRegistry::instance().plugins(),
                                                              m_nativePluginHandles);

    QVector<IPluginProjectAdapter*> pluginProjectAdapters;
    pluginProjectAdapters.reserve(static_cast<qsizetype>(m_pluginProjectAdapters.size()));
    for (const auto& adapter : m_pluginProjectAdapters) {
        pluginProjectAdapters.push_back(adapter.get());
    }
```

After `m_logPanel = new LogPanel(this);`, append diagnostics:

```cpp
    for (const QString& diagnostic : nativeDiagnostics) {
        m_logPanel->appendMessage(diagnostic, QColor(180, 120, 20));
    }
```

- [ ] **Step 6: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: `plugin_test passed`; app target builds.

Commit:

```bash
git add qt/inc/plugins/pluginprojectadapter.h qt/src/plugins/manifestpluginprojectadapter.cpp qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/test/plugin_test.cpp qt/xmake.lua
git commit -m "feat: prefer native plugin project adapters"
```

---

### Task 4: Add Native Project State Validation And Migration Hooks

**Files:**
- Modify: `qt/inc/plugins/pluginprojectadapter.h`
- Modify: `qt/src/plugins/manifestpluginprojectadapter.cpp`
- Modify: `qt/src/project/projectstateservice.cpp`
- Modify: `qt/test/projectdocument_test.cpp`

- [ ] **Step 1: Write failing migration test**

Add this adapter in `qt/test/projectdocument_test.cpp`:

```cpp
class MigratingProjectAdapter final : public IPluginProjectAdapter {
public:
    QVector<PluginParameterSection> parameterSections() const override {
        return {};
    }

    bool migrateState(ProjectPluginStateRecord& record, QString* error) const override {
        Q_UNUSED(error);
        record.schema = QStringLiteral("finepaper.demo-state-v2");
        QJsonObject globals = record.state.value(QStringLiteral("global_parameters")).toObject();
        globals.insert(QStringLiteral("migrated"), true);
        record.state.insert(QStringLiteral("global_parameters"), globals);
        return true;
    }
};
```

Add this test:

```cpp
void testProjectStateServiceMigratesStateThroughAdapter() {
    ProjectDocument document = validProjectDocument();
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.demo");
    state.instanceId = QStringLiteral("demo_0");
    state.schema = QStringLiteral("finepaper.demo-state-v1");
    state.state = QJsonObject{{QStringLiteral("global_parameters"), QJsonObject{}}};
    document.pluginStates.push_back(state);

    ProjectStateService service;
    MigratingProjectAdapter adapter;
    QString error;
    require(service.loadFromDocument(document, {&adapter}, &error),
            qPrintable(error));

    ProjectDocument saved = validProjectDocument();
    service.writeToDocument(saved);
    require(saved.pluginStates.first().schema == QStringLiteral("finepaper.demo-state-v2"),
            "adapter should migrate schema");
    require(saved.pluginStates.first()
                .state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("migrated"))
                .toBool(),
            "adapter should migrate state content");
}
```

Call it from `main()` after the existing `ProjectStateService` tests.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: compile fails because `IPluginProjectAdapter::migrateState()` and the overloaded `ProjectStateService::loadFromDocument()` do not exist.

- [ ] **Step 3: Add adapter hooks**

In `qt/inc/plugins/pluginprojectadapter.h`, add to `IPluginProjectAdapter`:

```cpp
    virtual bool migrateState(ProjectPluginStateRecord& record, QString* error) const {
        Q_UNUSED(record);
        Q_UNUSED(error);
        return true;
    }

    virtual bool validateState(const ProjectPluginStateRecord& record, QString* error) const {
        Q_UNUSED(record);
        Q_UNUSED(error);
        return true;
    }
```

Add include:

```cpp
#include "project/pluginstate.h"
```

- [ ] **Step 4: Add ProjectStateService adapter-aware load**

In `qt/inc/project/projectstateservice.h`, add:

```cpp
class IPluginProjectAdapter;

bool loadFromDocument(const ProjectDocument& document,
                      const QVector<IPluginProjectAdapter*>& adapters,
                      QString* error);
```

In `qt/src/project/projectstateservice.cpp`, implement:

```cpp
bool ProjectStateService::loadFromDocument(const ProjectDocument& document,
                                           const QVector<IPluginProjectAdapter*>& adapters,
                                           QString* error) {
    m_pluginStates = document.pluginStates;

    for (ProjectPluginStateRecord& record : m_pluginStates) {
        for (const IPluginProjectAdapter* adapter : adapters) {
            if (!adapter) {
                continue;
            }
            if (!adapter->migrateState(record, error)) {
                return false;
            }
            if (!adapter->validateState(record, error)) {
                return false;
            }
        }
    }

    return true;
}
```

Keep the existing `void loadFromDocument(const ProjectDocument& document)` by delegating:

```cpp
void ProjectStateService::loadFromDocument(const ProjectDocument& document) {
    QString error;
    loadFromDocument(document, {}, &error);
}
```

- [ ] **Step 5: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
```

Expected: `projectdocument_test passed`.

Commit:

```bash
git add qt/inc/plugins/pluginprojectadapter.h qt/inc/project/projectstateservice.h qt/src/project/projectstateservice.cpp qt/test/projectdocument_test.cpp
git commit -m "feat: let plugin adapters migrate project state"
```

---

### Task 5: Dispatch Native Validation Before External DRC

**Files:**
- Modify: `qt/inc/validation/drcrunner.h`
- Modify: `qt/src/validation/drcrunner.cpp`
- Modify: `qt/test/validation_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Write failing native validator test**

Add this validator to `qt/test/validation_test.cpp`:

```cpp
class AlwaysFailNativeValidator final : public IPluginValidator {
public:
    QList<ValidationResult> validate(const Graph& graph,
                                     const QVector<ProjectPluginStateRecord>& pluginStates) override {
        Q_UNUSED(graph);
        Q_UNUSED(pluginStates);
        return {ValidationResult(ValidationSeverity::Error,
                                 QStringLiteral("native validator was used"),
                                 QStringLiteral("rave_0_0"),
                                 QStringLiteral("NativeDRC"))};
    }
};
```

Add this test:

```cpp
void testDrcRunnerUsesNativeValidatorWhenProvided() {
    Graph graph;
    require(graph.addModule(makeRaveTile(QStringLiteral("rave_0_0"), 0, 0)),
            "failed to add RaveTile");

    DRCRunner runner;
    AlwaysFailNativeValidator validator;
    const QList<ValidationResult> results =
        runner.validate(&graph, ravenocPluginState(), &validator);

    require(results.size() == 1, "native validator should return one result");
    require(results.first().message() == QStringLiteral("native validator was used"),
            "native validator result should be returned directly");
    require(results.first().ruleName() == QStringLiteral("NativeDRC"),
            "native validator rule name should be preserved");
}
```

Call it from `main()` before external DRC tests.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt validation_test
```

Expected: compile fails because `IPluginValidator` is not included and `DRCRunner::validate()` has no native validator overload.

- [ ] **Step 3: Add optional native validator argument**

In `qt/inc/validation/drcrunner.h`, include native interface:

```cpp
#include "plugins/nativeplugininterface.h"
```

Change signature:

```cpp
QList<ValidationResult> validate(const Graph* graph,
                                 const QVector<ProjectPluginStateRecord>& pluginStates,
                                 IPluginValidator* nativeValidator = nullptr);
```

In `qt/src/validation/drcrunner.cpp`, update the function signature and add at the top after clearing `m_externalToInternalIds`:

```cpp
    if (nativeValidator && graph) {
        return nativeValidator->validate(*graph, pluginStates);
    }
```

- [ ] **Step 4: Run test and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt validation_test
```

Expected: `validation_test passed`.

Commit:

```bash
git add qt/inc/validation/drcrunner.h qt/src/validation/drcrunner.cpp qt/test/validation_test.cpp qt/xmake.lua
git commit -m "feat: support native plugin validation"
```

---

### Task 6: Dispatch Native Generation Before External Command

**Files:**
- Modify: `qt/inc/plugins/generatorrunner.h`
- Modify: `qt/src/plugins/generatorrunner.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/test/plugin_test.cpp`

- [ ] **Step 1: Write failing native generator test**

Add this generator to `qt/test/plugin_test.cpp`:

```cpp
class RecordingNativeGenerator final : public IPluginGenerator {
public:
    NativeGenerationResult generate(const Graph& graph,
                                    const QVector<ProjectPluginStateRecord>& pluginStates,
                                    const QString& outputDirectory,
                                    const QString& designName) override {
        Q_UNUSED(graph);
        Q_UNUSED(pluginStates);
        Q_UNUSED(outputDirectory);
        NativeGenerationResult result;
        result.handled = true;
        result.success = true;
        result.artifacts = {designName + QStringLiteral(".sv")};
        return result;
    }
};
```

Add this test:

```cpp
void testGeneratorRunnerUsesNativeGeneratorWhenProvided() {
    Graph graph;
    auto module = std::make_unique<Module>(QStringLiteral("tile"), QStringLiteral("NativeTile"));
    require(graph.addModule(std::move(module)), "failed to add native module");

    RecordingNativeGenerator generator;
    const NativeGenerationResult result =
        GeneratorRunner::generateNative(&graph,
                                        {},
                                        QStringLiteral("/tmp/finepaper-native-out"),
                                        QStringLiteral("design"),
                                        &generator);

    require(result.handled, "native generator should handle generation");
    require(result.success, "native generator should succeed");
    require(result.artifacts.contains(QStringLiteral("design.sv")),
            "native generator artifacts should be preserved");
}
```

Call it from `main()` after generator runner command tests.

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: compile fails because `GeneratorRunner::generateNative()` does not exist.

- [ ] **Step 3: Add native generator helper**

In `qt/inc/plugins/generatorrunner.h`, include native interface and plugin state:

```cpp
#include "plugins/nativeplugininterface.h"
#include "project/pluginstate.h"
```

Add:

```cpp
static NativeGenerationResult generateNative(const Graph* graph,
                                             const QVector<ProjectPluginStateRecord>& pluginStates,
                                             const QString& outputDirectory,
                                             const QString& designName,
                                             IPluginGenerator* nativeGenerator);
```

In `qt/src/plugins/generatorrunner.cpp`, implement:

```cpp
NativeGenerationResult GeneratorRunner::generateNative(
    const Graph* graph,
    const QVector<ProjectPluginStateRecord>& pluginStates,
    const QString& outputDirectory,
    const QString& designName,
    IPluginGenerator* nativeGenerator) {
    NativeGenerationResult result;
    if (!nativeGenerator || !graph) {
        return result;
    }
    return nativeGenerator->generate(*graph, pluginStates, outputDirectory, designName);
}
```

- [ ] **Step 4: Wire MainWindow native generation path**

In `MainWindow::generateVerilog()` before resolving external `GeneratorCommand`, find a native generator from the active plugin handle:

```cpp
    std::unique_ptr<IPluginGenerator> nativeGenerator;
    auto nativeIt = m_nativePluginHandles.find(m_activePluginId);
    if (nativeIt != m_nativePluginHandles.end() && nativeIt.value().plugin()) {
        const PluginDescriptor* descriptor = PluginRegistry::instance().plugin(m_activePluginId);
        if (descriptor) {
            nativeGenerator = nativeIt.value().plugin()->createGenerator(*descriptor);
        }
    }
```

Then run it before external command fallback:

```cpp
    const NativeGenerationResult nativeResult =
        GeneratorRunner::generateNative(m_graph,
                                        m_projectStateService->pluginStates(),
                                        outputDirectory,
                                        designName,
                                        nativeGenerator.get());
    if (nativeResult.handled) {
        if (!nativeResult.success) {
            m_logPanel->appendMessage("[Generate] " + nativeResult.error, QColor(220, 50, 50));
            QMessageBox::warning(this, "Generation Failed", nativeResult.error);
            return;
        }
        const GeneratedProjectSnapshotResult projectSnapshot =
            writeGeneratedProjectSnapshot(*m_graph,
                                          outputDirectory,
                                          designName,
                                          m_projectStateService->pluginStates());
        if (!projectSnapshot.success) {
            m_logPanel->appendMessage("[Generate] Could not write project: " + projectSnapshot.error,
                                      QColor(220, 50, 50));
            QMessageBox::warning(this, "Project Snapshot Failed", projectSnapshot.error);
            return;
        }
        for (const QString& artifact : nativeResult.artifacts) {
            m_logPanel->appendMessage("[Generate] Artifact=" + artifact, QColor(70, 110, 190));
        }
        m_logPanel->appendMessage("[Generate] Project=" + projectSnapshot.path, QColor(70, 110, 190));
        statusBar()->showMessage("Generation complete.", 3000);
        return;
    }
```

- [ ] **Step 5: Run tests and commit**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: `plugin_test passed`; app target builds.

Commit:

```bash
git add qt/inc/plugins/generatorrunner.h qt/src/plugins/generatorrunner.cpp qt/src/app/mainwindow.cpp qt/test/plugin_test.cpp
git commit -m "feat: support native plugin generation"
```

---

### Task 7: Full Verification

**Files:**
- No planned source edits.

- [ ] **Step 1: Run focused Qt tests**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
CCACHE_DISABLE=1 xmake run -P qt projectdocument_test
CCACHE_DISABLE=1 xmake run -P qt validation_test
CCACHE_DISABLE=1 xmake run -P qt propertypanel_test
```

Expected:

```text
plugin_test passed
projectdocument_test passed
validation_test passed
propertypanel_test passed
```

- [ ] **Step 2: Run full Qt test suite**

Run:

```bash
CCACHE_DISABLE=1 xmake test -P qt
```

Expected: all Qt tests pass.

- [ ] **Step 3: Build app target**

Run:

```bash
CCACHE_DISABLE=1 xmake -P qt -r qt
```

Expected: app target builds successfully.

- [ ] **Step 4: Check diff hygiene**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` prints no output. `git status --short` shows only intended changes before the final commit.

- [ ] **Step 5: Commit final verification docs if needed**

If execution changes this plan or related docs, commit those doc edits:

```bash
git add docs/superpowers/plans/2026-05-08-native-plugin-interfaces.md docs/superpowers/specs/2026-05-07-plugin-owned-project-state-design.md
git commit -m "docs: plan native plugin interfaces"
```

## Self-Review

- Spec coverage: covers the remaining native C++ plugin-interface work from the 2026-05-07 spec while preserving the completed project-state and generated-project-snapshot behavior.
- Placeholder scan: no open-ended placeholder steps remain; each task has file paths, code, commands, expected failures, expected passes, and commit commands.
- Type consistency: the native interface names used by loader, adapter factory, DRC, generation, and tests match across tasks.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-08-native-plugin-interfaces.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh high-reasoning subagent per task, review between tasks, use xhigh for review.
2. **Inline Execution** - Execute tasks in this session using executing-plans, with checkpoints after each task.
