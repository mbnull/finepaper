// Hard-cutover architecture gate for the public ipcraft v1 contract.
#include "ipcraft/ipcraftmanifestreader.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toLocal8Bit().constData());
    }
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

QString repositoryRootPath() {
    QDir root(QFileInfo(repositoryPath(QStringLiteral("qt/test/v1architecturegate_test.cpp")))
                  .absoluteDir());
    require(root.cdUp() && root.cdUp(),
            "repository root should be reachable from architecture gate source");
    return root.absolutePath();
}

QString relativeRepositoryPath(const QString& absolutePath) {
    return QDir(repositoryRootPath()).relativeFilePath(absolutePath);
}

QString dotted(std::initializer_list<const char*> parts) {
    QStringList values;
    for (const char* part : parts) {
        values.append(QString::fromLatin1(part));
    }
    return values.join(QLatin1Char('.'));
}

QString projectSchemaName() {
    return QStringLiteral("ipcraft.project.v1");
}

QString packageSchemaName() {
    return QStringLiteral("ipcraft.package.v1");
}

QString emittedInputsSchemaName() {
    return QStringLiteral("ipcraft.emitted-inputs.v1");
}

QString cliResultSchemaName() {
    return QStringLiteral("ipcraft.cli.result.v1");
}

QString diagnosticsSchemaName() {
    return QStringLiteral("ipcraft.diagnostics.v1");
}

QString graphConfigSchemaName() {
    return QStringLiteral("ipcraft.graph-config.v1");
}

QString oldNocProjectSchemaName() {
    return dotted({"ipcraft", "noc", "project", "v1"});
}

QString oldNocInstanceStateSchemaName() {
    return dotted({"ipcraft", "noc", "instance-state", "v1"});
}

QString oldManifestSchemaName() {
    return dotted({"ipcraft", "manifest", "v1"});
}

QString authoringYamlFileName() {
    return QStringLiteral("ipcore") + QStringLiteral(".yml");
}

QString readTextFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),
            QStringLiteral("file should be readable: %1").arg(path));
    return QString::fromUtf8(file.readAll());
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("failed to open test file: %1").arg(path));
    require(file.write(content) == content.size(),
            QStringLiteral("failed to write test file: %1").arg(path));
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),
            QStringLiteral("JSON file should be readable: %1").arg(path));
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError,
            QStringLiteral("JSON file should parse: %1: %2")
                .arg(path, error.errorString()));
    require(document.isObject(),
            QStringLiteral("JSON file should contain an object: %1").arg(path));
    return document.object();
}

