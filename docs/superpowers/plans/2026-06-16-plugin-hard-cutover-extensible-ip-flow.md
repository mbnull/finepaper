# Plugin Hard Cutover Extensible IP Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the hard cutover from first-party NoC adapters to a static-plugin, registry-driven IP integration flow where a new NoC package can be added without product Qt/C++ runtime edits.

**Architecture:** The runtime uses a minimal plugin kernel, typed service registry, extension point registry, capability coverage registry, and `DesignEditingService`. `PackagePlugin` publishes package facts only; `NoCPlugin` handles `noc.v1` through semantic descriptors; `MainWindow` renders contributions. Normal save, validation, and generation flow from `ProjectDesign`, not `Graph`.

**Tech Stack:** Qt 6/C++23, xmake Qt tests, Ruby package/spec generators, `ipcraft.package.v1`, `ipcraft.project.v1`, `ipcraft.diagnostics.v1`.

**Execution Requirement:** Execute in isolated worktree `/home/bnl/dev/finepaper/.worktrees/plugin-hard-cutover` on branch `plugin-hard-cutover`. Dispatch each implementation and review subagent with model `gpt-5.5` and reasoning effort `xhigh`.

---

## Source Spec

- Design spec: `docs/superpowers/specs/2026-06-16-plugin-hard-cutover-extensible-ip-flow-design.md`
- Existing plugin primitives:
  - `qt/inc/app/pluginhost.h`
  - `qt/src/app/pluginhost.cpp`
  - `qt/inc/app/appcontext.h`
  - `qt/src/project/projectplugin.cpp`
  - `qt/src/package/packageplugin.cpp`
  - `qt/src/app/toolpipelineplugin.cpp`
- Existing project core:
  - `qt/inc/ipcraft/core/project_design.h`
  - `qt/inc/ipcraft/core/project_patch.h`
  - `qt/src/ipcraft/core/project_patch.cpp`
  - `qt/inc/project/projectservice.h`
  - `qt/src/project/projectservice.cpp`
- Existing debt to cut:
  - `ProjectGenerationRequest::graph` in `qt/inc/app/projectgenerationrunner.h`
  - `GraphProjectSerializer::toProject` in generation/validation helpers
  - concrete IP/package/module behavior outside package fixtures and tests

## Task 1: Minimal Plugin Context And Registries

**Files:**
- Create: `qt/inc/app/servicekey.h`
- Create: `qt/inc/app/serviceregistry.h`
- Create: `qt/src/app/serviceregistry.cpp`
- Create: `qt/inc/app/extensionpointregistry.h`
- Create: `qt/src/app/extensionpointregistry.cpp`
- Create: `qt/inc/app/capabilityregistry.h`
- Create: `qt/src/app/capabilityregistry.cpp`
- Modify: `qt/inc/app/appcontext.h`
- Modify: `qt/inc/app/pluginhost.h`
- Modify: `qt/src/app/pluginhost.cpp`
- Create: `qt/test/plugin_registry_test.cpp`
- Modify: `qt/test/pluginhost_foundation_test.cpp`
- Modify: `qt/test/projectplugin_test.cpp`
- Modify: `qt/test/packageplugin_test.cpp`
- Modify: `qt/test/toolpipelineplugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing registry tests**

Create `qt/test/plugin_registry_test.cpp`:

```cpp
#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/serviceregistry.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FakeService {
    int value = 7;
};

void testServiceRegistryStoresTypedServices() {
    ServiceRegistry registry;
    FakeService service;
    require(registry.registerService(ServiceKey::fromLiteral("test.fake"), &service),
            "first service registration should succeed");
    require(!registry.registerService(ServiceKey::fromLiteral("test.fake"), &service),
            "duplicate service registration should fail");
    require(registry.service<FakeService>(ServiceKey::fromLiteral("test.fake")) == &service,
            "typed service lookup should return the registered service");
    require(registry.service<FakeService>(ServiceKey::fromLiteral("test.missing")) == nullptr,
            "missing service lookup should return null");
}

void testExtensionPointRegistryStoresContributions() {
    ExtensionPointRegistry registry;
    ExtensionContribution contribution;
    contribution.id = QStringLiteral("finepaper.test.action");
    contribution.extensionPoint = QStringLiteral("ui.action");
    contribution.ownerPluginId = QStringLiteral("finepaper.test");
    contribution.label = QStringLiteral("Test Action");

    require(registry.registerContribution(contribution),
            "first contribution registration should succeed");
    require(!registry.registerContribution(contribution),
            "duplicate contribution registration should fail");
    const QVector<ExtensionContribution> actions =
        registry.contributions(QStringLiteral("ui.action"));
    require(actions.size() == 1, "one action contribution should be registered");
    require(actions.first().id == contribution.id, "registered contribution id should match");
}

void testCapabilityRegistryReportsRequiredMissingHandler() {
    CapabilityRegistry registry;
    PackageCapabilityDescriptor capability;
    capability.id = QStringLiteral("noc.v1");
    capability.required = true;
    capability.packageId = QStringLiteral("vendor.meshnoc");

    registry.recordPackageCapability(capability);
    const QVector<CapabilityCoverageRecord> coverage =
        registry.coverageForPackage(QStringLiteral("vendor.meshnoc"));
    require(coverage.size() == 1, "one package capability coverage record should exist");
    require(coverage.first().status == CapabilityCoverageStatus::Blocking,
            "required capability without handler should be blocking");
}

void testCapabilityRegistryReportsHandledCapability() {
    CapabilityRegistry registry;
    CapabilityHandlerDescriptor handler;
    handler.capabilityId = QStringLiteral("noc.v1");
    handler.ownerPluginId = QStringLiteral("finepaper.noc");
    handler.extensionPoints = {QStringLiteral("ui.inspectorSection"),
                               QStringLiteral("editor.tool")};
    require(registry.registerHandler(handler), "handler registration should succeed");

    PackageCapabilityDescriptor capability;
    capability.id = QStringLiteral("noc.v1");
    capability.required = true;
    capability.packageId = QStringLiteral("vendor.meshnoc");
    registry.recordPackageCapability(capability);

    const QVector<CapabilityCoverageRecord> coverage =
        registry.coverageForPackage(QStringLiteral("vendor.meshnoc"));
    require(coverage.size() == 1, "one handled capability coverage record should exist");
    require(coverage.first().status == CapabilityCoverageStatus::Handled,
            "capability with registered handler should be handled");
}

