// Public contract examples smoke test for black-box audit fixtures.
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
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
        testContractExamplesExistAndDeclarePublicSchemas();
        testPublicArchitectureAndAuditDocsExist();
    } catch (const std::exception& error) {
        std::cerr << "ipcraft_contract_examples_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_contract_examples_test passed\n";
    return 0;
}
