// BasicValidator and plugin DRC integration tests.
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "project/ipinstancestate.h"
#include "validation/drcrunner.h"
#include "validation/projectvalidationrunner.h"
#include "validation/validationmanager.h"
#include "validation/validator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeEndpoint(const QString& id) {
    auto module = std::make_unique<Module>(id, "Endpoint");
    module->addPort(Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc"));
    return module;
}

std::unique_ptr<Module> makeOwnedModule(const QString& id,
                                        const QString& ipcoreId,
                                        const QString& instanceId,
                                        const QString& externalId) {
    auto module = std::make_unique<Module>(id, QStringLiteral("ScriptedTile"));
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    module->setParameter(QStringLiteral("external_id"), externalId);
    return module;
}

std::unique_ptr<Module> makeRaveTile(const QString& id, int x, int y) {
    auto module = std::make_unique<Module>(id, "RaveTile");
    module->setIpcoreId(QStringLiteral("finepaper.ravenoc"));
    module->setInstanceId(QStringLiteral("ravenoc_0"));
    module->addPort(Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("North"), {}, QStringLiteral("router"),
                         QStringLiteral("ravenoc_router_link"), QStringLiteral("north")));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("ravenoc_router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("South"), {}, QStringLiteral("router"),
                         QStringLiteral("ravenoc_router_link"), QStringLiteral("south")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("ravenoc_router_link"), QStringLiteral("west")));
    module->setParameter(QStringLiteral("x"), x);
    module->setParameter(QStringLiteral("y"), y);
    return module;
}

std::unique_ptr<Module> makeManualRaveTile(const QString& id, int x, int y) {
    auto module = makeRaveTile(id, x, y);
    module->setParameter(QStringLiteral("mesh_col"), 0);
    module->setParameter(QStringLiteral("mesh_row"), 0);
    return module;
}

std::unique_ptr<Module> makeRaveTileWithLogicalCoordinate(const QString& id,
                                                          int x,
                                                          int y,
                                                          int meshCol,
                                                          int meshRow) {
    auto module = makeRaveTile(id, x, y);
    module->setParameter(QStringLiteral("mesh_col"), meshCol);
    module->setParameter(QStringLiteral("mesh_row"), meshRow);
    return module;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void addConnection(Graph& graph,
                   const QString& id,
                   const QString& sourceModule,
                   const QString& sourcePort,
                   const QString& targetModule,
                   const QString& targetPort) {
    graph.addConnection(std::make_unique<Connection>(
        id,
        PortRef{sourceModule, sourcePort},
        PortRef{targetModule, targetPort}));
}

bool hasMessageContaining(const QList<ValidationResult>& results, const QString& text) {
    for (const auto& result : results) {
        if (result.message().contains(text)) {
            return true;
        }
    }

    return false;
}

bool hasRule(const QList<ValidationResult>& results, const QString& ruleName) {
    for (const auto& result : results) {
        if (result.ruleName() == ruleName) {
            return true;
        }
    }

    return false;
}

int countResults(const QList<ValidationResult>& results,
                 ValidationSeverity severity,
                 const QString& messageText) {
    int count = 0;
    for (const auto& result : results) {
        if (result.severity() == severity && result.message().contains(messageText)) {
            ++count;
        }
    }

    return count;
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

IpCatalogEntry ravenocCatalogEntry() {
    IpCatalogEntry entry;
    entry.id = QStringLiteral("finepaper.ravenoc");
    entry.name = QStringLiteral("RaveNoC");
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.sourceRootPath = repositoryPath(QStringLiteral("ipcores/ravenoc"));
    entry.drc.command = QStringLiteral("ruby");
    entry.drc.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.drc.args = {
        QStringLiteral("generator/bin/drc"),
        QStringLiteral("-i"),
        QStringLiteral("{input}")
    };
    return entry;
}

ProjectIpInstanceRecord ravenocIpcoreStateRecord() {
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("finepaper.ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("type"), QStringLiteral("RaveNoC")},
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32},
            {QStringLiteral("flit_type_width"), 2},
            {QStringLiteral("flit_buffer_depth"), 2},
            {QStringLiteral("virtual_channels"), 3},
            {QStringLiteral("routing_algorithm"), QStringLiteral("xy")},
            {QStringLiteral("priority"), QStringLiteral("zero_high")},
            {QStringLiteral("max_packet_flits"), 256},
            {QStringLiteral("axi_addr_width"), 32},
            {QStringLiteral("axi_data_width"), 32},
            {QStringLiteral("axi_cdc_required"), QStringLiteral("all")},
            {QStringLiteral("bypass_cdc"), false}
        }}
    };
    return state;
}

