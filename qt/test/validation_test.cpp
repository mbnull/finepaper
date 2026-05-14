// BasicValidator and plugin DRC integration tests.
#include "graph/graph.h"
#include "ipcraft/ipcraftbuiltinvalidator.h"
#include "modules/moduleregistry.h"
#include "ipcore/ipcorecommandrunner.h"
#include "project/ipinstancestate.h"
#include "validation/drcrunner.h"
#include "validation/projectvalidationrunner.h"
#include "validation/validationmanager.h"
#include "validation/validator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

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

IpcraftInterfaceAcceptRule acceptRule(const QString& connectionClassId, const QString& role) {
    IpcraftInterfaceAcceptRule rule;
    rule.connectionClassId = connectionClassId;
    rule.role = role;
    return rule;
}

IpcraftInterfaceDescriptor interfaceDescriptor(const QString& id,
                                               const QString& connectionClassId = QStringLiteral("link"),
                                               const QString& role = QStringLiteral("initiator")) {
    IpcraftInterfaceDescriptor descriptor;
    descriptor.id = id;
    descriptor.label = id;
    descriptor.modes = {role};
    descriptor.ipxactBusInterface = id;
    descriptor.multiConnection = true;
    descriptor.accepts = {acceptRule(connectionClassId, role)};
    return descriptor;
}

IpcraftModuleDescriptor manifestModule(const QString& id,
                                       QVector<IpcraftInterfaceDescriptor> interfaces = {
                                           interfaceDescriptor(QStringLiteral("link"))
                                       }) {
    IpcraftModuleDescriptor descriptor;
    descriptor.id = id;
    descriptor.name = id;
    descriptor.graphRole = QStringLiteral("attached");
    descriptor.interfaces = std::move(interfaces);
    return descriptor;
}

IpcraftConnectionClass peerConnectionClass(const QString& id = QStringLiteral("link")) {
    IpcraftConnectionClass connectionClass;
    connectionClass.id = id;
    connectionClass.roles = {QStringLiteral("initiator")};
    connectionClass.symmetric = true;
    return connectionClass;
}

IpcraftCommandDescriptor manifestCommand(const QString& name, const QString& executablePath) {
    IpcraftCommandDescriptor command;
    command.name = name;
    command.executablePath = executablePath;
    command.resolvedExecutablePath = executablePath;
    command.inputSchema = QStringLiteral("ipcraft.noc.project.v1");
    command.args = {QStringLiteral("{input}")};
    return command;
}

IpcraftPackageManifest packageManifest(const QString& ipcoreId) {
    IpcraftPackageManifest manifest;
    manifest.schema = QStringLiteral("ipcraft.manifest.v1");
    manifest.id = ipcoreId;
    manifest.name = ipcoreId;
    manifest.version = QStringLiteral("1.0");
    manifest.connectionClasses = {peerConnectionClass()};
    manifest.modules = {manifestModule(QStringLiteral("Tile"))};
    return manifest;
}

IpCatalogEntry ipcraftCatalogEntryWithoutDrc(const QString& ipcoreId) {
    IpCatalogEntry entry = projectCatalogEntryWithoutDrc(ipcoreId);
    entry.packageId = ipcoreId;
    entry.packageManifest = packageManifest(ipcoreId);
    entry.moduleTypes = {QStringLiteral("Tile")};
    return entry;
}

IpCatalogEntry ipcraftCatalogEntryWithValidate(const QString& ipcoreId,
                                               const QString& sourceRootPath,
                                               const QString& commandPath) {
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.sourceRootPath = sourceRootPath;
    entry.packageManifest.packageRootPath = sourceRootPath;
    entry.packageManifest.commands.insert(QStringLiteral("validate"),
                                          manifestCommand(QStringLiteral("validate"), commandPath));
    entry.drc.command = commandPath;
    entry.drc.inputFormat = QStringLiteral("ipcraft.noc.project.v1");
    entry.drc.args = {QStringLiteral("{input}")};
    return entry;
}

std::unique_ptr<Module> makeManifestOwnedModule(const QString& id,
                                                const QString& ipcoreId,
                                                const QString& instanceId,
                                                const QString& type = QStringLiteral("Tile")) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    module->addPort(Port(QStringLiteral("link"),
                         Port::Direction::InOut,
                         QStringLiteral("bus"),
                         QStringLiteral("Link"),
                         {},
                         QStringLiteral("attachment"),
                         QStringLiteral("link"),
                         QStringLiteral("link")));
    return module;
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
    require(results.first().ruleName().startsWith(QStringLiteral("built_in")),
            "missing runtime/catalog entry should be reported from built-in validation");
    require(results.first().message().contains(QStringLiteral("missing_0")),
            "missing runtime/catalog entry error should include the instance id");
    require(results.first().message().contains(QStringLiteral("finepaper.missing")),
            "missing runtime/catalog entry error should include the IP core id");
}