QStringList pathSegments(const QString& relativePath) {
    return relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool pathHasSegmentContaining(const QString& relativePath, const QString& token) {
    const QString foldedToken = token.toCaseFolded();
    for (const QString& segment : pathSegments(relativePath)) {
        if (segment.toCaseFolded().contains(foldedToken)) {
            return true;
        }
    }
    return false;
}

bool isLegacyOrMigrationOnlyPath(const QString& relativePath) {
    if (relativePath.startsWith(QStringLiteral("docs/superpowers/"))) {
        return true;
    }

    return pathHasSegmentContaining(relativePath, QStringLiteral("migration")) ||
           pathHasSegmentContaining(relativePath, QStringLiteral("migrate")) ||
           pathHasSegmentContaining(relativePath, QStringLiteral("legacy")) ||
           pathHasSegmentContaining(relativePath, QStringLiteral("archive")) ||
           pathHasSegmentContaining(relativePath, QStringLiteral("history"));
}

bool hasMigrationOnlyMarker(const QString& source) {
    return source.contains(QStringLiteral(
        "Migration-only legacy schema handling. Not used by normal runtime loading."));
}

bool isSkippedRepositoryFile(const QString& relativePath) {
    const QString normalized = QDir::cleanPath(relativePath);
    return normalized.startsWith(QStringLiteral(".git/")) ||
           normalized.startsWith(QStringLiteral(".codex/")) ||
           normalized.startsWith(QStringLiteral(".worktrees/")) ||
           normalized.startsWith(QStringLiteral(".xmake/")) ||
           normalized.startsWith(QStringLiteral("build/")) ||
           normalized.startsWith(QStringLiteral("qt/build/")) ||
           normalized.contains(QStringLiteral("/build/")) ||
           normalized.contains(QStringLiteral("/.gens/")) ||
           normalized.contains(QStringLiteral("/.objs/")) ||
           normalized.contains(QStringLiteral("/.deps/"));
}

bool isTextContractFile(const QString& relativePath) {
    const QFileInfo info(relativePath);
    const QString suffix = info.suffix().toCaseFolded();
    static const QSet<QString> suffixes = {
        QStringLiteral("cpp"),
        QStringLiteral("h"),
        QStringLiteral("hpp"),
        QStringLiteral("c"),
        QStringLiteral("cc"),
        QStringLiteral("rb"),
        QStringLiteral("py"),
        QStringLiteral("md"),
        QStringLiteral("json"),
        QStringLiteral("yml"),
        QStringLiteral("yaml"),
        QStringLiteral("txt"),
        QStringLiteral("lua"),
        QStringLiteral("xml"),
        QStringLiteral("ui"),
        QStringLiteral("qrc")
    };

    if (suffixes.contains(suffix)) {
        return true;
    }

    const QString fileName = info.fileName();
    return fileName == QStringLiteral("spec-gen") ||
           fileName == QStringLiteral("ipcraft-generate") ||
           fileName == QStringLiteral("xmake.lua");
}

bool isPublicOrRuntimeContractPath(const QString& relativePath) {
    if (relativePath.startsWith(QStringLiteral("qt/test/")) ||
        relativePath.startsWith(QStringLiteral("qt/doc/")) ||
        relativePath.contains(QStringLiteral("/test/"))) {
        return false;
    }

    return relativePath.startsWith(QStringLiteral("qt/inc/")) ||
           relativePath.startsWith(QStringLiteral("qt/src/")) ||
           relativePath.startsWith(QStringLiteral("qt/cli/")) ||
           relativePath.startsWith(QStringLiteral("spec_generator/lib/")) ||
           relativePath.startsWith(QStringLiteral("spec_generator/bin/")) ||
           relativePath.startsWith(QStringLiteral("ipcraft_generator/")) ||
           relativePath.startsWith(QStringLiteral("ipcores/")) ||
           relativePath.startsWith(QStringLiteral("schemas/")) ||
           relativePath.startsWith(QStringLiteral("examples/contracts/")) ||
           relativePath.startsWith(QStringLiteral("docs/architecture/")) ||
           relativePath.startsWith(QStringLiteral("docs/audit/")) ||
           relativePath == QStringLiteral("qt/xmake.lua");
}

QVector<QFileInfo> repositoryTextFiles() {
    QVector<QFileInfo> files;
    const QString root = repositoryRootPath();
    QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relativePath = QDir(root).relativeFilePath(info.absoluteFilePath());
        if (isSkippedRepositoryFile(relativePath) || !isTextContractFile(relativePath)) {
            continue;
        }
        files.push_back(info);
    }
    return files;
}

QVector<QFileInfo> repositoryFilesUnder(const QString& relativeRootPath) {
    QVector<QFileInfo> files;
    const QFileInfo rootInfo(repositoryPath(relativeRootPath));
    if (!rootInfo.exists()) {
        return files;
    }
    if (rootInfo.isFile()) {
        files.push_back(rootInfo);
        return files;
    }

    QDirIterator iterator(rootInfo.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        files.push_back(iterator.fileInfo());
    }
    return files;
}

QString summarizeViolations(const QStringList& violations, int limit = 80) {
    if (violations.isEmpty()) {
        return {};
    }

    QStringList summary;
    const int count = std::min(limit, static_cast<int>(violations.size()));
    for (int index = 0; index < count; ++index) {
        summary.append(violations.at(index));
    }
    if (violations.size() > limit) {
        summary.append(QStringLiteral("... %1 more").arg(violations.size() - limit));
    }
    return summary.join(QLatin1Char('\n'));
}

void failIfViolations(const QString& message, const QStringList& violations) {
    if (!violations.isEmpty()) {
        throw std::runtime_error(QStringLiteral("%1\n%2")
                                     .arg(message, summarizeViolations(violations))
                                     .toLocal8Bit()
                                     .constData());
    }
}

QStringList requiredSchemaFiles() {
    return {
        QStringLiteral("schemas/ipcraft.project.v1.schema.json"),
        QStringLiteral("schemas/ipcraft.package.v1.schema.json"),
        QStringLiteral("schemas/ipcraft.diagnostics.v1.schema.json"),
        QStringLiteral("schemas/ipcraft.graph-config.v1.schema.json"),
        QStringLiteral("schemas/ipcraft.emitted-inputs.v1.schema.json"),
        QStringLiteral("schemas/ipcraft.cli.result.v1.schema.json")
    };
}

