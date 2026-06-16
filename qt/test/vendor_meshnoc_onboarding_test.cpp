// Vendor MeshNoC zero-C++ onboarding fixture test.
#include "app/capabilityregistry.h"
#include "app/projectgenerationrunner.h"
#include "ipcraft/flowrunner.h"
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"
#include "modules/moduleregistry.h"
#include "package/packagecoverage.h"
#include "package/packageservice.h"
#include "project/ipinstancestate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

constexpr auto kPackageId = "vendor.meshnoc";

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireMessage(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toLocal8Bit().constData());
    }
}

QString repositoryPath(const QString& relativePath) {
    const QStringList startPaths = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath()
    };

    for (const QString& startPath : startPaths) {
        QDir dir(startPath);
        while (true) {
            const QFileInfo info(dir.filePath(relativePath));
            if (info.exists()) {
                return info.absoluteFilePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QFileInfo(QDir(startPaths.first()).filePath(relativePath)).absoluteFilePath();
}

QString diagnosticsText(const QVector<IpcraftDiagnostic>& diagnostics) {
    QStringList messages;
    for (const IpcraftDiagnostic& diagnostic : diagnostics) {
        messages.append(QStringLiteral("%1: %2").arg(diagnostic.ruleId, diagnostic.message));
    }
    return messages.join(QLatin1Char('\n'));
}

QString flowDiagnosticsText(const ipcraft::DiagnosticStore& diagnostics) {
    QStringList messages;
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        messages.append(QStringLiteral("%1: %2").arg(diagnostic.ruleId, diagnostic.message));
    }
    return messages.join(QLatin1Char('\n'));
}

void registerCapability(CapabilityRegistry& registry, const QString& capabilityId) {
    CapabilityHandlerDescriptor handler;
    handler.capabilityId = capabilityId;
    handler.ownerPluginId = QStringLiteral("finepaper.test");
    requireMessage(registry.registerHandler(handler),
                   QStringLiteral("capability handler should register for %1").arg(capabilityId));
}

CapabilityRegistry packageCapabilityRegistry() {
    CapabilityRegistry registry;
    for (const QString& capabilityId : {
             QStringLiteral("noc.v1"),
             QStringLiteral("ipcraft.artifacts"),
             QStringLiteral("ipcraft.emitters"),
             QStringLiteral("ipcraft.flows"),
             QStringLiteral("ipcraft.graph_config"),
             QStringLiteral("ipcraft.views")
         }) {
        registerCapability(registry, capabilityId);
    }
    return registry;
}

PackageFeatureCoverageItem requireCoverageItem(const PackageCoverageReport& report,
                                               const QString& id,
                                               const char* message) {
    const PackageFeatureCoverageItem item = report.item(id);
    require(item.id == id, message);
    return item;
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        requireMessage(false, QStringLiteral("JSON file should open: %1").arg(path));
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    requireMessage(document.isObject(),
                   QStringLiteral("JSON file should contain an object: %1").arg(path));
    return document.object();
}

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        requireMessage(false, QStringLiteral("Text file should open: %1").arg(path));
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject graphConfigObject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{}},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };
}

ProjectIpInstanceRecord instanceRecord(const IpCatalogEntry& entry) {
    ProjectIpInstanceRecord record;
    record.id = QStringLiteral("meshnoc_0");
    record.displayName = QStringLiteral("meshnoc_0");
    record.package = ProjectPackageRef{entry.id, entry.version};
    record.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("fabric_width"), 128}}}
    };
    record.hasGraphConfig = true;
    record.graphConfig = graphConfigObject();
    record.ipcoreId = entry.id;
    record.instanceId = record.id;
    record.schema = entry.id + QStringLiteral(".instance-state.v1");
    record.state = record.config;
    return record;
}

ipcraft::core::ProjectDesign projectDesignFor(const IpCatalogEntry& entry,
                                              const ProjectIpInstanceRecord& instance) {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("vendor_meshnoc_project");
    design.name = QStringLiteral("Vendor MeshNoC Project");
    design.packages.append(ipcraft::core::PackageRef{entry.id, entry.version});

    ipcraft::core::ComponentInstance component;
    component.id = instance.instanceId;
    component.packageRef = entry.id + QLatin1Char('@') + entry.version;
    component.type = QStringLiteral("VendorSwitch");
    component.config = instance.config;
    design.components.append(component);
    return design;
}

