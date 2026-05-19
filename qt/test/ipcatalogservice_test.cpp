// IP catalog service tests.
#include "app/appsettings.h"
#include "ipcraft/ipcraftmanifest.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcorecommandrunner.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
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

IpCoreRuntimeDescriptor ravenocDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("0.1");
    descriptor.kind = QStringLiteral("noc");
    descriptor.runtimeRootPath = QStringLiteral("/tmp/ipcores/ravenoc");
    descriptor.sourceRootPath = QStringLiteral("/tmp/ipcores/ravenoc");
    descriptor.modulesPath = QStringLiteral("/tmp/ipcores/ravenoc/module-bundle.xml");
    descriptor.graphicsPath = QStringLiteral("/tmp/ipcores/ravenoc/views");

    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    descriptor.instanceParameters.insert(width.name, width);

    TopologyPresetDescriptor mesh;
    mesh.id = QStringLiteral("mesh");
    mesh.label = QStringLiteral("Mesh");
    mesh.kind = QStringLiteral("mesh");
    descriptor.topologyPresets.push_back(mesh);

    return descriptor;
}

void configureDefaultPackageRootsForTest() {
    static QTemporaryDir settingsRoot;
    require(settingsRoot.isValid(), "temporary settings root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcatalogservice_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcatalogservice_test_app"));
    AppSettings().setIpcorePaths(QStringList{repositoryPath(QStringLiteral("ipcores"))});
}

QString scopedTypeName(const QString& packageId, const QString& moduleId) {
    return packageId + QStringLiteral("::") + moduleId;
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

void writeMinimalPackage(const QString& packageRootPath, const QString& packageId) {
    QDir root(packageRootPath);
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    writeFile(root.filePath(QStringLiteral("views/Module.xml")),
              QByteArrayLiteral(R"xml(<module-view schema="v1" module="Module">
  <anchors><anchor ref="bus" x="0" y="0" /></anchors>
</module-view>)xml"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QStringLiteral(R"json({
  "schema": "ipcraft.manifest.v1",
  "id": "%1",
  "name": "Duplicate Test",
  "version": "1.0.0",
  "connection_classes": [
    { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
  ],
  "modules": [
    {
      "id": "Module",
      "interfaces": [
        {
          "id": "bus",
          "modes": ["initiator"],
          "accepts": [{ "class": "demo_link", "role": "initiator" }]
        }
      ]
    }
  ],
  "views": [
    { "module": "Module", "file": "views/Module.xml" }
  ]
})json").arg(packageId).toUtf8());
}

IpcraftPackageManifest packageWithSingleModule(const QString& packageId,
                                               const QString& moduleId,
                                               const QString& moduleName,
                                               const QString& graphRole = QStringLiteral("host")) {
    IpcraftPackageManifest manifest;
    manifest.id = packageId;
    manifest.name = packageId;

    IpcraftModuleDescriptor module;
    module.id = moduleId;
    module.name = moduleName;
    module.graphRole = graphRole;
    manifest.modules.push_back(module);

    return manifest;
}

void testCatalogEntryCopiesDiscoveredMetadata() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    ModuleType tile;
    tile.name = QStringLiteral("RaveTile");
    tile.ipcoreId = QStringLiteral("finepaper.ravenoc");
    require(registry.registerType(tile), "test module type should register");

    const IpCoreRuntimeDescriptor descriptor = ravenocDescriptor();
    IpCatalogService service({descriptor}, &registry);

    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));
    require(entry.has_value(), "catalog should expose discovered IP core");
    require(entry->id == descriptor.id, "entry should keep id");
    require(entry->packageId == descriptor.id, "compatibility entry should mirror package id");
    require(entry->name == descriptor.name, "entry should keep name");
    require(entry->version == descriptor.version, "entry should keep version");
    require(entry->kind == QStringLiteral("noc"), "entry should keep kind");
    require(entry->runtimeRootPath == descriptor.runtimeRootPath,
            "entry should keep runtime root");
    require(entry->sourceRootPath == descriptor.sourceRootPath,
            "entry should keep source root");
    require(entry->modulesPath == descriptor.modulesPath,
            "entry should keep modules path");
    require(entry->graphicsPath == descriptor.graphicsPath,
            "entry should keep graphics path");
    require(entry->moduleTypes == QStringList{QStringLiteral("RaveTile")},
            "entry should expose loaded module types");
    require(entry->hasModules(), "entry with module path should report modules");
    require(entry->isSelectable(), "entry with modules should be selectable");
    require(entry->instanceParameters.contains(QStringLiteral("flit_data_width")),
            "entry should keep instance parameters");
    require(entry->topologyPresets.size() == 1, "entry should keep presets");
}

