// RemoveIpInstanceCommand tests.
#include "commands/commandmanager.h"
#include "commands/addipinstancecommand.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#define private public
#include "commands/removeipinstancecommand.h"
#undef private

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ProjectIpInstanceRecord makeRecord(const QString& ipcoreId,
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

IpCatalogEntry makeCatalogEntry(const QString& ipcoreId, const QString& name, const QString& kind) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.name = name;
    entry.version = QStringLiteral("1.0");
    entry.kind = kind;
    return entry;
}

std::unique_ptr<Module> makeModule(const QString& moduleId,
                                   const QString& ipcoreId,
                                   const QString& instanceId,
                                   const std::vector<Port>& ports) {
    auto module = std::make_unique<Module>(moduleId, QStringLiteral("TestModule"));
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : ports) {
        module->addPort(port);
    }
    return module;
}

void addConnection(Graph& graph,
                   const QString& id,
                   const QString& sourceModuleId,
                   const QString& sourcePortId,
                   const QString& targetModuleId,
                   const QString& targetPortId) {
    graph.addConnection(std::make_unique<Connection>(id,
                                                     PortRef{sourceModuleId, sourcePortId},
                                                     PortRef{targetModuleId, targetPortId}));
}

void testProjectStateServiceSupportsIndexedTakeAndInsert() {
    ProjectStateService stateService;
    const ProjectIpInstanceRecord ravenoc0 =
        makeRecord(QStringLiteral("finepaper.ravenoc"), QStringLiteral("ravenoc_0"), QStringLiteral("noc"));
    const ProjectIpInstanceRecord fabric0 =
        makeRecord(QStringLiteral("finepaper.fabric"), QStringLiteral("fabric_0"), QStringLiteral("fabric"));

    require(stateService.ensureIpInstanceRecord(ravenoc0), "first record should insert");
    require(stateService.ensureIpInstanceRecord(fabric0), "second record should insert");
    require(stateService.indexOfIpInstanceRecord(ravenoc0.ipcoreId, ravenoc0.instanceId) == 0,
            "indexOf should find the first record");
    require(stateService.indexOfIpInstanceRecord(fabric0.ipcoreId, fabric0.instanceId) == 1,
            "indexOf should find the second record");
    require(stateService.ipInstanceRecord(ravenoc0.ipcoreId, ravenoc0.instanceId).has_value(),
            "ipInstanceRecord should return an existing record");

    const std::optional<ProjectIpInstanceRecord> takenFabric =
        stateService.takeIpInstanceRecord(fabric0.ipcoreId, fabric0.instanceId);
    require(takenFabric.has_value(), "take should return the removed record");
    require(stateService.indexOfIpInstanceRecord(fabric0.ipcoreId, fabric0.instanceId) == -1,
            "taken record should no longer have an index");
    require(stateService.insertIpInstanceRecord(-10, *takenFabric),
            "insert should clamp negative indexes to the front");
    require(stateService.ipInstanceRecords().front().instanceId == QStringLiteral("fabric_0"),
            "negative insert indexes should clamp to zero");

    const std::optional<ProjectIpInstanceRecord> takenAgain =
        stateService.takeIpInstanceRecord(fabric0.ipcoreId, fabric0.instanceId);
    require(takenAgain.has_value(), "record should still be removable after reinsertion");
    require(stateService.insertIpInstanceRecord(999, *takenAgain),
            "insert should clamp large indexes to append");
    require(stateService.ipInstanceRecords().back().instanceId == QStringLiteral("fabric_0"),
            "large insert indexes should append");
    require(!stateService.insertIpInstanceRecord(1, *takenAgain),
            "duplicate insert should be rejected");
}

