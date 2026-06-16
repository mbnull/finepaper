#include "app/appcontext.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/plugininteractionregistry.h"
#include "app/pluginhost.h"
#include "app/serviceregistry.h"
#include "app/workbenchservice.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "package/packagecoverage.h"
#include "package/packageplugin.h"
#include "package/packageservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QJsonObject coverageDescriptor() {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("vendor.meshnoc")},
        {QStringLiteral("extensions"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("noc.v1")},
                         {QStringLiteral("required"), true},
                         {QStringLiteral("metadata"),
                          QJsonObject{{QStringLiteral("semantic"), QStringLiteral("mesh")}}}},
             QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.extra.v1")},
                         {QStringLiteral("required"), false}}
         }},
        {QStringLiteral("native"),
         QJsonObject{{QStringLiteral("vendor_blob"), true}}},
        {QStringLiteral("flows"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("validate")}}
         }},
        {QStringLiteral("artifacts"), QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("mesh_manifest")}}
         }},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("display_group"), QStringLiteral("Vendor")}}},
        {QStringLiteral("vendorExperimental"),
         QJsonObject{{QStringLiteral("feature"), QStringLiteral("sideband")}}}
    };
}

QJsonObject serviceDescriptor() {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("ipcraft.package.v1")},
        {QStringLiteral("id"), QStringLiteral("vendor.servicecoverage")},
        {QStringLiteral("name"), QStringLiteral("Service Coverage Fixture")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("extensions"), QJsonArray{}},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("coverage_marker"), true}}},
        {QStringLiteral("native"),
         QJsonObject{{QStringLiteral("vendor_blob"), true}}},
        {QStringLiteral("vendorServiceSection"),
         QJsonObject{{QStringLiteral("service_marker"), true}}}
    };
}

void writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error("failed to open JSON fixture for writing");
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
}

PackageFeatureCoverageItem requireItem(const PackageCoverageReport& report,
                                       const QString& id,
                                       const char* message) {
    const PackageFeatureCoverageItem item = report.item(id);
    require(item.id == id, message);
    return item;
}

void testRequiredUnhandledCapabilityBlocksPackage() {
    CapabilityRegistry capabilities;
    const PackageCoverageReport report =
        buildPackageCoverageReport(coverageDescriptor(), capabilities);

    require(report.packageId == QStringLiteral("vendor.meshnoc"),
            "package id should be reported");
    require(report.hasBlockingItems(),
            "required unhandled capability should block the package");
    require(requireItem(report,
                        QStringLiteral("capability:noc.v1"),
                        "noc.v1 coverage item should exist").status ==
                PackageFeatureCoverageStatus::Blocking,
            "required noc.v1 without handler should be blocking");
}

void testOptionalUnknownCapabilityIsUnsupported() {
    CapabilityRegistry capabilities;
    const PackageCoverageReport report =
        buildPackageCoverageReport(coverageDescriptor(), capabilities);

    require(requireItem(report,
                        QStringLiteral("capability:vendor.extra.v1"),
                        "optional capability coverage item should exist").status ==
                PackageFeatureCoverageStatus::Unsupported,
            "optional unknown capability should be unsupported");
}

void testRegisteredHandlerMakesRequiredCapabilityHandled() {
    CapabilityRegistry capabilities;
    CapabilityHandlerDescriptor handler;
    handler.capabilityId = QStringLiteral("noc.v1");
    handler.ownerPluginId = QStringLiteral("finepaper.noc");
    require(capabilities.registerHandler(handler), "capability handler should register");

    const PackageCoverageReport report =
        buildPackageCoverageReport(coverageDescriptor(), capabilities);

    require(!report.hasBlockingItems(),
            "handled required capability should not block the package");
    require(requireItem(report,
                        QStringLiteral("capability:noc.v1"),
                        "handled capability coverage item should exist").status ==
                PackageFeatureCoverageStatus::Handled,
            "registered handler should make noc.v1 handled");
}

