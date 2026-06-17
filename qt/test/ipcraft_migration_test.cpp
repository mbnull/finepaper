// Ipcraft explicit project migration contract smoke tests.
#include "ipcraft/migration.h"
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"
#include "project/projectwriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString writeJson(const QString& path, const QJsonObject& object) {
    QFile file(path);
    bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    require(opened, "fixture should open for writing");
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size(), "fixture should write");
    return path;
}

QString writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    require(opened, "fixture should open for writing");
    require(file.write(content) == content.size(), "fixture should write");
    return path;
}

QJsonObject legacyProject() {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("kind"), QStringLiteral("finepaper-project")},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Legacy Project")},
            {QStringLiteral("version"), QStringLiteral("0.9")}
        }},
        {QStringLiteral("ipcores"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("vendor.example.legacy")},
                        {QStringLiteral("version"), QStringLiteral("1.2.3")}}
        }},
        {QStringLiteral("ipcore_state"), QJsonArray{
            QJsonObject{
                {QStringLiteral("ipcore"), QStringLiteral("vendor.example.legacy")},
                {QStringLiteral("instance"), QStringLiteral("legacy_0")},
                {QStringLiteral("schema"), QStringLiteral("ipcraft.noc.instance-state.v1")},
                {QStringLiteral("state"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("noc")},
                    {QStringLiteral("type"), QStringLiteral("Legacy IP")},
                    {QStringLiteral("global_parameters"), QJsonObject{
                        {QStringLiteral("width"), 32}
                    }}
                }}
            }
        }},
        {QStringLiteral("graph"), QJsonObject{
            {QStringLiteral("modules"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("tile_0")},
                    {QStringLiteral("ipcore"), QStringLiteral("vendor.example.legacy")},
                    {QStringLiteral("instance"), QStringLiteral("legacy_0")},
                    {QStringLiteral("type"), QStringLiteral("Tile")},
                    {QStringLiteral("parameters"), QJsonObject{
                        {QStringLiteral("x"), 11},
                        {QStringLiteral("y"), 22},
                        {QStringLiteral("collapsed"), true},
                        {QStringLiteral("routing"), QStringLiteral("xy")}
                    }}
                }
            }},
            {QStringLiteral("connections"), QJsonArray{}}
        }}
    };
}

QJsonObject legacyGraphConnectionProject() {
    QJsonObject project = legacyProject();
    QJsonObject graph = project.value(QStringLiteral("graph")).toObject();
    graph.insert(QStringLiteral("modules"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("producer")},
            {QStringLiteral("ipcore"), QStringLiteral("vendor.example.legacy")},
            {QStringLiteral("instance"), QStringLiteral("legacy_0")},
            {QStringLiteral("type"), QStringLiteral("Producer")},
            {QStringLiteral("parameters"), QJsonObject{
                {QStringLiteral("x"), 1},
                {QStringLiteral("y"), 2}
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("consumer")},
            {QStringLiteral("ipcore"), QStringLiteral("vendor.example.legacy")},
            {QStringLiteral("instance"), QStringLiteral("legacy_0")},
            {QStringLiteral("type"), QStringLiteral("Consumer")},
            {QStringLiteral("parameters"), QJsonObject{
                {QStringLiteral("x"), 3},
                {QStringLiteral("y"), 4}
            }}
        }
    });
    graph.insert(QStringLiteral("connections"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("link0")},
            {QStringLiteral("source"), QJsonObject{
                {QStringLiteral("module"), QStringLiteral("producer")},
                {QStringLiteral("port"), QStringLiteral("out")}
            }},
            {QStringLiteral("target"), QJsonObject{
                {QStringLiteral("module"), QStringLiteral("consumer")},
                {QStringLiteral("port"), QStringLiteral("in")}
            }}
        }
    });
    project.insert(QStringLiteral("graph"), graph);
    return project;
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

QJsonObject migrationMetadata(const QJsonObject& flatProject) {
    return flatProject.value(QStringLiteral("metadata")).toObject()
        .value(QStringLiteral("ipcraft.migration.v1")).toObject();
}

QJsonObject preservedLegacyState(const QJsonObject& flatProject) {
    return migrationMetadata(flatProject)
        .value(QStringLiteral("preserved")).toObject()
        .value(QStringLiteral("legacy_state")).toObject();
}