void testAppContextUsesRegistries() {
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;
    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    require(context.services == &services, "context should expose service registry");
    require(context.extensionPoints == &extensionPoints, "context should expose extension point registry");
    require(context.capabilities == &capabilities, "context should expose capability registry");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testServiceRegistryStoresTypedServices();
        testExtensionPointRegistryStoresContributions();
        testCapabilityRegistryReportsRequiredMissingHandler();
        testCapabilityRegistryReportsHandledCapability();
        testAppContextUsesRegistries();
    } catch (const std::exception& error) {
        std::cerr << "plugin_registry_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "plugin_registry_test passed\n";
    return 0;
}
```

Register target in `qt/xmake.lua`:

```lua
target("plugin_registry_test")
    set_kind("binary")
    add_rules("qt.console")
    add_files("test/plugin_registry_test.cpp")
    add_files("src/app/serviceregistry.cpp")
    add_files("src/app/extensionpointregistry.cpp")
    add_files("src/app/capabilityregistry.cpp")
    add_headerfiles("inc/app/servicekey.h")
    add_headerfiles("inc/app/serviceregistry.h")
    add_headerfiles("inc/app/extensionpointregistry.h")
    add_headerfiles("inc/app/capabilityregistry.h")
    add_includedirs("inc")
    add_packages("qt6core")
    set_policy("build.warning", true)
    on_run(function (target)
        os.execv(target:targetfile(), {}, {
            pass_outputs = "plugin_registry_test passed"
        })
    end)
```

- [ ] **Step 2: Run the failing test**

Run:

```bash
xmake run -P qt plugin_registry_test
```

Expected: FAIL because the registry headers and implementation files do not exist.

- [ ] **Step 3: Implement service registry**

Create `qt/inc/app/servicekey.h`:

```cpp
#pragma once

#include <QString>

class ServiceKey {
public:
    static ServiceKey fromLiteral(const char* value);

    ServiceKey() = default;
    explicit ServiceKey(QString value);

    QString value() const;
    bool isValid() const;
    bool operator==(const ServiceKey& other) const;

private:
    QString m_value;
};
```

Create `qt/inc/app/serviceregistry.h`:

```cpp
#pragma once

#include "app/servicekey.h"

#include <QHash>
#include <type_traits>

class ServiceRegistry {
public:
    bool registerService(const ServiceKey& key, void* service);
    void* service(const ServiceKey& key) const;
    bool contains(const ServiceKey& key) const;

    template <typename T>
    T* service(const ServiceKey& key) const {
        static_assert(!std::is_void_v<T>, "Service type must not be void.");
        return static_cast<T*>(service(key));
    }

private:
    QHash<QString, void*> m_services;
};
```

Create `qt/src/app/serviceregistry.cpp`:

```cpp
#include "app/serviceregistry.h"

ServiceKey ServiceKey::fromLiteral(const char* value) {
    return ServiceKey(QString::fromUtf8(value));
}

ServiceKey::ServiceKey(QString value) : m_value(std::move(value)) {}

QString ServiceKey::value() const {
    return m_value;
}

bool ServiceKey::isValid() const {
    return !m_value.trimmed().isEmpty() && m_value == m_value.trimmed();
}

bool ServiceKey::operator==(const ServiceKey& other) const {
    return m_value == other.m_value;
}

bool ServiceRegistry::registerService(const ServiceKey& key, void* service) {
    if (!key.isValid() || !service || m_services.contains(key.value())) {
        return false;
    }
    m_services.insert(key.value(), service);
    return true;
}

void* ServiceRegistry::service(const ServiceKey& key) const {
    return key.isValid() ? m_services.value(key.value(), nullptr) : nullptr;
}

bool ServiceRegistry::contains(const ServiceKey& key) const {
    return key.isValid() && m_services.contains(key.value());
}
```

- [ ] **Step 4: Implement extension point registry**

Create `qt/inc/app/extensionpointregistry.h`:

```cpp
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct ExtensionContribution {
    QString id;
    QString extensionPoint;
    QString ownerPluginId;
    QString label;
    QJsonObject descriptor;
};

class ExtensionPointRegistry {
public:
    bool registerContribution(const ExtensionContribution& contribution);
    QVector<ExtensionContribution> contributions(const QString& extensionPoint) const;
    QVector<ExtensionContribution> allContributions() const;

private:
    QHash<QString, ExtensionContribution> m_byId;
    QMultiHash<QString, QString> m_idsByExtensionPoint;
};
```

Create `qt/src/app/extensionpointregistry.cpp`:

```cpp
#include "app/extensionpointregistry.h"

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

} // namespace

bool ExtensionPointRegistry::registerContribution(const ExtensionContribution& contribution) {
    if (!canonical(contribution.id) ||
        !canonical(contribution.extensionPoint) ||
        !canonical(contribution.ownerPluginId) ||
        m_byId.contains(contribution.id)) {
        return false;
    }
    m_byId.insert(contribution.id, contribution);
    m_idsByExtensionPoint.insert(contribution.extensionPoint, contribution.id);
    return true;
}

QVector<ExtensionContribution> ExtensionPointRegistry::contributions(
    const QString& extensionPoint) const {
    QVector<ExtensionContribution> result;
    const QList<QString> ids = m_idsByExtensionPoint.values(extensionPoint);
    result.reserve(ids.size());
    for (const QString& id : ids) {
        result.append(m_byId.value(id));
    }
    return result;
}

QVector<ExtensionContribution> ExtensionPointRegistry::allContributions() const {
    QVector<ExtensionContribution> result;
    result.reserve(m_byId.size());
    for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it) {
        result.append(it.value());
    }
    return result;
}
```

- [ ] **Step 5: Implement capability registry**

Create `qt/inc/app/capabilityregistry.h`:

```cpp
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

enum class CapabilityCoverageStatus {
    Handled,
    Visible,
    Unsupported,
    Blocking,
    Invalid,
};

struct CapabilityHandlerDescriptor {
    QString capabilityId;
    QString ownerPluginId;
    QStringList extensionPoints;
};

struct PackageCapabilityDescriptor {
    QString packageId;
    QString capabilityId;
    QString id;
    bool required = false;
};

struct CapabilityCoverageRecord {
    QString packageId;
    QString capabilityId;
    QString handlerPluginId;
    CapabilityCoverageStatus status = CapabilityCoverageStatus::Unsupported;
    QString message;
};

class CapabilityRegistry {
public:
    bool registerHandler(const CapabilityHandlerDescriptor& handler);
    void recordPackageCapability(const PackageCapabilityDescriptor& capability);
    QVector<CapabilityCoverageRecord> coverageForPackage(const QString& packageId) const;
    QVector<CapabilityHandlerDescriptor> handlers() const;

private:
    CapabilityCoverageRecord coverageFor(const PackageCapabilityDescriptor& capability) const;

    QHash<QString, CapabilityHandlerDescriptor> m_handlersByCapability;
    QMultiHash<QString, PackageCapabilityDescriptor> m_capabilitiesByPackage;
};
```

Create `qt/src/app/capabilityregistry.cpp`:

```cpp
#include "app/capabilityregistry.h"

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

QString capabilityKey(const PackageCapabilityDescriptor& capability) {
    return capability.capabilityId.trimmed().isEmpty() ? capability.id : capability.capabilityId;
}

} // namespace

bool CapabilityRegistry::registerHandler(const CapabilityHandlerDescriptor& handler) {
    if (!canonical(handler.capabilityId) ||
        !canonical(handler.ownerPluginId) ||
        m_handlersByCapability.contains(handler.capabilityId)) {
        return false;
    }
    m_handlersByCapability.insert(handler.capabilityId, handler);
    return true;
}

