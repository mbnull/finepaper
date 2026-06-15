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
