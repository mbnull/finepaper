// IP catalog panel widget tests.
#include "app/appsettings.h"
#include "app/servicekey.h"
#include "app/serviceregistry.h"
#include "commands/addmodulecommand.h"
#include "commands/commandmanager.h"
#include "ipcraft/schemaids.h"
#include "ipcore/ipcatalogservice.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "panels/ipcatalogpanel.h"
#include "panels/logpanel.h"
#include "panels/propertypanel.h"
#include "project/designeditingservice.h"
#include "project/projectipservice.h"
#include "project/projectservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>
#include <memory>
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

void acceptOpenInputDialogs() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
            dialog->accept();
        }
    }
}

void scheduleInputDialogAccepts() {
    for (int index = 0; index < 8; ++index) {
        QTimer::singleShot(index * 10, &acceptOpenInputDialogs);
    }
}

void captureAndRejectPackageRootsDialog(const std::shared_ptr<QStringList>& diagnostics) {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget || widget->objectName() != QStringLiteral("ipcorePathsDialog")) {
            continue;
        }
        if (auto* list = widget->findChild<QListWidget*>(
                QStringLiteral("ipcorePathsDiagnostics"))) {
            diagnostics->clear();
            for (int row = 0; row < list->count(); ++row) {
                const QListWidgetItem* item = list->item(row);
                if (item) {
                    diagnostics->append(item->text());
                }
            }
        }
        if (auto* dialog = qobject_cast<QDialog*>(widget)) {
            dialog->reject();
        } else {
            widget->close();
        }
        return;
    }
}

void schedulePackageRootsDialogCapture(const std::shared_ptr<QStringList>& diagnostics) {
    for (int index = 0; index < 20; ++index) {
        QTimer::singleShot(index * 10, [diagnostics] {
            captureAndRejectPackageRootsDialog(diagnostics);
        });
    }
}

void processEventsFor(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

DesignEditingService* designEditingServiceFromRegistry(MainWindow& window) {
    require(window.m_serviceRegistry != nullptr,
            "MainWindow should own a service registry");
    DesignEditingService* service = window.m_serviceRegistry->service<DesignEditingService>(
        ServiceKey::fromLiteral("finepaper.design-editing"));
    require(service != nullptr,
            "finepaper.design-editing should resolve to DesignEditingService");
    return service;
}

ipcraft::core::ProjectDesign loadedDesignFixture(const QString& name) {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("project_sync_0");
    design.name = name;
    design.packages.append(ipcraft::core::PackageRef{QStringLiteral("vendor.designpkg"),
                                                     QStringLiteral("1.0.0")});

    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("component0");
    component.type = QStringLiteral("DesignComponent");
    component.packageRef = QStringLiteral("vendor.designpkg@1.0.0");
    component.config.insert(QStringLiteral("parameters"),
                            QJsonObject{{QStringLiteral("width"), 4}});
    design.components.append(component);
    return design;
}

void writeDesignFixtureProject(const QString& path,
                               const ipcraft::core::ProjectDesign& design) {
    ProjectService writer;
    writer.replaceDesign(design);
    const ProjectServiceResult save = writer.saveFile(path);
    require(save.success, "design fixture project should save");
}

ipcraft::core::ProjectPatch setComponentConfigPatch(const QString& componentId,
                                                    const QString& key,
                                                    const QJsonValue& value) {
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    patch.id = QStringLiteral("set-component-config");
    patch.ops.append(ipcraft::core::PatchOperation{
        QStringLiteral("set_config"),
        QStringLiteral("component:%1").arg(componentId),
        QStringLiteral("/%1").arg(key),
        value,
        {}
    });
    return patch;
}

ipcraft::core::ProjectPatch addDesignComponentPatch(const QString& componentId) {
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    patch.id = QStringLiteral("add-design-component");
    patch.ops.append(ipcraft::core::PatchOperation{
        QStringLiteral("add"),
        QStringLiteral("component"),
        QStringLiteral("/components/-"),
        QJsonValue(QJsonValue::Undefined),
        QJsonObject{
            {QStringLiteral("id"), componentId},
            {QStringLiteral("type"), QStringLiteral("DesignComponent")},
            {QStringLiteral("packageRef"), QStringLiteral("vendor.designpkg")},
            {QStringLiteral("config"), QJsonObject{{QStringLiteral("width"), 8}}}
        }
    });
    return patch;
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

void configureSettingsRoot(const QString& rootPath, const QString& applicationName) {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, rootPath);
    QCoreApplication::setOrganizationName(QStringLiteral("ipcatalogpanel_reload_test_org"));
    QCoreApplication::setApplicationName(applicationName);
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
  "schema": "ipcraft.package.v1",
  "id": "%1",
  "name": "Reload Package",
  "version": "1.0.0",
  "extensions": [
    "ipcraft.views"
  ],
  "views": [
    { "module": "Module", "file": "views/Module.xml" }
  ],
  "native": {
    "ipcraft": {
      "editor": {
        "connection_classes": [
          { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
        ],
        "modules": [
          {
            "id": "Module",
            "name": "Reload Module",
            "interfaces": [
              {
                "id": "bus",
                "modes": ["initiator"],
                "accepts": [{ "class": "demo_link", "role": "initiator" }]
              }
            ]
          }
        ]
      }
    }
  }
})json").arg(packageId).toUtf8());
}