void CapabilityRegistry::recordPackageCapability(const PackageCapabilityDescriptor& capability) {
    if (!canonical(capability.packageId)) {
        return;
    }
    PackageCapabilityDescriptor stored = capability;
    stored.capabilityId = capabilityKey(capability);
    if (!canonical(stored.capabilityId)) {
        return;
    }
    m_capabilitiesByPackage.insert(stored.packageId, stored);
}

QVector<CapabilityCoverageRecord> CapabilityRegistry::coverageForPackage(
    const QString& packageId) const {
    QVector<CapabilityCoverageRecord> result;
    const QList<PackageCapabilityDescriptor> capabilities =
        m_capabilitiesByPackage.values(packageId);
    result.reserve(capabilities.size());
    for (const PackageCapabilityDescriptor& capability : capabilities) {
        result.append(coverageFor(capability));
    }
    return result;
}

QVector<CapabilityHandlerDescriptor> CapabilityRegistry::handlers() const {
    QVector<CapabilityHandlerDescriptor> result;
    result.reserve(m_handlersByCapability.size());
    for (auto it = m_handlersByCapability.cbegin(); it != m_handlersByCapability.cend(); ++it) {
        result.append(it.value());
    }
    return result;
}

CapabilityCoverageRecord CapabilityRegistry::coverageFor(
    const PackageCapabilityDescriptor& capability) const {
    CapabilityCoverageRecord record;
    record.packageId = capability.packageId;
    record.capabilityId = capability.capabilityId;
    const auto handler = m_handlersByCapability.constFind(capability.capabilityId);
    if (handler != m_handlersByCapability.cend()) {
        record.status = CapabilityCoverageStatus::Handled;
        record.handlerPluginId = handler.value().ownerPluginId;
        record.message = QStringLiteral("Capability is handled.");
        return record;
    }
    record.status = capability.required
        ? CapabilityCoverageStatus::Blocking
        : CapabilityCoverageStatus::Unsupported;
    record.message = capability.required
        ? QStringLiteral("Required capability has no registered handler.")
        : QStringLiteral("Optional capability has no registered handler.");
    return record;
}
```

- [ ] **Step 6: Shrink AppContext to registry pointers while preserving legacy fields**

Modify `qt/inc/app/appcontext.h` to add registries first and keep legacy fields during migration:

```cpp
#pragma once

class CapabilityRegistry;
class ExtensionPointRegistry;
class PackageService;
class ProjectService;
class ServiceRegistry;
class ToolPipelineService;
class WorkbenchService;

struct AppContext {
    ServiceRegistry* services = nullptr;
    ExtensionPointRegistry* extensionPoints = nullptr;
    CapabilityRegistry* capabilities = nullptr;

    WorkbenchService* workbench = nullptr;
    ProjectService* projectService = nullptr;
    PackageService* packageService = nullptr;
    ToolPipelineService* toolPipelineService = nullptr;
};
```

- [ ] **Step 7: Make PluginHost require registries**

Modify `qt/src/app/pluginhost.cpp` so `activatePlugins()` fails when any registry pointer is missing:

```cpp
    if (!m_context.services || !m_context.extensionPoints || !m_context.capabilities) {
        result.success = false;
        result.error = QStringLiteral("Plugin registries are required before activating plugins.");
        return result;
    }
```

Keep the existing `WorkbenchService` guard in place for this task.

- [ ] **Step 8: Run tests**

Update existing plugin tests to construct `ServiceRegistry`,
`ExtensionPointRegistry`, and `CapabilityRegistry` and assign them into
`AppContext` before `PluginHost` activation. Existing tests that intentionally
verify missing services should still provide the three registries and omit only
the specific legacy service under test. Update the corresponding `qt/xmake.lua`
targets to link `src/app/serviceregistry.cpp`,
`src/app/extensionpointregistry.cpp`, and `src/app/capabilityregistry.cpp`.

Run:

```bash
xmake run -P qt plugin_registry_test
xmake run -P qt pluginhost_foundation_test
xmake run -P qt projectplugin_test
xmake run -P qt packageplugin_test
xmake run -P qt toolpipelineplugin_test
```

Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add qt/inc/app qt/src/app qt/test/plugin_registry_test.cpp qt/test/pluginhost_foundation_test.cpp qt/test/projectplugin_test.cpp qt/test/packageplugin_test.cpp qt/test/toolpipelineplugin_test.cpp qt/xmake.lua
git commit -m "feat: add plugin registries"
```

## Task 2: Static Plugin Catalog And Registry-Based Plugin Activation

**Files:**
- Create: `qt/inc/app/staticplugincatalog.h`
- Create: `qt/src/app/staticplugincatalog.cpp`
- Modify: `qt/src/project/projectplugin.cpp`
- Modify: `qt/src/package/packageplugin.cpp`
- Modify: `qt/src/app/toolpipelineplugin.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: `qt/inc/app/mainwindow.h`
- Create: `qt/test/staticplugincatalog_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing static catalog test**

Create `qt/test/staticplugincatalog_test.cpp`:

```cpp
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/staticplugincatalog.h"
#include "app/workbenchservice.h"
#include "package/packageservice.h"
#include "project/projectservice.h"
#include "app/toolpipelineservice.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testStaticCatalogRegistersCorePlugins() {
    WorkbenchService workbench;
    ProjectService project;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService package(&registry);
    ToolPipelineService tools;
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;

    services.registerService(ServiceKey::fromLiteral("finepaper.project"), &project);
    services.registerService(ServiceKey::fromLiteral("finepaper.package"), &package);
    services.registerService(ServiceKey::fromLiteral("finepaper.tool-pipeline"), &tools);

    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.workbench = &workbench;
    context.projectService = &project;
    context.packageService = &package;
    context.toolPipelineService = &tools;

    PluginHost host(context);
    registerStaticPlugins(host);

    const QStringList ids = host.pluginIds();
    require(ids.contains(QStringLiteral("finepaper.project")),
            "static catalog should include project plugin");
    require(ids.contains(QStringLiteral("finepaper.package")),
            "static catalog should include package plugin");
    require(ids.contains(QStringLiteral("finepaper.tool-pipeline")),
            "static catalog should include tool pipeline plugin");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "static plugins should activate");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testStaticCatalogRegistersCorePlugins();
    } catch (const std::exception& error) {
        std::cerr << "staticplugincatalog_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "staticplugincatalog_test passed\n";
    return 0;
}
```

Register target in `qt/xmake.lua` with sources used by existing plugin tests plus the new registry/catalog files.

- [ ] **Step 2: Run failing test**

Run:

```bash
xmake run -P qt staticplugincatalog_test
```

Expected: FAIL because `staticplugincatalog` does not exist.

- [ ] **Step 3: Implement static catalog**

Create `qt/inc/app/staticplugincatalog.h`:

```cpp
#pragma once

class PluginHost;

void registerStaticPlugins(PluginHost& host);
```

Create `qt/src/app/staticplugincatalog.cpp`:

```cpp
#include "app/staticplugincatalog.h"

#include "app/pluginhost.h"
#include "app/toolpipelineplugin.h"
#include "package/packageplugin.h"
#include "project/projectplugin.h"

void registerStaticPlugins(PluginHost& host) {
    host.registerPlugin(createProjectPlugin());
    host.registerPlugin(createPackagePlugin());
    host.registerPlugin(createToolPipelinePlugin());
}
```

- [ ] **Step 4: Register services from plugin activation**

Modify plugin activation methods so each plugin verifies service registry entries rather than only legacy direct pointers. Use service keys:

```cpp
ServiceKey::fromLiteral("finepaper.project")
ServiceKey::fromLiteral("finepaper.package")
ServiceKey::fromLiteral("finepaper.tool-pipeline")
```

Activation remains compatible with existing direct context fields while migration is underway.

- [ ] **Step 5: Wire MainWindow through registries and static catalog**

Modify `MainWindow` members to own:

```cpp
std::unique_ptr<ServiceRegistry> m_serviceRegistry;
std::unique_ptr<ExtensionPointRegistry> m_extensionPointRegistry;
std::unique_ptr<CapabilityRegistry> m_capabilityRegistry;
std::unique_ptr<PluginHost> m_pluginHost;
```

In `MainWindow` construction, instantiate registries before plugin activation, register current services, build `AppContext`, call `registerStaticPlugins(*m_pluginHost)`, then activate.

- [ ] **Step 6: Run tests**

Run:

```bash
xmake run -P qt staticplugincatalog_test
xmake run -P qt pluginhost_foundation_test
xmake run -P qt plugin_architecture_phase1_scan_test
xmake run -P qt qt
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/app qt/src/app qt/src/project/projectplugin.cpp qt/src/package/packageplugin.cpp qt/src/app/mainwindow.cpp qt/inc/app/mainwindow.h qt/test/staticplugincatalog_test.cpp qt/xmake.lua
git commit -m "feat: activate static plugins through registries"
```

## Task 3: Package Capability Coverage And Inspector Data

**Files:**
- Create: `qt/inc/package/packagecoverage.h`
- Create: `qt/src/package/packagecoverage.cpp`
- Modify: `qt/inc/package/packageservice.h`
- Modify: `qt/src/package/packageservice.cpp`
- Modify: `qt/src/package/packageplugin.cpp`
- Create: `qt/test/packagecoverage_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing package coverage tests**

Create `qt/test/packagecoverage_test.cpp`:

```cpp
#include "app/capabilityregistry.h"
#include "package/packagecoverage.h"

#include <QCoreApplication>
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

QJsonObject packageDescriptor() {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("vendor.meshnoc")},
        {QStringLiteral("extensions"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("noc.v1")},
                        {QStringLiteral("required"), true}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.extra.v1")},
                        {QStringLiteral("required"), false}}
        }},
        {QStringLiteral("native"), QJsonObject{{QStringLiteral("vendor_blob"), true}}},
        {QStringLiteral("flows"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("validate")}}
        }},
        {QStringLiteral("artifacts"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("mesh_manifest")}}
        }}
    };
}

void testRequiredCapabilityWithoutHandlerBlocksPackage() {
    CapabilityRegistry capabilities;
    const PackageCoverageReport report =
        buildPackageCoverageReport(packageDescriptor(), capabilities);
    require(report.packageId == QStringLiteral("vendor.meshnoc"), "package id should be reported");
    require(report.hasBlockingItems(), "required noc.v1 without handler should block");
    require(report.item(QStringLiteral("capability:noc.v1")).status == PackageFeatureCoverageStatus::Blocking,
            "noc.v1 should be blocking without handler");
    require(report.item(QStringLiteral("capability:vendor.extra.v1")).status ==
                PackageFeatureCoverageStatus::Unsupported,
            "optional unknown capability should be unsupported");
}