void testBuiltInValidationReportsMissingPackage() {
    Graph graph;
    const QList<IpCatalogEntry> entries{
        ipcraftCatalogEntryWithoutDrc(QStringLiteral("finepaper.present"))
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(QStringLiteral("finepaper.missing"), QStringLiteral("missing_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 1, "missing package should produce one built-in finding");
    require(results.first().severity() == ValidationSeverity::Error,
            "missing package should be an error");
    require(results.first().ruleName().startsWith(QStringLiteral("built_in")),
            "missing package should use a built-in rule");
    require(results.first().message().contains(QStringLiteral("missing_0")),
            "missing package error should include the instance id");
    require(results.first().message().contains(QStringLiteral("finepaper.missing")),
            "missing package error should include the package id");
}

void testBuiltInValidationRunsBeforePackageValidate() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary DRC directory");
    const QString scriptPath = writeDrcScript(tempDir);
    const QString ipcoreId = QStringLiteral("finepaper.scripted");
    Graph graph;
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("bad_module"),
                                                    ipcoreId,
                                                    QStringLiteral("bad_0"),
                                                    QStringLiteral("MissingTile"))),
            "bad module should add");
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("good_module"),
                                                    ipcoreId,
                                                    QStringLiteral("good_0"))),
            "good module should add");
    const QList<IpCatalogEntry> entries{
        ipcraftCatalogEntryWithValidate(ipcoreId, tempDir.path(), scriptPath)
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("bad_0")),
        projectInstanceRecord(ipcoreId, QStringLiteral("good_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(results.size() == 2,
            "built-in error should block only the bad instance while good instance DRC still runs");
    require(results.at(0).ruleName().startsWith(QStringLiteral("built_in")),
            "built-in diagnostics should appear before package DRC diagnostics");
    require(results.at(0).message().contains(QStringLiteral("MissingTile")),
            "first result should report the bad module type");
    require(results.at(1).ruleName() == QStringLiteral("DRC"),
            "second result should be the package DRC result for the unblocked instance");
    require(results.at(1).message().contains(QStringLiteral("good_0: scripted DRC violation")),
            "DRC should run only for the unblocked good instance");
    require(!hasMessageContaining(results, QStringLiteral("bad_0: scripted DRC violation")),
            "DRC should not run for the instance with a blocking built-in error");
}

void testCommandRunnerRejectsSchemaMismatch() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary command directory");
    const QString scriptPath = writeDrcScript(tempDir);
    const QString inputPath = tempDir.filePath(QStringLiteral("input.json"));
    QFile inputFile(inputPath);
    require(inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create command input JSON");
    inputFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("finepaper-ipcore-graph-v1")}
    }).toJson(QJsonDocument::Compact));
    inputFile.close();

    const IpCatalogEntry entry =
        ipcraftCatalogEntryWithValidate(QStringLiteral("org.example.schema"),
                                        tempDir.path(),
                                        scriptPath);

    const IpCoreResolvedCommand command =
        IpCoreCommandRunner::resolveDrc(entry, inputPath, tempDir.path());

    require(!command.valid,
            "command runner should reject exported project schema mismatches");
    require(command.errorMessage.contains(QStringLiteral("ipcraft.noc.project.v1")),
            "schema mismatch error should mention the declared input schema");
    require(command.errorMessage.contains(QStringLiteral("finepaper-ipcore-graph-v1")),
            "schema mismatch error should mention the exported schema");
}

void testBuiltInValidationReportsMissingInterface() {
    const QString ipcoreId = QStringLiteral("finepaper.links");
    Graph graph;
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("tile_a"),
                                                    ipcoreId,
                                                    QStringLiteral("links_0"))),
            "first tile should add");
    require(graph.addModule(makeManifestOwnedModule(QStringLiteral("tile_b"),
                                                    ipcoreId,
                                                    QStringLiteral("links_0"))),
            "second tile should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("bad_link"),
        PortRef{QStringLiteral("tile_a"), QStringLiteral("link")},
        PortRef{QStringLiteral("tile_b"), QStringLiteral("link")},
        QStringLiteral("link"),
        QVector<ConnectionInterfaceRef>{
            {QStringLiteral("tile_a"), QStringLiteral("link")},
            {QStringLiteral("tile_b"), QStringLiteral("missing")}
        }));
    const QList<IpCatalogEntry> entries{
        ipcraftCatalogEntryWithoutDrc(ipcoreId)
    };
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("links_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results, ValidationSeverity::Error, QStringLiteral("missing interface")) == 1,
            "built-in validation should report saved connection interfaces that are not in the manifest");
    require(hasRule(results, QStringLiteral("built_in_connection")),
            "missing connection interface should use built_in_connection");
}