IpCatalogEntry projectCatalogEntryWithoutDrc(const QString& ipcoreId) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    return entry;
}

ProjectIpInstanceRecord projectInstanceRecord(const QString& ipcoreId,
                                              const QString& instanceId) {
    ProjectIpInstanceRecord state;
    state.ipcoreId = ipcoreId;
    state.instanceId = instanceId;
    state.schema = ipcoreId + QStringLiteral("-project-state-v1");
    state.state = QJsonObject{{QStringLiteral("kind"), QStringLiteral("noc")}};
    return state;
}

IpCatalogEntry projectCatalogEntryWithDrc(const QString& ipcoreId,
                                          const QString& sourceRootPath,
                                          const QString& commandPath) {
    IpCatalogEntry entry = projectCatalogEntryWithoutDrc(ipcoreId);
    entry.sourceRootPath = sourceRootPath;
    entry.drc.command = commandPath;
    entry.drc.inputFormat = QStringLiteral("ipcore_graph_v1");
    entry.drc.args = {QStringLiteral("{input}")};
    return entry;
}

QString writeDrcScript(QTemporaryDir& tempDir) {
    const QString path = tempDir.filePath(QStringLiteral("drc.sh"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create temporary DRC script");
    file.write("#!/bin/sh\n");
    file.write("echo \"ERROR design: scripted DRC violation\" >&2\n");
    file.write("exit 0\n");
    file.close();
    require(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
            "failed to mark temporary DRC script executable");
    return path;
}

QString writeDrcInputEchoScript(QTemporaryDir& tempDir) {
    const QString path = tempDir.filePath(QStringLiteral("drc_input_echo.rb"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create temporary input echo DRC script");
    file.write("#!/usr/bin/env ruby\n");
    file.write("require 'json'\n");
    file.write("input = JSON.parse(File.read(ARGV.fetch(0)))\n");
    file.write("module_ids = input.fetch('modules').map { |mod| mod.fetch('id') }.sort.join(',')\n");
    file.write("warn \"ERROR design: #{input.fetch('instance')} modules=#{module_ids}\"\n");
    file.write("exit 0\n");
    file.close();
    require(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
            "failed to mark temporary input echo DRC script executable");
    return path;
}

void testProjectValidationRunnerRejectsNullGraphWithoutCrashing() {
    const QList<IpCatalogEntry> entries{
        projectCatalogEntryWithoutDrc(QStringLiteral("finepaper.noc_a"))
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(QStringLiteral("finepaper.noc_a"), QStringLiteral("noc_a_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(nullptr, entries, instances);

    require(results.size() == 1, "null graph should produce one validation error");
    require(results.first().severity() == ValidationSeverity::Error,
            "null graph validation result should be an error");
    require(results.first().message().contains(QStringLiteral("Graph")),
            "null graph validation result should explain that the graph is missing");
}

void testValidationManagerHandlesMissingGraphAndLogPanel() {
    ValidationManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
    manager.runValidation();
}

void testProjectValidationRunnerWarnsForEachProjectInstanceWithoutDrc() {
    Graph graph;
    const QList<IpCatalogEntry> entries{
        projectCatalogEntryWithoutDrc(QStringLiteral("finepaper.noc_a")),
        projectCatalogEntryWithoutDrc(QStringLiteral("finepaper.noc_b"))
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(QStringLiteral("finepaper.noc_a"), QStringLiteral("noc_a_0")),
        projectInstanceRecord(QStringLiteral("finepaper.noc_b"), QStringLiteral("noc_b_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 2, "two IP instances without DRC should produce two findings");
    require(countResults(results, ValidationSeverity::Warning, QStringLiteral("noc_a_0")) == 1,
            "first project instance without DRC should produce one warning containing its instance id");
    require(countResults(results, ValidationSeverity::Warning, QStringLiteral("noc_b_0")) == 1,
            "second project instance without DRC should produce one warning containing its instance id");
}

void testProjectValidationRunnerErrorsWhenCatalogEntryIsMissing() {
    Graph graph;
    const QList<IpCatalogEntry> entries{
        projectCatalogEntryWithoutDrc(QStringLiteral("finepaper.present"))
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(QStringLiteral("finepaper.missing"), QStringLiteral("missing_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 1, "missing runtime/catalog entry should produce one finding");
    require(results.first().severity() == ValidationSeverity::Error,
            "missing runtime/catalog entry should be an error");
    require(results.first().ruleName() == QStringLiteral("DRC"),
            "missing runtime/catalog entry should be reported from DRC validation");
    require(results.first().message().contains(QStringLiteral("missing_0")),
            "missing runtime/catalog entry error should include the instance id");
    require(results.first().message().contains(QStringLiteral("finepaper.missing")),
            "missing runtime/catalog entry error should include the IP core id");
}

void testProjectValidationRunnerRunsDrcForEveryProjectInstance() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary DRC directory");
    const QString scriptPath = writeDrcScript(tempDir);
    Graph graph;
    const QList<IpCatalogEntry> entries{
        projectCatalogEntryWithDrc(QStringLiteral("finepaper.scripted"),
                                   tempDir.path(),
                                   scriptPath)
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(QStringLiteral("finepaper.scripted"), QStringLiteral("scripted_0")),
        projectInstanceRecord(QStringLiteral("finepaper.scripted"), QStringLiteral("scripted_1"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 2, "DRC should run for each project instance");
    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("scripted_0: scripted DRC violation")) == 1,
            "first project instance DRC result should be prefixed with its instance id");
    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("scripted_1: scripted DRC violation")) == 1,
            "second project instance DRC result should be prefixed with its instance id");
}

void testProjectValidationRunnerDrcReceivesEachInstanceScopedGraph() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary DRC directory");
    const QString scriptPath = writeDrcInputEchoScript(tempDir);
    const QString ipcoreId = QStringLiteral("finepaper.scripted");
    Graph graph;
    require(graph.addModule(makeOwnedModule(QStringLiteral("module_a"),
                                            ipcoreId,
                                            QStringLiteral("scripted_0"),
                                            QStringLiteral("only_a"))),
            "first instance module should add");
    require(graph.addModule(makeOwnedModule(QStringLiteral("module_b"),
                                            ipcoreId,
                                            QStringLiteral("scripted_1"),
                                            QStringLiteral("only_b"))),
            "second instance module should add");
    const QList<IpCatalogEntry> entries{
        projectCatalogEntryWithDrc(ipcoreId, tempDir.path(), scriptPath)
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("scripted_0")),
        projectInstanceRecord(ipcoreId, QStringLiteral("scripted_1"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 2,
            "DRC should receive one instance-scoped graph per project instance");
    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("scripted_0: scripted_0 modules=only_a")) == 1,
            "first DRC input should include only the first instance module");
    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("scripted_1: scripted_1 modules=only_b")) == 1,
            "second DRC input should include only the second instance module");
}

void testBasicValidatorLeavesIpDrcToPluginCommand() {
    Graph graph;
    require(graph.addModule(makeEndpoint("endpoint")), "failed to add endpoint");

    BasicValidator validator;
    const QList<ValidationResult> results = validator.validate(&graph);

    require(!hasRule(results, QStringLiteral("unconnected_port")),
            "unconnected port warnings should come from IP DRC scripts");
    require(!hasRule(results, QStringLiteral("isolated_xp")),
            "IP topology rules should come from IP DRC scripts");
}

void testDrcRunnerUsesIpcoreGraphForRaveNoC() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for DRC flavor test");

    Graph graph;
    require(graph.addModule(makeRaveTile(QStringLiteral("rave_0_0"), 0, 0)),
            "failed to add first RaveTile");
    require(graph.addModule(makeRaveTile(QStringLiteral("rave_0_1"), 1, 0)),
            "failed to add second RaveTile");
    addConnection(graph,
                  QStringLiteral("rave_0_0_east"),
                  QStringLiteral("rave_0_0"),
                  QStringLiteral("east"),
                  QStringLiteral("rave_0_1"),
                  QStringLiteral("west"));

    DRCRunner runner;
    const QList<ValidationResult> results =
        runner.validate(&graph, ravenocCatalogEntry(), ravenocIpcoreStateRecord());
    QStringList messages;
    for (const ValidationResult& result : results) {
        messages.append(result.message());
    }

    const QByteArray messageBytes = messages.join('\n').toLocal8Bit();
    require(results.isEmpty(), messageBytes.constData());
    require(!messages.join('\n').contains(QStringLiteral("expected schema finepaper-ipcore-graph-v1")),
            "RaveNoC DRC should receive IP-core graph JSON");
    require(!messages.join('\n').contains(QStringLiteral("missing ipcore_state")),
            "RaveNoC DRC should receive ipcore_state");
}

void testDrcRunnerAcceptsManualRaveTilePlacement() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for manual placement DRC test");

    Graph graph;
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_a"), 100, 80)),
            "failed to add first manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_b"), 320, 80)),
            "failed to add second manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_c"), 100, 248)),
            "failed to add third manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_d"), 320, 248)),
            "failed to add fourth manual RaveTile");
    addConnection(graph, QStringLiteral("rave_a_east"),
                  QStringLiteral("rave_a"), QStringLiteral("east"),
                  QStringLiteral("rave_b"), QStringLiteral("west"));
    addConnection(graph, QStringLiteral("rave_c_east"),
                  QStringLiteral("rave_c"), QStringLiteral("east"),
                  QStringLiteral("rave_d"), QStringLiteral("west"));
    addConnection(graph, QStringLiteral("rave_a_south"),
                  QStringLiteral("rave_a"), QStringLiteral("south"),
                  QStringLiteral("rave_c"), QStringLiteral("north"));
    addConnection(graph, QStringLiteral("rave_b_south"),
                  QStringLiteral("rave_b"), QStringLiteral("south"),
                  QStringLiteral("rave_d"), QStringLiteral("north"));

    DRCRunner runner;
    const QList<ValidationResult> results =
        runner.validate(&graph, ravenocCatalogEntry(), ravenocIpcoreStateRecord());
    QStringList messages;
    for (const ValidationResult& result : results) {
        messages.append(result.message());
    }

    const QByteArray messageBytes = messages.join('\n').toLocal8Bit();
    require(results.isEmpty(), messageBytes.constData());
}

