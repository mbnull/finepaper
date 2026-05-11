// IP catalog panel widget tests.
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "panels/ipcatalogpanel.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>
#include <stdexcept>

#define private public
#include "app/mainwindow.h"
#undef private

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void closeOpenMessageBoxes() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
            messageBox->accept();
        }
    }
}

void triggerFirstOpenMenuAction() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* menu = qobject_cast<QMenu*>(widget)) {
            const QList<QAction*> actions = menu->actions();
            if (!actions.isEmpty()) {
                actions.front()->trigger();
            }
            menu->close();
            return;
        }
    }
}

QTreeWidgetItem* firstCatalogEntry(QTreeWidget* catalog) {
    if (!catalog || catalog->topLevelItemCount() == 0) {
        return nullptr;
    }

    QTreeWidgetItem* category = catalog->topLevelItem(0);
    return category && category->childCount() > 0 ? category->child(0) : nullptr;
}

QTreeWidgetItem* findCatalogCategory(QTreeWidget* catalog, const QString& text) {
    if (!catalog) {
        return nullptr;
    }

    for (int index = 0; index < catalog->topLevelItemCount(); ++index) {
        QTreeWidgetItem* item = catalog->topLevelItem(index);
        if (item && item->text(0) == text) {
            return item;
        }
    }
    return nullptr;
}

IpCoreRuntimeDescriptor ravenocDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.ravenoc");
    descriptor.name = QStringLiteral("RaveNoC");
    descriptor.version = QStringLiteral("0.1");
    descriptor.kind = QStringLiteral("noc");
    descriptor.generator.command = QStringLiteral("ruby");

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

IpCoreRuntimeDescriptor fabricDescriptor() {
    IpCoreRuntimeDescriptor descriptor;
    descriptor.id = QStringLiteral("finepaper.fabric");
    descriptor.name = QStringLiteral("Fabric");
    descriptor.version = QStringLiteral("1.0");
    descriptor.kind = QStringLiteral("fabric");
    descriptor.drc.command = QStringLiteral("ruby");
    return descriptor;
}

struct TestHarness {
    ModuleRegistry registry{ModuleRegistry::LoadMode::Empty};
    IpCoreRuntimeDescriptor ravenoc = ravenocDescriptor();
    IpCoreRuntimeDescriptor fabric = fabricDescriptor();
    IpCatalogService catalog;
    ProjectStateService stateService;
    ProjectIpService projectIpService;
    ActiveWorkspaceController workspaceController;

    TestHarness()
        : catalog(QList<IpCoreRuntimeDescriptor>{ravenoc, fabric}, &registry),
          projectIpService(&stateService),
          workspaceController(&projectIpService, &catalog) {
        ModuleType raveTile;
        raveTile.name = QStringLiteral("RaveTile");
        raveTile.ipcoreId = ravenoc.id;
        raveTile.paletteLabel = QStringLiteral("Rave Tile");
        require(registry.registerType(raveTile), "RaveTile should register");

        ModuleType fabricSwitch;
        fabricSwitch.name = QStringLiteral("FabricSwitch");
        fabricSwitch.ipcoreId = fabric.id;
        fabricSwitch.paletteLabel = QStringLiteral("Fabric Switch");
        require(registry.registerType(fabricSwitch), "FabricSwitch should register");

        catalog = IpCatalogService(QList<IpCoreRuntimeDescriptor>{ravenoc, fabric}, &registry);
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
    auto* catalog = panel.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    require(search != nullptr, "search field should exist");
    require(catalog != nullptr, "catalog tree should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("projectIpList")) != nullptr,
            "project IP list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeModuleList")) != nullptr,
            "active module list should exist");
    require(panel.findChild<QListWidget*>(QStringLiteral("activeToolList")) != nullptr,
            "active tool list should exist");

    require(catalog->topLevelItemCount() == 2, "catalog should start with two category rows");
    require(findCatalogCategory(catalog, QStringLiteral("NoC")) != nullptr,
            "catalog should group NoC entries under a category row");
    search->setText(QStringLiteral("rave"));
    QCoreApplication::processEvents();
    require(catalog->topLevelItemCount() == 1, "search should filter empty categories");
    QTreeWidgetItem* category = catalog->topLevelItem(0);
    require(category->childCount() == 1, "search should keep one matching catalog entry");
    require(category->child(0)->data(0, Qt::UserRole).toString() == QStringLiteral("finepaper.ravenoc"),
            "search should keep matching RaveNoC entry");
}