void testCatalogEntriesAreSortedAndSelectableEntriesAreFiltered() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    ModuleType betaType;
    betaType.name = QStringLiteral("BetaTile");
    betaType.ipcoreId = QStringLiteral("finepaper.beta");
    require(registry.registerType(betaType), "beta module type should register");

    IpCoreRuntimeDescriptor alpha;
    alpha.id = QStringLiteral("finepaper.alpha");
    alpha.name = QStringLiteral("Alpha");
    alpha.version = QStringLiteral("1.0");

    IpCoreRuntimeDescriptor beta;
    beta.id = QStringLiteral("finepaper.beta");
    beta.name = QStringLiteral("Beta");
    beta.version = QStringLiteral("1.0");

    IpCatalogService service({beta, alpha}, &registry);
    const QList<IpCatalogEntry> entries = service.entries();
    require(entries.size() == 2, "catalog should keep all descriptors");
    require(entries.first().id == QStringLiteral("finepaper.alpha"),
            "entries should sort by name");

    const QList<IpCatalogEntry> selectable = service.selectableEntries();
    require(selectable.size() == 1, "selectable entries should require module types");
    require(selectable.first().id == QStringLiteral("finepaper.beta"),
            "selectable entry should be beta");
}

void testDefaultCatalogDiscoversConfiguredIpcraftPackages() {
    configureDefaultPackageRootsForTest();

    const IpCatalogService service = IpCatalogService::fromRuntimeRegistries();
    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));

    require(entry.has_value(), "default catalog should discover configured RaveNoC ipcraft package");
    require(entry->id == QStringLiteral("finepaper.ravenoc"),
            "catalog id should use package id");
    require(entry->runtimeRootPath == repositoryPath(QStringLiteral("ipcores/ravenoc")),
            "catalog runtime root compatibility path should be the ipcraft package root");
    require(entry->sourceRootPath == repositoryPath(QStringLiteral("ipcores/ravenoc")),
            "catalog source root compatibility path should be the ipcraft package root");
    require(QFileInfo(QDir(entry->runtimeRootPath).filePath(QStringLiteral("ipcraft.json"))).isFile(),
            "catalog package root should contain ipcraft.json");
    const QString retiredBundleRoot = QStringLiteral("generated") + QLatin1Char('/') + QStringLiteral("ipcores");
    require(!entry->runtimeRootPath.contains(retiredBundleRoot),
            "default catalog discovery should not depend on retired bundle roots");
    require(entry->modulesPath.isEmpty(),
            "ipcraft catalog entries should not expose legacy module bundle paths");
    require(entry->graphicsPath.isEmpty(),
            "ipcraft catalog entries should not expose generated graphics paths");
    require(!entry->modulesPath.contains(retiredBundleRoot),
            "catalog module compatibility path should not point into retired bundle roots");
    require(!entry->graphicsPath.contains(retiredBundleRoot),
            "catalog graphics compatibility path should not point into retired bundle roots");
}