void testRemoveIpInstanceCommandRemovesOwnedStateAndSupportsUndoRedo() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    CommandManager commandManager;

    const QString removedIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString removedInstanceId = QStringLiteral("ravenoc_0");
    const QString keptIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString keptInstanceId = QStringLiteral("ravenoc_1");

    require(stateService.ensureIpInstanceRecord(makeRecord(removedIpcoreId,
                                                           removedInstanceId,
                                                           QStringLiteral("noc"))),
            "target record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(keptIpcoreId,
                                                           keptInstanceId,
                                                           QStringLiteral("noc"))),
            "remaining record should insert");
    require(projectIpService.selectInstance(removedIpcoreId, removedInstanceId),
            "target instance should become selected before deletion");

    const std::vector<Port> routerPorts{
        Port(QStringLiteral("east"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("East")),
        Port(QStringLiteral("west"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("West")),
        Port(QStringLiteral("local"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Local"))};
    const std::vector<Port> endpointPorts{
        Port(QStringLiteral("noc"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("NoC"))};

    require(graph.addModule(makeModule(QStringLiteral("target_router"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       routerPorts)),
            "target router should insert");
    require(graph.addModule(makeModule(QStringLiteral("target_endpoint"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       endpointPorts)),
            "target endpoint should insert");
    require(graph.addModule(makeModule(QStringLiteral("other_router"),
                                       keptIpcoreId,
                                       keptInstanceId,
                                       routerPorts)),
            "other router should insert");
    require(graph.addModule(makeModule(QStringLiteral("other_endpoint"),
                                       keptIpcoreId,
                                       keptInstanceId,
                                       endpointPorts)),
            "other endpoint should insert");

    addConnection(graph,
                  QStringLiteral("target_internal"),
                  QStringLiteral("target_router"),
                  QStringLiteral("local"),
                  QStringLiteral("target_endpoint"),
                  QStringLiteral("noc"));
    addConnection(graph,
                  QStringLiteral("cross_instance"),
                  QStringLiteral("target_router"),
                  QStringLiteral("east"),
                  QStringLiteral("other_router"),
                  QStringLiteral("west"));
    addConnection(graph,
                  QStringLiteral("other_internal"),
                  QStringLiteral("other_router"),
                  QStringLiteral("local"),
                  QStringLiteral("other_endpoint"),
                  QStringLiteral("noc"));
    require(graph.connections().size() == 3, "graph should start with three connections");

    std::unique_ptr<Command> rejected = commandManager.executeCommand(
        std::make_unique<RemoveIpInstanceCommand>(&graph,
                                                  &stateService,
                                                  &projectIpService,
                                                  removedIpcoreId,
                                                  removedInstanceId));
    require(rejected == nullptr, "remove command should execute successfully");
    require(stateService.ipInstanceRecords().size() == 1,
            "removing an instance should remove its project-state record");
    require(stateService.ipInstanceRecords().first().instanceId == keptInstanceId,
            "remaining record should be kept");
    require(graph.getModule(QStringLiteral("target_router")) == nullptr,
            "owned modules should be removed");
    require(graph.getModule(QStringLiteral("target_endpoint")) == nullptr,
            "all owned modules should be removed");
    require(graph.getModule(QStringLiteral("other_router")) != nullptr,
            "other instance modules should remain");
    require(graph.getModule(QStringLiteral("other_endpoint")) != nullptr,
            "other instance endpoint should remain");
    require(graph.connections().size() == 1,
            "incident internal and cross-instance connections should be removed");
    require(graph.connections().front()->id() == QStringLiteral("other_internal"),
            "unrelated connections should remain");
    require(projectIpService.selectedIpInstance().has_value(),
            "removing the selected instance should move selection to a remaining one");
    require(projectIpService.selectedIpInstance()->instanceId == keptInstanceId,
            "selection should move to the remaining instance");

    commandManager.undo();

    require(stateService.ipInstanceRecords().size() == 2,
            "undo should restore both project-state records");
    require(stateService.ipInstanceRecords().at(0).instanceId == removedInstanceId,
            "undo should restore the removed record at its original index");
    require(graph.getModule(QStringLiteral("target_router")) != nullptr,
            "undo should restore removed modules");
    require(graph.getModule(QStringLiteral("target_endpoint")) != nullptr,
            "undo should restore all removed modules");
    require(graph.connections().size() == 3,
            "undo should restore removed connections");
    require(projectIpService.selectedIpInstance().has_value(),
            "undo should restore the previous selection");
    require(projectIpService.selectedIpInstance()->instanceId == removedInstanceId,
            "undo should restore the original selected instance");

    commandManager.redo();

    require(stateService.ipInstanceRecords().size() == 1,
            "redo should remove the record again");
    require(graph.getModule(QStringLiteral("target_router")) == nullptr,
            "redo should remove target modules again");
    require(graph.getModule(QStringLiteral("other_router")) != nullptr,
            "redo should keep other modules");
    require(graph.connections().size() == 1,
            "redo should remove cross-instance connections again");
    require(projectIpService.selectedIpInstance().has_value(),
            "redo should leave a remaining instance selected");
    require(projectIpService.selectedIpInstance()->instanceId == keptInstanceId,
            "redo should reselect the remaining instance");
}

void testRemoveIpInstanceCommandSelectsNextRecordWhenRemovingCurrentMiddleInstance() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    CommandManager commandManager;

    require(stateService.ensureIpInstanceRecord(makeRecord(QStringLiteral("finepaper.ravenoc"),
                                                           QStringLiteral("ravenoc_0"),
                                                           QStringLiteral("noc"))),
            "first record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(QStringLiteral("finepaper.fabric"),
                                                           QStringLiteral("fabric_0"),
                                                           QStringLiteral("fabric"))),
            "middle record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(QStringLiteral("finepaper.mesh"),
                                                           QStringLiteral("mesh_0"),
                                                           QStringLiteral("mesh"))),
            "last record should insert");
    require(projectIpService.selectInstance(QStringLiteral("finepaper.fabric"),
                                            QStringLiteral("fabric_0")),
            "middle record should be selected before removal");

    std::unique_ptr<Command> rejected = commandManager.executeCommand(
        std::make_unique<RemoveIpInstanceCommand>(&graph,
                                                  &stateService,
                                                  &projectIpService,
                                                  QStringLiteral("finepaper.fabric"),
                                                  QStringLiteral("fabric_0")));

    require(rejected == nullptr, "middle removal should execute");
    require(projectIpService.selectedIpInstance().has_value(),
            "removing the current middle record should keep a workspace selected");
    require(projectIpService.selectedIpInstance()->ipcoreId == QStringLiteral("finepaper.mesh"),
            "selection should move to the next available record after the removed index");
    require(projectIpService.selectedIpInstance()->instanceId == QStringLiteral("mesh_0"),
            "selection should keep the next available instance id");
}

void testRemoveIpInstanceCommandUndoRestoresEmptyPreviousSelection() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    CommandManager commandManager;

    require(stateService.ensureIpInstanceRecord(makeRecord(QStringLiteral("finepaper.ravenoc"),
                                                           QStringLiteral("ravenoc_0"),
                                                           QStringLiteral("noc"))),
            "first record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(QStringLiteral("finepaper.fabric"),
                                                           QStringLiteral("fabric_0"),
                                                           QStringLiteral("fabric"))),
            "second record should insert");
    require(!projectIpService.selectedIpInstance().has_value(),
            "test should start without an active IP selection");

    std::unique_ptr<Command> rejected = commandManager.executeCommand(
        std::make_unique<RemoveIpInstanceCommand>(&graph,
                                                  &stateService,
                                                  &projectIpService,
                                                  QStringLiteral("finepaper.ravenoc"),
                                                  QStringLiteral("ravenoc_0")));

    require(rejected == nullptr, "removal should execute");
    require(projectIpService.selectedIpInstance().has_value(),
            "execute should select an available instance after deletion");

    commandManager.undo();

    require(stateService.ipInstanceRecords().size() == 2,
            "undo should restore the removed record");
    require(!projectIpService.selectedIpInstance().has_value(),
            "undo should restore the previous empty selection");
}