QStringList requiredAuditDocFiles() {
    return {
        QStringLiteral("docs/architecture/v1-core-architecture.md"),
        QStringLiteral("docs/audit/black-box-audit-guide.md"),
        QStringLiteral("docs/audit/coverage-matrix.md"),
        QStringLiteral("docs/audit/failure-report-format.md"),
        QStringLiteral("docs/audit/rule-id-catalog.md")
    };
}

QStringList requiredCliCommands() {
    return {
        QStringLiteral("inspect-project"),
        QStringLiteral("validate-project"),
        QStringLiteral("emit-inputs"),
        QStringLiteral("run-flow"),
        QStringLiteral("migrate-project"),
        QStringLiteral("collect-artifacts")
    };
}

QStringList requiredPublicRuleIds() {
    return {
        QStringLiteral("project.unsupported_schema"),
        QStringLiteral("project.duplicate_id"),
        QStringLiteral("project.unknown_package"),
        QStringLiteral("project.unknown_instance"),
        QStringLiteral("project.config_invalid"),
        QStringLiteral("package.unsupported_schema"),
        QStringLiteral("package.duplicate_id"),
        QStringLiteral("package.extension_required"),
        QStringLiteral("package.unknown_extension"),
        QStringLiteral("package.path_escape"),
        QStringLiteral("composition.unknown_instance"),
        QStringLiteral("composition.unknown_interface"),
        QStringLiteral("composition.unknown_connection_class"),
        QStringLiteral("composition.role_mismatch"),
        QStringLiteral("composition.multiply_driven_input"),
        QStringLiteral("composition.clock_reset_source_count"),
        QStringLiteral("composition.incompatible_endpoint"),
        QStringLiteral("graph_config.duplicate_object"),
        QStringLiteral("graph_config.unknown_endpoint_object"),
        QStringLiteral("emitter.path_absolute"),
        QStringLiteral("emitter.path_escape"),
        QStringLiteral("emitter.write_failed"),
        QStringLiteral("flow.executable_missing"),
        QStringLiteral("flow.exec_failed"),
        QStringLiteral("flow.timeout"),
        QStringLiteral("flow.command_policy_violation"),
        QStringLiteral("flow.output_truncated"),
        QStringLiteral("artifact.glob_escape"),
        QStringLiteral("artifact.required_missing"),
        QStringLiteral("cli.unknown_command"),
        QStringLiteral("cli.missing_argument"),
        QStringLiteral("cli.argument_conflict"),
        QStringLiteral("cli.instance_scope_required"),
        QStringLiteral("migration.target_required"),
        QStringLiteral("migration.unsupported_legacy_content")
    };
}

void testNoNormalRuntimePathUsesNocProjectV1() {
    const QStringList forbiddenTokens = {
        oldNocProjectSchemaName(),
        oldNocInstanceStateSchemaName(),
        oldManifestSchemaName()
    };

    QStringList violations;
    for (const QFileInfo& fileInfo : repositoryTextFiles()) {
        const QString relativePath = relativeRepositoryPath(fileInfo.absoluteFilePath());
        if (!isPublicOrRuntimeContractPath(relativePath)) {
            continue;
        }

        const QString source = readTextFile(fileInfo.absoluteFilePath());
        if (isLegacyOrMigrationOnlyPath(relativePath) || hasMigrationOnlyMarker(source)) {
            continue;
        }

        for (const QString& token : forbiddenTokens) {
            if (source.contains(token)) {
                violations.append(QStringLiteral("%1 contains %2")
                                      .arg(relativePath)
                                      .arg(token));
            }
        }
    }

    failIfViolations(
        QStringLiteral("old ipcraft NoC/manifest schemas remain in normal runtime or public contract paths"),
        violations);
}

