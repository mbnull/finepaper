// Project IP service and active workspace tests.
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"
#include "project/projectipservice.h"
#include "project/projectdocument.h"
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
    entry.instanceLimits.push_back(IpCatalogInstanceLimit{
        QStringLiteral("kind:noc"),
        QStringLiteral("NoC IP instance"),
        1
    });

    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
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

IpCatalogEntry fabricVariantEntry() {
    IpCatalogEntry entry = fabricEntry();
    entry.id = QStringLiteral("vendor.fabric");
    entry.name = QStringLiteral("Fabric Variant");
    return entry;
}

IpCatalogEntry ravenocVariantEntry() {
    IpCatalogEntry entry = ravenocEntry();
    entry.id = QStringLiteral("vendor.ravenoc");
    entry.name = QStringLiteral("RaveNoC Variant");
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

    const ProjectIpServiceResult result = service.createInstanceForIpcore(ravenocEntry());
    require(result.success, "IP service should create NoC instance");
    require(stateService.ipInstanceRecords().size() == 1,
            "state service should store one instance");

    const ProjectIpInstanceRecord& record = stateService.ipInstanceRecords().first();
    require(record.ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "record should keep ipcore id");
    require(record.instanceId == QStringLiteral("ravenoc_0"),
            "record should use default instance id");
    require(record.schema == QStringLiteral("ipcraft.noc.instance-state.v1"),
            "record should use public ipcraft instance state schema");
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

void testProjectIpServiceRejectsRepeatedNocInstances() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult firstResult = service.createInstanceForIpcore(ravenocEntry());
    const ProjectIpServiceResult sameNocResult = service.createInstanceForIpcore(ravenocEntry());
    const ProjectIpServiceResult otherNocResult = service.createInstanceForIpcore(ravenocVariantEntry());

    require(firstResult.success, "IP service should create first NoC instance");
    require(!sameNocResult.success,
            "IP service should reject a second instance of the same NoC IP core");
    require(!otherNocResult.success,
            "IP service should reject a second NoC IP core even when the catalog id differs");
    require(sameNocResult.error.contains(QStringLiteral("NoC")),
            "same-NoC rejection should explain the NoC singleton rule");
    require(otherNocResult.error.contains(QStringLiteral("NoC")),
            "other-NoC rejection should explain the NoC singleton rule");
    require(stateService.ipInstanceRecords().size() == 1,
            "project state should keep only the first NoC instance");
    require(stateService.ipInstanceRecords().first().instanceId == QStringLiteral("ravenoc_0"),
            "first NoC record should remain untouched");
    require(service.selectedIpInstance().has_value(),
            "selection should remain on the existing NoC instance");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_0"),
            "rejected NoC creation should not move selection");
}

void testProjectIpServiceCreatesRepeatedNonNocInstancesForSameIpcore() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult firstResult = service.createInstanceForIpcore(fabricEntry());
    require(firstResult.success, "IP service should create first instance");
    const ProjectIpServiceResult secondResult = service.createInstanceForIpcore(fabricEntry());
    require(secondResult.success, "IP service should create second instance for the same non-NoC IP core");
    require(stateService.ipInstanceRecords().size() == 2,
            "same non-NoC IP core should append a second project record");
    require(firstResult.record.ipcoreId == QStringLiteral("finepaper.fabric"),
            "first record should keep the requested IP core id");
    require(firstResult.record.instanceId == QStringLiteral("fabric_0"),
            "first record should use the first deterministic instance id");
    require(secondResult.record.ipcoreId == QStringLiteral("finepaper.fabric"),
            "second record should keep the requested IP core id");
    require(secondResult.record.instanceId == QStringLiteral("fabric_1"),
            "second record should use the next deterministic instance id");
    require(service.selectedIpInstance().has_value(),
            "new instance should become the selection");
    require(service.selectedIpInstance()->ipcoreId == QStringLiteral("finepaper.fabric"),
            "selection should stay on the created IP core");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("fabric_1"),
            "selection should move to the newest instance id");

    const ProjectIpInstanceRecord& firstRecord = stateService.ipInstanceRecords().at(0);
    const ProjectIpInstanceRecord& secondRecord = stateService.ipInstanceRecords().at(1);
    require(firstRecord.ipcoreId == QStringLiteral("finepaper.fabric"),
            "first created record should be retained first");
    require(firstRecord.instanceId == QStringLiteral("fabric_0"),
            "first created record should keep its deterministic id");
    require(secondRecord.ipcoreId == QStringLiteral("finepaper.fabric"),
            "second created record should be appended");
    require(secondRecord.instanceId == QStringLiteral("fabric_1"),
            "second created record should use the next deterministic id");
}

