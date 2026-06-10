// Plugin host foundation tests.
#include "app/appcontext.h"
#include "app/pluginhost.h"
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

class ContributingPlugin final : public IAppPlugin {
public:
    explicit ContributingPlugin(QString id) : m_id(std::move(id)) {}

    QString id() const override {
        return m_id;
    }

    void activate(AppContext& context) override {
        ++activateCount;

        context.workbench->addAction(WorkbenchActionContribution{
            .id = m_id + QStringLiteral(".action"),
            .text = QStringLiteral("Open"),
            .menuPath = QStringLiteral("File"),
            .toolBar = QStringLiteral("Main"),
            .objectName = m_id + QStringLiteral("Action"),
            .factory = [](QObject* parent) {
                return new QAction(QStringLiteral("Open"), parent);
            }
        });

        context.workbench->addPanel(WorkbenchPanelContribution{
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

void testActivatesPluginsWithAppContext() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;
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

void testRejectsDuplicatePluginIds() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;
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
    AppContext context;
    context.workbench = &workbench;
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

void testRejectsActivationWhenRequiredServicesAreMissing() {
    AppContext context;
    PluginHost host(context);

    auto plugin = std::make_unique<ContributingPlugin>(QStringLiteral("project"));
    ContributingPlugin* pluginPtr = plugin.get();

    require(host.registerPlugin(std::move(plugin)), "plugin should register without services");

    const PluginActivationResult result = host.activatePlugins();

    require(!result.success, "activation should fail without workbench service");
    require(result.error.contains(QStringLiteral("WorkbenchService")),
            "missing service error should mention WorkbenchService");
    require(pluginPtr->activateCount == 0, "plugin should not activate without workbench service");
}

void testActivationFailureDoesNotRetryPlugins() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;
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
    require(firstResult.error.contains(QStringLiteral("broken")) ||
                firstResult.error.contains(QStringLiteral("activation failed")),
            "activation failure error should identify plugin failure");
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
        testRejectsDuplicatePluginIds();
        testRejectsNonCanonicalPluginIds();
        testRejectsActivationWhenRequiredServicesAreMissing();
        testActivationFailureDoesNotRetryPlugins();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "pluginhost_foundation_test passed\n";
    return 0;
}