bool hasFlow(const ipcraft::PackageSpec& package, const QString& flowId) {
    for (const QJsonValue& flowValue : package.flows) {
        if (flowValue.isObject() &&
            flowValue.toObject().value(QStringLiteral("id")).toString() == flowId) {
            return true;
        }
    }
    return false;
}

ipcraft::PackageSpec readPackageSpec(const QString& packageRoot) {
    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(packageRoot);
    requireMessage(result.ok, flowDiagnosticsText(result.diagnostics));
    return result.spec;
}

struct LoadedPackageContext {
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    CapabilityRegistry capabilities = packageCapabilityRegistry();
    PackageService service{&registry};
    QString packageRoot;
    IpCatalogEntry entry;
};

void loadVendorMeshNoC(LoadedPackageContext& context) {
    context.packageRoot = repositoryPath(QStringLiteral("ipcores/vendor-meshnoc"));
    requireMessage(QFileInfo(context.packageRoot).isDir(),
                   QStringLiteral("Vendor MeshNoC package root should exist: %1")
                       .arg(context.packageRoot));

    context.service.setCapabilityRegistry(&context.capabilities);
    const PackageServiceLoadResult result =
        context.service.reloadPackageRoots(QStringList{context.packageRoot});
    requireMessage(result.success, diagnosticsText(result.diagnostics));
    require(result.packageCount == 1, "Vendor MeshNoC package should be counted once");

    const std::optional<IpCatalogEntry> entry =
        context.service.catalog().entry(QString::fromLatin1(kPackageId));
    require(entry.has_value(), "Vendor MeshNoC catalog entry should exist");
    context.entry = *entry;
}

