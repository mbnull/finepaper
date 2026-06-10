// ProjectExternalValidationRunner tests package-owned validate flows.
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcraft/schemaids.h"
#include "validation/projectexternalvalidationrunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    require(opened, "failed to create JSON file");
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size(), "failed to write JSON file");
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    require(opened, "failed to create test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

void makeExecutable(const QString& path) {
    QFile file(path);
    require(file.setPermissions(QFile::ReadOwner |
                                QFile::WriteOwner |
                                QFile::ExeOwner |
                                QFile::ReadGroup |
                                QFile::ExeGroup |
                                QFile::ReadOther |
                                QFile::ExeOther),
            "failed to mark test file executable");
}

QJsonArray stringArray(QStringList values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject flowCapture() {
    return QJsonObject{
        {QStringLiteral("stdout"), QStringLiteral("stdout.log")},
        {QStringLiteral("stderr"), QStringLiteral("stderr.log")},
        {QStringLiteral("max_bytes"), 1048576}
    };
}

QJsonObject validateCommand(const QString& executable) {
    return QJsonObject{
        {QStringLiteral("executable"), executable},
        {QStringLiteral("args"), stringArray({QStringLiteral("{inputs.manifest}")})},
        {QStringLiteral("cwd"), QStringLiteral("run_dir")},
        {QStringLiteral("env"), QJsonObject{{QStringLiteral("allow"), QJsonArray{}}}},
        {QStringLiteral("capture"), flowCapture()}
    };
}

QJsonArray validateFlows(const QString& executable) {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("validate")},
            {QStringLiteral("label"), QStringLiteral("Validate")},
            {QStringLiteral("scope"), QStringLiteral("instance")},
            {QStringLiteral("steps"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("kind"), QStringLiteral("emit_inputs")}},
                 QJsonObject{
                     {QStringLiteral("kind"), QStringLiteral("exec")},
                     {QStringLiteral("command"), validateCommand(executable)}
                 }
             }}
        }
    };
}

QString writeValidateScript(QTemporaryDir& tempDir, const QByteArray& body) {
    const QString toolsPath = QDir(tempDir.path()).filePath(QStringLiteral("tools"));
    require(QDir().mkpath(toolsPath), "failed to create tools directory");
    const QString path = QDir(toolsPath).filePath(QStringLiteral("validate.sh"));
    writeFile(path, QByteArrayLiteral("#!/bin/sh\n") + body);
    makeExecutable(path);
    return QStringLiteral("tools/validate.sh");
}

void writePackageSpec(const QString& packageRoot,
                      const QString& packageId,
                      const QJsonArray& flows) {
    QJsonObject spec = {
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), packageId},
        {QStringLiteral("name"), packageId},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), stringArray({QStringLiteral("ipcraft.flows")})}
    };
    if (!flows.isEmpty()) {
        spec.insert(QStringLiteral("flows"), flows);
    }
    writeJsonFile(QDir(packageRoot).filePath(QStringLiteral("ipcraft.json")), spec);
}

IpcraftPackageManifest packageManifest(const QString& packageRoot, const QString& packageId) {
    IpcraftPackageManifest manifest;
    manifest.schema = ipcraft::schemaids::packageV1;
    manifest.id = packageId;
    manifest.name = packageId;
    manifest.version = QStringLiteral("1.0.0");
    manifest.packageRootPath = packageRoot;
    return manifest;
}

IpCatalogEntry catalogEntry(const QString& packageRoot, const QString& packageId) {
    IpCatalogEntry entry;
    entry.id = packageId;
    entry.name = packageId;
    entry.version = QStringLiteral("1.0.0");
    entry.kind = QStringLiteral("noc");
    entry.packageId = packageId;
    entry.packageManifest = packageManifest(packageRoot, packageId);
    entry.sourceRootPath = packageRoot;
    entry.runtimeRootPath = packageRoot;
    return entry;
}

ProjectIpInstanceRecord instanceRecord(const QString& packageId, const QString& instanceId) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = packageId;
    record.instanceId = instanceId;
    record.id = instanceId;
    record.schema = packageId + QStringLiteral("-project-state-v1");
    record.state = QJsonObject{{QStringLiteral("kind"), QStringLiteral("noc")}};
    return record;
}