void writePackageRequiringCapability(const QString& packageRootPath,
                                     const QString& packageId,
                                     const QString& capabilityId) {
    QDir root(packageRootPath);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QStringLiteral(R"json({
  "schema": "ipcraft.package.v1",
  "id": "%1",
  "name": "Capability Diagnostics Package",
  "version": "1.0.0",
  "extensions": [
    { "id": "%2", "required": true }
  ]
})json").arg(packageId, capabilityId).toUtf8());
}

void writePackageWithParameter(const QString& packageRootPath,
                               const QString& packageId,
                               const QString& parameterName,
                               const QString& parameterLabel) {
    QDir root(packageRootPath);
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    writeFile(root.filePath(QStringLiteral("views/Module.xml")),
              QByteArrayLiteral(R"xml(<module-view schema="v1" module="Module">
  <anchors><anchor ref="bus" x="0" y="0" /></anchors>
</module-view>)xml"));
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QStringLiteral(R"json({
  "schema": "ipcraft.package.v1",
  "id": "%1",
  "name": "Reload Package",
  "version": "1.0.0",
  "extensions": [
    "ipcraft.views"
  ],
  "views": [
    { "module": "Module", "file": "views/Module.xml" }
  ],
  "native": {
    "ipcraft": {
      "editor": {
        "connection_classes": [
          { "id": "demo_link", "roles": ["initiator", "target"], "symmetric": false }
        ],
        "parameters": {
          "%2": { "type": "int", "default": 7, "label": "%3" }
        },
        "modules": [
          {
            "id": "Module",
            "name": "Reload Module",
            "interfaces": [
              {
                "id": "bus",
                "modes": ["initiator"],
                "accepts": [{ "class": "demo_link", "role": "initiator" }]
              }
            ]
          }
        ]
      }
    }
  }
})json").arg(packageId, parameterName, parameterLabel).toUtf8());
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

QTreeWidgetItem* findCatalogEntry(QTreeWidget* catalog, const QString& ipcoreId) {
    if (!catalog) {
        return nullptr;
    }

    for (int categoryIndex = 0; categoryIndex < catalog->topLevelItemCount(); ++categoryIndex) {
        QTreeWidgetItem* category = catalog->topLevelItem(categoryIndex);
        if (!category) {
            continue;
        }
        for (int childIndex = 0; childIndex < category->childCount(); ++childIndex) {
            QTreeWidgetItem* item = category->child(childIndex);
            if (item && item->data(0, Qt::UserRole).toString() == ipcoreId) {
                return item;
            }
        }
    }
    return nullptr;
}

bool listContainsText(QListWidget* list, const QString& text) {
    if (!list) {
        return false;
    }

    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item && item->text().contains(text)) {
            return true;
        }
    }
    return false;
}

bool designHasComponentWithWidth(const ipcraft::core::ProjectDesign& design,
                                 const QString& componentId,
                                 int width) {
    for (const ipcraft::core::ComponentInstance& component : design.components) {
        if (component.id == componentId &&
            component.config.value(QStringLiteral("width")).toInt() == width) {
            return true;
        }
    }
    return false;
}

bool graphConfigHasObject(const ProjectDocument& document,
                          const QString& instanceId,
                          const QString& objectId) {
    for (const ProjectIpInstanceRecord& instance : document.instances) {
        if (instance.id != instanceId || !instance.hasGraphConfig) {
            continue;
        }
        const QJsonArray objects = instance.graphConfig.value(QStringLiteral("objects")).toArray();
        for (const QJsonValue& objectValue : objects) {
            if (objectValue.isObject() &&
                objectValue.toObject().value(QStringLiteral("id")).toString() == objectId) {
                return true;
            }
        }
    }
    return false;
}

QListWidgetItem* firstListItemContaining(QListWidget* list, const QString& text) {
    if (!list) {
        return nullptr;
    }

    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item && item->text().contains(text)) {
            return item;
        }
    }
    return nullptr;
}

bool widgetHasLabel(QWidget* root, const QString& label) {
    if (!root) {
        return false;
    }

    for (const QLabel* child : root->findChildren<QLabel*>()) {
        if (child && child->text() == label) {
            return true;
        }
    }
    return false;
}

class ModuleRegistryRestore {
public:
    ModuleRegistryRestore()
        : m_packages(ModuleRegistry::instance().packageManifests()) {}

