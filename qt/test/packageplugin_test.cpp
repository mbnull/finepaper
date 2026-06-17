#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"
#include "modules/moduleregistry.h"
#include "package/packageplugin.h"
#include "package/packageservice.h"
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

void testPackagePluginActivatesWithPackageService() {
    WorkbenchService workbench;
    ProjectService projectService;
    ModuleRegistry moduleRegistry(ModuleRegistry::LoadMode::Empty);
    PackageService packageService(&moduleRegistry);
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                                &workbench),
            "workbench service should register");
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.project"),
                                                &projectService),
            "project service should register");
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.package"),
                                                &packageService),
            "package service should register");

    PluginHost host(context);
    require(host.registerPlugin(createPackagePlugin()), "package plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "package plugin activation should succeed");
    require(result.activatedPluginIds == QStringList{QStringLiteral("finepaper.package")},
            "activation result should contain package plugin id");
}

void testPackagePluginRequiresPackageService() {
    WorkbenchService workbench;
    ProjectService projectService;
    RegistrySet registries;
    AppContext context;
    registries.attachTo(context);
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.workbench"),
                                                &workbench),
            "workbench service should register");
    require(registries.services.registerService(ServiceKey::fromLiteral("finepaper.project"),
                                                &projectService),
            "project service should register");

    PluginHost host(context);
    require(host.registerPlugin(createPackagePlugin()), "package plugin should register");

    const PluginActivationResult result = host.activatePlugins();
    require(!result.success, "package plugin activation should fail without PackageService");
    require(result.error.contains(QStringLiteral("PackageService")),
            "activation error should mention PackageService");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    try {
        testPackagePluginActivatesWithPackageService();
        testPackagePluginRequiresPackageService();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "packageplugin_test passed\n";
    return 0;
}