void testProjectDocumentSurfaceUsesProjectV1() {
    const QStringList projectSurfaceFiles = {
        QStringLiteral("qt/inc/project/projectdocument.h"),
        QStringLiteral("qt/src/project/projectreader.cpp"),
        QStringLiteral("qt/src/project/projectwriter.cpp"),
        QStringLiteral("qt/src/project/graphprojectserializer.cpp")
    };

    QStringList violations;
    for (const QString& relativePath : projectSurfaceFiles) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }

        const QString source = readTextFile(info.absoluteFilePath());
        if (!source.contains(projectSchemaName()) &&
            !source.contains(QStringLiteral("schemaids::projectV1"))) {
            violations.append(QStringLiteral("%1 does not expose %2")
                                  .arg(relativePath)
                                  .arg(projectSchemaName()));
        }
        if (source.contains(QStringLiteral("QStringLiteral(\"v1\")")) ||
            source.contains(QStringLiteral("\"schema\": \"v1\"")) ||
            source.contains(QStringLiteral("schema must be v1"))) {
            violations.append(QStringLiteral("%1 still encodes legacy root project schema v1").arg(relativePath));
        }
    }

    failIfViolations(QStringLiteral("project document public surface must be %1").arg(projectSchemaName()),
                     violations);
}

void testPackageRuntimeLoadDoesNotRequireIpcoreYml() {
    QStringList violations;

    const QVector<QFileInfo> packageFiles = repositoryFilesUnder(QStringLiteral("ipcores"));
    bool foundRuntimePackage = false;
    for (const QFileInfo& fileInfo : packageFiles) {
        if (fileInfo.fileName() != QStringLiteral("ipcraft.json")) {
            continue;
        }
        foundRuntimePackage = true;
        const QString relativePath = relativeRepositoryPath(fileInfo.absoluteFilePath());
        const QJsonObject manifest = readJsonObject(fileInfo.absoluteFilePath());
        const QString schema = manifest.value(QStringLiteral("schema")).toString();
        if (schema != packageSchemaName()) {
            violations.append(QStringLiteral("%1 schema is %2, expected %3")
                                  .arg(relativePath, schema, packageSchemaName()));
        }

        const QJsonObject commands = manifest.value(QStringLiteral("commands")).toObject();
        for (auto it = commands.constBegin(); it != commands.constEnd(); ++it) {
            const QString inputSchema =
                it.value().toObject().value(QStringLiteral("input_schema")).toString();
            if (!inputSchema.isEmpty() && inputSchema == oldNocProjectSchemaName()) {
                violations.append(QStringLiteral("%1 commands.%2.input_schema still uses %3")
                                      .arg(relativePath, it.key(), inputSchema));
            }
        }
    }
    require(foundRuntimePackage, "repository should include runtime package specs under ipcores/");

    QTemporaryDir packageRoot;
    require(packageRoot.isValid(), "temporary package root should be valid");
    writeFile(QDir(packageRoot.path()).filePath(QStringLiteral("ipcraft.json")),
              QStringLiteral(R"json({
  "schema": "%1",
  "id": "org.example.loader-gate",
  "name": "Loader Gate",
  "version": "1.0.0",
  "extensions": [
    "ipcraft.emitters",
    "ipcraft.flows",
    "ipcraft.artifacts"
  ],
  "flows": [],
  "emitters": [],
  "artifacts": []
})json").arg(packageSchemaName()).toUtf8());
    require(!QFileInfo::exists(QDir(packageRoot.path()).filePath(authoringYamlFileName())),
            "loader smoke fixture should not include authoring YAML");

    const IpcraftManifestReadResult result =
        IpcraftManifestReader().readPackage(packageRoot.path());
    if (!result.ok || result.manifest.schema != packageSchemaName()) {
        QStringList diagnostics;
        for (const IpcraftDiagnostic& diagnostic : result.diagnostics) {
            diagnostics.append(diagnostic.message);
        }
        violations.append(QStringLiteral("runtime loader did not accept %1 without %2: %3")
                              .arg(packageSchemaName(),
                                   authoringYamlFileName(),
                                   diagnostics.join(QStringLiteral("; "))));
    }

    const QStringList runtimeLoaderFiles = {
        QStringLiteral("qt/src/ipcraft/ipcraftmanifestreader.cpp"),
        QStringLiteral("qt/src/ipcraft/ipcraftregistry.cpp"),
        QStringLiteral("qt/src/modules/moduleprovider.cpp"),
        QStringLiteral("qt/src/modules/moduleregistry.cpp")
    };
    for (const QString& relativePath : runtimeLoaderFiles) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }
        const QString source = readTextFile(info.absoluteFilePath());
        if (source.contains(authoringYamlFileName())) {
            violations.append(QStringLiteral("%1 reads or names authoring-only %2")
                                  .arg(relativePath, authoringYamlFileName()));
        }
    }

    failIfViolations(QStringLiteral("runtime package loading must consume %1 from ipcraft.json without %2")
                         .arg(packageSchemaName(), authoringYamlFileName()),
                     violations);
}