QString findCliBinary() {
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        const QString candidate = dir.filePath(QStringLiteral("ipcraft-cli"));
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ipcraft-cli");
}

QJsonObject runCliJson(const QStringList& arguments, int* exitCode) {
    QProcess process;
    process.start(findCliBinary(), arguments);
    require(process.waitForStarted(), "ipcraft-cli should start");
    require(process.waitForFinished(10000), "ipcraft-cli should finish");
    *exitCode = process.exitCode();
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
    require(document.isObject(), "ipcraft-cli should output JSON object");
    return document.object();
}

void testNormalProjectLoadRejectsOldFinepaperSchema() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");
    const QString path = writeJson(QDir(temp.path()).filePath(QStringLiteral("old.fpproj")),
                                   legacyProject());

    const ProjectReadResult result = ProjectReader::readFile(path);
    require(!result.success, "normal project load must reject old finepaper schema");
    require(hasRule(result.diagnostics, QStringLiteral("project.unsupported_schema")),
            "normal load should report project.unsupported_schema");
}

void testMigrateProjectRequiresToProjectV1() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");
    const QString path = writeJson(QDir(temp.path()).filePath(QStringLiteral("old.fpproj")),
                                   legacyProject());

    int exitCode = 0;
    const QJsonObject result = runCliJson({QStringLiteral("migrate-project"), path}, &exitCode);
    require(exitCode != 0, "migrate-project should require --to");
    require(result.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::cliResultV1,
            "migration error should use CLI result schema");
    const ipcraft::DiagnosticStore diagnostics = ipcraft::DiagnosticStore::fromJson(
        result.value(QStringLiteral("diagnostics")).toObject());
    require(hasRule(diagnostics, QStringLiteral("migration.target_required")),
            "missing migration target should report migration.target_required");
}

void testMigrateProjectNormalizesInputDiagnostics() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");
    QDir dir(temp.path());

    int exitCode = 0;
    QJsonObject result = runCliJson({QStringLiteral("migrate-project"),
                                     dir.filePath(QStringLiteral("missing.fpproj")),
                                     QStringLiteral("--to"),
                                     ipcraft::schemaids::projectV1},
                                    &exitCode);
    require(exitCode != 0, "missing migration input should fail");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.input_missing")),
            "missing migration input should emit migration.input_missing");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.read_failed")),
            "missing migration input should also emit migration.read_failed alias");

    const QString malformedPath = writeFile(dir.filePath(QStringLiteral("malformed.fpproj")),
                                            QByteArrayLiteral("{ not-json"));
    result = runCliJson({QStringLiteral("migrate-project"),
                         malformedPath,
                         QStringLiteral("--to"),
                         ipcraft::schemaids::projectV1},
                        &exitCode);
    require(exitCode != 0, "malformed migration input should fail");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.invalid_input")),
            "malformed migration input should emit migration.invalid_input");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.invalid_json")),
            "malformed migration input should also emit migration.invalid_json alias");
}

void testMigrateProjectRejectsUnsupportedSchemaWithMigrationRule() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");
    const QString path = writeJson(QDir(temp.path()).filePath(QStringLiteral("unsupported.fpproj")),
                                   QJsonObject{{QStringLiteral("schema"), QStringLiteral("vendor.other")}});

    int exitCode = 0;
    const QJsonObject result = runCliJson({QStringLiteral("migrate-project"),
                                           path,
                                           QStringLiteral("--to"),
                                           ipcraft::schemaids::projectV1},
                                          &exitCode);
    require(exitCode != 0, "unsupported migration schema should fail");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.unsupported_schema")),
            "unsupported migration schema should emit migration.unsupported_schema");
}

void testMigrateProjectRejectsUnsupportedTarget() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");
    const QString path = writeJson(QDir(temp.path()).filePath(QStringLiteral("old.fpproj")),
                                   legacyProject());

    int exitCode = 0;
    const QJsonObject result = runCliJson({QStringLiteral("migrate-project"),
                                           path,
                                           QStringLiteral("--to"),
                                           QStringLiteral("ipcraft.project.v2")},
                                          &exitCode);
    require(exitCode != 0, "unsupported migration target should fail");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.unsupported_target")),
            "unsupported migration target should emit migration.unsupported_target");
}