    ~ModuleRegistryRestore() {
        ModuleRegistry& registry = ModuleRegistry::instance();
        registry = ModuleRegistry(ModuleRegistry::LoadMode::Empty);
        registry.loadIpcraftPackages(m_packages);
    }

private:
    QVector<IpcraftPackageManifest> m_packages;
};

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

    IpCatalogEntry fabricEntry() const {
        const std::optional<IpCatalogEntry> entry = catalog.entry(QStringLiteral("finepaper.fabric"));
        require(entry.has_value(), "Fabric catalog entry should exist");
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

    require(projectList != nullptr, "project list should exist");
    require(moduleList != nullptr, "active module list should exist");
    require(toolList != nullptr, "active tool list should exist");
    require(projectList->count() == 1, "project list should show one IP instance");
    require(moduleList->count() == 1, "active module list should show RaveTile");
    require(moduleList->item(0)->data(Qt::UserRole).toString() == QStringLiteral("RaveTile"),
            "active module row should store module type");
    require(toolList->count() == 1, "active tool list should only include topology tools");
    require(toolList->item(0)->text() == QStringLiteral("Mesh"),
            "active tool list should show the Mesh topology preset");
    for (int row = 0; row < toolList->count(); ++row) {
        QListWidgetItem* item = toolList->item(row);
        require(item->data(Qt::UserRole).toString() != QStringLiteral("generate"),
                "active tool list should not expose generate");
        require(item->data(Qt::UserRole).toString() != QStringLiteral("drc"),
                "active tool list should not expose DRC");
    }
}

void testPanelEmitsWorkspaceToolIntentWithActiveInstance() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.ravenocEntry()).success,
            "RaveNoC instance should be created");
    auto* toolList = panel.findChild<QListWidget*>(QStringLiteral("activeToolList"));
    require(toolList != nullptr, "active tool list should exist");
    require(toolList->count() == 1, "active tool list should expose the Mesh topology preset");

    QString requestedTool;
    QString requestedIpcore;
    QString requestedInstance;
    int requestCount = 0;
    QObject::connect(&panel, &IpCatalogPanel::workspaceToolRequested, &panel,
                     [&](const QString& toolId, const QString& ipcoreId, const QString& instanceId) {
                         requestedTool = toolId;
                         requestedIpcore = ipcoreId;
                         requestedInstance = instanceId;
                         ++requestCount;
                     });

    QListWidgetItem* item = toolList->item(0);
    require(item != nullptr, "first workspace tool should exist");
    const bool invoked = QMetaObject::invokeMethod(toolList,
                                                   "itemActivated",
                                                   Qt::DirectConnection,
                                                   Q_ARG(QListWidgetItem*, item));
    require(invoked, "active tool list should expose itemActivated");
    require(requestCount == 1, "workspace tool activation should emit one intent");
    require(requestedTool == QStringLiteral("topology:mesh"),
            "workspace tool intent should include topology tool id");
    require(requestedIpcore == QStringLiteral("finepaper.ravenoc"),
            "workspace tool intent should include active ipcore id");
    require(requestedInstance == QStringLiteral("ravenoc_0"),
            "workspace tool intent should include active instance id");
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

void testCatalogActivationDoesNotEmitDuplicateAddIntent() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    int addSignals = 0;
    QString requestedAdd;
    QObject::connect(&panel, &IpCatalogPanel::addIpcoreRequested, &panel,
                     [&](const QString& ipcoreId) {
                         requestedAdd = ipcoreId;
                         ++addSignals;
                     });

    auto* catalog = panel.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    QTreeWidgetItem* nocCategory = findCatalogCategory(catalog, QStringLiteral("NoC"));
    require(nocCategory != nullptr, "NoC category should exist");
    QTreeWidgetItem* catalogEntry = nocCategory->childCount() > 0 ? nocCategory->child(0) : nullptr;
    require(catalogEntry != nullptr, "catalog should expose selectable IP entries");

    const bool doubleClicked = QMetaObject::invokeMethod(catalog,
                                                         "itemDoubleClicked",
                                                         Qt::DirectConnection,
                                                         Q_ARG(QTreeWidgetItem*, catalogEntry),
                                                         Q_ARG(int, 0));
    require(doubleClicked, "catalog should expose itemDoubleClicked");
    const bool activated = QMetaObject::invokeMethod(catalog,
                                                     "itemActivated",
                                                     Qt::DirectConnection,
                                                     Q_ARG(QTreeWidgetItem*, catalogEntry),
                                                     Q_ARG(int, 0));
    require(activated, "catalog should expose itemActivated");

    require(addSignals == 1,
            "catalog double-click activation should emit one add intent");
    require(requestedAdd == QStringLiteral("finepaper.ravenoc"),
            "catalog activation should keep the requested IP core id");
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

    require(harness.projectIpService.createInstanceForIpcore(harness.fabricEntry()).success,
            "first Fabric instance should be created");
    require(harness.projectIpService.createInstanceForIpcore(harness.fabricEntry()).success,
            "second Fabric instance should be created");

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
    require(removedIpcore == QStringLiteral("finepaper.fabric"),
            "context menu remove should emit the clicked item ipcore");
    require(removedInstance == QStringLiteral("fabric_1"),
            "context menu remove should emit the clicked item instead of the current selection");
}

