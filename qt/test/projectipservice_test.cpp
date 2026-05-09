// Project IP service and active workspace tests.
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

IpCatalogEntry ravenocEntry() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.ravenoc");
    entry.name = QStringLiteral("RaveNoC");
    entry.version = QStringLiteral("0.1");
    entry.kind = QStringLiteral("noc");
    entry.moduleTypes = QStringList{QStringLiteral("RaveTile")};

    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("integer");
    width.defaultValue = 32;
    entry.instanceParameters.insert(width.name, width);

    TopologyPresetDescriptor mesh;
    mesh.id = QStringLiteral("mesh");
    mesh.label = QStringLiteral("Mesh");
    mesh.kind = QStringLiteral("mesh");
    entry.topologyPresets.push_back(mesh);

    return entry;
}

IpCatalogEntry fabricEntry() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.fabric");
    entry.name = QStringLiteral("Fabric");
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("fabric");
    entry.moduleTypes = QStringList{QStringLiteral("FabricSwitch")};
    return entry;
}

ProjectIpInstanceRecord existingRecord(const QString& ipcoreId,
                                       const QString& instanceId,
                                       const QString& kind) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.schema = ipcoreId + QStringLiteral("-project-state-v1");
    record.state.insert(QStringLiteral("kind"), kind);
    record.state.insert(QStringLiteral("type"), ipcoreId);
    record.state.insert(QStringLiteral("global_parameters"), QJsonObject{});
    return record;
}

void testProjectIpServiceCreatesDefaultStateAndSelectsIt() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult result = service.ensureInstanceForIpcore(ravenocEntry());
    require(result.success, "IP service should create NoC instance");
    require(stateService.ipInstanceRecords().size() == 1,
            "state service should store one instance");

    const ProjectIpInstanceRecord& record = stateService.ipInstanceRecords().first();
    require(record.ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "record should keep ipcore id");
    require(record.instanceId == QStringLiteral("ravenoc_0"),
            "record should use default instance id");
    require(record.schema == QStringLiteral("finepaper.ravenoc-project-state-v1"),
            "record should use ipcore state schema");
    require(record.state.value(QStringLiteral("kind")).toString() == QStringLiteral("noc"),
            "record should keep kind");
    require(record.state.value(QStringLiteral("type")).toString() == QStringLiteral("RaveNoC"),
            "record should keep display type");
    require(record.state.value(QStringLiteral("global_parameters"))
                .toObject()
                .value(QStringLiteral("flit_data_width"))
                .toInt() == 32,
            "record should copy default global parameter");
    require(service.selectedIpInstance().has_value(), "new instance should be selected");
    require(service.selectedIpInstance()->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "selection should point at new ipcore");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_0"),
            "selection should point at new instance");
}

void testProjectIpServiceRejectsSecondNocInstance() {
    ProjectStateService stateService;
    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.othernoc"),
                                                               QStringLiteral("othernoc_0"),
                                                               QStringLiteral("noc"))),
            "seed NoC instance should insert");
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult result = service.ensureInstanceForIpcore(ravenocEntry());
    require(!result.success, "IP service should reject second NoC instance");
    require(result.error.contains(QStringLiteral("noc"), Qt::CaseInsensitive),
            "error should mention NoC");
    require(stateService.ipInstanceRecords().size() == 1,
            "rejected NoC should not mutate records");
    require(!service.selectedIpInstance().has_value(),
            "rejected NoC should not change selection");
}

void testProjectIpServiceRemoveClearsSelection() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    require(service.ensureInstanceForIpcore(ravenocEntry()).success,
            "NoC instance should be created");
    require(service.removeInstance(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0")),
            "selected instance should remove");
    require(stateService.ipInstanceRecords().isEmpty(),
            "removed instance should leave no records");
    require(!service.selectedIpInstance().has_value(),
            "removed selected instance should clear selection");
}

void testActiveWorkspaceChangesWhenSelectionChanges() {
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    PluginDescriptor ravenocDescriptor;
    ravenocDescriptor.id = QStringLiteral("finepaper.ravenoc");
    ravenocDescriptor.name = QStringLiteral("RaveNoC");
    ravenocDescriptor.kind = QStringLiteral("noc");
    ravenocDescriptor.topologyPresets = ravenocEntry().topologyPresets;
    ModuleType raveTile;
    raveTile.name = QStringLiteral("RaveTile");
    raveTile.pluginId = ravenocDescriptor.id;
    require(registry.registerType(raveTile), "RaveTile should register");

    PluginDescriptor fabricDescriptor;
    fabricDescriptor.id = QStringLiteral("finepaper.fabric");
    fabricDescriptor.name = QStringLiteral("Fabric");
    fabricDescriptor.kind = QStringLiteral("fabric");
    ModuleType fabricSwitch;
    fabricSwitch.name = QStringLiteral("FabricSwitch");
    fabricSwitch.pluginId = fabricDescriptor.id;
    require(registry.registerType(fabricSwitch), "FabricSwitch should register");

    IpCatalogService catalog({fabricDescriptor, ravenocDescriptor}, &registry);
    ActiveWorkspaceController controller(&projectIpService, &catalog);
    require(!controller.state().hasActiveIp, "workspace should start empty");

    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.ravenoc"),
                                                               QStringLiteral("ravenoc_0"),
                                                               QStringLiteral("noc"))),
            "ravenoc record should insert");
    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.fabric"),
                                                               QStringLiteral("fabric_0"),
                                                               QStringLiteral("fabric"))),
            "fabric record should insert");

    require(projectIpService.selectInstance(QStringLiteral("finepaper.ravenoc"),
                                            QStringLiteral("ravenoc_0")),
            "ravenoc selection should succeed");
    ActiveWorkspaceState state = controller.state();
    require(state.hasActiveIp, "workspace should become active");
    require(state.ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "workspace should expose selected ipcore id");
    require(state.instanceId == QStringLiteral("ravenoc_0"),
            "workspace should expose selected instance id");
    require(state.label == QStringLiteral("RaveNoC"), "workspace should expose label");
    require(state.kind == QStringLiteral("noc"), "workspace should expose kind");
    require(state.moduleTypes == QStringList{QStringLiteral("RaveTile")},
            "workspace should expose module types");
    require(state.topologyPresets.size() == 1, "workspace should expose presets");

    require(projectIpService.selectInstance(QStringLiteral("finepaper.fabric"),
                                            QStringLiteral("fabric_0")),
            "fabric selection should succeed");
    state = controller.state();
    require(state.ipcoreId == QStringLiteral("finepaper.fabric"),
            "workspace should move to second ipcore");
    require(state.label == QStringLiteral("Fabric"), "workspace should expose second label");
    require(state.kind == QStringLiteral("fabric"), "workspace should expose second kind");
    require(state.moduleTypes == QStringList{QStringLiteral("FabricSwitch")},
            "workspace should expose second module types");
    require(state.topologyPresets.isEmpty(), "workspace should expose second presets");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testProjectIpServiceCreatesDefaultStateAndSelectsIt();
        testProjectIpServiceRejectsSecondNocInstance();
        testProjectIpServiceRemoveClearsSelection();
        testActiveWorkspaceChangesWhenSelectionChanges();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    std::cout << "projectipservice_test passed" << std::endl;
    return 0;
}