void testMigrateProjectRejectsDirectoryInputWithoutOpening() {
    QTemporaryDir temp;
    require(temp.isValid(), "temp dir should be valid");

    int exitCode = 0;
    const QJsonObject result = runCliJson({QStringLiteral("migrate-project"),
                                           temp.path(),
                                           QStringLiteral("--to"),
                                           ipcraft::schemaids::projectV1},
                                          &exitCode);
    require(exitCode != 0, "directory migration input should fail");
    require(hasRule(ipcraft::DiagnosticStore::fromJson(
                        result.value(QStringLiteral("diagnostics")).toObject()),
                    QStringLiteral("migration.invalid_input")),
            "non-file migration input should emit migration.invalid_input");
}

void testMigratePreservesOpaqueLegacyStateWithoutInstances() {
    QJsonObject legacy = {
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("project"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Opaque Legacy")}}},
        {QStringLiteral("ipcore_state"), QJsonArray{
            QJsonObject{
                {QStringLiteral("schema"), QStringLiteral("ipcraft.noc.instance-state.v1")},
                {QStringLiteral("opaque"), true}
            }
        }}
    };

    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacy, ipcraft::schemaids::projectV1);
    require(result.ok, "opaque legacy state should migrate even when no instance can be reconstructed");
    require(result.document.instances.isEmpty(),
            "opaque-only legacy state should not synthesize incomplete instances");
    const QJsonObject preserved =
        preservedLegacyState(ProjectWriter::toJsonObject(result.document));
    require(preserved.value(QStringLiteral("ipcore_state")).toArray().first().toObject()
                .value(QStringLiteral("opaque")).toBool(),
            "opaque legacy state should be preserved");
}

void testMigratePreservesTopLevelLegacyState() {
    QJsonObject legacy = {
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("project"), QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy")},
                                                {QStringLiteral("name"), QStringLiteral("Legacy")}}},
        {QStringLiteral("legacy_state"), QJsonObject{{QStringLiteral("opaque"), true}}}
    };

    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacy, ipcraft::schemaids::projectV1);
    require(result.ok, "top-level opaque legacy state should migrate");
    const QJsonObject preserved =
        preservedLegacyState(ProjectWriter::toJsonObject(result.document));
    require(preserved.value(QStringLiteral("opaque")).toBool(),
            "top-level legacy_state object should be preserved under flat migration metadata");
}

void testMigratePreservesOldIpcoreStateUnderMigrationPreserved() {
    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacyProject(), ipcraft::schemaids::projectV1);
    require(result.ok, "legacy project should migrate");

    const QJsonObject migrated = ProjectWriter::toJsonObject(result.document);
    require(migrated.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::projectV1,
            "migrated document should use project v1 schema");
    require(!migrated.contains(QStringLiteral("migration")),
            "migrated public project should not emit wrapper migration root");
    const QJsonObject preserved = preservedLegacyState(migrated);
    require(preserved.value(QStringLiteral("ipcore_state")).isArray(),
            "migration should preserve old ipcore_state under flat migration metadata");
    require(preserved.value(QStringLiteral("ipcore_state")).toArray().first().toObject()
                .value(QStringLiteral("schema")).toString() ==
                QStringLiteral("ipcraft.noc.instance-state.v1"),
            "migration should preserve old instance-state schema opaquely");
}

void testMigrateMovesXYCollapsedIntoLayout() {
    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacyProject(), ipcraft::schemaids::projectV1);
    require(result.ok, "legacy project should migrate");

    const QJsonObject graphView =
        result.document.layout.value(QStringLiteral("views")).toArray().first().toObject();
    require(graphView.value(QStringLiteral("id")).toString() == QStringLiteral("graph"),
            "legacy graph layout should migrate under the graph view id");
    const QJsonObject node = graphView.value(QStringLiteral("canvas")).toObject()
                                 .value(QStringLiteral("nodes")).toObject()
                                 .value(QStringLiteral("tile_0")).toObject();
    require(node.value(QStringLiteral("x")).toInt() == 11,
            "legacy x should migrate to layout");
    require(node.value(QStringLiteral("y")).toInt() == 22,
            "legacy y should migrate to layout");
    require(node.value(QStringLiteral("collapsed")).toBool(),
            "legacy collapsed should migrate to layout");
    const QJsonObject parameters =
        result.document.instances.first().config.value(QStringLiteral("parameters")).toObject();
    require(!parameters.contains(QStringLiteral("x")) &&
                !parameters.contains(QStringLiteral("y")) &&
                !parameters.contains(QStringLiteral("collapsed")),
            "layout keys should not remain config parameters");
    require(parameters.value(QStringLiteral("routing")).toString() == QStringLiteral("xy"),
            "non-layout module parameter should migrate when mapping is clear");
}