void testPackageExtensionSurfacesAreEnforced() {
    QStringList violations;

    const QString packageSchemaPath = QStringLiteral("schemas/ipcraft.package.v1.schema.json");
    const QFileInfo packageSchemaInfo(repositoryPath(packageSchemaPath));
    if (!packageSchemaInfo.exists()) {
        violations.append(QStringLiteral("%1 is missing").arg(packageSchemaPath));
    } else {
        const QString schemaText = readTextFile(packageSchemaInfo.absoluteFilePath());
        if (!schemaText.contains(QStringLiteral("extensions"))) {
            violations.append(QStringLiteral("%1 does not document package extensions").arg(packageSchemaPath));
        }
        if (!schemaText.contains(QStringLiteral("package.extension_required"))) {
            violations.append(QStringLiteral("%1 does not expose package.extension_required").arg(packageSchemaPath));
        }
        if (!schemaText.contains(QStringLiteral("dependent")) &&
            !schemaText.contains(QStringLiteral("\"if\"")) &&
            !schemaText.contains(QStringLiteral("oneOf"))) {
            violations.append(QStringLiteral("%1 does not appear to encode extension-gated sections")
                                  .arg(packageSchemaPath));
        }
    }

    const QStringList enforcementSurfaces = {
        QStringLiteral("qt/src/ipcraft/packagespec.cpp")
    };
    for (const QString& relativePath : enforcementSurfaces) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }
        const QString source = readTextFile(info.absoluteFilePath());
        if (!source.contains(QStringLiteral("extension_required"))) {
            violations.append(QStringLiteral("%1 does not expose package.extension_required enforcement")
                                  .arg(relativePath));
        }
    }

    failIfViolations("package extension-owned sections must be rejected unless the extension is declared",
                     violations);
}

void testDefaultValidateProjectIsSideEffectFree() {
    QStringList violations;
    const QStringList defaultValidationFiles = {
        QStringLiteral("qt/inc/validation/projectvalidationrunner.h"),
        QStringLiteral("qt/src/validation/projectvalidationrunner.cpp")
    };
    const QStringList forbiddenTokens = {
        QStringLiteral("DRCRunner"),
        QStringLiteral("QProcess"),
        QStringLiteral("IpCoreCommandRunner"),
        QStringLiteral("validate(graph, *entry, instance)")
    };

    for (const QString& relativePath : defaultValidationFiles) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }
        const QString source = readTextFile(info.absoluteFilePath());
        for (const QString& token : forbiddenTokens) {
            if (source.contains(token)) {
                violations.append(QStringLiteral("%1 default validation references %2")
                                      .arg(relativePath, token));
            }
        }
    }

    const QString flowRunnerPath = QStringLiteral("qt/src/ipcraft/flowrunner.cpp");
    if (!QFileInfo::exists(repositoryPath(flowRunnerPath))) {
        violations.append(QStringLiteral("%1 is missing; external validation/generation must be behind FlowRunner")
                              .arg(flowRunnerPath));
    }

    failIfViolations("default validate-project and ProjectValidationRunner must be static and side-effect free",
                     violations);
}

void testCliCommandsReturnMachineReadableJson() {
    QStringList violations;

    const QString cliSourcePath = QStringLiteral("qt/cli/ipcraft_cli_main.cpp");
    const QFileInfo cliSourceInfo(repositoryPath(cliSourcePath));
    if (!cliSourceInfo.exists()) {
        violations.append(QStringLiteral("%1 is missing").arg(cliSourcePath));
    } else {
        const QString cliSource = readTextFile(cliSourceInfo.absoluteFilePath());
        const QString cliResultSourcePath = QStringLiteral("qt/src/cli/cliresult.cpp");
        const QString cliResultSource =
            readTextFile(repositoryPath(cliResultSourcePath));
        if (!cliSource.contains(QStringLiteral("schemaids::cliResultV1")) &&
            !cliResultSource.contains(QStringLiteral("schemaids::cliResultV1")) &&
            !cliResultSource.contains(cliResultSchemaName())) {
            violations.append(QStringLiteral("%1/%2 do not emit %3")
                                  .arg(cliSourcePath, cliResultSourcePath, cliResultSchemaName()));
        }
        if (!cliResultSource.contains(QStringLiteral("QJsonDocument"))) {
            violations.append(QStringLiteral("%1 does not appear to write JSON output").arg(cliResultSourcePath));
        }
        for (const QString& command : requiredCliCommands()) {
            if (!cliSource.contains(command)) {
                violations.append(QStringLiteral("%1 does not register CLI command %2")
                                      .arg(cliSourcePath, command));
            }
        }
    }

    const QFileInfo xmakeInfo(repositoryPath(QStringLiteral("qt/xmake.lua")));
    require(xmakeInfo.exists(), "qt/xmake.lua should exist");
    const QString xmakeSource = readTextFile(xmakeInfo.absoluteFilePath());
    if (!xmakeSource.contains(QStringLiteral("ipcraft-cli"))) {
        violations.append(QStringLiteral("qt/xmake.lua does not define the ipcraft-cli target"));
    }

    const QString cliSchemaPath = QStringLiteral("schemas/ipcraft.cli.result.v1.schema.json");
    if (!QFileInfo::exists(repositoryPath(cliSchemaPath))) {
        violations.append(QStringLiteral("%1 is missing").arg(cliSchemaPath));
    }

    failIfViolations("ipcraft-cli must expose required commands as machine-readable JSON",
                     violations);
}