void testAddAndRemoveIpInstanceCommandsShareUndoHistory() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    CommandManager commandManager;

    const QString ipcoreId = QStringLiteral("finepaper.ravenoc");
    const QString instanceId = QStringLiteral("ravenoc_0");
    const ProjectIpInstanceRecord originalRecord =
        makeRecord(ipcoreId, instanceId, QStringLiteral("noc"));

    require(stateService.ensureIpInstanceRecord(originalRecord),
            "original record should insert");
    require(projectIpService.selectInstance(ipcoreId, instanceId),
            "original record should be selected");

    std::unique_ptr<Command> rejectedRemove = commandManager.executeCommand(
        std::make_unique<RemoveIpInstanceCommand>(&graph,
                                                  &stateService,
                                                  &projectIpService,
                                                  ipcoreId,
                                                  instanceId));
    require(rejectedRemove == nullptr, "remove command should execute");
    require(stateService.ipInstanceRecords().isEmpty(),
            "remove command should delete the only instance");

    std::unique_ptr<Command> rejectedAdd = commandManager.executeCommand(
        std::make_unique<AddIpInstanceCommand>(
            &stateService,
            &projectIpService,
            makeCatalogEntry(ipcoreId, QStringLiteral("RaveNoC"), QStringLiteral("noc"))));
    require(rejectedAdd == nullptr, "add command should execute through history");
    require(stateService.ipInstanceRecords().size() == 1,
            "add command should create a replacement instance");
    require(stateService.ipInstanceRecords().first().instanceId == instanceId,
            "replacement instance may reuse the available deterministic id");

    commandManager.undo();

    require(stateService.ipInstanceRecords().isEmpty(),
            "undoing add should remove the replacement before delete can be undone");
    require(!projectIpService.selectedIpInstance().has_value(),
            "undoing add should restore the empty selection left by delete");

    commandManager.undo();

    require(stateService.ipInstanceRecords().size() == 1,
            "undoing delete should restore the original instance after add undo");
    require(stateService.ipInstanceRecords().first().ipcoreId == originalRecord.ipcoreId,
            "restored original should keep its IP core id");
    require(stateService.ipInstanceRecords().first().instanceId == originalRecord.instanceId,
            "restored original should keep its instance id");
    require(projectIpService.selectedIpInstance().has_value(),
            "undoing delete should restore original selection");
    require(projectIpService.selectedIpInstance()->instanceId == instanceId,
            "restored selection should point at original instance");
}