void testPanelContextMenuRemoveSurvivesListRefreshWhileMenuIsOpen() {
    TestHarness harness;
    IpCatalogPanel panel(&harness.catalog,
                         &harness.stateService,
                         &harness.projectIpService,
                         &harness.workspaceController);

    require(harness.projectIpService.createInstanceForIpcore(harness.fabricEntry()).success,
            "first Fabric instance should be created");
    require(harness.projectIpService.createInstanceForIpcore(harness.fabricEntry()).success,
            "second Fabric instance should be created");

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
            harness.fabricEntry());
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
    require(removedIpcore == QStringLiteral("finepaper.fabric"),
            "context menu remove should preserve the clicked ipcore across refresh");
    require(removedInstance == QStringLiteral("fabric_1"),
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

void testMainWindowIgnoresStaleWorkspaceTopologyToolInstance() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("stale_tool_guard.fpproj"));
    require(window.createProjectAt(projectPath), "project should be created for topology guard test");

    const std::optional<IpCatalogEntry> entry =
        window.m_ipCatalogService->entry(QStringLiteral("finepaper.ravenoc"));
    require(entry.has_value(), "RaveNoC entry should exist in the runtime catalog");
    const ProjectIpServiceResult result = window.m_projectIpService->createInstanceForIpcore(*entry);
    require(result.success, "RaveNoC instance should be created");
    require(window.m_activeWorkspaceController->activeContext().has_value(),
            "test should have an active workspace context");
    require(window.m_activeWorkspaceController->activeContext()->record.instanceId
                == QStringLiteral("ravenoc_0"),
            "active workspace should use the created instance");
    require(window.m_graph->modules().empty(),
            "graph should start without topology modules");

    scheduleInputDialogAccepts();
    window.createTopologyPresetFor(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("stale_instance"),
                                   QStringLiteral("mesh"));
    processEventsFor(100);

    require(window.m_graph->modules().empty(),
            "stale workspace tool instance should not create topology modules");
}

void testMainWindowIgnoresTopologyToolWhenActiveInstanceChangesDuringPrompt() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("prompt_reentry_guard.fpproj"));
    require(window.createProjectAt(projectPath), "project should be created for prompt guard test");

    const std::optional<IpCatalogEntry> entry =
        window.m_ipCatalogService->entry(QStringLiteral("finepaper.ravenoc"));
    require(entry.has_value(), "RaveNoC entry should exist in the runtime catalog");
    const ProjectIpServiceResult created = window.m_projectIpService->createInstanceForIpcore(*entry);
    require(created.success, "RaveNoC instance should be created");
    require(window.m_activeWorkspaceController->activeContext().has_value(),
            "test should have an active workspace context");
    require(window.m_activeWorkspaceController->activeContext()->record.instanceId
                == QStringLiteral("ravenoc_0"),
            "created instance should be active before topology request");

    QTimer::singleShot(0, &window, [&window] {
        window.m_projectStateService->removeIpInstanceRecord(QStringLiteral("finepaper.ravenoc"),
                                                             QStringLiteral("ravenoc_0"));
        window.m_projectIpService->handleIpInstanceRecordsMutated(std::nullopt);
    });
    scheduleInputDialogAccepts();
    window.createTopologyPresetFor(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"),
                                   QStringLiteral("mesh"));
    processEventsFor(100);

    require(window.m_graph->modules().empty(),
            "topology should not be created after active instance changes during prompts");
    require(!window.m_activeWorkspaceController->activeContext().has_value(),
            "prompt reentry should have cleared the active instance");
}

void testLoadGraphReportsFailureForInvalidPath() {
    MainWindow window;
    QTimer::singleShot(0, &closeOpenMessageBoxes);
    require(!window.loadGraph(QStringLiteral("/tmp/does-not-exist.fpproj")),
            "loadGraph should report failure for invalid paths");
    require(!window.hasOpenProject(),
            "failed loadGraph should leave MainWindow without an open project");
}

void testMainWindowSeedsDesignEditingServiceWhenProjectIsCreated() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("created_design_sync.fpproj"));
    require(window.createProjectAt(projectPath),
            "project should be created before checking design editing seed");

    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    require(editing->design().name == window.m_projectService->design().name,
            "created project should seed design editing service from ProjectService design");
    require(editing->design().schema == window.m_projectService->design().schema,
            "created project should seed the project schema");
}

