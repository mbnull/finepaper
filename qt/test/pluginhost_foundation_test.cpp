// Plugin host foundation tests.
#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QStringList>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RegistrySet {
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;

    void attachTo(AppContext& context) {
        context.services = &services;
        context.extensionPoints = &extensionPoints;
        context.capabilities = &capabilities;
    }

    void registerWorkbench(WorkbenchService& workbench) {
        require(services.registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                         &workbench),
                "workbench service should register");
    }
};

class ContributingPlugin final : public IAppPlugin {
public:
    explicit ContributingPlugin(QString id) : m_id(std::move(id)) {}

    QString id() const override {
        return m_id;
    }

    void activate(AppContext& context) override {
        ++activateCount;

        WorkbenchService* service = context.services
            ? context.services->service<WorkbenchService>(
                  ServiceKey::fromLiteral("finepaper.workbench"))
            : nullptr;
        if (!service) {
            throw std::runtime_error("WorkbenchService is required by ContributingPlugin.");
        }

        service->addAction(WorkbenchActionContribution{
            .id = m_id + QStringLiteral(".action"),
            .text = QStringLiteral("Open"),
            .menuPath = QStringLiteral("File"),
            .toolBar = QStringLiteral("Main"),
            .objectName = m_id + QStringLiteral("Action"),
            .factory = [](QObject* parent) {
                return new QAction(QStringLiteral("Open"), parent);
            }
        });

        service->addPanel(WorkbenchPanelContribution{
            .id = m_id + QStringLiteral(".panel"),
            .title = QStringLiteral("Project"),
            .objectName = m_id + QStringLiteral("Panel"),
            .area = WorkbenchPanelArea::Right,
            .factory = [](QWidget* parent) {
                return new QLabel(QStringLiteral("Project"), parent);
            }
        });
    }

    int activateCount = 0;

private:
    QString m_id;
};

class ThrowingPlugin final : public IAppPlugin {
public:
    explicit ThrowingPlugin(QString id) : m_id(std::move(id)) {}

    QString id() const override {
        return m_id;
    }

    void activate(AppContext&) override {
        ++activateCount;
        throw std::runtime_error("activation exploded");
    }

    int activateCount = 0;

private:
    QString m_id;
};

class RenamingPlugin final : public IAppPlugin {
public:
    RenamingPlugin(QString id, QString activatedId)
        : m_id(std::move(id)), m_activatedId(std::move(activatedId)) {}

    QString id() const override {
        return m_id;
    }

    void activate(AppContext&) override {
        ++activateCount;
        m_id = m_activatedId;
    }

    int activateCount = 0;

private:
    QString m_id;
    QString m_activatedId;
};

void testActivatesPluginsWithAppContext() {
    WorkbenchService workbench;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    registries.registerWorkbench(workbench);
    PluginHost host(context);

    auto plugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* pluginPtr = plugin.get();

    require(host.registerPlugin(std::move(plugin)), "plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    const QStringList expectedIds{QStringLiteral("project")};

    require(result.success, "activation should succeed");
    require(result.activatedPluginIds == expectedIds, "activated plugin ids should match");
    require(pluginPtr->activateCount == 1, "plugin should activate once");
    require(workbench.actions().size() == 1, "plugin should contribute one action");
    require(workbench.panels().size() == 1, "plugin should contribute one panel");
}

void testSuccessfulActivationDoesNotRetryPlugins() {
    WorkbenchService workbench;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    registries.registerWorkbench(workbench);
    PluginHost host(context);

    auto plugin = std::make_unique<RenamingPlugin>(QStringLiteral("project"),
                                                   QStringLiteral("project.activated"));
    RenamingPlugin* pluginPtr = plugin.get();

    require(host.registerPlugin(std::move(plugin)), "plugin should register");

    const PluginActivationResult firstResult = host.activatePlugins();
    const QStringList expectedIds{QStringLiteral("project")};

    require(firstResult.success, "first activation should succeed");
    require(firstResult.activatedPluginIds == expectedIds,
            "first activation should report initial plugin id");
    require(pluginPtr->activateCount == 1, "plugin should activate once");

    const PluginActivationResult secondResult = host.activatePlugins();

    require(secondResult.success, "second activation should succeed");
    require(secondResult.activatedPluginIds == firstResult.activatedPluginIds,
            "second activation should preserve first activated ids snapshot");
    require(pluginPtr->activateCount == 1, "second activation should not reactivate plugin");
}

void testRejectsDuplicatePluginIds() {
    WorkbenchService workbench;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    registries.registerWorkbench(workbench);
    PluginHost host(context);

    require(host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("package"))),
            "first plugin should register");
    require(!host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("package"))),
            "duplicate plugin id should be rejected");

    const QStringList expectedIds{QStringLiteral("package")};
    require(host.pluginIds() == expectedIds, "registered plugin ids should remain unchanged");
}

