// IP catalog panel widget tests.
#include "app/mainwindow.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "panels/ipcatalogpanel.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QSignalBlocker>
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
    descriptor.generator.command = QStringLiteral("ruby");

    PluginInstanceParameterDescriptor width;
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

PluginDescriptor fabricDescriptor() {
    PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.fabric");
    descriptor.name = QStringLiteral("Fabric");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("fabric");
    descriptor.drc.command = QStringLiteral("ruby");
    return descriptor;
}

struct TestHarness {
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    PluginDescriptor ravenoc = ravenocDescriptor();
    PluginDescriptor fabric = fabricDescriptor();
    IpCatalogService catalog;
    ProjectStateService stateService;
    ProjectIpService projectIpService;
    ActiveWorkspaceController workspaceController;

    TestHarness()
        : catalog(QList<PluginDescriptor>{ravenoc, fabric}, &registry),
          projectIpService(&stateService),
          workspaceController(&projectIpService, &catalog) {
        ModuleType raveTile;
        raveTile.name = QStringLiteral("RaveTile");
        raveTile.pluginId = ravenoc.id;
        raveTile.paletteLabel = QStringLiteral("Rave Tile");
        require(registry.registerType(raveTile), "RaveTile should register");

        ModuleType fabricSwitch;
        fabricSwitch.name = QStringLiteral("FabricSwitch");
        fabricSwitch.pluginId = fabric.id;
        fabricSwitch.paletteLabel = QStringLiteral("Fabric Switch");
        require(registry.registerType(fabricSwitch), "FabricSwitch should register");

        catalog = IpCatalogService(QList<PluginDescriptor>{ravenoc, fabric}, &registry);
    }

    IpCatalogEntry ravenocEntry() const {
        const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("finepaper.ravenoc"));
        require(entry.has_value(), "RaveNoC catalog entry should exist");
        return *entry;
    }
};

void testSearchFiltersCatalogEntries() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(panel.objectName() == QStringLiteral("ipCatalogPanel"),
            "panel object name should be stable");
    auto* search = panel.findChild<QLineEdit*>(QStringLiteral("ipCatalogSearch"));
    auto* catalog = panel.findChild<QListWidget*>(QStringLiteral("ipCatalogList"));
    require(search != nullptr, "search field should exist");
    require(catalog != nullptr, "catalog list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("projectIpList")) != nullptr,
            "project IP list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeModuleList")) != nullptr,
            "active module list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeToolList")) != nullptr,
            "active tool list should exist");

    require(catalog->count() == 2, "catalog should start with two entries");
    search->setText(QStringLiteral("rave"));
    QCoreApplication::processEvents();
    require(catalog->count() == 1, "search should filter catalog entries");
    require(catalog->item(0)->data(Qt::UserRole).toString() == QStringLiteral("finepaper.ravenoc"),
            "search should keep matching RaveNoC entry");
}

void testSelectingIpInstanceUpdatesActiveModuleAndToolLists() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.ensureInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    auto* moduleList = panel.findChild<QListWidget*>(QStringLiteral("activeModuleList"));
    auto* toolList = panel.findChild<QListWidget*>(QStringLiteral("activeToolList"));

    require(projectList->count() == 1, "project list should show one IP instance");
    require(moduleList->count() == 1, "active module list should show RaveTile");
    require(moduleList->item(0)->data(Qt::UserRole).toString() == QStringLiteral("RaveTile"),
            "active module row should store module type");
    require(toolList->count() >= 2, "active tool list should include topology/generator tools");
}

void testPanelEmitsAddAndSelectSignals() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    QString requestedAdd;
    QObject::connect(&panel, &IpCatalogPanel::addIpcoreRequested, &panel,
                     [&](const QString& ipcoreId) { requestedAdd = ipcoreId; });
    auto* catalog = panel.findChild<QListWidget*>(QStringLiteral("ipCatalogList"));
    QMetaObject::invokeMethod(catalog,
                              "itemActivated",
                              Qt::DirectConnection,
                              Q_ARG(QListWidgetItem*, catalog->item(0)));
    require(!requestedAdd.isEmpty(), "panel should expose add intent signal");

    require(harness.projectIpService.ensureInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    QString selectedIpcore;
    QString selectedInstance;
    QObject::connect(&panel, &IpCatalogPanel::selectIpInstanceRequested, &panel,
                     [&](const QString& ipcoreId, const QString& instanceId) {
                         selectedIpcore = ipcoreId;
                         selectedInstance = instanceId;
                     });
    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    projectList->setCurrentRow(0);
    QCoreApplication::processEvents();
    require(selectedIpcore == QStringLiteral("finepaper.ravenoc"),
            "panel should emit selected IP core");
    require(selectedInstance == QStringLiteral("ravenoc_0"),
            "panel should emit selected instance");
}

void testMainWindowUsesIpCatalogDockWithoutActiveCombo() {
    MainWindow window;
    require(window.findChild<QDockWidget*>(QStringLiteral("ipCatalogDock")) != nullptr,
            "MainWindow should expose IP catalog dock");
    require(window.findChild<IpCatalogPanel*>(QStringLiteral("ipCatalogPanel")) != nullptr,
            "MainWindow should own IP catalog panel");
    require(window.findChild<QComboBox*>(QStringLiteral("activeIpCombo")) == nullptr,
            "MainWindow should remove toolbar active IP combo");
    require(window.findChild<QDockWidget*>(QStringLiteral("paletteDock")) == nullptr,
            "MainWindow should remove legacy palette dock");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testSearchFiltersCatalogEntries();
        testSelectingIpInstanceUpdatesActiveModuleAndToolLists();
        testPanelEmitsAddAndSelectSignals();
        testMainWindowUsesIpCatalogDockWithoutActiveCombo();
    } catch (const std::exception& error) {
        std::cerr << "ipcatalogpanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcatalogpanel_test passed\n";
    return 0;
}