void testMigrateSameInstanceGraphConnectionIntoGraphConfig() {
    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacyGraphConnectionProject(),
                                              ipcraft::schemaids::projectV1);
    require(result.ok, "same-instance legacy graph connection should migrate");

    const QJsonObject graphConfig = result.document.instances.first().graphConfig;
    require(result.document.instances.first().hasGraphConfig,
            "legacy graph should migrate into instance graph_config");
    require(graphConfig.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::graphConfigV1,
            "migrated graph_config should use graph-config v1 schema");
    require(graphConfig.value(QStringLiteral("objects")).toArray().size() == 2,
            "legacy modules should migrate into graph_config objects");
    const QJsonObject relationship =
        graphConfig.value(QStringLiteral("relationships")).toArray().first().toObject();
    require(relationship.value(QStringLiteral("endpoints")).toArray().size() == 2,
            "legacy connection should migrate into n-ary endpoint array");
    require(!relationship.contains(QStringLiteral("source")) &&
                !relationship.contains(QStringLiteral("target")),
            "migrated graph_config must not expose old source/target fields");
}

void testUnsupportedLegacyContentReportsDiagnostic() {
    QJsonObject legacy = legacyProject();
    QJsonObject graph = legacy.value(QStringLiteral("graph")).toObject();
    graph.insert(QStringLiteral("modules"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("orphan")},
            {QStringLiteral("ipcore"), QStringLiteral("vendor.example.missing")},
            {QStringLiteral("instance"), QStringLiteral("missing_0")},
            {QStringLiteral("type"), QStringLiteral("Tile")},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("width"), 64}}}
        }
    });
    legacy.insert(QStringLiteral("graph"), graph);

    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacy, ipcraft::schemaids::projectV1);
    require(!result.ok, "unsupported legacy module ownership should fail migration");
    require(hasRule(result.diagnostics, QStringLiteral("migration.unsupported_legacy_content")),
            "unsupported legacy content should report stable migration diagnostic");
}

void testUnsupportedLegacyRootFieldReportsDiagnostic() {
    QJsonObject legacy = {
        {QStringLiteral("schema"), QStringLiteral("v1")},
        {QStringLiteral("project"), QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy")},
                                                {QStringLiteral("name"), QStringLiteral("Legacy")}}},
        {QStringLiteral("unsupported_legacy_content"), QJsonObject{{QStringLiteral("x"), true}}}
    };

    const ipcraft::ProjectMigrationResult result =
        ipcraft::ProjectMigrator::migrateJson(legacy, ipcraft::schemaids::projectV1);
    require(!result.ok, "unsupported legacy root field should fail migration");
    require(hasRule(result.diagnostics, QStringLiteral("migration.unsupported_legacy_content")),
            "unsupported legacy root field should report stable migration diagnostic");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testNormalProjectLoadRejectsOldFinepaperSchema();
        testMigrateProjectRequiresToProjectV1();
        testMigrateProjectNormalizesInputDiagnostics();
        testMigrateProjectRejectsUnsupportedSchemaWithMigrationRule();
        testMigrateProjectRejectsUnsupportedTarget();
        testMigrateProjectRejectsDirectoryInputWithoutOpening();
        testMigratePreservesOpaqueLegacyStateWithoutInstances();
        testMigratePreservesTopLevelLegacyState();
        testMigratePreservesOldIpcoreStateUnderMigrationPreserved();
        testMigrateMovesXYCollapsedIntoLayout();
        testMigrateSameInstanceGraphConnectionIntoGraphConfig();
        testUnsupportedLegacyContentReportsDiagnostic();
        testUnsupportedLegacyRootFieldReportsDiagnostic();
    } catch (const std::exception& error) {
        std::cerr << "ipcraft_migration_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_migration_test passed\n";
    return 0;
}