void testPackageServiceLoadsVendorMeshNoCFromPackageData() {
    LoadedPackageContext context;
    loadVendorMeshNoC(context);

    require(context.entry.id == QString::fromLatin1(kPackageId),
            "catalog entry id should be vendor.meshnoc");
    require(context.entry.kind == QStringLiteral("noc"),
            "catalog entry should be recognized as a NoC package");
    require(context.entry.isSelectable(),
            "catalog entry should be selectable from package module data");
    require(context.entry.moduleTypes.contains(QStringLiteral("vendor.meshnoc::VendorSwitch")),
            "catalog entry should expose VendorSwitch");
    require(context.entry.moduleTypes.contains(QStringLiteral("vendor.meshnoc::VendorHost")),
            "catalog entry should expose VendorHost");

    const ModuleType* switchType =
        context.registry.getType(QString::fromLatin1(kPackageId), QStringLiteral("VendorSwitch"));
    const ModuleType* hostType =
        context.registry.getType(QString::fromLatin1(kPackageId), QStringLiteral("VendorHost"));
    require(switchType != nullptr, "VendorSwitch should load from package data");
    require(hostType != nullptr, "VendorHost should load from package data");
    require(switchType->moduleId == QStringLiteral("VendorSwitch"),
            "VendorSwitch module id should come from descriptor");
    require(hostType->moduleId == QStringLiteral("VendorHost"),
            "VendorHost module id should come from descriptor");
    require(QFileInfo(switchType->viewFilePath).isFile(),
            "VendorSwitch view file should resolve");
    require(QFileInfo(hostType->viewFilePath).isFile(),
            "VendorHost view file should resolve");

    const PackageCoverageReport* report =
        context.service.coverageReport(QString::fromLatin1(kPackageId));
    require(report != nullptr, "coverage report should exist for vendor.meshnoc");
    require(!report->hasBlockingItems(),
            "handled required capabilities and optional vendor capability should not block");
    require(requireCoverageItem(*report,
                                QStringLiteral("capability:noc.v1"),
                                "noc.v1 capability should be visible").status ==
                PackageFeatureCoverageStatus::Handled,
            "noc.v1 should be handled by the registered test capability");
    const PackageFeatureCoverageItem vendorCapability =
        requireCoverageItem(*report,
                            QStringLiteral("capability:vendor.experimental.v1"),
                            "optional vendor capability should be visible");
    require(vendorCapability.status == PackageFeatureCoverageStatus::Unsupported,
            "unknown optional vendor capability should be unsupported, not blocking");
    require(vendorCapability.descriptor.value(QStringLiteral("required")).toBool(true) == false,
            "optional vendor capability should preserve required:false");
    require(requireCoverageItem(*report,
                                QStringLiteral("flow:validate"),
                                "validate flow should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "validate flow should be visible in coverage");
    require(requireCoverageItem(*report,
                                QStringLiteral("flow:generate"),
                                "generate flow should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "generate flow should be visible in coverage");
    require(requireCoverageItem(*report,
                                QStringLiteral("artifact:vendor_mesh_manifest"),
                                "vendor mesh manifest artifact should be visible").status ==
                PackageFeatureCoverageStatus::Visible,
            "vendor mesh manifest artifact should be visible in coverage");
}

void testPackageValidateAndGenerateFlowsRunThroughPackageRunners() {
    LoadedPackageContext context;
    loadVendorMeshNoC(context);
    const ipcraft::PackageSpec package = readPackageSpec(context.packageRoot);
    require(hasFlow(package, QStringLiteral("validate")),
            "Vendor MeshNoC package should declare validate flow");
    require(hasFlow(package, QStringLiteral("generate")),
            "Vendor MeshNoC package should declare generate flow");

    QTemporaryDir validateRunRoot;
    QTemporaryDir validateOutputRoot;
    require(validateRunRoot.isValid(), "validate run root should be valid");
    require(validateOutputRoot.isValid(), "validate output root should be valid");
    ipcraft::FlowRunRequest validateRequest;
    validateRequest.projectId = QStringLiteral("vendor_meshnoc_project");
    validateRequest.instanceId = QStringLiteral("meshnoc_0");
    validateRequest.flowId = QStringLiteral("validate");
    validateRequest.runId = QStringLiteral("validate_0");
    validateRequest.runRoot = validateRunRoot.path();
    validateRequest.outputRoot = validateOutputRoot.path();
    validateRequest.packageRoot = context.packageRoot;
    validateRequest.package = package;
    validateRequest.config.parameters.insert(QStringLiteral("fabric_width"), 128);
    validateRequest.graphConfig = ipcraft::GraphConfig::fromJson(graphConfigObject()).config;

    const ipcraft::FlowRunResult validateResult =
        ipcraft::FlowRunner::runFlow(validateRequest);
    requireMessage(validateResult.ok, flowDiagnosticsText(validateResult.diagnostics));
    require(readText(QDir(validateResult.runRoot).filePath(QStringLiteral("stdout.log")))
                .contains(QStringLiteral("Vendor MeshNoC validation succeeded")),
            "validate flow should execute the package validate script");

    QTemporaryDir workspace;
    require(workspace.isValid(), "generation workspace should be valid");
    QDir root(workspace.path());
    const ProjectIpInstanceRecord instance = instanceRecord(context.entry);
    const ipcraft::core::ProjectDesign design = projectDesignFor(context.entry, instance);

    ProjectGenerationRequest request;
    request.projectDesign = &design;
    request.projectPath = root.filePath(QStringLiteral("project/vendor_meshnoc.fpproj"));
    request.designName = QStringLiteral("vendor_meshnoc_project");
    request.outputRoot = root.filePath(QStringLiteral("generated"));
    request.catalogEntries = context.service.catalog().entries();

    ProjectGenerationRunner runner(QStringList{});
    const ProjectGenerationResult result = runner.generate(request);
    requireMessage(result.success, result.error);
    require(result.instances.size() == 1, "one Vendor MeshNoC instance should be generated");

    const ProjectGenerationInstanceResult generated = result.instances.first();
    require(generated.success, generated.error.toLocal8Bit().constData());
    require(generated.ipcoreId == QString::fromLatin1(kPackageId),
            "generated instance should keep package id");
    require(generated.inputSchema == ipcraft::schemaids::emittedInputsV1,
            "generated instance should use emitted inputs v1");
    require(generated.artifactPaths.contains(QStringLiteral("vendor_mesh_manifest.json")),
            "generated artifact list should include vendor mesh manifest");
    require(generated.standardOutput.contains(QStringLiteral("Vendor MeshNoC generation succeeded")),
            "generate flow should execute the package generate script");

    const QJsonObject manifest =
        readJsonObject(QDir(generated.outputDirectory)
                           .filePath(QStringLiteral("vendor_mesh_manifest.json")));
    require(manifest.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("vendor.meshnoc.generated-manifest.v1"),
            "vendor mesh manifest should use fixture schema");
    require(manifest.value(QStringLiteral("package")).toString() == QString::fromLatin1(kPackageId),
            "vendor mesh manifest should identify the package");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testPackageServiceLoadsVendorMeshNoCFromPackageData();
        testPackageValidateAndGenerateFlowsRunThroughPackageRunners();
    } catch (const std::exception& error) {
        std::cerr << "vendor_meshnoc_onboarding_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "vendor_meshnoc_onboarding_test passed\n";
    return 0;
}