void testHandledCapabilityAndVisibleUnknownDataAreReported() {
    CapabilityRegistry capabilities;
    CapabilityHandlerDescriptor handler;
    handler.capabilityId = QStringLiteral("noc.v1");
    handler.ownerPluginId = QStringLiteral("finepaper.noc");
    capabilities.registerHandler(handler);

    const PackageCoverageReport report =
        buildPackageCoverageReport(packageDescriptor(), capabilities);
    require(!report.hasBlockingItems(), "handled required capability should not block");
    require(report.item(QStringLiteral("capability:noc.v1")).status == PackageFeatureCoverageStatus::Handled,
            "noc.v1 should be handled");
    require(report.item(QStringLiteral("native")).status == PackageFeatureCoverageStatus::Visible,
            "native descriptor data should be visible");
    require(report.item(QStringLiteral("flow:validate")).status == PackageFeatureCoverageStatus::Visible,
            "flow declaration should be visible");
    require(report.item(QStringLiteral("artifact:mesh_manifest")).status == PackageFeatureCoverageStatus::Visible,
            "artifact declaration should be visible");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRequiredCapabilityWithoutHandlerBlocksPackage();
        testHandledCapabilityAndVisibleUnknownDataAreReported();
    } catch (const std::exception& error) {
        std::cerr << "packagecoverage_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "packagecoverage_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
xmake run -P qt packagecoverage_test
```

Expected: FAIL because package coverage API does not exist.

- [ ] **Step 3: Implement coverage model**

Create `qt/inc/package/packagecoverage.h`:

```cpp
#pragma once

#include "app/capabilityregistry.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

enum class PackageFeatureCoverageStatus {
    Handled,
    Visible,
    Unsupported,
    Blocking,
    Invalid,
};

struct PackageFeatureCoverageItem {
    QString id;
    QString label;
    PackageFeatureCoverageStatus status = PackageFeatureCoverageStatus::Visible;
    QString message;
    QJsonObject descriptor;
};

struct PackageCoverageReport {
    QString packageId;
    QVector<PackageFeatureCoverageItem> items;

    bool hasBlockingItems() const;
    PackageFeatureCoverageItem item(const QString& id) const;
};

PackageCoverageReport buildPackageCoverageReport(const QJsonObject& descriptor,
                                                 const CapabilityRegistry& capabilities);
```

Create `qt/src/package/packagecoverage.cpp` with explicit reporting for capabilities, flows, artifacts, native, metadata, and unknown objects. Use the capability registry to decide handled vs blocking/unsupported.

- [ ] **Step 4: Integrate PackageService coverage**

Add to `PackageService`:

```cpp
void setCapabilityRegistry(const CapabilityRegistry* registry);
const QVector<PackageCoverageReport>& coverageReports() const;
const PackageCoverageReport* coverageReport(const QString& packageId) const;
```

When `reloadPackageRoots()` succeeds or partially succeeds, build coverage reports from package manifests and preserved raw descriptors. If raw descriptor access is not exposed by the manifest reader, add a minimal raw `QJsonObject` field to the manifest read result and keep it package-owned.

- [ ] **Step 5: Register PackagePlugin coverage contribution**

In `PackagePlugin::activate`, register an inspector contribution:

```cpp
ExtensionContribution contribution;
contribution.id = QStringLiteral("finepaper.package.coverage-inspector");
contribution.extensionPoint = QStringLiteral("ui.inspectorSection");
contribution.ownerPluginId = QStringLiteral("finepaper.package");
contribution.label = QStringLiteral("Package Coverage");
context.extensionPoints->registerContribution(contribution);
```

This task verifies that the contribution is registered and visible through
`ExtensionPointRegistry`. Widget rendering remains the responsibility of
existing inspector/panel rendering tasks and is not a hidden dependency of
package coverage.

- [ ] **Step 6: Run tests**

Run:

```bash
xmake run -P qt packagecoverage_test
xmake run -P qt packageservice_test
xmake run -P qt packageplugin_test
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/package qt/src/package qt/test/packagecoverage_test.cpp qt/xmake.lua
git commit -m "feat: report package capability coverage"
```

## Task 4: Static NoC Plugin With Semantic noc.v1 Handler

**Files:**
- Create: `qt/inc/noc/nocplugin.h`
- Create: `qt/src/noc/nocplugin.cpp`
- Create: `qt/inc/noc/nocsemanticmodel.h`
- Create: `qt/src/noc/nocsemanticmodel.cpp`
- Modify: `qt/inc/app/staticplugincatalog.h`
- Modify: `qt/src/app/staticplugincatalog.cpp`
- Create: `qt/test/nocplugin_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing NoC plugin test**

Create `qt/test/nocplugin_test.cpp`:

```cpp
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"
#include "noc/nocplugin.h"

#include <QCoreApplication>
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

void testNoCPluginRegistersNocCapabilityAndContributions() {
    WorkbenchService workbench;
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;
    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.workbench = &workbench;

    PluginHost host(context);
    host.registerPlugin(createNoCPlugin());
    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "NoC plugin should activate");

    bool sawNocHandler = false;
    for (const CapabilityHandlerDescriptor& handler : capabilities.handlers()) {
        sawNocHandler = sawNocHandler || handler.capabilityId == QStringLiteral("noc.v1");
    }
    require(sawNocHandler, "NoC plugin should register noc.v1 handler");
    require(!extensionPoints.contributions(QStringLiteral("editor.tool")).isEmpty(),
            "NoC plugin should register editor tools");
    require(!extensionPoints.contributions(QStringLiteral("connection.ruleProvider")).isEmpty(),
            "NoC plugin should register connection rule provider");
    require(!extensionPoints.contributions(QStringLiteral("tool.flowInputProjector")).isEmpty(),
            "NoC plugin should register flow input projector");
}

void testNoCSemanticModelReadsRolesWithoutConcreteNames() {
    QJsonObject descriptor{
        {QStringLiteral("roles"), QJsonArray{
            QJsonObject{{QStringLiteral("module"), QStringLiteral("VendorSwitch")},
                        {QStringLiteral("semantic"), QStringLiteral("router")}},
            QJsonObject{{QStringLiteral("module"), QStringLiteral("VendorHost")},
                        {QStringLiteral("semantic"), QStringLiteral("endpoint")}}
        }}
    };
    const NoCSemanticModel model = NoCSemanticModel::fromJson(descriptor);
    require(model.semanticRoleForModule(QStringLiteral("VendorSwitch")) == QStringLiteral("router"),
            "router role should come from descriptor");
    require(model.semanticRoleForModule(QStringLiteral("VendorHost")) == QStringLiteral("endpoint"),
            "endpoint role should come from descriptor");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testNoCPluginRegistersNocCapabilityAndContributions();
        testNoCSemanticModelReadsRolesWithoutConcreteNames();
    } catch (const std::exception& error) {
        std::cerr << "nocplugin_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "nocplugin_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
xmake run -P qt nocplugin_test
```

Expected: FAIL because NoC plugin does not exist.

- [ ] **Step 3: Implement NoC semantic model**

Create `qt/inc/noc/nocsemanticmodel.h`:

```cpp
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

class NoCSemanticModel {
public:
    static NoCSemanticModel fromJson(const QJsonObject& descriptor);

    QString semanticRoleForModule(const QString& moduleId) const;
    bool isEmpty() const;

private:
    QHash<QString, QString> m_rolesByModule;
};
```

Implement `fromJson()` by reading `roles[]` entries with `module` and
`semantic` string fields.

- [ ] **Step 4: Implement NoC plugin**

Create `qt/inc/noc/nocplugin.h`:

```cpp
#pragma once

#include <memory>

class IAppPlugin;

std::unique_ptr<IAppPlugin> createNoCPlugin();
```

Create `qt/src/noc/nocplugin.cpp` with an internal `NoCPlugin` class. Its
`activate()` registers:

```cpp
CapabilityHandlerDescriptor handler;
handler.capabilityId = QStringLiteral("noc.v1");
handler.ownerPluginId = QStringLiteral("finepaper.noc");
handler.extensionPoints = {
    QStringLiteral("ui.inspectorSection"),
    QStringLiteral("editor.tool"),
    QStringLiteral("connection.ruleProvider"),
    QStringLiteral("tool.flowInputProjector"),
    QStringLiteral("artifact.presenter")
};
context.capabilities->registerHandler(handler);
```

Also register descriptor-only contributions for those extension points.

- [ ] **Step 5: Add NoCPlugin to static catalog**

Modify `qt/src/app/staticplugincatalog.cpp`:

```cpp
#include "noc/nocplugin.h"
```

Register `createNoCPlugin()` after package and before tool pipeline unless
tests show a stricter dependency order is needed.

- [ ] **Step 6: Run tests**

Run:

```bash
xmake run -P qt nocplugin_test
xmake run -P qt staticplugincatalog_test
xmake run -P qt plugin_registry_test
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/noc qt/src/noc qt/inc/app/staticplugincatalog.h qt/src/app/staticplugincatalog.cpp qt/test/nocplugin_test.cpp qt/xmake.lua
git commit -m "feat: add static noc capability plugin"
```

## Task 5: DesignEditingService And ProjectDesign Runtime Ownership

**Files:**
- Create: `qt/inc/project/designeditingservice.h`
- Create: `qt/src/project/designeditingservice.cpp`
- Modify: `qt/inc/project/projectservice.h`
- Modify: `qt/src/project/projectservice.cpp`
- Create: `qt/test/designeditingservice_test.cpp`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing DesignEditingService tests**

Create `qt/test/designeditingservice_test.cpp`:

```cpp
#include "project/designeditingservice.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ipcraft::core::ProjectPatch addComponentPatch() {
    ipcraft::core::ProjectPatch patch;
    patch.schema = QStringLiteral("ipcraft.project-patch.v1");
    patch.id = QStringLiteral("add-component");
    ipcraft::core::PatchOperation op;
    op.op = QStringLiteral("add");
    op.target = QStringLiteral("component");
    op.path = QStringLiteral("/components/-");
    op.payload = QJsonObject{
        {QStringLiteral("id"), QStringLiteral("ip0")},
        {QStringLiteral("type"), QStringLiteral("VendorSwitch")},
        {QStringLiteral("packageRef"), QStringLiteral("vendor.meshnoc")}
    };
    patch.ops.append(op);
    return patch;
}

void testApplyPatchMutatesDesignAndSupportsUndoRedo() {
    DesignEditingService service;
    ipcraft::core::ProjectDesign design;
    design.schema = QStringLiteral("ipcraft.project.v1");
    design.id = QStringLiteral("p0");
    design.name = QStringLiteral("Project");
    service.replaceDesign(design);

    const DesignEditResult apply = service.applyPatch(addComponentPatch());
    require(apply.success, "patch should apply");
    require(service.design().components.size() == 1, "component should be added");
    require(service.canUndo(), "successful edit should be undoable");

    const DesignEditResult undo = service.undo();
    require(undo.success, "undo should succeed");
    require(service.design().components.isEmpty(), "undo should remove component");
    require(service.canRedo(), "undo should enable redo");

    const DesignEditResult redo = service.redo();
    require(redo.success, "redo should succeed");
    require(service.design().components.size() == 1, "redo should restore component");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testApplyPatchMutatesDesignAndSupportsUndoRedo();
    } catch (const std::exception& error) {
        std::cerr << "designeditingservice_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "designeditingservice_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
xmake run -P qt designeditingservice_test
```

Expected: FAIL because `DesignEditingService` does not exist.

- [ ] **Step 3: Implement DesignEditingService**

Create `qt/inc/project/designeditingservice.h`:

```cpp
#pragma once

#include "ipcraft/core/project_patch.h"

#include <QObject>
#include <QVector>

struct DesignEditResult {
    bool success = false;
    QString error;
    QVector<ipcraft::core::ValidationIssue> issues;
};

class DesignEditingService : public QObject {
    Q_OBJECT

public:
    explicit DesignEditingService(QObject* parent = nullptr);

    const ipcraft::core::ProjectDesign& design() const;
    void replaceDesign(ipcraft::core::ProjectDesign design);

    DesignEditResult applyPatch(const ipcraft::core::ProjectPatch& patch);
    bool canUndo() const;
    bool canRedo() const;
    DesignEditResult undo();
    DesignEditResult redo();

signals:
    void designChanged();

private:
    DesignEditResult replaceWithHistory(ipcraft::core::ProjectDesign next);

    ipcraft::core::ProjectDesign m_design;
    QVector<ipcraft::core::ProjectDesign> m_undo;
    QVector<ipcraft::core::ProjectDesign> m_redo;
};
```

Implement using `ipcraft::core::applyPatch()`. Push the previous design into
undo history only after successful patch application. Clear redo after a new
edit.

- [ ] **Step 4: Expose runtime design from ProjectService**

Modify `ProjectService` to own `ipcraft::core::ProjectDesign m_design`.

Add:

```cpp
const ipcraft::core::ProjectDesign& design() const;
void replaceDesign(ipcraft::core::ProjectDesign design);
```

For this task, use existing `ProjectDocument` as persistence state and create a
conversion that preserves project id/name, package references, component ids,
component package refs, and component config. Task 6 extends the same converter
to the complete save/generate surface.

- [ ] **Step 5: Register DesignEditingService in AppContext services**

Add `DesignEditingService` pointer to `MainWindow` and register it:

```cpp
m_serviceRegistry->registerService(ServiceKey::fromLiteral("finepaper.design-editing"),
                                   m_designEditingService.get());
```

Keep existing graph command paths intact in this task.

- [ ] **Step 6: Run tests**

Run:

```bash
xmake run -P qt designeditingservice_test
xmake run -P qt projectdocument_test
xmake run -P qt plugin_registry_test
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add qt/inc/project qt/src/project qt/inc/app/mainwindow.h qt/src/app/mainwindow.cpp qt/test/designeditingservice_test.cpp qt/xmake.lua
git commit -m "feat: add design editing service"
```

## Task 6: ProjectDesign Save, Validate, And Generate Inputs

**Files:**
- Create: `qt/inc/project/projectdesignserializer.h`
- Create: `qt/src/project/projectdesignserializer.cpp`
- Modify: `qt/inc/app/projectgenerationrunner.h`
- Modify: `qt/src/app/projectgenerationrunner.cpp`
- Modify: `qt/inc/app/projectflowsupport.h`
- Modify: `qt/src/app/projectflowsupport.cpp`
- Modify: `qt/inc/validation/projectexternalvalidationrunner.h`
- Modify: `qt/src/validation/projectexternalvalidationrunner.cpp`
- Modify: `qt/inc/validation/validationmanager.h`
- Modify: `qt/src/validation/validationmanager.cpp`
- Modify: `qt/src/app/mainwindow.cpp`
- Modify: existing tests for generation and validation

- [ ] **Step 1: Add failing ProjectDesign serializer test**

Create or extend `qt/test/projectdocument_test.cpp` with:

```cpp
void testProjectDesignSerializesWithoutGraphProjection() {
    ipcraft::core::ProjectDesign design;
    design.schema = QStringLiteral("ipcraft.project.v1");
    design.id = QStringLiteral("project_0");
    design.name = QStringLiteral("Project 0");
    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("ip0");
    component.type = QStringLiteral("VendorSwitch");
    component.packageRef = QStringLiteral("vendor.meshnoc");
    design.components.append(component);

    const ProjectDocument document = ProjectDesignSerializer::toDocument(design);
    require(document.projectName == QStringLiteral("Project 0"),
            "project design name should serialize to document");
    require(document.instances.size() == 1,
            "project design component should serialize to project instance record");
    require(document.instances.first().id == QStringLiteral("ip0"),
            "component id should become instance id");
}
```

Include `project/projectdesignserializer.h` and register
`src/project/projectdesignserializer.cpp` in the test target.

- [ ] **Step 2: Add failing generation API test**

Extend `qt/test/projectgenerationrunner_test.cpp` with a test that constructs
`ProjectGenerationRequest` using `projectDesign` and no `graph` pointer. The
test should assert generation succeeds for a minimal package flow and that
compiled code no longer needs `request.graph`.

- [ ] **Step 3: Run failing tests**

Run:

```bash
xmake run -P qt projectdocument_test
xmake run -P qt projectgenerationrunner_test
```

Expected: FAIL because serializer and graph-free generation request do not
exist.

- [ ] **Step 4: Implement ProjectDesign serializer**

Create `qt/inc/project/projectdesignserializer.h`:

```cpp
#pragma once

#include "ipcraft/core/project_design.h"
#include "project/projectdocument.h"

class ProjectDesignSerializer {
public:
    static ProjectDocument toDocument(const ipcraft::core::ProjectDesign& design);
    static ipcraft::core::ProjectDesign fromDocument(const ProjectDocument& document);
};
```

Implement conversion for package refs, components/instances, config,
interfaces, connections, topologies, diagnostics, artifacts, and metadata using
existing `ProjectDocument` fields. Preserve unknown data in `native` or
extension blocks rather than dropping it.

- [ ] **Step 5: Replace generation request graph with ProjectDesign**

Modify `qt/inc/app/projectgenerationrunner.h`:

```cpp
struct ProjectGenerationRequest {
    const ipcraft::core::ProjectDesign* projectDesign = nullptr;
    QString projectPath;
    QString designName;
    QString outputRoot;
    QList<IpCatalogEntry> catalogEntries;
    QVector<ProjectIpInstanceRecord> instances;
};
```

Delete `const Graph* graph`.

Modify `ProjectGenerationRunner` and `ProjectFlowSupport` to build graph config
from `ProjectDesign` and `ProjectIpInstanceRecord`. Do not call
`GraphProjectSerializer::toProject`.

- [ ] **Step 6: Replace validation runner graph input where possible**

Keep `Graph*` only for current visual element highlighting while migration is
in progress. Add a `const ipcraft::core::ProjectDesign* projectDesign` field to
external validation request and use it for flow input projection. Any remaining
`Graph*` use must be documented as UI target resolution only and covered by a
scan allowlist.

- [ ] **Step 7: Wire MainWindow generation from ProjectService/DesignEditingService**

Modify `MainWindow::generateVerilog()` to set `request.projectDesign` from the
runtime project design service. It must not assign `request.graph`.

- [ ] **Step 8: Run tests**

Run:

```bash
xmake run -P qt projectdocument_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt projectexternalvalidationrunner_test
xmake run -P qt validation_test
xmake run -P qt commercial_noc_mvp_test
```

Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add qt/inc/project qt/src/project qt/inc/app/projectgenerationrunner.h qt/src/app/projectgenerationrunner.cpp qt/inc/app/projectflowsupport.h qt/src/app/projectflowsupport.cpp qt/inc/validation qt/src/validation qt/src/app/mainwindow.cpp qt/test
git commit -m "feat: drive tool flows from project design"
```

## Task 7: Vendor MeshNoC Zero C++ Onboarding Fixture

**Files:**
- Create: `ipcores/vendor-meshnoc/ipcraft.json`
- Create: `ipcores/vendor-meshnoc/ipcore.yml`
- Create: `ipcores/vendor-meshnoc/generator/bin/validate`
- Create: `ipcores/vendor-meshnoc/generator/bin/generate`
- Create: `ipcores/vendor-meshnoc/views/mesh.xml`
- Create: `examples/contracts/vendor_meshnoc_project/README.md`
- Create or modify: Qt test covering vendor package workflow
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `qt/xmake.lua`

- [ ] **Step 1: Add failing vendor MeshNoC workflow test**

Create `qt/test/vendor_meshnoc_onboarding_test.cpp`:

```cpp
#include "package/packageservice.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString repoRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("ipcores")) && dir.cdUp()) {}
    return dir.absolutePath();
}

