// Public contract examples smoke test for black-box audit fixtures.
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"
#include "project/projectwriter.h"
#include "jsonschemavalidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <iostream>
#include <stdexcept>

namespace {

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

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "JSON file should open");
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    require(document.isObject(), "JSON file should parse as an object");
    return document.object();
}

QString repositoryRoot() {
    QDir dir(QFileInfo(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")))
                 .absoluteDir());
    require(dir.cdUp(), "repository root should be reachable from schemas directory");
    return dir.absolutePath();
}

QString relativeToRepository(const QString& absolutePath) {
    return QDir(repositoryRoot()).relativeFilePath(absolutePath);
}

QVector<QFileInfo> filesMatching(const QString& relativeRoot, const QString& fileName) {
    QVector<QFileInfo> files;
    const QFileInfo root(repositoryPath(relativeRoot));
    require(root.isDir(), "scan root should exist");

    QDirIterator iterator(root.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().fileName() == fileName) {
            files.append(iterator.fileInfo());
        }
    }
    return files;
}

bool isNegativeContractFixture(const QFileInfo& project) {
    return project.absoluteDir().dirName().startsWith(QStringLiteral("negative_"));
}

void requireJsonMatchesSchema(const QString& jsonPath, const JsonSchemaValidator& schema) {
    QString error;
    const QJsonObject object = readJsonObject(jsonPath);
    const bool matchesSchema = schema.validate(object, &error);
    if (!matchesSchema && error.isEmpty()) {
        error = QStringLiteral("$: schema validation failed; top-level keys: %1")
            .arg(object.keys().join(QStringLiteral(", ")));
    }
    require(matchesSchema,
            QStringLiteral("%1 must match schema: %2")
                .arg(relativeToRepository(jsonPath), error)
                .toUtf8()
                .constData());
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

struct ExampleExpectation {
    QString name;
    QString expectedPackageRule;
};

QJsonObject flatProjectObject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::projectV1},
        {QStringLiteral("id"), QStringLiteral("flat_contract_project")},
        {QStringLiteral("name"), QStringLiteral("Flat Contract Project")},
        {QStringLiteral("packages"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("vendor.example.flat")},
                {QStringLiteral("version"), QStringLiteral("1.0.0")}
            }
        }},
        {QStringLiteral("components"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("flat0")},
                {QStringLiteral("type"), QStringLiteral("FlatComponent")},
                {QStringLiteral("packageRef"), QStringLiteral("vendor.example.flat@1.0.0")},
                {QStringLiteral("config"), QJsonObject{{QStringLiteral("width"), 32}}}
            }
        }},
        {QStringLiteral("interfaces"), QJsonArray{}},
        {QStringLiteral("connections"), QJsonArray{}},
        {QStringLiteral("topologies"), QJsonArray{}},
        {QStringLiteral("views"), QJsonArray{}},
        {QStringLiteral("diagnostics"), QJsonArray{}},
        {QStringLiteral("artifacts"), QJsonArray{}},
        {QStringLiteral("extensions"), QJsonArray{}},
        {QStringLiteral("metadata"), QJsonObject{}}
    };
}

void testContractExamplesExistAndDeclarePublicSchemas() {
    const QVector<ExampleExpectation> examples = {
        {QStringLiteral("simple_parameter_ip"), {}},
        {QStringLiteral("table_config_ip"), {}},
        {QStringLiteral("raw_document_ip"), {}},
        {QStringLiteral("composition_two_ip"), {}},
        {QStringLiteral("clock_fanout_project"), {}},
        {QStringLiteral("failing_validator_project"), {}},
        {QStringLiteral("artifact_collection_project"), {}},
        {QStringLiteral("noc_cutover_project"), {}},
        {QStringLiteral("negative_malformed_package"), QStringLiteral("package.missing_required")},
        {QStringLiteral("negative_extension_required"), QStringLiteral("package.extension_required")},
        {QStringLiteral("negative_path_escape"), QStringLiteral("package.path_escape")},
        {QStringLiteral("negative_flow_missing_executable"), {}}
    };

    const QString contractsRoot = repositoryPath(QStringLiteral("examples/contracts"));
    require(QFileInfo(contractsRoot).isDir(), "examples/contracts should exist");

    for (const ExampleExpectation& example : examples) {
        const QDir exampleDir(QDir(contractsRoot).filePath(example.name));
        require(exampleDir.exists(), "required contract example directory should exist");
        require(QFileInfo(exampleDir.filePath(QStringLiteral("README.md"))).isFile(),
                "contract example README should exist");

        const QString projectPath = exampleDir.filePath(QStringLiteral("project.fpproj"));
        require(ProjectReader::readFile(projectPath).success,
                "contract example project should parse as ipcraft.project.v1");

        const QString packagePath = exampleDir.filePath(QStringLiteral("package/ipcraft.json"));
        require(QFileInfo(packagePath).isFile(),
                "contract example package/ipcraft.json should exist");
        const QJsonObject packageObject = readJsonObject(packagePath);
        require(packageObject.value(QStringLiteral("schema")).toString() ==
                    ipcraft::schemaids::packageV1,
                "contract example package should declare ipcraft.package.v1");

        const ipcraft::PackageSpecReadResult packageResult =
            ipcraft::PackageSpecReader().readSpecFile(packagePath);
        if (example.expectedPackageRule.isEmpty()) {
            require(packageResult.ok,
                    "positive contract example package should parse");
        } else {
            require(!packageResult.ok,
                    "negative contract example package should emit diagnostics");
            require(hasRule(packageResult.diagnostics, example.expectedPackageRule),
                    "negative contract example should emit expected stable diagnostic");
        }
    }
}

