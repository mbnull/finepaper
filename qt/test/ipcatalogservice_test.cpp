// IP catalog service tests.
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PluginDescriptor ravenocDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("0.1");
    descriptor.kind = QStringLiteral("noc");
    descriptor.runtimeRootPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc");
    descriptor.sourceRootPath = QStringLiteral("/tmp/ipcores/ravenoc");
    descriptor.modulesPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc/modules.xml");
    descriptor.graphicsPath = QStringLiteral("/tmp/generated/ipcores/finepaper.ravenoc/graphics");

    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("integer");
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
    tile.pluginId = QStringLiteral("finepaper.ravenoc");
    require(registry.registerType(tile), "test module type should register");

    const PluginDescriptor descriptor = ravenocDescriptor();
    IpCatalogService service({descriptor}, &registry);

    const std::optional<IpCatalogEntry> entry =
        service.entry(QStringLiteral("finepaper.ravenoc"));
    require(entry.has_value(), "catalog should expose discovered IP core");
    require(entry->id == descriptor.id, "entry should keep id");
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
    betaType.pluginId = QStringLiteral("finepaper.beta");
    require(registry.registerType(betaType), "beta module type should register");

    PluginDescriptor alpha;
    alpha.id = QStringLiteral("finepaper.alpha");
    alpha.name = QStringLiteral("Alpha");
    alpha.version = QStringLiteral("1.0");

    PluginDescriptor beta;
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

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCatalogEntryCopiesDiscoveredMetadata();
        testCatalogEntriesAreSortedAndSelectableEntriesAreFiltered();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    std::cout << "ipcatalogservice_test passed" << std::endl;
    return 0;
}