void testBuiltInValidationReportsUnmappableInterfaceMode() {
    const QString ipcoreId = QStringLiteral("finepaper.mode");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.modules[0].interfaces[0].modes = {QStringLiteral("custom_mode")};

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("mode_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("mode 'custom_mode' is not mappable to IP-XACT")) == 1,
            "built-in validation should reject interface modes without native or enabled extension mapping");
    require(hasRule(results, QStringLiteral("built_in_manifest")),
            "unmappable interface mode should use built_in_manifest");
}

void testBuiltInValidationReportsUnmappableConnectionClass() {
    const QString ipcoreId = QStringLiteral("finepaper.connection_class");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.connectionClasses[0].roles = {QStringLiteral("custom_role")};
    entry.packageManifest.modules[0].interfaces[0].accepts = {
        acceptRule(QStringLiteral("link"), QStringLiteral("custom_role"))
    };

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("connection_class_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("Connection class 'link' is not mappable to IP-XACT")) == 1,
            "built-in validation should reject connection classes without native, extension, or explicit IP-XACT mapping");
    require(hasRule(results, QStringLiteral("built_in_manifest")),
            "unmappable connection class should use built_in_manifest");
}

void testBuiltInValidationReportsMissingIpxactBusInterface() {
    const QString ipcoreId = QStringLiteral("finepaper.ipxact");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.modules[0].interfaces[0].ipxactBusInterface.clear();

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("ipxact_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("ipxact.bus_interface")) == 1,
            "built-in validation should reject connection-capable interfaces without an IP-XACT bus interface");
    require(hasRule(results, QStringLiteral("built_in_manifest")),
            "missing IP-XACT bus interface should use built_in_manifest");
}

void testBuiltInValidationSkipsIpxactRootForUnusedCatalogPackage() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary package directory");

    const QString usedPackageId = QStringLiteral("finepaper.used_ipxact");
    const QString unusedPackageId = QStringLiteral("finepaper.unused_ipxact");
    IpCatalogEntry usedEntry = ipcraftCatalogEntryWithoutDrc(usedPackageId);
    IpCatalogEntry unusedEntry = ipcraftCatalogEntryWithoutDrc(unusedPackageId);

    IpcraftIpxactDescriptor unusedIpxact;
    unusedIpxact.rootPath = QDir(tempDir.path()).filePath(QStringLiteral("missing_component.xml"));
    unusedIpxact.resolvedRootPath = unusedIpxact.rootPath;
    unusedEntry.packageManifest.ipxact = unusedIpxact;

    Graph graph;
    const QList<IpCatalogEntry> entries{usedEntry, unusedEntry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(usedPackageId, QStringLiteral("used_ipxact_0"))
    };

    IpcraftBuiltInValidator validator;
    const IpcraftBuiltInValidator::Result result =
        validator.validate(&graph,
                           entries,
                           instances,
                           IpcraftBuiltInValidator::CommandPurpose::Validate);

    require(!hasRule(result.diagnostics, QStringLiteral("built_in_ipxact_connection")),
            "built-in validation should not check IP-XACT roots for unused catalog packages");
}

