#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/serviceregistry.h"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FakeService {
    int value = 7;
};

struct OtherFakeService {
    int value = 11;
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

void testServiceRegistryRejectsInvalidServicesAndTypeMismatches() {
    ServiceRegistry registry;
    FakeService service;

    require(!registry.registerService(ServiceKey::fromLiteral(""), &service),
            "empty service key should be rejected");
    require(!registry.registerService(ServiceKey::fromLiteral(" test.fake"), &service),
            "service key with leading whitespace should be rejected");
    require(!registry.registerService(ServiceKey::fromLiteral("test.fake "), &service),
            "service key with trailing whitespace should be rejected");
    require(!registry.registerService(ServiceKey::fromLiteral("test.null"),
                                      static_cast<FakeService*>(nullptr)),
            "null typed service should be rejected");

    require(registry.registerService(ServiceKey::fromLiteral("test.fake"), &service),
            "typed service registration should succeed");
    require(registry.service<OtherFakeService>(ServiceKey::fromLiteral("test.fake")) == nullptr,
            "typed lookup with mismatched type should return null");
    require(registry.service<FakeService>(ServiceKey::fromLiteral("test.fake")) == &service,
            "typed lookup with matching type should return the service");
}

ExtensionContribution contribution(QString id,
                                   QString extensionPoint,
                                   QString ownerPluginId) {
    ExtensionContribution contribution;
    contribution.id = std::move(id);
    contribution.extensionPoint = std::move(extensionPoint);
    contribution.ownerPluginId = std::move(ownerPluginId);
    contribution.label = QStringLiteral("Test Action");
    return contribution;
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

void testExtensionPointRegistryRejectsInvalidContributionsAndPreservesOrder() {
    ExtensionPointRegistry registry;
    require(!registry.registerContribution(contribution(QStringLiteral(" finepaper.invalid"),
                                                        QStringLiteral("ui.action"),
                                                        QStringLiteral("finepaper.test"))),
            "contribution id with leading whitespace should be rejected");
    require(!registry.registerContribution(contribution(QStringLiteral("finepaper.invalid"),
                                                        QStringLiteral("ui.action "),
                                                        QStringLiteral("finepaper.test"))),
            "extension point with trailing whitespace should be rejected");
    require(!registry.registerContribution(contribution(QStringLiteral("finepaper.invalid"),
                                                        QStringLiteral("ui.action"),
                                                        QStringLiteral(""))),
            "empty owner plugin id should be rejected");

    const ExtensionContribution first = contribution(QStringLiteral("finepaper.first"),
                                                     QStringLiteral("ui.action"),
                                                     QStringLiteral("finepaper.test"));
    const ExtensionContribution second = contribution(QStringLiteral("finepaper.second"),
                                                      QStringLiteral("ui.action"),
                                                      QStringLiteral("finepaper.test"));
    const ExtensionContribution panel = contribution(QStringLiteral("finepaper.panel"),
                                                     QStringLiteral("ui.panel"),
                                                     QStringLiteral("finepaper.test"));
    require(registry.registerContribution(first), "first ordered contribution should register");
    require(registry.registerContribution(second), "second ordered contribution should register");
    require(registry.registerContribution(panel), "panel contribution should register");

    const QVector<ExtensionContribution> actions =
        registry.contributions(QStringLiteral("ui.action"));
    require(actions.size() == 2, "two action contributions should be registered");
    require(actions.at(0).id == first.id, "first action contribution should preserve insertion order");
    require(actions.at(1).id == second.id, "second action contribution should preserve insertion order");

    const QVector<ExtensionContribution> all = registry.allContributions();
    require(all.size() == 3, "all contributions should include every registered contribution");
    require(all.at(0).id == first.id, "first contribution should preserve global insertion order");
    require(all.at(1).id == second.id, "second contribution should preserve global insertion order");
    require(all.at(2).id == panel.id, "third contribution should preserve global insertion order");
}

void testExtensionPointRegistryRejectsContributionsAtCapacity() {
    ExtensionPointRegistry registry;
    for (int index = 0; index < ExtensionPointRegistry::kMaxContributions; ++index) {
        require(registry.registerContribution(
                    contribution(QStringLiteral("finepaper.action.%1").arg(index),
                                 QStringLiteral("ui.action"),
                                 QStringLiteral("finepaper.test"))),
                "contribution below capacity should register");
    }

    require(!registry.registerContribution(contribution(QStringLiteral("finepaper.action.overflow"),
                                                        QStringLiteral("ui.action"),
                                                        QStringLiteral("finepaper.test"))),
            "contribution beyond capacity should be rejected");
    require(registry.allContributions().size() == ExtensionPointRegistry::kMaxContributions,
            "rejected overflow contribution should not be stored");
}

void testCapabilityRegistryReportsRequiredMissingHandler() {
    CapabilityRegistry registry;
    PackageCapabilityDescriptor capability;
    capability.id = QStringLiteral("noc.v1");
    capability.required = true;
    capability.packageId = QStringLiteral("vendor.meshnoc");

    require(registry.recordPackageCapability(capability),
            "valid package capability should be recorded");
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
    require(registry.recordPackageCapability(capability),
            "valid handled capability should be recorded");

    const QVector<CapabilityCoverageRecord> coverage =
        registry.coverageForPackage(QStringLiteral("vendor.meshnoc"));
    require(coverage.size() == 1, "one handled capability coverage record should exist");
    require(coverage.first().status == CapabilityCoverageStatus::Handled,
            "capability with registered handler should be handled");
}

void testCapabilityRegistryRejectsInvalidHandlersAndCapabilities() {
    CapabilityRegistry registry;

    CapabilityHandlerDescriptor handler;
    handler.capabilityId = QStringLiteral("noc.v1");
    handler.ownerPluginId = QStringLiteral("finepaper.noc");
    handler.extensionPoints = {QStringLiteral("ui.inspectorSection")};

    CapabilityHandlerDescriptor invalidCapability = handler;
    invalidCapability.capabilityId = QStringLiteral(" noc.v1");
    require(!registry.registerHandler(invalidCapability),
            "handler capability id with leading whitespace should be rejected");

    CapabilityHandlerDescriptor invalidOwner = handler;
    invalidOwner.ownerPluginId = QStringLiteral("finepaper.noc ");
    require(!registry.registerHandler(invalidOwner),
            "handler owner plugin id with trailing whitespace should be rejected");

    CapabilityHandlerDescriptor invalidExtensionPoint = handler;
    invalidExtensionPoint.extensionPoints = {QStringLiteral("ui.inspectorSection"),
                                             QStringLiteral(" editor.tool")};
    require(!registry.registerHandler(invalidExtensionPoint),
            "handler extension point with leading whitespace should be rejected");

    CapabilityHandlerDescriptor emptyExtensionPoint = handler;
    emptyExtensionPoint.extensionPoints = {QStringLiteral("")};
    require(!registry.registerHandler(emptyExtensionPoint),
            "handler empty extension point should be rejected");

    PackageCapabilityDescriptor invalidPackage;
    invalidPackage.packageId = QStringLiteral(" vendor.meshnoc");
    invalidPackage.id = QStringLiteral("noc.v1");
    invalidPackage.required = true;
    require(!registry.recordPackageCapability(invalidPackage),
            "invalid package id should be rejected");
    require(registry.coverageForPackage(QStringLiteral(" vendor.meshnoc")).isEmpty(),
            "capability with invalid package id should be ignored");

    PackageCapabilityDescriptor invalidCapabilityId;
    invalidCapabilityId.packageId = QStringLiteral("vendor.meshnoc");
    invalidCapabilityId.id = QStringLiteral(" noc.v1");
    invalidCapabilityId.required = true;
    require(!registry.recordPackageCapability(invalidCapabilityId),
            "invalid capability id should be rejected");
    require(registry.coverageForPackage(QStringLiteral("vendor.meshnoc")).isEmpty(),
            "capability with invalid capability id should be ignored");

    PackageCapabilityDescriptor whitespaceCapabilityId;
    whitespaceCapabilityId.packageId = QStringLiteral("vendor.meshnoc");
    whitespaceCapabilityId.capabilityId = QStringLiteral(" ");
    whitespaceCapabilityId.id = QStringLiteral("noc.v1");
    whitespaceCapabilityId.required = true;
    require(!registry.recordPackageCapability(whitespaceCapabilityId),
            "whitespace capability id should be rejected");
    require(registry.coverageForPackage(QStringLiteral("vendor.meshnoc")).isEmpty(),
            "capability with whitespace capability id should be ignored");
}

void testCapabilityRegistryRejectsHandlersAtCapacity() {
    CapabilityRegistry registry;
    for (int index = 0; index < CapabilityRegistry::kMaxHandlers; ++index) {
        CapabilityHandlerDescriptor handler;
        handler.capabilityId = QStringLiteral("capability.%1").arg(index);
        handler.ownerPluginId = QStringLiteral("finepaper.test");
        handler.extensionPoints = {QStringLiteral("ui.panel")};
        require(registry.registerHandler(handler), "handler below capacity should register");
    }

    CapabilityHandlerDescriptor overflow;
    overflow.capabilityId = QStringLiteral("capability.overflow");
    overflow.ownerPluginId = QStringLiteral("finepaper.test");
    overflow.extensionPoints = {QStringLiteral("ui.panel")};
    require(!registry.registerHandler(overflow), "handler beyond capacity should be rejected");
    require(registry.handlers().size() == CapabilityRegistry::kMaxHandlers,
            "rejected overflow handler should not be stored");
}

void testCapabilityRegistryRejectsPackageCapabilitiesAtCapacity() {
    CapabilityRegistry registry;
    for (int index = 0; index < CapabilityRegistry::kMaxPackageCapabilities; ++index) {
        PackageCapabilityDescriptor capability;
        capability.packageId = QStringLiteral("vendor.meshnoc");
        capability.id = QStringLiteral("capability.%1").arg(index);
        capability.required = false;
        require(registry.recordPackageCapability(capability),
                "package capability below capacity should be recorded");
    }

    PackageCapabilityDescriptor overflow;
    overflow.packageId = QStringLiteral("vendor.meshnoc");
    overflow.id = QStringLiteral("capability.overflow");
    overflow.required = false;
    require(!registry.recordPackageCapability(overflow),
            "package capability beyond capacity should be rejected");
    require(registry.coverageForPackage(QStringLiteral("vendor.meshnoc")).size() ==
                CapabilityRegistry::kMaxPackageCapabilities,
            "rejected overflow package capability should not be stored");
}

void testCapabilityRegistryPreservesHandlerAndCoverageOrder() {
    CapabilityRegistry registry;

    CapabilityHandlerDescriptor firstHandler;
    firstHandler.capabilityId = QStringLiteral("noc.v1");
    firstHandler.ownerPluginId = QStringLiteral("finepaper.noc");
    firstHandler.extensionPoints = {QStringLiteral("ui.inspectorSection")};

    CapabilityHandlerDescriptor secondHandler;
    secondHandler.capabilityId = QStringLiteral("diagnostics.v1");
    secondHandler.ownerPluginId = QStringLiteral("finepaper.diagnostics");
    secondHandler.extensionPoints = {QStringLiteral("ui.panel")};

    require(registry.registerHandler(firstHandler), "first handler should register");
    require(registry.registerHandler(secondHandler), "second handler should register");

    const QVector<CapabilityHandlerDescriptor> handlers = registry.handlers();
    require(handlers.size() == 2, "two handlers should be registered");
    require(handlers.at(0).capabilityId == firstHandler.capabilityId,
            "first handler should preserve insertion order");
    require(handlers.at(1).capabilityId == secondHandler.capabilityId,
            "second handler should preserve insertion order");

    PackageCapabilityDescriptor firstCapability;
    firstCapability.packageId = QStringLiteral("vendor.meshnoc");
    firstCapability.id = QStringLiteral("noc.v1");
    firstCapability.required = true;

    PackageCapabilityDescriptor secondCapability;
    secondCapability.packageId = QStringLiteral("vendor.meshnoc");
    secondCapability.capabilityId = QStringLiteral("diagnostics.v1");
    secondCapability.required = false;

    require(registry.recordPackageCapability(firstCapability),
            "first ordered package capability should be recorded");
    require(registry.recordPackageCapability(secondCapability),
            "second ordered package capability should be recorded");

    const QVector<CapabilityCoverageRecord> coverage =
        registry.coverageForPackage(QStringLiteral("vendor.meshnoc"));
    require(coverage.size() == 2, "two package coverage records should exist");
    require(coverage.at(0).capabilityId == QStringLiteral("noc.v1"),
            "first coverage record should preserve insertion order");
    require(coverage.at(1).capabilityId == QStringLiteral("diagnostics.v1"),
            "second coverage record should preserve insertion order");
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
        testServiceRegistryRejectsInvalidServicesAndTypeMismatches();
        testExtensionPointRegistryStoresContributions();
        testExtensionPointRegistryRejectsInvalidContributionsAndPreservesOrder();
        testExtensionPointRegistryRejectsContributionsAtCapacity();
        testCapabilityRegistryReportsRequiredMissingHandler();
        testCapabilityRegistryReportsHandledCapability();
        testCapabilityRegistryRejectsInvalidHandlersAndCapabilities();
        testCapabilityRegistryRejectsHandlersAtCapacity();
        testCapabilityRegistryRejectsPackageCapabilitiesAtCapacity();
        testCapabilityRegistryPreservesHandlerAndCoverageOrder();
        testAppContextUsesRegistries();
    } catch (const std::exception& error) {
        std::cerr << "plugin_registry_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "plugin_registry_test passed\n";
    return 0;
}