void testAuditDocsAndSchemasExist() {
    QStringList violations;

    const QHash<QString, QString> schemaNamesByPath = {
        {QStringLiteral("schemas/ipcraft.project.v1.schema.json"), projectSchemaName()},
        {QStringLiteral("schemas/ipcraft.package.v1.schema.json"), packageSchemaName()},
        {QStringLiteral("schemas/ipcraft.diagnostics.v1.schema.json"), diagnosticsSchemaName()},
        {QStringLiteral("schemas/ipcraft.graph-config.v1.schema.json"), graphConfigSchemaName()},
        {QStringLiteral("schemas/ipcraft.emitted-inputs.v1.schema.json"), emittedInputsSchemaName()},
        {QStringLiteral("schemas/ipcraft.cli.result.v1.schema.json"), cliResultSchemaName()}
    };

    for (const QString& relativePath : requiredSchemaFiles()) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }

        const QString source = readTextFile(info.absoluteFilePath());
        const QString schemaName = schemaNamesByPath.value(relativePath);
        if (!source.contains(schemaName)) {
            violations.append(QStringLiteral("%1 does not name %2")
                                  .arg(relativePath)
                                  .arg(schemaName));
        }
        if (!source.contains(QStringLiteral("additionalProperties")) ||
            !source.contains(QStringLiteral("false"))) {
            violations.append(QStringLiteral("%1 does not visibly enforce strict top-level object validation")
                                  .arg(relativePath));
        }
        readJsonObject(info.absoluteFilePath());
    }

    for (const QString& relativePath : requiredAuditDocFiles()) {
        const QFileInfo info(repositoryPath(relativePath));
        if (!info.exists()) {
            violations.append(QStringLiteral("%1 is missing").arg(relativePath));
            continue;
        }

        const QString source = readTextFile(info.absoluteFilePath());
        for (const QString& schemaName : {
                 projectSchemaName(),
                 packageSchemaName(),
                 emittedInputsSchemaName(),
                 cliResultSchemaName()
             }) {
            if (!source.contains(schemaName)) {
                violations.append(QStringLiteral("%1 does not document %2")
                                      .arg(relativePath, schemaName));
            }
        }
    }

    failIfViolations("public schemas and audit documentation must exist and name the hard-cutover contract",
                     violations);
}

void testRuleIdCatalogContainsEveryPublicDiagnostic() {
    const QString catalogPath = QStringLiteral("docs/audit/rule-id-catalog.md");
    const QFileInfo catalogInfo(repositoryPath(catalogPath));
    require(catalogInfo.exists(),
            QStringLiteral("%1 is missing").arg(catalogPath));

    const QString catalog = readTextFile(catalogInfo.absoluteFilePath());
    QStringList violations;
    for (const QString& ruleId : requiredPublicRuleIds()) {
        if (!catalog.contains(ruleId)) {
            violations.append(QStringLiteral("%1 missing %2")
                                  .arg(catalogPath)
                                  .arg(ruleId));
        }
    }

    failIfViolations("rule-id catalog must include every public diagnostic rule", violations);
}