void testProjectReaderAcceptsFlatProjectDesignCurrentInput() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString projectPath = QDir(tempDir.path())
        .filePath(QStringLiteral("ipcraft_contract_flat_project.fpproj"));
    QFile file(projectPath);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "flat project fixture should open for writing");
    const QByteArray content = QJsonDocument(flatProjectObject()).toJson(QJsonDocument::Indented);
    require(file.write(content) == content.size(), "flat project fixture should write");
    file.close();

    const ProjectReadResult result = ProjectReader::readFile(projectPath);
    require(result.success, "project reader should accept flat ProjectDesign input");
    require(result.document.projectId == QStringLiteral("flat_contract_project"),
            "flat project id should parse into ProjectDocument");
    require(result.document.instances.size() == 1,
            "flat components should parse into ProjectDocument instances");
    require(result.document.instances.first().id == QStringLiteral("flat0"),
            "flat component id should parse into instance id");
}

void testAllPositiveContractProjectsMatchPublicProjectSchema() {
    const JsonSchemaValidator projectSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")));
    const QVector<QFileInfo> projects =
        filesMatching(QStringLiteral("examples/contracts"), QStringLiteral("project.fpproj"));
    require(!projects.isEmpty(), "contract projects should exist");

    qsizetype positiveCount = 0;
    for (const QFileInfo& project : projects) {
        if (isNegativeContractFixture(project)) {
            continue;
        }
        ++positiveCount;
        requireJsonMatchesSchema(project.absoluteFilePath(), projectSchema);
    }
    require(positiveCount > 0,
            "contract project schema gate excluded only negative_* fixtures; no positive projects were checked");
}

void testProjectWriterOutputMatchesPublicProjectSchema() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("writer_flat_project");
    document.projectName = QStringLiteral("Writer Flat Project");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.example.writer"),
                                                QStringLiteral("1.0.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("writer0");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.example.writer"),
                                         QStringLiteral("1.0.0")};
    instance.native.insert(QStringLiteral("componentType"), QStringLiteral("WriterComponent"));
    instance.config.insert(QStringLiteral("parameters"),
                           QJsonObject{{QStringLiteral("width"), 32}});
    document.instances.append(instance);

    const QJsonObject written = ProjectWriter::toJsonObject(document);
    require(!written.contains(QStringLiteral("project")),
            "writer must not emit wrapper project root");
    require(!written.contains(QStringLiteral("instances")),
            "writer must not emit wrapper instances root");
    require(written.contains(QStringLiteral("components")),
            "writer must emit flat components");

    const JsonSchemaValidator projectSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.project.v1.schema.json")));
    QString error;
    require(projectSchema.validate(written, &error),
            QStringLiteral("ProjectWriter output must match project schema: %1")
                .arg(error)
                .toUtf8()
                .constData());
}

void testAllRepositoryRuntimePackagesMatchPublicPackageSchema() {
    const JsonSchemaValidator packageSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.package.v1.schema.json")));
    const QVector<QFileInfo> packages =
        filesMatching(QStringLiteral("ipcores"), QStringLiteral("ipcraft.json"));
    require(!packages.isEmpty(), "runtime package manifests should exist");
    for (const QFileInfo& package : packages) {
        requireJsonMatchesSchema(package.absoluteFilePath(), packageSchema);
    }
}

void testUnknownOptionalPackageExtensionIsSchemaValid() {
    const JsonSchemaValidator packageSchema =
        JsonSchemaValidator::fromFile(repositoryPath(QStringLiteral("schemas/ipcraft.package.v1.schema.json")));
    const QJsonObject package{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), QStringLiteral("vendor.experimental.schema")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("name"), QStringLiteral("Experimental Schema")},
        {QStringLiteral("extensions"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("vendor.experimental.v1")},
                {QStringLiteral("required"), false},
                {QStringLiteral("version"), QStringLiteral("0.1.0")}
            }
        }}
    };

    QString error;
    require(packageSchema.validate(package, &error),
            QStringLiteral("unknown optional extension should be schema valid: %1")
                .arg(error)
                .toUtf8()
                .constData());
}

void testPublicArchitectureAndAuditDocsExist() {
    const QStringList docs = {
        QStringLiteral("docs/architecture/v1-core-architecture.md"),
        QStringLiteral("docs/audit/black-box-audit-guide.md"),
        QStringLiteral("docs/audit/coverage-matrix.md"),
        QStringLiteral("docs/audit/failure-report-format.md"),
        QStringLiteral("docs/audit/rule-id-catalog.md")
    };
    for (const QString& doc : docs) {
        require(QFileInfo(repositoryPath(doc)).isFile(),
                "public architecture/audit document should exist");
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testProjectReaderAcceptsFlatProjectDesignCurrentInput();
        testProjectWriterOutputMatchesPublicProjectSchema();
        testUnknownOptionalPackageExtensionIsSchemaValid();
        testContractExamplesExistAndDeclarePublicSchemas();
        testAllPositiveContractProjectsMatchPublicProjectSchema();
        testAllRepositoryRuntimePackagesMatchPublicPackageSchema();
        testPublicArchitectureAndAuditDocsExist();
    } catch (const std::exception& error) {
        std::cerr << "ipcraft_contract_examples_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_contract_examples_test passed\n";
    return 0;
}
