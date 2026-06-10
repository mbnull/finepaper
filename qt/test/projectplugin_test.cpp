#include "app/appcontext.h"
#include "app/pluginhost.h"
#include "app/workbenchservice.h"
#include "project/projectplugin.h"
#include "project/projectservice.h"

#include <QApplication>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testProjectPluginActivatesWithProjectService() {
    WorkbenchService workbench;
    ProjectService projectService;
    AppContext context;
    context.workbench = &workbench;
    context.projectService = &projectService;

    PluginHost host(context);
    require(host.registerPlugin(createProjectPlugin()), "project plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "project plugin activation should succeed");
    require(result.activatedPluginIds == QStringList{QStringLiteral("finepaper.project")},
            "activation result should contain project plugin id");
}

void testProjectPluginRequiresProjectService() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;

    PluginHost host(context);
    require(host.registerPlugin(createProjectPlugin()), "project plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(!result.success, "project plugin activation should fail without ProjectService");
    require(result.error.contains(QStringLiteral("ProjectService")),
            "activation error should mention ProjectService");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    try {
        testProjectPluginActivatesWithProjectService();
        testProjectPluginRequiresProjectService();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "projectplugin_test passed\n";
    return 0;
}