void testCatalogSectionsAndCategoriesCanCollapse() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    auto* toggle = panel.findChild<QToolButton*>(QStringLiteral("ipCatalogIpCoresSectionToggle"));
    auto* content = panel.findChild<QWidget*>(QStringLiteral("ipCatalogIpCoresSectionContent"));
    auto* catalog = panel.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    require(toggle != nullptr, "IP cores section toggle should exist");
    require(content != nullptr, "IP cores section content should exist");
    require(catalog != nullptr, "catalog tree should exist");
    require(!content->isHidden(), "IP cores section should start expanded");

    toggle->click();
    require(content->isHidden(), "IP cores section toggle should collapse content");
    toggle->click();
    require(!content->isHidden(), "IP cores section toggle should expand content");

    QTreeWidgetItem* nocCategory = findCatalogCategory(catalog, QStringLiteral("NoC"));
    require(nocCategory != nullptr, "NoC category should exist");
    catalog->collapseItem(nocCategory);
    require(!nocCategory->isExpanded(), "catalog category should collapse");
    catalog->expandItem(nocCategory);
    require(nocCategory->isExpanded(), "catalog category should expand");
}

void testSelectingIpInstanceUpdatesActiveModuleAndToolLists() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
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
    auto* catalog = panel.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    QTreeWidgetItem* catalogEntry = firstCatalogEntry(catalog);
    require(catalogEntry != nullptr, "catalog should expose selectable IP entries");
    QMetaObject::invokeMethod(catalog,
                              "itemActivated",
                              Qt::DirectConnection,
                              Q_ARG(QTreeWidgetItem*, catalogEntry),
                              Q_ARG(int, 0));
    require(!requestedAdd.isEmpty(), "panel should expose add intent signal");

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
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

void testPanelEmitsRemoveSignalForActiveInstance() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    require(projectList != nullptr, "project list should exist");
    require(projectList->count() == 1, "project list should show one instance");
    projectList->setCurrentRow(0);
    QCoreApplication::processEvents();

    QString removedIpcore;
    QString removedInstance;
    int removeSignals = 0;
    QObject::connect(&panel, &IpCatalogPanel::removeIpInstanceRequested, &panel,
                     [&](const QString& ipcoreId, const QString& instanceId) {
                         removedIpcore = ipcoreId;
                         removedInstance = instanceId;
                         ++removeSignals;
                     });

    const QList<QAction*> actions = projectList->actions();
    require(actions.size() == 1, "project list should expose one remove action");
    actions.front()->trigger();
    require(removeSignals == 1, "remove action should emit one remove intent");
    require(removedIpcore == QStringLiteral("finepaper.ravenoc"),
            "remove intent should emit active ipcore id");
    require(removedInstance == QStringLiteral("ravenoc_0"),
            "remove intent should emit active instance id");

    removedIpcore.clear();
    removedInstance.clear();
    panel.show();
    projectList->setFocus();
    QCoreApplication::processEvents();
    QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QCoreApplication::sendEvent(projectList, &deleteKey);
    QCoreApplication::processEvents();
    require(removeSignals == 2, "Delete key should emit a second remove intent");
    require(removedIpcore == QStringLiteral("finepaper.ravenoc"),
            "Delete key should keep the active ipcore id");
    require(removedInstance == QStringLiteral("ravenoc_0"),
            "Delete key should keep the active instance id");
}

void testPanelContextMenuRemoveTargetsClickedItem() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "first RaveNoC instance should be created");
    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "second RaveNoC instance should be created");

    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    require(projectList != nullptr, "project list should exist");
    require(projectList->count() == 2, "project list should show both instances");

    panel.resize(320, 240);
    panel.show();
    QCoreApplication::processEvents();

    projectList->setCurrentRow(0);
    QCoreApplication::processEvents();
    QListWidgetItem* secondItem = projectList->item(1);
    require(secondItem != nullptr, "second project instance should exist");
    const QRect secondRect = projectList->visualItemRect(secondItem);
    require(secondRect.isValid(), "second project instance should have a visible rect");

    QString removedIpcore;
    QString removedInstance;
    int removeSignals = 0;
    QObject::connect(&panel, &IpCatalogPanel::removeIpInstanceRequested, &panel,
                     [&](const QString& ipcoreId, const QString& instanceId) {
                         removedIpcore = ipcoreId;
                         removedInstance = instanceId;
                         ++removeSignals;
                     });

    QTimer::singleShot(0, &triggerFirstOpenMenuAction);
    const bool invoked = QMetaObject::invokeMethod(projectList,
                                                   "customContextMenuRequested",
                                                   Qt::DirectConnection,
                                                   Q_ARG(QPoint, secondRect.center()));
    require(invoked, "project list should expose a custom context-menu signal");
    QCoreApplication::processEvents();

    require(removeSignals == 1, "context menu remove should emit one remove intent");
    require(removedIpcore == QStringLiteral("finepaper.ravenoc"),
            "context menu remove should emit the clicked item ipcore");
    require(removedInstance == QStringLiteral("ravenoc_1"),
            "context menu remove should emit the clicked item instead of the current selection");
}

