// ProjectExternalValidationRunner tests package-owned validate flows.
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcraft/core/project_design.h"
#include "ipcraft/schemaids.h"
#include "project/ipinstancestate.h"
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

QJsonArray inputCaptureEmitters() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("parameters")},
            {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
            {QStringLiteral("path"), QStringLiteral("parameters.json")}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("graph_config")},
            {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
            {QStringLiteral("path"), QStringLiteral("graph_config.json")}
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
                      const QJsonArray& flows,
                      const QJsonArray& emitters = QJsonArray{}) {
    QStringList extensions{QStringLiteral("ipcraft.flows")};
    if (!emitters.isEmpty()) {
        extensions.append(QStringLiteral("ipcraft.emitters"));
    }

    QJsonObject spec = {
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), packageId},
        {QStringLiteral("name"), packageId},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("extensions"), stringArray(extensions)}
    };
    if (!flows.isEmpty()) {
        spec.insert(QStringLiteral("flows"), flows);
    }
    if (!emitters.isEmpty()) {
        spec.insert(QStringLiteral("emitters"), emitters);
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

QJsonObject graphConfigForObject(const QString& objectId) {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), objectId},
                 {QStringLiteral("type"), QStringLiteral("component")},
                 {QStringLiteral("properties"), QJsonObject{}}
             }
         }},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{}}
    };
}

ipcraft::core::ProjectDesign projectDesignFor(const QString& designName,
                                              const QVector<ProjectIpInstanceRecord>& instances) {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = designName + QStringLiteral("_id");
    design.name = designName;

    QStringList packageKeys;
    for (const ProjectIpInstanceRecord& instance : instances) {
        const QString packageKey = instance.ipcoreId + QStringLiteral("@1.0.0");
        if (!packageKeys.contains(packageKey)) {
            packageKeys.append(packageKey);
            design.packages.append(ipcraft::core::PackageRef{instance.ipcoreId,
                                                             QStringLiteral("1.0.0")});
        }
        ipcraft::core::ComponentInstance component;
        component.id = instance.instanceId;
        component.packageRef = packageKey;
        component.config = instance.config;
        if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
            component.extensionData.insert(QStringLiteral("graph_config"), instance.graphConfig);
        }
        design.components.append(component);
    }

    return design;
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
                                            const ipcraft::core::ProjectDesign& design) {
    ProjectExternalValidationRequest request;
    request.projectDesign = &design;
    request.graph = &graph;
    request.projectPath = QStringLiteral("/tmp/projectexternalvalidationrunner.fpproj");
    request.designName = QStringLiteral("projectexternalvalidationrunner");
    request.catalogEntries = entries;
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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("missing_validate_flow"), instances);

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, design);
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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("structured_output"), instances);

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, design);
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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("structured_output_target"), instances);

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, design);
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.size() == 1, "structured error should produce one result");
    require(results.first().elementId() == QStringLiteral("tile_0"),
            "structured output instance id should resolve to the graph module id");
    require(hasMessage(results, QStringLiteral("Instance 'ip0': external DRC failed")),
            "structured output should keep instance context in the message");
}

void testPackageValidateRunsWithProjectDesignAndNoGraph() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.graph_free_validate");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("test -f \"${1:?inputs manifest}\"\n"
                                              "echo \"ERROR graph_free_0: graph-free DRC failed\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath));
    const QVector<ProjectIpInstanceRecord> instances{
        instanceRecord(packageId, QStringLiteral("graph_free_0"))
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("graph_free_validate"), instances);

    ProjectExternalValidationRequest request;
    request.projectDesign = &design;
    request.projectPath = QStringLiteral("/tmp/projectexternalvalidationrunner.fpproj");
    request.designName = QStringLiteral("graph_free_validate");
    request.catalogEntries = {catalogEntry(tempDir.path(), packageId)};
    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(results.size() == 1,
            "project-design validate flow should run without a graph");
    require(results.first().severity() == ValidationSeverity::Error,
            "scripted ERROR should remain an error without graph diagnostics");
    require(results.first().elementId() == QStringLiteral("graph_free_0"),
            "without graph, diagnostics should keep the package instance id");
    require(hasMessage(results, QStringLiteral("graph-free DRC failed")),
            "graph-free package validate output should be captured");
}