void testFlowRunnerUsesCentralContractKeysAndDiagnosticIds() {
    const QString flowRunnerPath = QStringLiteral("qt/src/ipcraft/flowrunner.cpp");
    const QString source = readTextFile(repositoryPath(flowRunnerPath));

    QStringList violations;
    if (!source.contains(QStringLiteral("#include \"ipcraft/contract/flowkeys.h\""))) {
        violations.append(QStringLiteral("%1 does not include flowkeys.h").arg(flowRunnerPath));
    }
    if (!source.contains(QStringLiteral("#include \"ipcraft/diagnosticids.h\""))) {
        violations.append(QStringLiteral("%1 does not include diagnosticids.h").arg(flowRunnerPath));
    }

    const QStringList requiredHelpers = {
        QStringLiteral("command"),
        QStringLiteral("executable"),
        QStringLiteral("frameworkTool"),
        QStringLiteral("args"),
        QStringLiteral("env"),
        QStringLiteral("allow"),
        QStringLiteral("capture"),
        QStringLiteral("stdout"),
        QStringLiteral("stderr"),
        QStringLiteral("cwd"),
        QStringLiteral("timeoutMs"),
        QStringLiteral("native")
    };
    for (const QString& helper : requiredHelpers) {
        const QString token = QStringLiteral("flowkeys::%1()").arg(helper);
        if (!source.contains(token)) {
            violations.append(QStringLiteral("%1 does not use %2").arg(flowRunnerPath, token));
        }
    }

    for (const QFileInfo& fileInfo : repositoryTextFiles()) {
        const QString relativePath = relativeRepositoryPath(fileInfo.absoluteFilePath());
        if (!(relativePath.startsWith(QStringLiteral("qt/src/")) ||
              relativePath.startsWith(QStringLiteral("qt/inc/"))) ||
            relativePath == QStringLiteral("qt/inc/ipcraft/diagnosticids.h")) {
            continue;
        }

        const QString productionSource = readTextFile(fileInfo.absoluteFilePath());
        if (productionSource.contains(QStringLiteral("flow.command_policy_violation"))) {
            violations.append(QStringLiteral("%1 contains raw flow.command_policy_violation")
                                  .arg(relativePath));
        }
    }

    failIfViolations("FlowRunner command parsing must use contract keys and diagnostic IDs",
                     violations);
}

void testProjectCompatibilityWrapperKeysAreCentralized() {
    const QString readerPath = QStringLiteral("qt/src/project/projectreader.cpp");
    const QString writerPath = QStringLiteral("qt/src/project/projectwriter.cpp");
    const QString reader = readTextFile(repositoryPath(readerPath));
    const QString writer = readTextFile(repositoryPath(writerPath));

    QStringList violations;
    if (!reader.contains(QStringLiteral("#include \"ipcraft/contract/legacyprojectkeys.h\""))) {
        violations.append(QStringLiteral("%1 does not include legacyprojectkeys.h").arg(readerPath));
    }

    const QStringList legacyHelpers = {
        QStringLiteral("project"),
        QStringLiteral("instances"),
        QStringLiteral("composition"),
        QStringLiteral("layout"),
        QStringLiteral("migration"),
        QStringLiteral("native")
    };
    for (const QString& helper : legacyHelpers) {
        const QString token = QStringLiteral("legacyprojectkeys::%1()").arg(helper);
        if (!reader.contains(token)) {
            violations.append(QStringLiteral("%1 does not route wrapper key through %2")
                                  .arg(readerPath, token));
        }
    }

    if (!writer.contains(QStringLiteral("#include \"ipcraft/contract/legacyprojectkeys.h\""))) {
        violations.append(QStringLiteral("%1 does not include legacyprojectkeys.h").arg(writerPath));
    }
    for (const QString& key : {
             QStringLiteral("instances"),
             QStringLiteral("composition"),
             QStringLiteral("layout"),
             QStringLiteral("migration"),
             QStringLiteral("native")
         }) {
        if (writer.contains(QStringLiteral("QStringLiteral(\"%1\")").arg(key))) {
            violations.append(QStringLiteral("%1 contains raw legacy wrapper key %2")
                                  .arg(writerPath, key));
        }
    }

    failIfViolations("legacy project wrapper keys must be isolated behind compatibility helpers",
                     violations);
}

QString combinedExampleText(const QFileInfo& directoryInfo) {
    QStringList chunks;
    QDirIterator iterator(directoryInfo.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        const QString relativePath = relativeRepositoryPath(fileInfo.absoluteFilePath());
        if (isTextContractFile(relativePath)) {
            chunks.append(readTextFile(fileInfo.absoluteFilePath()));
        }
    }
    return chunks.join(QLatin1Char('\n'));
}