void testPanelContextMenuRemoveSurvivesListRefreshWhileMenuIsOpen() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "first RaveNoC instance should be created");
    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "second RaveNoC instance should be created");

    auto* projectList = panel.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    require(projectList != nullptr, "project list should exist");
    require(projectList->count() == 2, "project list should show both instances");

    panel.resize(320, 240);
    panel.show();
    QCoreApplication::processEvents();

    QListWidgetItem* secondItem = projectList->item(1);
    require(secondItem != nullptr, "second project instance should exist");
    const QRect secondRect = projectList->visualItemRect(secondItem);
    require(secondRect.isValid(), "second project instance should have a visible rect");

    QString removedIpcore;
    QString removedInstance;
    int removeSignals = 0;
    bool mutationSucceeded = false;
    QObject::connect(&panel, &IpCatalogPanel::removeIpInstanceRequested, &panel,
                     [&](const QString& ipcoreId, const QString& instanceId) {
                         removedIpcore = ipcoreId;
                         removedInstance = instanceId;
                         ++removeSignals;
                     });

    QTimer::singleShot(0, &panel, [&harness, &mutationSucceeded] {
        const ProjectIpServiceResult result = harness.projectIpService.createInstanceForIpcore(
            harness.ravenocEntry());
        if (result.success) {
            mutationSucceeded = true;
        }
    });
    QTimer::singleShot(0, &triggerFirstOpenMenuAction);
    const bool invoked = QMetaObject::invokeMethod(projectList,
                                                   "customContextMenuRequested",
                                                   Qt::DirectConnection,
                                                   Q_ARG(QPoint, secondRect.center()));
    require(invoked, "project list should expose a custom context-menu signal");
    QCoreApplication::processEvents();

    require(mutationSucceeded, "state mutation during open menu should succeed");
    require(removeSignals == 1,
            "context menu remove should still emit after the list refreshes");
    require(removedIpcore == QStringLiteral("finepaper.ravenoc"),
            "context menu remove should preserve the clicked ipcore across refresh");
    require(removedInstance == QStringLiteral("ravenoc_1"),
            "context menu remove should preserve the clicked instance across refresh");
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

void testMainWindowStartsWithoutEditableUnsavedProject() {
    MainWindow window;
    require(!window.hasOpenProject(), "MainWindow should start without an open project");
    require(window.findChild<QAction*>(QStringLiteral("generateAction"))->isEnabled() == false,
            "Generate should be disabled without a saved project");
    require(window.findChild<QAction*>(QStringLiteral("validateAction"))->isEnabled() == false,
            "Validate should be disabled without a saved project");
    require(window.findChild<IpCatalogPanel*>(QStringLiteral("ipCatalogPanel"))->isEnabled() == false,
            "IP catalog editing should be disabled without a saved project");
}

void testMainWindowUsesProjectDirectoryForNewProjectDefault() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("projects/current_design.fpproj"));
    require(window.createProjectAt(projectPath), "project should be created");
    require(window.defaultProjectDirectoryPath() == QFileInfo(projectPath).absolutePath(),
            "new project flow should default to the current project directory");
}

void testLoadGraphReportsFailureForInvalidPath() {
    MainWindow window;
    QTimer::singleShot(0, &closeOpenMessageBoxes);
    require(!window.loadGraph(QStringLiteral("/tmp/does-not-exist.fpproj")),
            "loadGraph should report failure for invalid paths");
    require(!window.hasOpenProject(),
            "failed loadGraph should leave MainWindow without an open project");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("finepaper"));
    QApplication::setApplicationName(QStringLiteral("finepaper_ipcatalogpanel_test"));
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        std::cerr << "ipcatalogpanel_test failed: temporary settings directory should be valid\n";
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    try {
        testSearchFiltersCatalogEntries();
        testCatalogSectionsAndCategoriesCanCollapse();
        testSelectingIpInstanceUpdatesActiveModuleAndToolLists();
        testPanelEmitsAddAndSelectSignals();
        testPanelEmitsRemoveSignalForActiveInstance();
        testPanelContextMenuRemoveTargetsClickedItem();
        testPanelContextMenuRemoveSurvivesListRefreshWhileMenuIsOpen();
        testMainWindowUsesIpCatalogDockWithoutActiveCombo();
        testMainWindowStartsWithoutEditableUnsavedProject();
        testMainWindowUsesProjectDirectoryForNewProjectDefault();
        testLoadGraphReportsFailureForInvalidPath();
    } catch (const std::exception& error) {
        std::cerr << "ipcatalogpanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcatalogpanel_test passed\n";
    return 0;
}