void testProjectIpServiceRejectsPackageInstanceMax() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);
    IpCatalogEntry entry = fabricEntry();
    entry.maxInstances = 2;

    const ProjectIpServiceResult firstResult = service.createInstanceForIpcore(entry);
    const ProjectIpServiceResult secondResult = service.createInstanceForIpcore(entry);
    const ProjectIpServiceResult thirdResult = service.createInstanceForIpcore(entry);

    require(firstResult.success, "first package-limited instance should be created");
    require(secondResult.success, "second package-limited instance should be created");
    require(!thirdResult.success,
            "package maxInstances should reject creation after the declared maximum");
    require(thirdResult.error.contains(QStringLiteral("finepaper.fabric")),
            "package maxInstances rejection should name the IP core");
    require(stateService.ipInstanceRecords().size() == 2,
            "package maxInstances rejection should not append state");
    require(service.selectedIpInstance().has_value(),
            "package maxInstances rejection should preserve selection");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("fabric_1"),
            "package maxInstances rejection should keep selection on the newest valid instance");
}

void testProjectIpServiceCreatesProjectUniqueInstanceIdsAcrossIpcoreTokenCollisions() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    const ProjectIpServiceResult firstResult = service.createInstanceForIpcore(fabricEntry());
    const ProjectIpServiceResult secondResult = service.createInstanceForIpcore(fabricVariantEntry());

    require(firstResult.success, "first IP core instance should be created");
    require(secondResult.success, "second IP core with colliding token should be created");
    require(firstResult.record.instanceId == QStringLiteral("fabric_0"),
            "first token-colliding IP core should receive the first id");
    require(secondResult.record.instanceId == QStringLiteral("fabric_1"),
            "second token-colliding IP core should receive a project-unique id");
    require(stateService.ipInstanceRecords().at(0).ipcoreId == QStringLiteral("finepaper.fabric"),
            "first record should keep its IP core id");
    require(stateService.ipInstanceRecords().at(1).ipcoreId == QStringLiteral("vendor.fabric"),
            "second record should keep its distinct IP core id");
}

void testProjectIpServiceAllocatesMonotonicInstanceIdsAcrossStateGaps() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    require(service.createInstanceForIpcore(fabricEntry()).success,
            "first non-NoC instance should be created");
    require(service.createInstanceForIpcore(fabricEntry()).success,
            "second non-NoC instance should be created");
    require(stateService.removeIpInstanceRecord(QStringLiteral("finepaper.fabric"),
                                                QStringLiteral("fabric_0")),
            "test should be able to simulate an instance-id gap directly in project state");

    const ProjectIpServiceResult thirdResult = service.createInstanceForIpcore(fabricEntry());

    require(thirdResult.success, "creating after a gap should still succeed");
    require(thirdResult.record.instanceId == QStringLiteral("fabric_2"),
            "new instance id should advance monotonically instead of reusing the first hole");
    require(stateService.ipInstanceRecords().size() == 2,
            "state should keep the remaining instance and append the new one");
    require(stateService.ipInstanceRecords().first().instanceId == QStringLiteral("fabric_1"),
            "existing later instance should remain untouched");
    require(stateService.ipInstanceRecords().last().instanceId == QStringLiteral("fabric_2"),
            "new instance should append with the next monotonic id");
    require(service.selectedIpInstance().has_value(), "newly created instance should be selected");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("fabric_2"),
            "selection should move to the newest monotonic instance");
}

void testProjectIpServiceMutationHandlerPreservesCurrentSelectionWithoutPreferredSelection() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.ravenoc"),
                                                               QStringLiteral("ravenoc_0"),
                                                               QStringLiteral("noc"))),
            "first record should insert");
    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.ravenoc"),
                                                               QStringLiteral("ravenoc_1"),
                                                               QStringLiteral("noc"))),
            "second record should insert");
    require(service.selectInstance(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_1")),
            "selection should point at the second record");

    service.handleIpInstanceRecordsMutated(std::nullopt);

    require(service.selectedIpInstance().has_value(),
            "mutation handler should preserve a valid current selection");
    require(service.selectedIpInstance()->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "preserved selection should keep the same ipcore");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_1"),
            "preserved selection should keep the same instance");
}

void testProjectIpServiceMutationHandlerFallsBackAfterDirectStateRemoval() {
    ProjectStateService stateService;
    ProjectIpService service(&stateService);

    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.ravenoc"),
                                                               QStringLiteral("ravenoc_0"),
                                                               QStringLiteral("noc"))),
            "first record should insert");
    require(stateService.ensureIpInstanceRecord(existingRecord(QStringLiteral("finepaper.fabric"),
                                                               QStringLiteral("fabric_0"),
                                                               QStringLiteral("fabric"))),
            "second record should insert");
    require(service.selectInstance(QStringLiteral("finepaper.fabric"),
                                   QStringLiteral("fabric_0")),
            "selection should point at the record that will be removed directly");
    require(stateService.removeIpInstanceRecord(QStringLiteral("finepaper.fabric"),
                                                QStringLiteral("fabric_0")),
            "test should remove the selected record directly from state");

    service.handleIpInstanceRecordsMutated(std::nullopt);

    require(service.selectedIpInstance().has_value(),
            "mutation handler should fall back to a remaining record");
    require(service.selectedIpInstance()->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "fallback selection should choose the first remaining ipcore");
    require(service.selectedIpInstance()->instanceId == QStringLiteral("ravenoc_0"),
            "fallback selection should choose the first remaining instance");
}