void testNegativeContractExamplesExist() {
    const QString examplesRootPath = QStringLiteral("examples/contracts");
    const QFileInfo examplesRoot(repositoryPath(examplesRootPath));
    require(examplesRoot.exists() && examplesRoot.isDir(),
            QStringLiteral("%1 is missing").arg(examplesRootPath));

    QStringList negativeExampleTexts;
    QDir rootDir(examplesRoot.absoluteFilePath());
    const QFileInfoList children =
        rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& child : children) {
        const QString text = combinedExampleText(child);
        const QString relativePath = relativeRepositoryPath(child.absoluteFilePath());
        if (relativePath.contains(QStringLiteral("negative"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("rule_id")) ||
            text.contains(QStringLiteral("diagnostic"))) {
            negativeExampleTexts.append(text);
        }
    }

    require(!negativeExampleTexts.isEmpty(),
            "examples/contracts should contain negative audit fixtures with expected diagnostics");

    const QString combined = negativeExampleTexts.join(QLatin1Char('\n'));
    const QList<QPair<QString, QRegularExpression>> requiredCases = {
        {QStringLiteral("malformed package"),
         QRegularExpression(QStringLiteral("package\\.(unsupported_schema|parse|malformed)"))},
        {QStringLiteral("extension-required package"),
         QRegularExpression(QStringLiteral("package\\.extension_required"))},
        {QStringLiteral("path confinement"),
         QRegularExpression(QStringLiteral("(emitter\\.path_escape|artifact\\.glob_escape|package\\.path_escape)"))},
        {QStringLiteral("missing flow executable"),
         QRegularExpression(QStringLiteral("flow\\.executable_missing"))}
    };

    QStringList violations;
    for (const auto& requiredCase : requiredCases) {
        if (!requiredCase.second.match(combined).hasMatch()) {
            violations.append(QStringLiteral("missing negative contract example for %1")
                                  .arg(requiredCase.first));
        }
    }

    failIfViolations("negative contract examples must cover stable public diagnostics",
                     violations);
}

struct TestCase {
    QString name;
    std::function<void()> run;
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QVector<TestCase> tests = {
        {QStringLiteral("testNoNormalRuntimePathUsesNocProjectV1"),
         testNoNormalRuntimePathUsesNocProjectV1},
        {QStringLiteral("testProjectDocumentSurfaceUsesProjectV1"),
         testProjectDocumentSurfaceUsesProjectV1},
        {QStringLiteral("testPackageRuntimeLoadDoesNotRequireIpcoreYml"),
         testPackageRuntimeLoadDoesNotRequireIpcoreYml},
        {QStringLiteral("testPackageExtensionSurfacesAreEnforced"),
         testPackageExtensionSurfacesAreEnforced},
        {QStringLiteral("testDefaultValidateProjectIsSideEffectFree"),
         testDefaultValidateProjectIsSideEffectFree},
        {QStringLiteral("testCliCommandsReturnMachineReadableJson"),
         testCliCommandsReturnMachineReadableJson},
        {QStringLiteral("testAuditDocsAndSchemasExist"),
         testAuditDocsAndSchemasExist},
        {QStringLiteral("testRuleIdCatalogContainsEveryPublicDiagnostic"),
         testRuleIdCatalogContainsEveryPublicDiagnostic},
        {QStringLiteral("testFlowRunnerUsesCentralContractKeysAndDiagnosticIds"),
         testFlowRunnerUsesCentralContractKeysAndDiagnosticIds},
        {QStringLiteral("testProjectCompatibilityWrapperKeysAreCentralized"),
         testProjectCompatibilityWrapperKeysAreCentralized},
        {QStringLiteral("testNegativeContractExamplesExist"),
         testNegativeContractExamplesExist}
    };

    QStringList failures;
    for (const TestCase& test : tests) {
        try {
            test.run();
        } catch (const std::exception& error) {
            failures.append(QStringLiteral("[%1]\n%2")
                                .arg(test.name, QString::fromLocal8Bit(error.what())));
        }
    }

    if (!failures.isEmpty()) {
        std::cerr << "v1architecturegate_test failed:\n"
                  << failures.join(QStringLiteral("\n\n")).toLocal8Bit().constData()
                  << '\n';
        return 1;
    }

    std::cout << "v1architecturegate_test passed\n";
    return 0;
}