void testMainWindowSeedsDesignEditingServiceWhenProjectIsLoaded() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString projectPath = tempDir.filePath(QStringLiteral("loaded_design_sync.fpproj"));
    writeDesignFixtureProject(projectPath,
                              loadedDesignFixture(QStringLiteral("Loaded Design Sync")));

    MainWindow window;
    require(window.loadGraph(projectPath),
            "project should load before checking design editing seed");

    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    require(editing->design().name == QStringLiteral("Loaded Design Sync"),
            "loaded project should seed design editing service name");
    require(editing->design().components.size() == 1,
            "loaded project should seed design editing components");
    require(editing->design().components.first().id == QStringLiteral("component0"),
            "loaded project should seed the component id");
    require(editing->design().components.first().config.value(QStringLiteral("width")).toInt() == 4,
            "loaded project should seed flat runtime component config from ProjectService design");
}

void testMainWindowClearAndNewProjectResetDesignEditingService() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    require(window.createProjectAt(tempDir.filePath(QStringLiteral("first_sync.fpproj"))),
            "first project should be created before clear");
    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    require(!editing->design().name.isEmpty(),
            "created project should seed a non-empty editing design");

    window.clearDocument();
    require(!window.m_projectService->hasDocument(),
            "clearDocument should clear ProjectService");
    require(editing->design().name.isEmpty(),
            "clearDocument should reset the design editing service");
    require(editing->design().components.isEmpty(),
            "clearDocument should clear editing service components");

    require(window.createProjectAt(tempDir.filePath(QStringLiteral("second_sync.fpproj"))),
            "second project should be created after clear");
    require(editing->design().name == window.m_projectService->design().name,
            "new project after clear should reseed design editing service");
}

void testDesignEditingServiceEditSynchronizesProjectServiceDocument() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString projectPath = tempDir.filePath(QStringLiteral("edit_sync.fpproj"));
    writeDesignFixtureProject(projectPath,
                              loadedDesignFixture(QStringLiteral("Editable Design Sync")));

    MainWindow window;
    require(window.loadGraph(projectPath),
            "project should load before applying design edit");

    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    const DesignEditResult edit =
        editing->applyPatch(setComponentConfigPatch(QStringLiteral("component0"),
                                                    QStringLiteral("parameters"),
                                                    QJsonObject{
                                                        {QStringLiteral("width"), 4},
                                                        {QStringLiteral("frequency_mhz"), 900}
                                                    }));
    require(edit.success, "design editing patch should apply");
    require(window.m_projectService->design().components.first()
                .config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("frequency_mhz")).toInt() == 900,
            "ProjectService runtime design should reflect DesignEditingService edits");
    require(window.m_projectService->document().instances.first()
                .config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("frequency_mhz")).toInt() == 900,
            "ProjectService document should reflect DesignEditingService edits");
}

void testDesignEditingServiceAddComponentPersistsThroughMainWindowSave() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString projectPath = tempDir.filePath(QStringLiteral("design_edit_save.fpproj"));
    writeDesignFixtureProject(projectPath,
                              loadedDesignFixture(QStringLiteral("Design Edit Save")));

    MainWindow window;
    require(window.loadGraph(projectPath),
            "project should load before applying design edit");

    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    const DesignEditResult edit =
        editing->applyPatch(addDesignComponentPatch(QStringLiteral("component1")));
    require(edit.success, "design add-component patch should apply");
    const bool dirtyAfterEdit = window.m_documentDirty && window.isWindowModified();

    require(window.saveDocument(projectPath),
            "MainWindow should save after a design editing patch");
    const auto requireSavedComponent = [&projectPath](const char* context) {
        ProjectService reloaded;
        const ProjectServiceResult loadResult = reloaded.loadFile(projectPath);
        require(loadResult.success, "saved design-edit project should reload as valid V1");

        bool foundComponent = false;
        for (const ipcraft::core::ComponentInstance& component : reloaded.design().components) {
            if (component.id != QStringLiteral("component1")) {
                continue;
            }
            foundComponent = true;
            require(component.config.value(QStringLiteral("width")).toInt() == 8,
                    "saved design-edit component should reload flat config");
        }
        require(foundComponent, context);
    };

    requireSavedComponent("saved design-edit component should survive MainWindow save and reload");
    require(dirtyAfterEdit,
            "design editing patch should mark MainWindow dirty before save");
    require(!window.m_documentDirty && !window.isWindowModified(),
            "successful save should clear MainWindow dirty state");
    require(window.saveDocument(projectPath),
            "clean follow-up save should succeed after a design editing save");
    requireSavedComponent("clean follow-up save should preserve the design-edit component");
}