void testProjectDesignComponentRunsValidationWhenInstancesAreEmpty() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.design_only_validate");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("inputs_dir=$(dirname \"$1\")\n"
                                              "if ! grep -q 'derived-marker' \"$inputs_dir/parameters.json\"; then\n"
                                              "  echo \"ERROR design_only_0: derived config missing\"\n"
                                              "  exit 0\n"
                                              "fi\n"
                                              "echo \"ERROR design_only_0: design-only validate ran\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath), inputCaptureEmitters());

    ProjectIpInstanceRecord designOnly = instanceRecord(packageId, QStringLiteral("design_only_0"));
    designOnly.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("marker"), QStringLiteral("derived-marker")}}}
    };
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("design_only_validate"), {designOnly});

    ProjectExternalValidationRequest request;
    request.projectDesign = &design;
    request.projectPath = QStringLiteral("/tmp/projectexternalvalidationrunner.fpproj");
    request.designName = QStringLiteral("design_only_validate");
    request.catalogEntries = {catalogEntry(tempDir.path(), packageId)};

    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(hasMessage(results, QStringLiteral("design-only validate ran")),
            "project design component should run validate flow when instance records are empty");
}

void testProjectDesignGraphConfigFeedsExternalValidation() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create package root");
    const QString packageId = QStringLiteral("finepaper.design_graph_validate");
    const QString scriptPath =
        writeValidateScript(tempDir,
                            QByteArrayLiteral("inputs_dir=$(dirname \"$1\")\n"
                                              "if grep -q 'design-marker' \"$inputs_dir/parameters.json\" && grep -q 'design_graph_object' \"$inputs_dir/graph_config.json\"; then\n"
                                              "  echo \"ERROR design_graph_0: project design graph config preserved\"\n"
                                              "  exit 0\n"
                                              "fi\n"
                                              "if grep -q 'derived-marker' \"$inputs_dir/parameters.json\"; then\n"
                                              "  echo \"ERROR design_only_0: design-only validate ran\"\n"
                                              "  exit 0\n"
                                              "fi\n"
                                              "echo \"ERROR design: unrecognized validation inputs\"\n"
                                              "exit 0\n"));
    writePackageSpec(tempDir.path(), packageId, validateFlows(scriptPath), inputCaptureEmitters());

    ProjectIpInstanceRecord designOnly = instanceRecord(packageId, QStringLiteral("design_only_0"));
    designOnly.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("marker"), QStringLiteral("derived-marker")}}}
    };

    ProjectIpInstanceRecord designSideExplicit =
        instanceRecord(packageId, QStringLiteral("design_graph_0"));
    designSideExplicit.config = QJsonObject{
        {QStringLiteral("parameters"),
         QJsonObject{{QStringLiteral("marker"), QStringLiteral("design-marker")}}}
    };
    designSideExplicit.hasGraphConfig = true;
    designSideExplicit.graphConfig = graphConfigForObject(QStringLiteral("design_graph_object"));

    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("design_graph_validate"), {designOnly, designSideExplicit});

    ProjectExternalValidationRequest request;
    request.projectDesign = &design;
    request.projectPath = QStringLiteral("/tmp/projectexternalvalidationrunner.fpproj");
    request.designName = QStringLiteral("design_graph_validate");
    request.catalogEntries = {catalogEntry(tempDir.path(), packageId)};

    const QList<ValidationResult> results = ProjectExternalValidationRunner().validate(request);

    require(hasMessage(results, QStringLiteral("project design graph config preserved")),
            "ProjectDesign config and graph_config should feed external validation");
    require(hasMessage(results, QStringLiteral("design-only validate ran")),
            "each project design component should be derived and validated");
    require(!hasMessage(results, QStringLiteral("unrecognized validation inputs")),
            "validate flow should receive ProjectDesign-derived inputs");
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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("blocking"), instances);

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, design);
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
    const ipcraft::core::ProjectDesign design =
        projectDesignFor(QStringLiteral("block_all"), instances);

    ProjectExternalValidationRequest request =
        requestFor(graph, {catalogEntry(tempDir.path(), packageId)}, design);
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
        testPackageValidateRunsWithProjectDesignAndNoGraph();
        testProjectDesignComponentRunsValidationWhenInstancesAreEmpty();
        testProjectDesignGraphConfigFeedsExternalValidation();
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