void testCatalogEntryExposesIpcraftManifestData() {
    configureDefaultPackageRootsForTest();

    const IpCatalogService service = IpCatalogService::fromRuntimeRegistries();
    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));

    require(entry.has_value(), "RaveNoC package should be present in catalog");
    require(entry->packageId == QStringLiteral("finepaper.ravenoc"),
            "entry should expose package id");
    require(entry->packageManifest.id == QStringLiteral("finepaper.ravenoc"),
            "entry should retain full ipcraft manifest id");
    require(entry->packageManifest.packageRootPath == repositoryPath(QStringLiteral("ipcores/ravenoc")),
            "entry should retain package root path");
    require(entry->packageManifest.modules.size() == 2,
            "entry should expose manifest modules");
    require(entry->moduleTypes == QStringList({
                scopedTypeName(QStringLiteral("finepaper.ravenoc"), QStringLiteral("RaveEndpoint")),
                scopedTypeName(QStringLiteral("finepaper.ravenoc"), QStringLiteral("RaveTile"))
            }),
            "entry should expose module type ids from the manifest");

    const auto tileIt = std::find_if(entry->packageManifest.modules.cbegin(),
                                     entry->packageManifest.modules.cend(),
                                     [](const auto& module) {
                                         return module.id == QStringLiteral("RaveTile");
                                     });
    require(tileIt != entry->packageManifest.modules.cend(),
            "RaveTile module descriptor should be present");
    require(tileIt->graphRole == QStringLiteral("host"),
            "module descriptor should expose graph role");
    require(tileIt->interfaces.size() == 5,
            "RaveTile should expose manifest interfaces");

    const auto localIt = std::find_if(tileIt->interfaces.cbegin(),
                                      tileIt->interfaces.cend(),
                                      [](const auto& interfaceDescriptor) {
                                          return interfaceDescriptor.id == QStringLiteral("local");
                                      });
    require(localIt != tileIt->interfaces.cend(),
            "RaveTile local interface should be present");
    require(localIt->accepts.size() == 1,
            "interface should expose accepted connection classes");
    require(localIt->accepts.first().connectionClassId == QStringLiteral("ravenoc_endpoint_link"),
            "interface accept rule should expose connection class id");

    require(entry->packageManifest.connectionClasses.size() == 2,
            "entry should expose connection classes");
    const auto routerClassIt = std::find_if(entry->packageManifest.connectionClasses.cbegin(),
                                            entry->packageManifest.connectionClasses.cend(),
                                            [](const auto& connectionClass) {
                                                return connectionClass.id == QStringLiteral("ravenoc_router_link");
                                            });
    require(routerClassIt != entry->packageManifest.connectionClasses.cend(),
            "router connection class should be present");
    require(routerClassIt->roles == QStringList({QStringLiteral("initiator"), QStringLiteral("target")}),
            "connection class should expose roles");

    const auto tileViewIt = std::find_if(entry->packageManifest.views.cbegin(),
                                         entry->packageManifest.views.cend(),
                                         [](const auto& view) {
                                             return view.moduleId == QStringLiteral("RaveTile");
                                         });
    require(tileViewIt != entry->packageManifest.views.cend(),
            "entry should expose module views");
    require(tileViewIt->filePath == QStringLiteral("views/RaveTile.xml"),
            "view should keep package-relative source view path");
    require(tileViewIt->resolvedFilePath == repositoryPath(QStringLiteral("ipcores/ravenoc/views/RaveTile.xml")),
            "view should expose resolved source XML path");

    require(entry->packageManifest.commands.contains(QStringLiteral("validate")),
            "entry should expose validate command");
    require(entry->packageManifest.commands.contains(QStringLiteral("generate")),
            "entry should expose generate command");
    require(entry->packageManifest.commands.value(QStringLiteral("generate")).inputSchema ==
                QStringLiteral("ipcraft.noc.project.v1"),
            "entry should expose command input schema");
}

void testDefaultPackageCommandsResolveWithIpcraftProjectSchema() {
    configureDefaultPackageRootsForTest();

    const IpCatalogService service = IpCatalogService::fromRuntimeRegistries();
    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));

    require(entry.has_value(), "RaveNoC package should be present in catalog");
    require(entry->generator.hasCommand(), "package catalog entry should expose generator command");
    require(entry->drc.hasCommand(), "package catalog entry should expose validate command as DRC");
    require(entry->generator.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "generator should retain package command input schema");
    require(entry->drc.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "DRC should retain package command input schema");

    const QString frameworkToolsPath = repositoryPath(QStringLiteral("ipcraft_generator/bin"));
    const IpCoreResolvedCommand generator =
        IpCoreCommandRunner::resolveGenerator(*entry,
                                              QStringLiteral("/tmp/project.json"),
                                              QStringLiteral("/tmp/generated"),
                                              QStringList{frameworkToolsPath});
    require(generator.valid,
            "package generator should resolve instead of rejecting ipcraft.noc.project.v1");
    require(generator.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "resolved generator should carry package command input schema");
    require(generator.command == QDir(frameworkToolsPath).filePath(QStringLiteral("ipcraft-generate")),
            "resolved generator should use the controlled framework tool path");

    const IpCoreResolvedCommand drc =
        IpCoreCommandRunner::resolveDrc(*entry,
                                        QStringLiteral("/tmp/project.json"),
                                        QStringLiteral("/tmp/generated"));
    require(drc.valid,
            "package DRC should resolve instead of rejecting ipcraft.noc.project.v1");
    require(drc.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "resolved DRC should carry package command input schema");
}