void testDrcRunnerUsesConnectionsWhenRaveTileLogicalCoordinatesAreStale() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for stale logical coordinate DRC test");

    Graph graph;
    require(graph.addModule(makeRaveTileWithLogicalCoordinate(QStringLiteral("rave_left"),
                                                             100,
                                                             80,
                                                             1,
                                                             0)),
            "failed to add left stale-coordinate RaveTile");
    require(graph.addModule(makeRaveTileWithLogicalCoordinate(QStringLiteral("rave_right"),
                                                             320,
                                                             80,
                                                             0,
                                                             0)),
            "failed to add right stale-coordinate RaveTile");
    addConnection(graph, QStringLiteral("rave_left_east"),
                  QStringLiteral("rave_left"), QStringLiteral("east"),
                  QStringLiteral("rave_right"), QStringLiteral("west"));

    DRCRunner runner;
    const QList<ValidationResult> results =
        runner.validate(&graph, ravenocCatalogEntry(), ravenocIpcoreStateRecord());
    QStringList messages;
    for (const ValidationResult& result : results) {
        messages.append(result.message());
    }

    const QByteArray messageBytes = messages.join('\n').toLocal8Bit();
    require(results.isEmpty(), messageBytes.constData());
}

void testDrcRunnerRejectsManualRaveTileNonMesh() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for manual placement DRC test");

    Graph graph;
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_a"), 100, 80)),
            "failed to add first manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_b"), 320, 80)),
            "failed to add second manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_c"), 100, 248)),
            "failed to add third manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_d"), 320, 248)),
            "failed to add fourth manual RaveTile");

    DRCRunner runner;
    const QList<ValidationResult> results =
        runner.validate(&graph, ravenocCatalogEntry(), ravenocIpcoreStateRecord());

    require(hasMessageContaining(results, QStringLiteral("missing mesh link")),
            "manual RaveTile graph without mesh links should fail DRC");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testProjectValidationRunnerRejectsNullGraphWithoutCrashing();
        testValidationManagerHandlesMissingGraphAndLogPanel();
        testProjectValidationRunnerWarnsForEachProjectInstanceWithoutDrc();
        testProjectValidationRunnerErrorsWhenCatalogEntryIsMissing();
        testProjectValidationRunnerRunsDrcForEveryProjectInstance();
        testProjectValidationRunnerDrcReceivesEachInstanceScopedGraph();
        testBasicValidatorLeavesIpDrcToPluginCommand();
        testDrcRunnerUsesIpcoreGraphForRaveNoC();
        testDrcRunnerAcceptsManualRaveTilePlacement();
        testDrcRunnerUsesConnectionsWhenRaveTileLogicalCoordinatesAreStale();
        testDrcRunnerRejectsManualRaveTileNonMesh();
    } catch (const std::exception& error) {
        std::cerr << "validation_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "validation_test passed\n";
    return 0;
}
