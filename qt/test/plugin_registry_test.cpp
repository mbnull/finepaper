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