void testMainWindowSavePersistsMixedGraphAndDesignEdits() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString projectPath = tempDir.filePath(QStringLiteral("mixed_graph_design_save.fpproj"));
    writeDesignFixtureProject(projectPath,
                              loadedDesignFixture(QStringLiteral("Mixed Graph Design Save")));

    MainWindow window;
    require(window.loadGraph(projectPath),
            "project should load before mixed graph and design save test");

    auto module = std::make_unique<Module>(QStringLiteral("mixed_graph_node"),
                                           QStringLiteral("MixedGraphModule"));
    module->setIpcoreId(QStringLiteral("vendor.designpkg"));
    module->setInstanceId(QStringLiteral("component0"));
    std::unique_ptr<Command> rejected = window.m_commandManager->executeCommand(
        std::make_unique<AddModuleCommand>(window.m_graph,
                                           std::move(module),
                                           QStringLiteral("vendor.designpkg"),
                                           QStringLiteral("component0")));
    require(rejected == nullptr,
            "graph add-module command should execute before mixed save");

    DesignEditingService* editing = designEditingServiceFromRegistry(window);
    const DesignEditResult edit =
        editing->applyPatch(addDesignComponentPatch(QStringLiteral("component1")));
    require(edit.success, "design add-component patch should apply before mixed save");
    require(window.m_commandManager->currentStateId() != window.m_cleanStateId,
            "graph command should leave graph history dirty before mixed save");
    require(window.m_designEditingDirty,
            "design edit should leave design editing state dirty before mixed save");

    require(window.saveDocument(projectPath),
            "MainWindow should save mixed graph and design edits");

    ProjectService reloaded;
    const ProjectServiceResult loadResult = reloaded.loadFile(projectPath);
    require(loadResult.success, "mixed-save project should reload as valid V1");
    require(graphConfigHasObject(reloaded.document(),
                                 QStringLiteral("component0"),
                                 QStringLiteral("mixed_graph_node")),
            "mixed save should persist the graph-projected module");
    require(designHasComponentWithWidth(reloaded.design(),
                                        QStringLiteral("component1"),
                                        8),
            "mixed save should persist the design-added component");
}

void testNewProjectAddsIpcraftPackageInstanceFromCatalog() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("catalog_add_package.fpproj"));
    require(window.createProjectAt(projectPath), "project should be created before catalog edits");

    auto* catalog = window.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    require(catalog != nullptr, "MainWindow catalog tree should exist");
    QTreeWidgetItem* ravenocItem = findCatalogEntry(catalog, QStringLiteral("finepaper.ravenoc"));
    require(ravenocItem != nullptr, "RaveNoC package manifest entry should appear in the catalog");

    const bool activated = QMetaObject::invokeMethod(catalog,
                                                     "itemActivated",
                                                     Qt::DirectConnection,
                                                     Q_ARG(QTreeWidgetItem*, ravenocItem),
                                                     Q_ARG(int, 0));
    require(activated, "catalog tree should expose itemActivated");
    QCoreApplication::processEvents();

    const QVector<ProjectIpInstanceRecord>& records =
        window.m_projectStateService->ipInstanceRecords();
    require(records.size() == 1, "catalog activation should add one project IP instance");
    require(records.first().ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "project instance should use the package id from the manifest catalog entry");
    require(records.first().instanceId == QStringLiteral("ravenoc_0"),
            "project instance should get a stable package-derived instance id");
    require(records.first().state.value(QStringLiteral("global_parameters"))
                .toObject()
                .contains(QStringLiteral("flit_data_width")),
            "project instance should be initialized from package parameters");

    auto* projectList = window.findChild<QListWidget*>(QStringLiteral("projectIpList"));
    auto* moduleList = window.findChild<QListWidget*>(QStringLiteral("activeModuleList"));
    auto* toolList = window.findChild<QListWidget*>(QStringLiteral("activeToolList"));
    require(projectList != nullptr && projectList->count() == 1,
            "project instance list should show the added package instance");
    require(listContainsText(moduleList, QStringLiteral("Rave")),
            "active workspace module list should show package modules for the added instance");
    require(toolList != nullptr && toolList->count() == 1,
            "workspace tools should expose only active-instance editing helpers");
    require(toolList->item(0)->data(Qt::UserRole).toString() == QStringLiteral("topology:mesh"),
            "workspace tools should expose the package topology preset");

    auto* logList = window.m_logPanel->findChild<QListWidget*>();
    require(listContainsText(logList, QStringLiteral("IP core package finepaper.ravenoc")),
            "startup log should include the loaded package manifest");
}