void testIpcraftModuleTypesAreScopedByPackage() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const IpcraftPackageManifest fabric =
        packageWithSingleModule(QStringLiteral("org.example.fabric"),
                                QStringLiteral("Tile"),
                                QStringLiteral("Fabric Tile"));
    const IpcraftPackageManifest noc =
        packageWithSingleModule(QStringLiteral("org.example.noc"),
                                QStringLiteral("Tile"),
                                QStringLiteral("NoC Tile"));

    require(registry.loadIpcraftPackages({fabric, noc}),
            "duplicate bare manifest module ids should load when packages differ");

    const QString fabricTypeName =
        scopedTypeName(QStringLiteral("org.example.fabric"), QStringLiteral("Tile"));
    const QString nocTypeName =
        scopedTypeName(QStringLiteral("org.example.noc"), QStringLiteral("Tile"));
    const ModuleType* fabricType = registry.getType(fabricTypeName);
    const ModuleType* nocType = registry.getType(nocTypeName);
    require(fabricType != nullptr, "fabric Tile should be registered under a package-scoped type name");
    require(nocType != nullptr, "NoC Tile should be registered under a package-scoped type name");
    require(fabricType->moduleId == QStringLiteral("Tile") &&
                nocType->moduleId == QStringLiteral("Tile"),
            "scoped module types should preserve bare manifest module ids");
    require(fabricType->paletteLabel == QStringLiteral("Fabric Tile") &&
                nocType->paletteLabel == QStringLiteral("NoC Tile"),
            "scoped module type keys should not replace user-visible labels");
    require(registry.getType(QStringLiteral("Tile")) == nullptr,
            "ambiguous bare module id lookup should not select one package globally");
    require(registry.getType(QStringLiteral("org.example.noc"), QStringLiteral("Tile")) == nocType,
            "package-scoped lookup should resolve the requested package module");
    require(registry.getType(QStringLiteral("org.example.fabric"), nocTypeName) == nullptr,
            "package-scoped lookup should not return an already-scoped type from another package");

    IpCatalogService catalog(QVector<IpcraftPackageManifest>{fabric, noc}, &registry);
    const std::optional<IpCatalogEntry> fabricEntry = catalog.entry(QStringLiteral("org.example.fabric"));
    const std::optional<IpCatalogEntry> nocEntry = catalog.entry(QStringLiteral("org.example.noc"));
    require(fabricEntry.has_value() && nocEntry.has_value(),
            "catalog should expose both packages");
    require(fabricEntry->moduleTypes == QStringList{fabricTypeName},
            "fabric catalog entry should expose its package-scoped Tile type");
    require(nocEntry->moduleTypes == QStringList{nocTypeName},
            "NoC catalog entry should expose its package-scoped Tile type");
}

void testCatalogEntryExposesInstancePolicies() {
    IpcraftPackageManifest manifest =
        packageWithSingleModule(QStringLiteral("org.example.noc"),
                                QStringLiteral("Tile"),
                                QStringLiteral("Tile"));
    manifest.instances.max = 4;

    IpcraftExtensionDescriptor nocExtension;
    nocExtension.id = QStringLiteral("noc.v1");
    nocExtension.enabled = true;
    manifest.extensions.insert(nocExtension.id, nocExtension);

    IpCatalogService catalog(QVector<IpcraftPackageManifest>{manifest}, nullptr);
    const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("org.example.noc"));

    require(entry.has_value(), "catalog should expose package with instance policies");
    require(entry->maxInstances.has_value() && *entry->maxInstances == 4,
            "catalog should expose manifest instances.max");
    require(entry->kind == QStringLiteral("noc"),
            "catalog should derive NoC kind from noc extension");
    require(entry->instanceLimits.size() == 1,
            "catalog should attach built-in NoC kind instance policy");
    require(entry->instanceLimits.first().scope == QStringLiteral("kind:noc"),
            "built-in NoC policy should use a shared kind scope");
    require(entry->instanceLimits.first().max == 1,
            "built-in NoC policy should allow one NoC instance");
}