std::unique_ptr<Module> moduleForInstance(const QString& moduleId,
                                          const QString& packageId,
                                          const QString& instanceId) {
    auto module = std::make_unique<Module>(moduleId, QStringLiteral("Tile"));
    module->setIpcoreId(packageId);
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

ProjectExternalValidationRequest requestFor(Graph& graph,
                                            const QList<IpCatalogEntry>& entries,
                                            const QVector<ProjectIpInstanceRecord>& instances) {
    ProjectExternalValidationRequest request;
    request.graph = &graph;
    request.projectPath = QStringLiteral("/tmp/projectexternalvalidationrunner.fpproj");
    request.designName = QStringLiteral("projectexternalvalidationrunner");
    request.catalogEntries = entries;
    request.instances = instances;
    return request;
}

bool hasMessage(const QList<ValidationResult>& results, const QString& text) {
    for (const ValidationResult& result : results) {
        if (result.message().contains(text)) {
            return true;
        }
    }
    return false;
}

int countMessages(const QList<ValidationResult>& results, const QString& text) {
    int count = 0;
    for (const ValidationResult& result : results) {
        if (result.message().contains(text)) {
            ++count;
        }
    }
    return count;
}

void testMissingValidateFlowWarns() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.no_validate");
    writePackageSpec(tempDir.path(), packageId, QJsonArray{});

    Graph graph;
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_0"), packageId, QStringLiteral("ip0"))),
            "module should add");
    const QVector<ProjectIpInstanceRecord> instances{instanceRecord(packageId, QStringLiteral("ip0"))};

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, instances);
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.size() == 1, "missing validate flow should produce one warning");
    require(results.first().severity() == ValidationSeverity::Warning,
            "missing validate flow should be a warning");
    require(hasMessage(results, QStringLiteral("does not declare a validate flow")),
            "warning should describe missing validate flow");
}

void testStructuredOutputBecomesValidationResult() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.scripted");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("test -f \"${1:?inputs manifest}\"\n"
                                              "echo \"ERROR design: external DRC failed\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath));

    Graph graph;
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_0"), packageId, QStringLiteral("ip0"))),
            "module should add");
    const QVector<ProjectIpInstanceRecord> instances{instanceRecord(packageId, QStringLiteral("ip0"))};

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, instances);
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.size() == 1, "structured error should produce one result");
    require(results.first().severity() == ValidationSeverity::Error,
            "structured ERROR should become an error");
    require(results.first().ruleName() == QStringLiteral("DRC"),
            "structured output should use DRC rule");
    require(results.first().elementId() == QStringLiteral("design"),
            "structured output should preserve element id");
    require(hasMessage(results, QStringLiteral("Instance 'ip0': external DRC failed")),
            "structured output should include instance context");
}

void testStructuredOutputInstanceIdTargetsGraphModule() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.scripted");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("test -f \"${1:?inputs manifest}\"\n"
                                              "echo \"ERROR ip0: external DRC failed\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath));

    Graph graph;
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_0"), packageId, QStringLiteral("ip0"))),
            "module should add");
    const QVector<ProjectIpInstanceRecord> instances{instanceRecord(packageId, QStringLiteral("ip0"))};

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, instances);
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.size() == 1, "structured error should produce one result");
    require(results.first().elementId() == QStringLiteral("tile_0"),
            "structured output instance id should resolve to the graph module id");
    require(hasMessage(results, QStringLiteral("Instance 'ip0': external DRC failed")),
            "structured output should keep instance context in the message");
}

void testBlockingIdsSkipOnlyMatchingInstances() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.blocking");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("echo \"ERROR design: external DRC failed\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath));

    Graph graph;
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_0"), packageId, QStringLiteral("ip0"))),
            "first module should add");
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_1"), packageId, QStringLiteral("ip1"))),
            "second module should add");
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(packageId, QStringLiteral("ip0")),
        instanceRecord(packageId, QStringLiteral("ip1"))
    };

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, instances);
    request.blockingInstanceIds = {QStringLiteral("ip0")};
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(countMessages(results, QStringLiteral("external DRC failed")) == 1,
            "only unblocked instance should run external validation");
    require(!hasMessage(results, QStringLiteral("Instance 'ip0'")),
            "blocked instance should not run external validation");
    require(hasMessage(results, QStringLiteral("Instance 'ip1'")),
            "unblocked instance should run external validation");
}

void testBlockAllSuppressesExternalValidation() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.block_all");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("echo \"ERROR design: should not run\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath));

    Graph graph;
    require(graph.addModule(moduleForInstance(QStringLiteral("tile_0"), packageId, QStringLiteral("ip0"))),
            "module should add");
    const QVector<ProjectIpInstanceRecord> instances{instanceRecord(packageId, QStringLiteral("ip0"))};

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, instances);
    request.blockAllExternalValidation = true;
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.isEmpty(), "blockAllExternalValidation should suppress package DRC");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMissingValidateFlowWarns();
        testStructuredOutputBecomesValidationResult();
        testStructuredOutputInstanceIdTargetsGraphModule();
        testBlockingIdsSkipOnlyMatchingInstances();
        testBlockAllSuppressesExternalValidation();
    } catch (const std::exception& error) {
        std::cerr << "projectexternalvalidationrunner_test failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "projectexternalvalidationrunner_test passed\n";
    return 0;
}
