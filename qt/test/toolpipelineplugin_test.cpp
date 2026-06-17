#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
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

struct RegistrySet {
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;

    void attachTo(AppContext& context) {
        context.services = &services;
        context.extensionPoints = &extensionPoints;
        context.capabilities = &capabilities;
    }
};

void testToolPipelinePluginActivatesWithService() {
    WorkbenchService workbench;
    ToolPipelineService toolPipelineService;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                                &workbench),
            "workbench service should register");
    require(registries.services.registerService(
                ServiceKey::fromLiteral("finepaper.tool-pipeline"),
                &toolPipelineService),
            "tool pipeline service should register");

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
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                                &workbench),
            "workbench service should register");

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