void testRejectsNonCanonicalPluginIds() {
    WorkbenchService workbench;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    registries.registerWorkbench(workbench);
    PluginHost host(context);

    require(host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("package"))),
            "canonical plugin id should register");
    require(!host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral(" package "))),
            "plugin id with surrounding whitespace should be rejected");
    require(!host.registerPlugin(std::make_unique<ContributingPlugin>(QStringLiteral("\ttools"))),
            "plugin id with leading tab should be rejected");

    const QStringList expectedIds{QStringLiteral("package")};
    require(host.pluginIds() == expectedIds, "non-canonical ids should not change plugin ids");
}

void testRejectsActivationWhenRegistriesAreMissing() {
    AppContext context;
    PluginHost host(context);

    auto plugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* pluginPtr = plugin.get();

    require(host.registerPlugin(std::move(plugin)), "plugin should register without services");

    const PluginActivationResult result = host.activatePlugins();

    require(!result.success, "activation should fail without plugin registries");
    require(result.error.contains(QStringLiteral("Plugin registries")),
            "missing registry error should mention plugin registries");
    require(pluginPtr->activateCount == 0, "plugin should not activate without plugin registries");
}

void testRejectsActivationWhenWorkbenchServiceIsMissing() {
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    PluginHost host(context);

    auto plugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* pluginPtr = plugin.get();

    require(host.registerPlugin(std::move(plugin)), "plugin should register without workbench");

    const PluginActivationResult result = host.activatePlugins();

    require(!result.success, "activation should fail without workbench service");
    require(result.error.contains(QStringLiteral("WorkbenchService")),
            "missing service error should mention WorkbenchService");
    require(pluginPtr->activateCount == 0, "plugin should not activate without workbench service");
}

void testActivationFailureDoesNotRetryPlugins() {
    WorkbenchService workbench;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    registries.registerWorkbench(workbench);
    PluginHost host(context);

    auto contributingPlugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* contributingPluginPtr = contributingPlugin.get();
    auto throwingPlugin = std::make_unique<ThrowingPlugin>(QStringLiteral("broken"));
    ThrowingPlugin* throwingPluginPtr = throwingPlugin.get();

    require(host.registerPlugin(std::move(contributingPlugin)), "contributing plugin should register");
    require(host.registerPlugin(std::move(throwingPlugin)), "throwing plugin should register");

    const PluginActivationResult firstResult = host.activatePlugins();
    const QStringList expectedActivatedIds{QStringLiteral("project")};

    require(!firstResult.success, "activation should fail when a plugin throws");
    require(firstResult.error.contains(QStringLiteral("broken")),
            "activation failure error should identify failed plugin id");
    require(firstResult.error.contains(QStringLiteral("activation exploded")) ||
                firstResult.error.contains(QStringLiteral("activation failed")),
            "activation failure error should include exception text or fixed failure semantics");
    require(firstResult.activatedPluginIds == expectedActivatedIds,
            "failed activation should report previously activated plugin ids");
    require(contributingPluginPtr->activateCount == 1, "preceding plugin should activate once");
    require(throwingPluginPtr->activateCount == 1, "throwing plugin should activate once");

    const PluginActivationResult secondResult = host.activatePlugins();

    require(!secondResult.success, "activation retry should preserve failure");
    require(secondResult.error == firstResult.error, "activation retry should preserve error");
    require(secondResult.activatedPluginIds == expectedActivatedIds,
            "activation retry should preserve activated plugin ids");
    require(contributingPluginPtr->activateCount == 1, "preceding plugin should not activate again");
    require(throwingPluginPtr->activateCount == 1, "throwing plugin should not activate again");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testActivatesPluginsWithAppContext();
        testSuccessfulActivationDoesNotRetryPlugins();
        testRejectsDuplicatePluginIds();
        testRejectsNonCanonicalPluginIds();
        testRejectsActivationWhenRegistriesAreMissing();
        testRejectsActivationWhenWorkbenchServiceIsMissing();
        testActivationFailureDoesNotRetryPlugins();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "pluginhost_foundation_test passed\n";
    return 0;
}