void testVendorMeshNoCPackageLoadsWithoutQtRuntimeEdits() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    const QString root = QDir(repoRoot()).filePath(QStringLiteral("ipcores/vendor-meshnoc"));
    const PackageServiceLoadResult result = service.reloadPackageRoots({root});
    require(result.packageCount == 1, "vendor-meshnoc package should load");
    require(service.catalog().entry(QStringLiteral("vendor.meshnoc")).has_value(),
            "vendor-meshnoc catalog entry should exist");
    require(registry.getType(QStringLiteral("vendor.meshnoc"), QStringLiteral("VendorSwitch")) != nullptr,
            "VendorSwitch module should come from package data");
    require(registry.getType(QStringLiteral("vendor.meshnoc"), QStringLiteral("VendorHost")) != nullptr,
            "VendorHost module should come from package data");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testVendorMeshNoCPackageLoadsWithoutQtRuntimeEdits();
    } catch (const std::exception& error) {
        std::cerr << "vendor_meshnoc_onboarding_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "vendor_meshnoc_onboarding_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
xmake run -P qt vendor_meshnoc_onboarding_test
```

Expected: FAIL because the package does not exist.

- [ ] **Step 3: Add vendor MeshNoC package descriptors**

Create `ipcores/vendor-meshnoc/ipcraft.json` with:

```json
{
  "schema": "ipcraft.package.v1",
  "id": "vendor.meshnoc",
  "version": "1.0.0",
  "name": "Vendor MeshNoC",
  "extensions": [
    "noc.v1",
    "vendor.experimental.v1"
  ],
  "modules": [
    {
      "id": "VendorSwitch",
      "name": "Vendor Switch",
      "interfaces": [
        {"id": "north", "kind": "noc_link", "protocol": "mesh", "role": "router", "direction": "bidirectional"},
        {"id": "south", "kind": "noc_link", "protocol": "mesh", "role": "router", "direction": "bidirectional"},
        {"id": "east", "kind": "noc_link", "protocol": "mesh", "role": "router", "direction": "bidirectional"},
        {"id": "west", "kind": "noc_link", "protocol": "mesh", "role": "router", "direction": "bidirectional"},
        {"id": "host", "kind": "noc_attachment", "protocol": "mesh", "role": "target", "direction": "bidirectional"}
      ],
      "metadata": {"noc_role": "router"}
    },
    {
      "id": "VendorHost",
      "name": "Vendor Host",
      "interfaces": [
        {"id": "mesh", "kind": "noc_attachment", "protocol": "mesh", "role": "initiator", "direction": "bidirectional"}
      ],
      "metadata": {"noc_role": "endpoint"}
    }
  ],
  "flows": [
    {
      "id": "validate",
      "label": "Validate",
      "scope": "instance",
      "steps": [
        {"kind": "emit_inputs"},
        {
          "kind": "exec",
          "command": {
            "executable": "generator/bin/validate",
            "args": ["--input", "{inputs.manifest}"],
            "cwd": "package",
            "timeout_ms": 300000,
            "env": {"allow": []},
            "capture": {"stdout": "stdout.log", "stderr": "stderr.log", "max_bytes": 1048576}
          }
        }
      ]
    },
    {
      "id": "generate",
      "label": "Generate",
      "scope": "instance",
      "steps": [
        {"kind": "emit_inputs"},
        {
          "kind": "exec",
          "command": {
            "executable": "generator/bin/generate",
            "args": ["--input", "{inputs.manifest}", "--output", "{out}"],
            "cwd": "package",
            "timeout_ms": 300000,
            "env": {"allow": []},
            "capture": {"stdout": "stdout.log", "stderr": "stderr.log", "max_bytes": 1048576}
          }
        }
      ]
    }
  ],
  "artifacts": [
    {"id": "vendor_mesh_manifest", "path": "vendor_mesh_manifest.json", "kind": "manifest"}
  ],
  "metadata": {
    "noc.v1": {
      "roles": [
        {"module": "VendorSwitch", "semantic": "router"},
        {"module": "VendorHost", "semantic": "endpoint"}
      ]
    },
    "vendor.experimental.v1": {
      "note": "Visible optional vendor capability fixture"
    }
  }
}
```

If the current package schema requires a different field shape, update this
descriptor to the existing accepted `ipcraft.package.v1` structure while
preserving the semantic intent, module names, flows, artifacts, and optional
unknown capability.

- [ ] **Step 4: Add generator scripts**

Create executable Ruby or shell scripts:

`ipcores/vendor-meshnoc/generator/bin/validate`:

```bash
#!/usr/bin/env bash
set -euo pipefail
input=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --input) input="$2"; shift 2 ;;
    *) echo "ERROR design: unexpected argument $1" >&2; exit 1 ;;
  esac