void testDescriptorSectionsAreVisible() {
    CapabilityRegistry capabilities;
    CapabilityHandlerDescriptor handler;
    handler.capabilityId = QStringLiteral("noc.v1");
    handler.ownerPluginId = QStringLiteral("finepaper.noc");
    capabilities.registerHandler(handler);

    const PackageCoverageReport report =
        buildPackageCoverageReport(coverageDescriptor(), capabilities);

    const PackageFeatureCoverageItem native =
        requireItem(report, QStringLiteral("native"), "native section should be visible");
    require(native.status == PackageFeatureCoverageStatus::Visible,
            "native descriptor data should be visible");
    require(native.descriptor.value(QStringLiteral("vendor_blob")).toBool(),
            "native descriptor item should preserve raw data");

    require(requireItem(report,
                        QStringLiteral("flow:validate"),
                        "flow declaration should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "flow declaration should be visible");
    require(requireItem(report,
                        QStringLiteral("artifact:mesh_manifest"),
                        "artifact declaration should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "artifact declaration should be visible");
    require(requireItem(report,
                        QStringLiteral("metadata"),
                        "metadata section should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "metadata descriptor data should be visible");
    const PackageFeatureCoverageItem unknown =
        requireItem(report,
                    QStringLiteral("unknown:vendorExperimental"),
                    "unknown descriptor section should be visible");
    require(unknown.status == PackageFeatureCoverageStatus::Visible,
            "unknown descriptor section should be visible");
    require(unknown.descriptor.value(QStringLiteral("feature")).toString() ==
                QStringLiteral("sideband"),
            "unknown descriptor item should preserve raw data");
}

void testSpecConnectionRuleCompatibilityCoveragePreservesEndpointMatches() {
    ipcraft::PackageSpec spec;
    spec.id = QStringLiteral("vendor.compatibilitycoverage");

    ipcraft::PackageCompatibilityRule rule;
    rule.connectionType = QStringLiteral("mesh-link");
    rule.arity = QStringLiteral("one-to-one");
    rule.from.kind = QStringLiteral("initiator");
    rule.from.protocol = QStringLiteral("axi4");
    rule.from.role = QStringLiteral("master");
    rule.from.direction = QStringLiteral("out");
    rule.to.kind = QStringLiteral("target");
    rule.to.protocol = QStringLiteral("axi4");
    rule.to.role = QStringLiteral("slave");
    rule.to.direction = QStringLiteral("in");
    rule.metadata.insert(QStringLiteral("latency"), QStringLiteral("bounded"));
    spec.connectionRules.compatibility.append(rule);

    CapabilityRegistry capabilities;
    const PackageCoverageReport report = buildPackageCoverageReport(spec, capabilities);
    const PackageFeatureCoverageItem item =
        requireItem(report,
                    QStringLiteral("connection_rules"),
                    "connection rules coverage item should exist");
    const QJsonArray compatibility =
        item.descriptor.value(QStringLiteral("compatibility")).toArray();
    require(compatibility.size() == 1,
            "connection rule compatibility declaration should be visible");

    const QJsonObject compatibilityRule = compatibility.at(0).toObject();
    const QJsonObject from = compatibilityRule.value(QStringLiteral("from")).toObject();
    const QJsonObject to = compatibilityRule.value(QStringLiteral("to")).toObject();
    require(from.value(QStringLiteral("kind")).toString() == QStringLiteral("initiator"),
            "compatibility from.kind should be preserved");
    require(from.value(QStringLiteral("protocol")).toString() == QStringLiteral("axi4"),
            "compatibility from.protocol should be preserved");
    require(from.value(QStringLiteral("role")).toString() == QStringLiteral("master"),
            "compatibility from.role should be preserved");
    require(from.value(QStringLiteral("direction")).toString() == QStringLiteral("out"),
            "compatibility from.direction should be preserved");
    require(to.value(QStringLiteral("kind")).toString() == QStringLiteral("target"),
            "compatibility to.kind should be preserved");
    require(to.value(QStringLiteral("protocol")).toString() == QStringLiteral("axi4"),
            "compatibility to.protocol should be preserved");
    require(to.value(QStringLiteral("role")).toString() == QStringLiteral("slave"),
            "compatibility to.role should be preserved");
    require(to.value(QStringLiteral("direction")).toString() == QStringLiteral("in"),
            "compatibility to.direction should be preserved");
}

void testDirectDescriptorObjectFormExtensionsArePayloadsNotCapabilities() {
    QJsonObject descriptor;
    descriptor.insert(QStringLiteral("id"), QStringLiteral("vendor.payloadcoverage"));
    descriptor.insert(QStringLiteral("extensions"),
                      QJsonObject{
                          {QStringLiteral("vendor.payload.v1"),
                           QJsonObject{{QStringLiteral("enabled"), true}}}
                      });

    CapabilityRegistry capabilities;
    const PackageCoverageReport report = buildPackageCoverageReport(descriptor, capabilities);

    require(!report.hasBlockingItems(),
            "object-form extension payloads should not create blocking capabilities");
    require(report.item(QStringLiteral("capability:vendor.payload.v1")).status ==
                PackageFeatureCoverageStatus::Invalid,
            "object-form extension payload keys should not be reported as capability ids");
}

void testPackageServiceStoresCoverageFromFullPackageSpecs() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary package root should be valid");
    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("service")), "package directory should be created");
    writeJsonFile(root.filePath(QStringLiteral("service/ipcraft.json")), serviceDescriptor());

    CapabilityRegistry capabilities;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService service(&registry);
    service.setCapabilityRegistry(&capabilities);

    const PackageServiceLoadResult result =
        service.reloadPackageRoots(QStringList{root.absolutePath()});

    require(result.success, "service fixture package should load");
    require(service.coverageReports().size() == 1,
            "service should store one coverage report after reload");
    const PackageCoverageReport* report =
        service.coverageReport(QStringLiteral("vendor.servicecoverage"));
    require(report != nullptr, "service coverage report should be addressable by package id");
    require(requireItem(*report, QStringLiteral("metadata"), "metadata should be covered").status ==
                PackageFeatureCoverageStatus::Visible,
            "service coverage should include top-level metadata from full spec");
    require(requireItem(*report, QStringLiteral("native"), "native should be covered").status ==
                PackageFeatureCoverageStatus::Visible,
            "service coverage should include top-level native data from full spec");
    const PackageFeatureCoverageItem unknown =
        requireItem(*report,
                    QStringLiteral("unknown:vendorServiceSection"),
                    "service unknown section should be covered");
    require(unknown.status == PackageFeatureCoverageStatus::Visible,
            "service coverage should include unknown top-level sections from full spec");
    require(unknown.descriptor.value(QStringLiteral("service_marker")).toBool(),
            "service unknown section should preserve raw data");
}

void testPackagePluginRegistersCoverageInspectorContribution() {
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;
    PluginInteractionRegistry interactions;
    WorkbenchService workbench;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService packageService(&registry);
    require(services.registerService(ServiceKey::fromLiteral("finepaper.package"),
                                     &packageService),
            "package service should register");

    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.interactions = &interactions;
    context.workbench = &workbench;
    context.packageService = &packageService;

    PluginHost host(context);
    require(host.registerPlugin(createPackagePlugin()), "package plugin should register");
    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "package plugin should activate");

    const QVector<ExtensionContribution> inspectorSections =
        extensionPoints.contributions(QStringLiteral("ui.inspectorSection"));
    bool sawCoverageInspector = false;
    for (const ExtensionContribution& contribution : inspectorSections) {
        sawCoverageInspector = sawCoverageInspector ||
            contribution.id == QStringLiteral("finepaper.package.coverage-inspector");
    }
    require(sawCoverageInspector,
            "package plugin should register package coverage inspector contribution");
}

void testPackagePluginRegistersGenericCoverageInteractionProvider() {
    ServiceRegistry services;
    ExtensionPointRegistry extensionPoints;
    CapabilityRegistry capabilities;
    PluginInteractionRegistry interactions;
    WorkbenchService workbench;
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PackageService packageService(&registry);
    require(services.registerService(ServiceKey::fromLiteral("finepaper.package"),
                                     &packageService),
            "package service should register");

    AppContext context;
    context.services = &services;
    context.extensionPoints = &extensionPoints;
    context.capabilities = &capabilities;
    context.interactions = &interactions;
    context.workbench = &workbench;
    context.packageService = &packageService;

    PluginHost host(context);
    require(host.registerPlugin(createPackagePlugin()), "package plugin should register");
    const PluginActivationResult result = host.activatePlugins();
    require(result.success, "package plugin should activate");

    ActiveWorkspaceState workspace;
    workspace.hasActiveIp = true;
    workspace.ipcoreId = QStringLiteral("vendor.dynamic");
    workspace.instanceId = QStringLiteral("dynamic_0");

    IpCatalogEntry entry;
    entry.id = QStringLiteral("vendor.dynamic");
    entry.packageId = QStringLiteral("vendor.dynamic");

    PackageCoverageReport coverage;
    coverage.packageId = QStringLiteral("vendor.dynamic");
    coverage.items.append(PackageFeatureCoverageItem{
        QStringLiteral("capability:vendor.dynamic.feature.v1"),
        QStringLiteral("vendor.dynamic.feature.v1"),
        PackageFeatureCoverageStatus::Unsupported,
        QStringLiteral("Optional capability has no registered handler."),
        QJsonObject{{QStringLiteral("metadata"),
                     QJsonObject{{QStringLiteral("label"), QStringLiteral("Dynamic Feature")}}}}
    });
    coverage.items.append(PackageFeatureCoverageItem{
        QStringLiteral("view:dynamic_view"),
        QStringLiteral("Dynamic View"),
        PackageFeatureCoverageStatus::Visible,
        QStringLiteral("Descriptor declaration is visible in the package inspector."),
        QJsonObject{{QStringLiteral("id"), QStringLiteral("dynamic_view")}}
    });

    const QVector<PluginInteractionDescriptor> projected =
        interactions.interactionsForWorkspace(workspace, entry, &coverage);
    require(projected.size() == 2,
            "package provider should expose every coverage item as an interaction");
    require(projected.at(0).id ==
                QStringLiteral("package:capability:vendor.dynamic.feature.v1"),
            "package provider should expose arbitrary vendor capability ids without hardcoded tokens");
    require(projected.at(0).label == QStringLiteral("Dynamic Feature"),
            "package provider should use descriptor metadata labels when present");
    require(projected.at(1).id == QStringLiteral("package:view:dynamic_view"),
            "package provider should expose view declarations as interactions");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRequiredUnhandledCapabilityBlocksPackage();
        testOptionalUnknownCapabilityIsUnsupported();
        testRegisteredHandlerMakesRequiredCapabilityHandled();
        testDescriptorSectionsAreVisible();
        testSpecConnectionRuleCompatibilityCoveragePreservesEndpointMatches();
        testDirectDescriptorObjectFormExtensionsArePayloadsNotCapabilities();
        testPackageServiceStoresCoverageFromFullPackageSpecs();
        testPackagePluginRegistersCoverageInspectorContribution();
        testPackagePluginRegistersGenericCoverageInteractionProvider();
    } catch (const std::exception& error) {
        std::cerr << "packagecoverage_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "packagecoverage_test passed\n";
    return 0;
}