void testBuiltInValidationReportsViewReferenceError() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary package directory");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString viewPath = root.filePath(QStringLiteral("views/Tile.xml"));
    QFile viewFile(viewPath);
    require(viewFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create test view XML");
    viewFile.write(QByteArrayLiteral(R"xml(<module-view schema="v1" module="Tile">
  <anchors><anchor ref="missing" x="0.5" y="0.5"/></anchors>
</module-view>)xml"));
    viewFile.close();

    const QString ipcoreId = QStringLiteral("finepaper.viewed");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.packageRootPath = root.path();
    IpcraftViewDescriptor view;
    view.moduleId = QStringLiteral("Tile");
    view.filePath = QStringLiteral("views/Tile.xml");
    view.resolvedFilePath = viewPath;
    entry.packageManifest.views = {view};

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("viewed_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results, ValidationSeverity::Error, QStringLiteral("View XML anchor references missing interface")) == 1,
            "built-in validation should report invalid view XML interface references");
    require(hasRule(results, QStringLiteral("built_in_view")),
            "invalid view XML should use built_in_view");
}

void testBuiltInValidationReportsViewInterfaceAttributeReferenceError() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary package directory");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString viewPath = root.filePath(QStringLiteral("views/Tile.xml"));
    QFile viewFile(viewPath);
    require(viewFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create test view XML");
    viewFile.write(QByteArrayLiteral(R"xml(<module-view schema="v1" module="Tile">
  <decorations><marker interface="missing" /></decorations>
</module-view>)xml"));
    viewFile.close();

    const QString ipcoreId = QStringLiteral("finepaper.viewed_attr");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.packageRootPath = root.path();
    IpcraftViewDescriptor view;
    view.moduleId = QStringLiteral("Tile");
    view.filePath = QStringLiteral("views/Tile.xml");
    view.resolvedFilePath = viewPath;
    entry.packageManifest.views = {view};

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("viewed_attr_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("View XML interface reference attribute 'interface' references missing interface 'missing'")) == 1,
            "built-in validation should report invalid generic view XML interface references");
    require(hasRule(results, QStringLiteral("built_in_view")),
            "invalid generic view XML interface reference should use built_in_view");
}

void testBuiltInValidationReportsViewAttachmentZoneReferenceError() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary package directory");
    QDir root(tempDir.path());
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString viewPath = root.filePath(QStringLiteral("views/Tile.xml"));
    QFile viewFile(viewPath);
    require(viewFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to create test view XML");
    viewFile.write(QByteArrayLiteral(R"xml(<module-view schema="v1" module="Tile">
  <attachment-zone zone="missing_zone" />
</module-view>)xml"));
    viewFile.close();

    const QString ipcoreId = QStringLiteral("finepaper.viewed_zone");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    IpcraftModuleDescriptor endpoint = manifestModule(QStringLiteral("Endpoint"));
    endpoint.attach = QJsonObject{
        {QStringLiteral("hosts"), QJsonArray{QStringLiteral("Tile")}},
        {QStringLiteral("zone"), QStringLiteral("valid_zone")}
    };
    entry.packageManifest.modules.append(endpoint);
    entry.packageManifest.packageRootPath = root.path();
    IpcraftViewDescriptor view;
    view.moduleId = QStringLiteral("Tile");
    view.filePath = QStringLiteral("views/Tile.xml");
    view.resolvedFilePath = viewPath;
    entry.packageManifest.views = {view};

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("viewed_zone_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("references missing attachment zone 'missing_zone'")) == 1,
            "built-in validation should reject view XML references to undeclared attachment zones");
    require(hasRule(results, QStringLiteral("built_in_view")),
            "invalid attachment zone XML should use built_in_view");
}

void testBuiltInValidationReportsTopologyMetadataReferenceErrors() {
    const QString ipcoreId = QStringLiteral("finepaper.topology");
    IpCatalogEntry entry = ipcraftCatalogEntryWithoutDrc(ipcoreId);
    entry.packageManifest.topologies = {
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("mesh")},
            {QStringLiteral("kind"), QStringLiteral("mesh")},
            {QStringLiteral("module"), QStringLiteral("Tile")},
            {QStringLiteral("connection_class"), QStringLiteral("missing")},
            {QStringLiteral("ports"), QJsonObject{
                {QStringLiteral("east"), QStringLiteral("link")}
            }}
        }
    };

    Graph graph;
    const QList<IpCatalogEntry> entries{entry};
    const QVector<ProjectIpInstanceRecord> instances{
        projectInstanceRecord(ipcoreId, QStringLiteral("topology_0"))
    };

    ProjectValidationRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph, entries, instances);

    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("connection class 'missing'")) == 1,
            "built-in validation should reject topology connection_class values that do not exist");
    require(countResults(results,
                         ValidationSeverity::Error,
                         QStringLiteral("graph_role 'attached' is not host/router-capable")) == 1,
            "built-in validation should reject mesh topologies backed by attached modules");
    require(hasRule(results, QStringLiteral("built_in_topology")),
            "invalid topology metadata should use built_in_topology");
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
        testBuiltInValidationReportsMissingPackage();
        testBuiltInValidationRunsBeforePackageValidate();
        testCommandRunnerRejectsSchemaMismatch();
        testBuiltInValidationReportsMissingInterface();
        testBuiltInValidationReportsUnmappableInterfaceMode();
        testBuiltInValidationReportsUnmappableConnectionClass();
        testBuiltInValidationReportsMissingIpxactBusInterface();
        testBuiltInValidationSkipsIpxactRootForUnusedCatalogPackage();
        testBuiltInValidationReportsViewReferenceError();
        testBuiltInValidationReportsViewInterfaceAttributeReferenceError();
        testBuiltInValidationReportsViewAttachmentZoneReferenceError();
        testBuiltInValidationReportsTopologyMetadataReferenceErrors();
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