done
test -f "$input"
echo "Validated Vendor MeshNoC input $input"
```

`ipcores/vendor-meshnoc/generator/bin/generate`:

```bash
#!/usr/bin/env bash
set -euo pipefail
input=""
output=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --input) input="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    *) echo "ERROR design: unexpected argument $1" >&2; exit 1 ;;
  esac
done
test -f "$input"
mkdir -p "$output"
cat > "$output/vendor_mesh_manifest.json" <<JSON
{"schema":"vendor.meshnoc.manifest.v1","input":"$input"}
JSON
echo "Generated Vendor MeshNoC artifacts in $output"
```

Run:

```bash
chmod +x ipcores/vendor-meshnoc/generator/bin/validate ipcores/vendor-meshnoc/generator/bin/generate
```

- [ ] **Step 5: Run package and Qt tests**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
xmake run -P qt vendor_meshnoc_onboarding_test
xmake run -P qt commercial_noc_mvp_test
```

Expected: all pass. If `spec-gen --check` requires generated output updates,
generate them with the existing spec generator command style used by the
repository and include generated package output in the commit.

- [ ] **Step 6: Commit**

```bash
git add ipcores/vendor-meshnoc examples/contracts/vendor_meshnoc_project qt/test/vendor_meshnoc_onboarding_test.cpp qt/xmake.lua spec_generator/test/spec_generator_test.rb generated || true
git commit -m "test: add vendor meshnoc onboarding fixture"
```