void testAmbiguousConnectionAppearsInPropertyPanelAndLog() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    MainWindow window;
    const QString projectPath = tempDir.filePath(QStringLiteral("ambiguous_connection.fpproj"));
    require(window.createProjectAt(projectPath), "project should be created before graph edits");

    auto source = std::make_unique<Module>(QStringLiteral("source"), QStringLiteral("Source"));
    source->addPort(Port(QStringLiteral("out"),
                         Port::Direction::Output,
                         QStringLiteral("bus"),
                         QStringLiteral("Out")));
    auto target = std::make_unique<Module>(QStringLiteral("target"), QStringLiteral("Target"));
    target->addPort(Port(QStringLiteral("in"),
                         Port::Direction::Input,
                         QStringLiteral("bus"),
                         QStringLiteral("In")));
    require(window.m_graph->addModule(std::move(source)), "source module should add");
    require(window.m_graph->addModule(std::move(target)), "target module should add");

    window.m_graph->addConnection(std::make_unique<Connection>(
        QStringLiteral("ambiguous_conn"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")},
        QStringLiteral("chi_node_interface"),
        QVector<ConnectionInterfaceRef>{
            ConnectionInterfaceRef{QStringLiteral("source"), QStringLiteral("out")},
            ConnectionInterfaceRef{QStringLiteral("target"), QStringLiteral("in")}
        },
        QStringLiteral("ambiguous"),
        QStringList{QStringLiteral("chi_node_interface"), QStringLiteral("monitor_tap")}));
    QCoreApplication::processEvents();

    auto* logList = window.m_logPanel->findChild<QListWidget*>();
    QListWidgetItem* ambiguityLog =
        firstListItemContaining(logList, QStringLiteral("multiple valid classes"));
    require(ambiguityLog != nullptr,
            "ambiguous connection should append a warning to the activity log");

    const bool clicked = QMetaObject::invokeMethod(logList,
                                                   "itemClicked",
                                                   Qt::DirectConnection,
                                                   Q_ARG(QListWidgetItem*, ambiguityLog));
    require(clicked, "log list should expose itemClicked");
    QCoreApplication::processEvents();

    auto* comboBox = window.m_propertyPanel->findChild<QComboBox*>(
        QStringLiteral("connectionClassCombo"));
    require(comboBox != nullptr,
            "clicking the ambiguity log entry should show the connection in the property panel");
    require(comboBox->currentText() == QStringLiteral("chi_node_interface"),
            "property panel should show the selected ambiguous connection class");
    require(comboBox->findText(QStringLiteral("monitor_tap")) >= 0,
            "property panel should show every ambiguous connection class alternative");
}

void testCatalogReloadUsesConfiguredPackageRoots() {
    ModuleRegistryRestore restoreRegistry;
    QTemporaryDir settingsRoot;
    QTemporaryDir packageRoot;
    require(settingsRoot.isValid() && packageRoot.isValid(),
            "temporary directories should be valid");
    writeMinimalPackage(packageRoot.path(), QStringLiteral("org.example.reload"));
    configureSettingsRoot(settingsRoot.path(), QStringLiteral("ipcatalogpanel_reload_test_app"));

    MainWindow window;
    require(!window.m_ipCatalogService->entry(QStringLiteral("org.example.reload")).has_value(),
            "test package should not be loaded before configuring package roots");

    AppSettings().setIpcorePaths({packageRoot.path()});
    window.reloadIpcoreCatalog();

    require(window.m_ipCatalogService->entry(QStringLiteral("org.example.reload")).has_value(),
            "catalog reload should use configured package roots");
    require(ModuleRegistry::instance().getType(QStringLiteral("org.example.reload"),
                                               QStringLiteral("Module")) != nullptr,
            "catalog reload should load configured package modules into the registry");

    auto* catalog = window.findChild<QTreeWidget*>(QStringLiteral("ipCatalogList"));
    require(findCatalogEntry(catalog, QStringLiteral("org.example.reload")) != nullptr,
            "catalog reload should refresh the IP catalog panel");

    auto* logList = window.m_logPanel->findChild<QListWidget*>();
    require(listContainsText(logList, QStringLiteral("org.example.reload")),
            "catalog reload should append package reload details to the activity log");
}

void testCatalogReloadRefreshesPropertyPanelParameters() {
    ModuleRegistryRestore restoreRegistry;
    QTemporaryDir settingsRoot;
    QTemporaryDir firstPackageRoot;
    QTemporaryDir secondPackageRoot;
    QTemporaryDir projectRoot;
    require(settingsRoot.isValid() &&
                firstPackageRoot.isValid() &&
                secondPackageRoot.isValid() &&
                projectRoot.isValid(),
            "temporary directories should be valid");

    const QString packageId = QStringLiteral("org.example.parameters");
    writePackageWithParameter(firstPackageRoot.path(),
                              packageId,
                              QStringLiteral("old_width"),
                              QStringLiteral("Old Width"));
    writePackageWithParameter(secondPackageRoot.path(),
                              packageId,
                              QStringLiteral("new_width"),
                              QStringLiteral("New Width"));
    configureSettingsRoot(settingsRoot.path(), QStringLiteral("ipcatalogpanel_parameters_test_app"));
    AppSettings().setIpcorePaths({firstPackageRoot.path()});

    MainWindow window;
    require(window.createProjectAt(projectRoot.filePath(QStringLiteral("parameters.fpproj"))),
            "project should be created for property panel reload test");
    const std::optional<IpCatalogEntry> oldEntry = window.m_ipCatalogService->entry(packageId);
    require(oldEntry.has_value(), "initial package entry should load");
    require(window.m_projectIpService->createInstanceForIpcore(*oldEntry).success,
            "initial package instance should be created");
    window.m_propertyPanel->setSelectedModule(QString());
    require(widgetHasLabel(window.m_propertyPanel, QStringLiteral("Old Width")),
            "property panel should render the initial package parameter");

    AppSettings().setIpcorePaths({secondPackageRoot.path()});
    window.reloadIpcoreCatalog();
    window.m_propertyPanel->setSelectedModule(QString());

    require(widgetHasLabel(window.m_propertyPanel, QStringLiteral("New Width")),
            "property panel should render reloaded package parameters");
    require(!widgetHasLabel(window.m_propertyPanel, QStringLiteral("Old Width")),
            "property panel should drop stale package parameters after reload");
}

