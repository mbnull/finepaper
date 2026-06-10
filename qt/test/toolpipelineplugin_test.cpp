#include "app/appcontext.h"
#include "app/pluginhost.h"
#include "app/toolpipelineplugin.h"
#include "app/toolpipelineservice.h"
#include "app/workbenchservice.h"

#include <QApplication>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testToolPipelinePluginActivatesWithService() {
    WorkbenchService workbench;
    ToolPipelineService toolPipelineService;
    AppContext context;
    context.workbench = &workbench;
    context.toolPipelineService = &toolPipelineService;

    PluginHost host(context);
    require(host.registerPlugin(createToolPipelinePlugin()),
            "tool pipeline plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "tool pipeline plugin activation should succeed");
    require(result.activatedPluginIds == QStringList{QStringLiteral("finepaper.tool-pipeline")},
            "activation result should contain tool pipeline plugin id");
}

void testToolPipelinePluginRequiresService() {
    WorkbenchService workbench;
    AppContext context;
    context.workbench = &workbench;

    PluginHost host(context);
    require(host.registerPlugin(createToolPipelinePlugin()),
            "tool pipeline plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(!result.success,
            "tool pipeline plugin activation should fail without ToolPipelineService");
    require(result.error.contains(QStringLiteral("ToolPipelineService")),
            "activation error should mention ToolPipelineService");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    try {
        testToolPipelinePluginActivatesWithService();
        testToolPipelinePluginRequiresService();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "toolpipelineplugin_test passed\n";
    return 0;
}