## Task 8: Hard Cutoff Scans And Completion Report

**Files:**
- Create: `qt/test/plugin_hard_cutover_scan_test.cpp`
- Modify: `qt/xmake.lua`
- Modify: `docs/architecture/plugin-architecture-completion-report.md`
- Modify: `docs/architecture/plugin-architecture-hardening-report.md`
- Modify: `docs/architecture/README.md`

- [ ] **Step 1: Add failing hard cutoff scan**

Create `qt/test/plugin_hard_cutover_scan_test.cpp`:

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), "scan file should open");
    return QString::fromUtf8(file.readAll());
}

QString repoRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("qt")) && dir.cdUp()) {}
    return dir.absolutePath();
}

void requireNotContains(const QString& text, const QString& token, const QString& context) {
    if (text.contains(token)) {
        throw std::runtime_error(QStringLiteral("%1 must not contain %2")
                                     .arg(context, token)
                                     .toStdString());
    }
}

void testMainWindowHasNoConcreteIpBehavior() {
    const QString source = readFile(QDir(repoRoot()).filePath(QStringLiteral("qt/src/app/mainwindow.cpp")));
    for (const QString& token : {QStringLiteral("finepaper.ravenoc"),
                                 QStringLiteral("finepaper.opennoc"),
                                 QStringLiteral("finepaper.noc"),
                                 QStringLiteral("RaveTile"),
                                 QStringLiteral("OpenNoCXP")}) {
        requireNotContains(source, token, QStringLiteral("MainWindow"));
    }
}

void testPackagePluginDoesNotKnowNoCPlugin() {
    const QString source = readFile(QDir(repoRoot()).filePath(QStringLiteral("qt/src/package/packageplugin.cpp")));
    requireNotContains(source, QStringLiteral("nocplugin"), QStringLiteral("PackagePlugin"));
    requireNotContains(source, QStringLiteral("noc.v1"), QStringLiteral("PackagePlugin"));
}

void testNoCPluginDoesNotKnowConcreteIpPackages() {
    const QString source = readFile(QDir(repoRoot()).filePath(QStringLiteral("qt/src/noc/nocplugin.cpp")));
    for (const QString& token : {QStringLiteral("finepaper.ravenoc"),
                                 QStringLiteral("finepaper.opennoc"),
                                 QStringLiteral("finepaper.noc"),
                                 QStringLiteral("vendor.meshnoc"),
                                 QStringLiteral("RaveTile"),
                                 QStringLiteral("OpenNoCXP"),
                                 QStringLiteral("VendorSwitch")}) {
        requireNotContains(source, token, QStringLiteral("NoCPlugin"));
    }
}

void testGenerationRequestDoesNotExposeGraph() {
    const QString header = readFile(QDir(repoRoot()).filePath(QStringLiteral("qt/inc/app/projectgenerationrunner.h")));
    requireNotContains(header, QStringLiteral("const Graph* graph"), QStringLiteral("ProjectGenerationRequest"));
}

void testCompletionReportHasHardPassLanguage() {
    const QString report = readFile(QDir(repoRoot()).filePath(
        QStringLiteral("docs/architecture/plugin-architecture-completion-report.md")));
    requireNotContains(report, QStringLiteral("go-with-debt"), QStringLiteral("completion report"));
    require(report.contains(QStringLiteral("hard pass")) ||
                report.contains(QStringLiteral("blocked")),
            "completion report should contain hard pass or blocked verdict");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMainWindowHasNoConcreteIpBehavior();
        testPackagePluginDoesNotKnowNoCPlugin();
        testNoCPluginDoesNotKnowConcreteIpPackages();
        testGenerationRequestDoesNotExposeGraph();
        testCompletionReportHasHardPassLanguage();
    } catch (const std::exception& error) {
        std::cerr << "plugin_hard_cutover_scan_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "plugin_hard_cutover_scan_test passed\n";
    return 0;
}
```

- [ ] **Step 2: Run failing scan**

Run:

```bash
xmake run -P qt plugin_hard_cutover_scan_test
```

Expected: FAIL until previous tasks have removed the forbidden paths and
reports have hard pass language.

- [ ] **Step 3: Update architecture reports**

Update reports to state:

```text
Final verdict: hard pass
```

Only use hard pass if all tests and scan gates pass. If any hard acceptance
criterion is still not met, use:

```text
Final verdict: blocked
```

Do not use `go-with-debt`.

- [ ] **Step 4: Run final verification**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
ruby spec_generator/test/spec_generator_test.rb
ruby -I ipcraft_generator/lib ipcraft_generator/test/ipcraft_generator_test.rb
xmake run -P qt plugin_registry_test
xmake run -P qt staticplugincatalog_test
xmake run -P qt packagecoverage_test
xmake run -P qt nocplugin_test
xmake run -P qt designeditingservice_test
xmake run -P qt vendor_meshnoc_onboarding_test
xmake run -P qt plugin_hard_cutover_scan_test
xmake run -P qt validation_test
xmake run -P qt projectexternalvalidationrunner_test
xmake run -P qt projectgenerationrunner_test
xmake run -P qt commercial_noc_mvp_test
xmake build -P qt qt
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add qt/test/plugin_hard_cutover_scan_test.cpp qt/xmake.lua docs/architecture
git commit -m "test: enforce plugin hard cutover gates"
```

## Final Review

- [ ] Run `git status --short`.
- [ ] Run the final verification command block from Task 8.
- [ ] Dispatch final spec-compliance reviewer with the design spec and this plan.
- [ ] Dispatch final code-quality reviewer over the full branch diff.
- [ ] Use `superpowers:finishing-a-development-branch` only after all tests and review gates pass.