void testRemoveIpInstanceCommandUndoDoesNotPartiallyRestoreOnModuleConflict() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);
    CommandManager commandManager;

    const QString removedIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString removedInstanceId = QStringLiteral("ravenoc_0");
    const QString keptIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString keptInstanceId = QStringLiteral("ravenoc_1");

    require(stateService.ensureIpInstanceRecord(makeRecord(removedIpcoreId,
                                                           removedInstanceId,
                                                           QStringLiteral("noc"))),
            "target record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(keptIpcoreId,
                                                           keptInstanceId,
                                                           QStringLiteral("noc"))),
            "remaining record should insert");
    require(projectIpService.selectInstance(removedIpcoreId, removedInstanceId),
            "target instance should become selected before deletion");

    const std::vector<Port> routerPorts{
        Port(QStringLiteral("east"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("East")),
        Port(QStringLiteral("west"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("West")),
        Port(QStringLiteral("local"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Local"))};
    const std::vector<Port> endpointPorts{
        Port(QStringLiteral("noc"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("NoC"))};

    require(graph.addModule(makeModule(QStringLiteral("target_router"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       routerPorts)),
            "target router should insert");
    require(graph.addModule(makeModule(QStringLiteral("target_endpoint"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       endpointPorts)),
            "target endpoint should insert");
    require(graph.addModule(makeModule(QStringLiteral("other_router"),
                                       keptIpcoreId,
                                       keptInstanceId,
                                       routerPorts)),
            "other router should insert");
    require(graph.addModule(makeModule(QStringLiteral("other_endpoint"),
                                       keptIpcoreId,
                                       keptInstanceId,
                                       endpointPorts)),
            "other endpoint should insert");

    addConnection(graph,
                  QStringLiteral("target_internal"),
                  QStringLiteral("target_router"),
                  QStringLiteral("local"),
                  QStringLiteral("target_endpoint"),
                  QStringLiteral("noc"));
    addConnection(graph,
                  QStringLiteral("cross_instance"),
                  QStringLiteral("target_router"),
                  QStringLiteral("east"),
                  QStringLiteral("other_router"),
                  QStringLiteral("west"));
    addConnection(graph,
                  QStringLiteral("other_internal"),
                  QStringLiteral("other_router"),
                  QStringLiteral("local"),
                  QStringLiteral("other_endpoint"),
                  QStringLiteral("noc"));

    std::unique_ptr<Command> rejected = commandManager.executeCommand(
        std::make_unique<RemoveIpInstanceCommand>(&graph,
                                                  &stateService,
                                                  &projectIpService,
                                                  removedIpcoreId,
                                                  removedInstanceId));
    require(rejected == nullptr, "command should execute");
    const int executedStateId = commandManager.currentStateId();
    require(stateService.ipInstanceRecords().size() == 1,
            "execute should remove the target state record");

    require(graph.addModule(makeModule(QStringLiteral("target_router"),
                                       QStringLiteral("finepaper.conflict"),
                                       QStringLiteral("conflict_0"),
                                       routerPorts)),
            "conflicting module should insert before undo");

    commandManager.undo();

    require(stateService.ipInstanceRecords().size() == 1,
            "failed undo should not restore the target state record");
    require(stateService.ipInstanceRecords().first().instanceId == keptInstanceId,
            "failed undo should leave state untouched");
    require(graph.getModule(QStringLiteral("target_router")) != nullptr,
            "conflicting module should remain after failed undo");
    require(graph.getModule(QStringLiteral("target_router"))->instanceId() == QStringLiteral("conflict_0"),
            "failed undo should not overwrite the conflicting module");
    require(graph.getModule(QStringLiteral("target_endpoint")) == nullptr,
            "failed undo should not partially restore other removed modules");
    require(graph.connections().size() == 1,
            "failed undo should not partially restore removed connections");
    require(graph.connections().front()->id() == QStringLiteral("other_internal"),
            "failed undo should keep the current graph connections untouched");
    require(projectIpService.selectedIpInstance().has_value(),
            "failed undo should keep the current selection");
    require(projectIpService.selectedIpInstance()->instanceId == keptInstanceId,
            "failed undo should leave selection untouched");
    require(commandManager.currentStateId() == executedStateId,
            "failed undo should not rewind command history state");
    require(commandManager.canUndo(),
            "failed undo should keep the command available on the undo stack");
    require(!commandManager.canRedo(),
            "failed undo should not move the command to redo");
}

void testRemoveIpInstanceCommandFailedUndoDoesNotEmitStateMutationSignals() {
    Graph graph;
    ProjectStateService stateService;
    ProjectIpService projectIpService(&stateService);

    const QString removedIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString removedInstanceId = QStringLiteral("ravenoc_0");
    const QString keptIpcoreId = QStringLiteral("finepaper.ravenoc");
    const QString keptInstanceId = QStringLiteral("ravenoc_1");

    require(stateService.ensureIpInstanceRecord(makeRecord(removedIpcoreId,
                                                           removedInstanceId,
                                                           QStringLiteral("noc"))),
            "target record should insert");
    require(stateService.ensureIpInstanceRecord(makeRecord(keptIpcoreId,
                                                           keptInstanceId,
                                                           QStringLiteral("noc"))),
            "remaining record should insert");

    const std::vector<Port> routerPorts{
        Port(QStringLiteral("east"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("East")),
        Port(QStringLiteral("west"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("West")),
        Port(QStringLiteral("local"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Local"))};
    const std::vector<Port> endpointPorts{
        Port(QStringLiteral("noc"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("NoC"))};

    require(graph.addModule(makeModule(QStringLiteral("target_router"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       routerPorts)),
            "target router should insert");
    require(graph.addModule(makeModule(QStringLiteral("target_endpoint"),
                                       removedIpcoreId,
                                       removedInstanceId,
                                       endpointPorts)),
            "target endpoint should insert");
    require(graph.addModule(makeModule(QStringLiteral("other_router"),
                                       keptIpcoreId,
                                       keptInstanceId,
                                       routerPorts)),
            "other router should insert");

    addConnection(graph,
                  QStringLiteral("target_internal"),
                  QStringLiteral("target_router"),
                  QStringLiteral("local"),
                  QStringLiteral("target_endpoint"),
                  QStringLiteral("noc"));
    addConnection(graph,
                  QStringLiteral("cross_instance"),
                  QStringLiteral("target_router"),
                  QStringLiteral("east"),
                  QStringLiteral("other_router"),
                  QStringLiteral("west"));

    RemoveIpInstanceCommand command(&graph,
                                    &stateService,
                                    &projectIpService,
                                    removedIpcoreId,
                                    removedInstanceId);
    command.execute();
    require(command.wasExecuted(), "command should execute before the failed undo regression setup");
    require(command.m_removedConnections.size() == 2,
            "execute should capture both incident connections for the removed instance");

    const Connection& duplicate = *command.m_removedConnections.front();
    command.m_removedConnections.push_back(std::make_unique<Connection>(duplicate.id(),
                                                                       duplicate.source(),
                                                                       duplicate.target()));

    int stateMutationSignals = 0;
    QObject::connect(&stateService,
                     &ProjectStateService::ipInstanceRecordsChanged,
                     &stateService,
                     [&stateMutationSignals] { ++stateMutationSignals; });

    command.undo();

    require(!command.wasUndone(), "undo should reject the duplicate restored connection");
    require(stateMutationSignals == 0,
            "failed undo should not emit transient project-state mutation signals");
    require(stateService.ipInstanceRecords().size() == 1,
            "failed undo should leave project-state records unchanged");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testProjectStateServiceSupportsIndexedTakeAndInsert();
        testRemoveIpInstanceCommandRemovesOwnedStateAndSupportsUndoRedo();
        testRemoveIpInstanceCommandSelectsNextRecordWhenRemovingCurrentMiddleInstance();
        testRemoveIpInstanceCommandUndoRestoresEmptyPreviousSelection();
        testAddAndRemoveIpInstanceCommandsShareUndoHistory();
        testRemoveIpInstanceCommandUndoDoesNotPartiallyRestoreOnModuleConflict();
        testRemoveIpInstanceCommandFailedUndoDoesNotEmitStateMutationSignals();
    } catch (const std::exception& error) {
        std::cerr << "removeipinstancecommand_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "removeipinstancecommand_test passed\n";
    return 0;
}