void testDuplicatePackageIdsAreDiagnosed() {
    QTemporaryDir first;
    QTemporaryDir second;
    require(first.isValid() && second.isValid(), "temporary directories should be valid");
    writeMinimalPackage(first.path(), QStringLiteral("org.example.dup"));
    writeMinimalPackage(second.path(), QStringLiteral("org.example.dup"));

    const IpcraftRegistryLoadResult result =
        loadIpcraftPackageManifestsWithDiagnostics({first.path(), second.path()});

    require(result.manifests.empty(), "duplicate package IDs should not silently select one package");
    require(result.diagnostics.size() == 1, "duplicate package ID should produce one diagnostic");
    require(result.diagnostics.first().message.contains(QStringLiteral("Duplicate package id org.example.dup")),
            "diagnostic should name duplicate package id");
    require(result.diagnostics.first().message.contains(first.path()) &&
                result.diagnostics.first().message.contains(second.path()),
            "diagnostic should name involved roots");
}

void testIpcraftIdentityFallbacksDoNotSpecialCaseLegacyEndpointNames() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    const IpcraftPackageManifest manifest =
        packageWithSingleModule(QStringLiteral("org.example.attachments"),
                                QStringLiteral("Endpoint"),
                                QStringLiteral("Attachment Endpoint"),
                                QStringLiteral("attached"));

    require(registry.loadIpcraftPackages({manifest}),
            "Endpoint module should load from manifest metadata");

    const ModuleType* endpointType =
        registry.getType(scopedTypeName(QStringLiteral("org.example.attachments"),
                                        QStringLiteral("Endpoint")));
    require(endpointType != nullptr, "Endpoint type should use package-scoped lookup");
    require(endpointType->graphRole == QStringLiteral("attached"),
            "Endpoint role should come from manifest graph_role");
    require(endpointType->externalIdPrefix == QStringLiteral("endpoint"),
            "Endpoint external id prefix should use generic manifest-derived fallback");
    require(endpointType->displayPrefix == QStringLiteral("Attachment Endpoint"),
            "Endpoint display prefix should use manifest label instead of legacy EP fallback");
}

void testIpcraftSingularAttachHostPropagatesToModuleType() {
    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    IpcraftPackageManifest manifest;
    manifest.id = QStringLiteral("org.example.attach_host");
    manifest.name = QStringLiteral("Attach Host");

    IpcraftModuleDescriptor host;
    host.id = QStringLiteral("Host");
    host.name = QStringLiteral("Host");
    host.graphRole = QStringLiteral("host");

    IpcraftModuleDescriptor endpoint;
    endpoint.id = QStringLiteral("Endpoint");
    endpoint.name = QStringLiteral("Endpoint");
    endpoint.graphRole = QStringLiteral("attached");
    endpoint.attach = QJsonObject{
        {QStringLiteral("host"), QStringLiteral("Host")},
        {QStringLiteral("zone"), QStringLiteral("endpoint_slot")}
    };

    manifest.modules = {host, endpoint};
    require(registry.loadIpcraftPackages({manifest}),
            "package with singular attach.host should load");

    const ModuleType* endpointType =
        registry.getType(QStringLiteral("org.example.attach_host"), QStringLiteral("Endpoint"));
    require(endpointType != nullptr, "attached module type should be registered");
    require(endpointType->attachHostModuleIds == QStringList{QStringLiteral("Host")},
            "singular attach.host should propagate to ModuleType attach host ids");
    require(endpointType->attachZoneId == QStringLiteral("endpoint_slot"),
            "attach.zone should propagate with singular attach.host");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCatalogEntryCopiesDiscoveredMetadata();
        testCatalogEntriesAreSortedAndSelectableEntriesAreFiltered();
        testDefaultCatalogDiscoversConfiguredIpcraftPackages();
        testCatalogEntryExposesIpcraftManifestData();
        testDefaultPackageCommandsResolveWithIpcraftProjectSchema();
        testIpcraftModuleTypesAreScopedByPackage();
        testCatalogEntryExposesInstancePolicies();
        testDuplicatePackageIdsAreDiagnosed();
        testIpcraftIdentityFallbacksDoNotSpecialCaseLegacyEndpointNames();
        testIpcraftSingularAttachHostPropagatesToModuleType();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    std::cout << "ipcatalogservice_test passed" << std::endl;
    return 0;
}
