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

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testActivatesPluginsWithAppContext();
        testRejectsDuplicatePluginIds();
        testRejectsActivationWhenRequiredServicesAreMissing();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "pluginhost_foundation_test passed\n";
    return 0;
}
