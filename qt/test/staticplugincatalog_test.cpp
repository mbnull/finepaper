#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/plugininteractionregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/staticplugincatalog.h"
#include "app/workbenchservice.h"
#include "package/packageservice.h"
#include "project/projectservice.h"
#include "app/toolpipelineservice.h"
#include "modules/moduleregistry.h"
#include "workspace/activeworkspacecontroller.h"

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
    PluginInteractionRegistry interactions;

    services.registerService(ServiceKey::fromLiteral("finepaper.workbench"), &workbench);
    services.registerService(ServiceKey::fromLiteral("finepaper.project"), &project);
    services.registerService(ServiceKey::fromLiteral("finepaper.package"), &package);
    services.registerService(ServiceKey::fromLiteral("finepaper.tool-pipeline"), &tools);

    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.interactions = &interactions;

    PluginHost host(context);
    registerStaticPlugins(host);
    require(registerStaticPluginInteractions(interactions),
            "static catalog should register built-in interaction providers");

    const QStringList ids = host.pluginIds();
    require(ids.contains(QStringLiteral("finepaper.project")),
            "static catalog should include project plugin");
    require(ids.contains(QStringLiteral("finepaper.package")),
            "static catalog should include package plugin");
    require(ids.contains(QStringLiteral("finepaper.noc-plugin")),
            "static catalog should include NoC plugin");
    require(ids.contains(QStringLiteral("finepaper.tool-pipeline")),
            "static catalog should include tool pipeline plugin");
    require(ids.indexOf(QStringLiteral("finepaper.package")) <
                ids.indexOf(QStringLiteral("finepaper.noc-plugin")),
            "NoC plugin should be registered after package plugin");
    require(ids.indexOf(QStringLiteral("finepaper.noc-plugin")) <
                ids.indexOf(QStringLiteral("finepaper.tool-pipeline")),
            "NoC plugin should be registered before tool pipeline plugin");

    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "static plugins should activate");

    bool sawNocHandler = false;
    for (const CapabilityHandlerDescriptor& handler : capabilities.handlers()) {
        sawNocHandler = sawNocHandler || handler.capabilityId == QStringLiteral("noc.v1");
    }
    require(sawNocHandler, "static plugin activation should register noc.v1 handler");

    ActiveWorkspaceState workspace;
    workspace.hasActiveIp = true;
    workspace.ipcoreId = QStringLiteral("org.example.noc");
    workspace.instanceId = QStringLiteral("noc_0");

    IpCatalogEntry entry;
    entry.id = QStringLiteral("org.example.noc");
    entry.packageId = QStringLiteral("org.example.noc");
    TopologyPresetDescriptor mesh;
    mesh.id = QStringLiteral("mesh");
    mesh.label = QStringLiteral("Mesh");
    entry.topologyPresets.append(mesh);

    const QVector<PluginInteractionDescriptor> workspaceInteractions =
        interactions.interactionsForWorkspace(workspace, entry, nullptr);
    require(workspaceInteractions.size() == 1,
            "static interaction provider should expose topology descriptors");
    require(workspaceInteractions.first().id == QStringLiteral("topology:mesh"),
            "topology interaction id should be provider-owned, not MainWindow-owned");
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
