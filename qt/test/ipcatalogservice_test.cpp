// IP catalog service tests.
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcorecommandrunner.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
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
    descriptor.runtimeRootPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc");
    descriptor.sourceRootPath = QStringLiteral("/tmp/ipcores/ravenoc");
    descriptor.modulesPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc/modules.xml");
    descriptor.graphicsPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc/graphics");

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

void testDefaultCatalogDiscoversRepositoryIpcraftPackages() {
    const IpCatalogService service = IpCatalogService::fromRuntimeRegistries();
    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));

    require(entry.has_value(), "default catalog should discover RaveNoC ipcraft package");
    require(entry->id == QStringLiteral("finepaper.ravenoc"),
            "catalog id should use package id");
    require(entry->runtimeRootPath == repositoryPath(QStringLiteral("ipcores/ravenoc")),
            "catalog runtime root compatibility path should be the ipcraft package root");
    require(entry->sourceRootPath == repositoryPath(QStringLiteral("ipcores/ravenoc")),
            "catalog source root compatibility path should be the ipcraft package root");
    require(QFileInfo(QDir(entry->runtimeRootPath).filePath(QStringLiteral("ipcraft.json"))).isFile(),
            "catalog package root should contain ipcraft.json");
    require(!entry->runtimeRootPath.contains(QStringLiteral("generated/ipcores")),
            "default catalog discovery should not depend on generated ipcores");
    require(entry->modulesPath.isEmpty(),
            "ipcraft catalog entries should not expose generated modules.xml paths");
    require(entry->graphicsPath.isEmpty(),
            "ipcraft catalog entries should not expose generated graphics paths");
    require(!entry->modulesPath.contains(QStringLiteral("generated/ipcores")),
            "catalog module compatibility path should not point into generated ipcores");
    require(!entry->graphicsPath.contains(QStringLiteral("generated/ipcores")),
            "catalog graphics compatibility path should not point into generated ipcores");
}

void testCatalogEntryExposesIpcraftManifestData() {
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
    require(entry->moduleTypes == QStringList({QStringLiteral("RaveEndpoint"), QStringLiteral("RaveTile")}),
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

    const IpCoreResolvedCommand generator =
        IpCoreCommandRunner::resolveGenerator(*entry,
                                              QStringLiteral("/tmp/project.json"),
                                              QStringLiteral("/tmp/generated"));
    require(generator.valid,
            "package generator should resolve instead of rejecting ipcraft.noc.project.v1");
    require(generator.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "resolved generator should carry package command input schema");

    const IpCoreResolvedCommand drc =
        IpCoreCommandRunner::resolveDrc(*entry,
                                        QStringLiteral("/tmp/project.json"),
                                        QStringLiteral("/tmp/generated"));
    require(drc.valid,
            "package DRC should resolve instead of rejecting ipcraft.noc.project.v1");
    require(drc.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "resolved DRC should carry package command input schema");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCatalogEntryCopiesDiscoveredMetadata();
        testCatalogEntriesAreSortedAndSelectableEntriesAreFiltered();
        testDefaultCatalogDiscoversRepositoryIpcraftPackages();
        testCatalogEntryExposesIpcraftManifestData();
        testDefaultPackageCommandsResolveWithIpcraftProjectSchema();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    std::cout << "ipcatalogservice_test passed" << std::endl;
    return 0;
}