void testProjectIpServiceLoadRestoresSelectionAndWorkspaceContext() {
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    IpCoreRuntimeDescriptor ravenocDescriptor;
    ravenocDescriptor.id = QStringLiteral("finepaper.ravenoc");
    ravenocDescriptor.name = QStringLiteral("RaveNoC");
    ravenocDescriptor.kind = QStringLiteral("noc");
    ModuleType raveTile;
    raveTile.name = QStringLiteral("RaveTile");
    raveTile.ipcoreId = ravenocDescriptor.id;
    require(registry.registerType(raveTile), "RaveTile should register");

    IpCatalogService catalog({ravenocDescriptor}, &registry);
    ActiveWorkspaceController controller(&projectIpService, &catalog);

    ProjectDocument document;
    document.ipcoreState.push_back(existingRecord(QStringLiteral("finepaper.ravenoc"),
                                                  QStringLiteral("ravenoc_0"),
                                                  QStringLiteral("noc")));

    projectIpService.loadFromDocument(document);

    const std::optional<ProjectIpInstanceRef> selection = projectIpService.selectedIpInstance();
    require(selection.has_value(), "project load should restore an IP instance selection");
    require(selection->ipcoreId == QStringLiteral("finepaper.ravenoc"),
            "loaded selection should point at the first project record ipcore");
    require(selection->instanceId == QStringLiteral("ravenoc_0"),
            "loaded selection should point at the first project record instance");
    require(controller.state().hasActiveIp,
            "active workspace should become active after project load");

    const std::optional<ActiveWorkspaceContext> context = controller.activeContext();
    require(context.has_value(), "workspace context should resolve after project load");
    require(context->entry.id == QStringLiteral("finepaper.ravenoc"),
            "workspace context should expose the active catalog entry");
    require(context->record.instanceId == QStringLiteral("ravenoc_0"),
            "workspace context should expose the active project record");
}

void testProjectIpServiceClearClearsSelectionAndWorkspaceContext() {
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    IpCoreRuntimeDescriptor ravenocDescriptor;
    ravenocDescriptor.id = QStringLiteral("finepaper.ravenoc");
    ravenocDescriptor.name = QStringLiteral("RaveNoC");
    ravenocDescriptor.kind = QStringLiteral("noc");
    ModuleType raveTile;
    raveTile.name = QStringLiteral("RaveTile");
    raveTile.ipcoreId = ravenocDescriptor.id;
    require(registry.registerType(raveTile), "RaveTile should register");

    IpCatalogService catalog({ravenocDescriptor}, &registry);
    ActiveWorkspaceController controller(&projectIpService, &catalog);
    require(projectIpService.createInstanceForIpcore(ravenocEntry()).success,
            "NoC instance should be created");
    require(controller.state().hasActiveIp, "workspace should start active");

    projectIpService.clear();

    require(stateService.ipInstanceRecords().isEmpty(),
            "project IP clear should remove project records");
    require(!projectIpService.selectedIpInstance().has_value(),
            "project IP clear should clear selection");
    require(!controller.state().hasActiveIp,
            "active workspace should clear after project IP clear");
    require(!controller.activeContext().has_value(),
            "workspace context should clear with the active workspace");
}

void testActiveWorkspaceChangesWhenSelectionChanges() {
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    IpCoreRuntimeDescriptor ravenocDescriptor;
    ravenocDescriptor.id = QStringLiteral("finepaper.ravenoc");
    ravenocDescriptor.name = QStringLiteral("RaveNoC");
    ravenocDescriptor.kind = QStringLiteral("noc");
    ravenocDescriptor.topologyPresets = ravenocEntry().topologyPresets;
    ModuleType raveTile;
    raveTile.name = QStringLiteral("RaveTile");
    raveTile.ipcoreId = ravenocDescriptor.id;
    require(registry.registerType(raveTile), "RaveTile should register");

    IpCoreRuntimeDescriptor fabricDescriptor;
    fabricDescriptor.id = QStringLiteral("finepaper.fabric");
    fabricDescriptor.name = QStringLiteral("Fabric");
    fabricDescriptor.kind = QStringLiteral("fabric");
    ModuleType fabricSwitch;
    fabricSwitch.name = QStringLiteral("FabricSwitch");
    fabricSwitch.ipcoreId = fabricDescriptor.id;
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
        testProjectIpServiceRejectsRepeatedNocInstances();
        testProjectIpServiceCreatesRepeatedNonNocInstancesForSameIpcore();
        testProjectIpServiceRejectsPackageInstanceMax();
        testProjectIpServiceCreatesProjectUniqueInstanceIdsAcrossIpcoreTokenCollisions();
        testProjectIpServiceAllocatesMonotonicInstanceIdsAcrossStateGaps();
        testProjectIpServiceMutationHandlerPreservesCurrentSelectionWithoutPreferredSelection();
        testProjectIpServiceMutationHandlerFallsBackAfterDirectStateRemoval();
        testProjectIpServiceLoadRestoresSelectionAndWorkspaceContext();
        testProjectIpServiceClearClearsSelectionAndWorkspaceContext();
        testActiveWorkspaceChangesWhenSelectionChanges();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    std::cout << "projectipservice_test passed" << std::endl;
    return 0;
}