void testPackageRootsDialogReportsMissingCapabilityDiagnostics() {
    ModuleRegistryRestore restoreRegistry;
    QTemporaryDir settingsRoot;
    QTemporaryDir packageRoot;
    require(settingsRoot.isValid() && packageRoot.isValid(),
            "temporary directories should be valid");

    configureSettingsRoot(settingsRoot.path(),
                          QStringLiteral("ipcatalogpanel_package_roots_diagnostics_test_app"));
    const QString capabilityId = QStringLiteral("vendor.dialog.required.v1");
    writePackageRequiringCapability(packageRoot.path(),
                                    QStringLiteral("org.example.dialogdiagnostics"),
                                    capabilityId);
    AppSettings().setIpcorePaths({packageRoot.path()});

    MainWindow window;
    const std::shared_ptr<QStringList> diagnostics = std::make_shared<QStringList>();
    schedulePackageRootsDialogCapture(diagnostics);
    window.manageIpcorePackageRoots();

    bool sawMissingCapabilityDiagnostic = false;
    for (const QString& diagnostic : *diagnostics) {
        sawMissingCapabilityDiagnostic =
            sawMissingCapabilityDiagnostic ||
            (diagnostic.contains(capabilityId) &&
             diagnostic.contains(QStringLiteral("Required capability has no registered handler.")));
    }
    require(sawMissingCapabilityDiagnostic,
            "package roots dialog should report missing required capability handlers");
}

void testDefaultRegistrySurvivesCatalogReloadTests() {
    QTemporaryDir settingsRoot;
    require(settingsRoot.isValid(), "temporary settings directory should be valid");
    configureSettingsRoot(settingsRoot.path(), QStringLiteral("ipcatalogpanel_registry_guard_app"));
    AppSettings().setIpcorePaths({repositoryPath(QStringLiteral("ipcores"))});

    MainWindow window;
    const ModuleType* raveTile =
        ModuleRegistry::instance().getType(QStringLiteral("finepaper.ravenoc"),
                                           QStringLiteral("RaveTile"));
    require(raveTile != nullptr,
            "tests after catalog reload should still see the default module registry");
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
    AppSettings().setIpcorePaths(QStringList{repositoryPath(QStringLiteral("ipcores"))});

    try {
        testSearchFiltersCatalogEntries();
        testCatalogSectionsAndCategoriesCanCollapse();
        testSelectingIpInstanceUpdatesActiveModuleAndToolLists();
        testPanelEmitsWorkspaceToolIntentWithActiveInstance();
        testPanelEmitsAddAndSelectSignals();
        testCatalogActivationDoesNotEmitDuplicateAddIntent();
        testPanelEmitsRemoveSignalForActiveInstance();
        testPanelContextMenuRemoveTargetsClickedItem();
        testPanelContextMenuRemoveSurvivesListRefreshWhileMenuIsOpen();
        testMainWindowUsesIpCatalogDockWithoutActiveCombo();
        testMainWindowStartsWithoutEditableUnsavedProject();
        testMainWindowUsesProjectDirectoryForNewProjectDefault();
        testMainWindowIgnoresStaleWorkspaceTopologyToolInstance();
        testMainWindowIgnoresTopologyToolWhenActiveInstanceChangesDuringPrompt();
        testLoadGraphReportsFailureForInvalidPath();
        testMainWindowSeedsDesignEditingServiceWhenProjectIsCreated();
        testMainWindowSeedsDesignEditingServiceWhenProjectIsLoaded();
        testMainWindowClearAndNewProjectResetDesignEditingService();
        testDesignEditingServiceEditSynchronizesProjectServiceDocument();
        testDesignEditingServiceAddComponentPersistsThroughMainWindowSave();
        testMainWindowSavePersistsMixedGraphAndDesignEdits();
        testNewProjectAddsIpcraftPackageInstanceFromCatalog();
        testAmbiguousConnectionAppearsInPropertyPanelAndLog();
        testCatalogReloadUsesConfiguredPackageRoots();
        testCatalogReloadRefreshesPropertyPanelParameters();
        testPackageRootsDialogReportsMissingCapabilityDiagnostics();
        testDefaultRegistrySurvivesCatalogReloadTests();
    } catch (const std::exception& error) {
        std::cerr << "ipcatalogpanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcatalogpanel_test passed\n";
    return 0;
}
