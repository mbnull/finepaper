// Ipcraft V1 PackageInputBuilder / emitted-inputs manifest contract tests.
#include "ipcraft/emitter.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId &&
            diagnostic.source == QStringLiteral("core") &&
            diagnostic.severity == QStringLiteral("error")) {
            return true;
        }
    }
    return false;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "emitted file should open");
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "file should open for writing");
    require(file.write(bytes) == bytes.size(), "file should write all bytes");
}

ipcraft::PackageSpec packageWithEmitters(QJsonArray emitters) {
    ipcraft::PackageSpec spec;
    spec.id = QStringLiteral("vendor.example.simple");
    spec.version = QStringLiteral("1.0.0");
    spec.emitters = std::move(emitters);
    return spec;
}

ipcraft::PackageInputBuildRequest requestFor(QTemporaryDir& outRoot, QJsonArray emitters) {
    ipcraft::PackageInputBuildRequest request;
    request.projectId = QStringLiteral("project_0");
    request.instanceId = QStringLiteral("ip0");
    request.runId = QStringLiteral("run0");
    request.outputRoot = outRoot.path();
    request.package = packageWithEmitters(std::move(emitters));
    request.config = ipcraft::ConfigBundle::fromJson(QJsonObject{
        {QStringLiteral("parameters"), QJsonObject{
            {QStringLiteral("zeta"), 3},
            {QStringLiteral("alpha"), true}
        }},
        {QStringLiteral("tables"), QJsonObject{
            {QStringLiteral("regions"), QJsonObject{
                {QStringLiteral("rows"), QJsonArray{
                    QJsonObject{{QStringLiteral("base"), 0}, {QStringLiteral("size"), 4096}}
                }}
            }}
        }},
        {QStringLiteral("documents"), QJsonObject{
            {QStringLiteral("system"), QJsonObject{
                {QStringLiteral("format"), QStringLiteral("json")},
                {QStringLiteral("content"), QJsonObject{
                    {QStringLiteral("width"), 64}
                }}
            }}
        }}
    });
    return request;
}

void testEmitParametersWritesDeterministicJson() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/params.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(result.ok, "emit_parameters should succeed");
    require(result.manifest.files.size() == 1, "manifest should contain emitted parameters file");
    require(result.manifest.toJson().value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::emittedInputsV1,
            "manifest schema should be emitted-inputs v1");
    require(readFile(QDir(outRoot.path()).filePath(QStringLiteral("input/params.json"))) ==
                QByteArrayLiteral("{\n    \"alpha\": true,\n    \"zeta\": 3\n}\n"),
            "parameters JSON should be deterministic");
}

void testEmitConfigDocumentRejectsAbsoluteOutputPath() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("kind"), QStringLiteral("emit_config_document")},
                    {QStringLiteral("document"), QStringLiteral("system")},
                    {QStringLiteral("path"), QDir(outRoot.path()).filePath(QStringLiteral("system.json"))}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "absolute emitter output paths should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.path_absolute")),
            "absolute output path should emit emitter.path_absolute");
    require(result.manifest.toJson().value(QStringLiteral("diagnostics")).toObject()
                .value(QStringLiteral("records")).toArray().size() == 1,
            "failure manifest should include diagnostics");
}

void testEmitConfigDocumentRejectsTraversal() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("kind"), QStringLiteral("emit_config_document")},
                    {QStringLiteral("document"), QStringLiteral("system")},
                    {QStringLiteral("path"), QStringLiteral("../system.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "traversal emitter output paths should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.path_escape")),
            "traversal output path should emit emitter.path_escape");

    ipcraft::PackageInputBuildRequest normalizedTraversal = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("kind"), QStringLiteral("emit_config_document")},
                    {QStringLiteral("document"), QStringLiteral("system")},
                    {QStringLiteral("path"), QStringLiteral("input/../system.json")}}
    });

    const ipcraft::PackageInputBuildResult normalizedTraversalResult =
        ipcraft::PackageInputBuilder::emitInputs(normalizedTraversal);
    require(!normalizedTraversalResult.ok, "embedded traversal should fail before normalization");
    require(hasRule(normalizedTraversalResult.manifest.diagnostics,
                    QStringLiteral("emitter.path_escape")),
            "embedded traversal should emit emitter.path_escape");
}

void testEmitConfigDocumentRejectsWindowsAbsoluteOutputPath() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("kind"), QStringLiteral("emit_config_document")},
                    {QStringLiteral("document"), QStringLiteral("system")},
                    {QStringLiteral("path"), QStringLiteral("C:/tmp/system.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "Windows absolute emitter output paths should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.path_absolute")),
            "Windows absolute output path should emit emitter.path_absolute");
}

void testMissingConfigSourceReturnsDiagnostic() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("missing_table")},
                    {QStringLiteral("kind"), QStringLiteral("emit_table")},
                    {QStringLiteral("table"), QStringLiteral("missing")},
                    {QStringLiteral("path"), QStringLiteral("input/missing.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("missing_document")},
                    {QStringLiteral("kind"), QStringLiteral("emit_config_document")},
                    {QStringLiteral("document"), QStringLiteral("missing")},
                    {QStringLiteral("path"), QStringLiteral("input/missing-doc.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("missing_graph")},
                    {QStringLiteral("kind"), QStringLiteral("emit_graph_config")},
                    {QStringLiteral("path"), QStringLiteral("input/graph.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "missing emitter source state should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.source_missing")),
            "missing source state should emit emitter.source_missing");
}

void testEmitCompositionWritesManifestEntry() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("composition")},
                    {QStringLiteral("kind"), QStringLiteral("emit_composition")},
                    {QStringLiteral("path"), QStringLiteral("input/composition.json")}}
    });

    ipcraft::SystemConnection connection;
    connection.id = QStringLiteral("conn0");
    connection.type = QStringLiteral("dependency");
    request.composition.connections.append(connection);

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(result.ok, "emit_composition should succeed");
    require(result.manifest.files.size() == 1, "manifest should include composition file");
    require(result.manifest.files.first().kind == QStringLiteral("composition"),
            "composition manifest entry should use composition kind");
    require(result.manifest.files.first().source.value(QStringLiteral("composition")).toBool(),
            "composition manifest entry should include source mapping");
}

void testManifestFilesAreRelativeAndDeterministic() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("table_z")},
                    {QStringLiteral("kind"), QStringLiteral("emit_table")},
                    {QStringLiteral("table"), QStringLiteral("regions")},
                    {QStringLiteral("path"), QStringLiteral("input/table.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params_a")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/params.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("composition_m")},
                    {QStringLiteral("kind"), QStringLiteral("emit_composition")},
                    {QStringLiteral("path"), QStringLiteral("input/composition.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(result.ok, "multi-emitter request should succeed");
    const QJsonArray files = result.manifest.toJson().value(QStringLiteral("files")).toArray();
    require(files.size() == 3, "manifest should contain all files");
    QStringList sortKeys;
    for (const QJsonValue& fileValue : files) {
        const QJsonObject file = fileValue.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        require(!QDir::isAbsolutePath(path), "manifest paths must be relative");
        require(!path.contains(QStringLiteral("..")), "manifest paths must not contain traversal");
        sortKeys.append(file.value(QStringLiteral("kind")).toString() + QLatin1Char('|') +
                        file.value(QStringLiteral("id")).toString() + QLatin1Char('|') +
                        path);
    }
    QStringList sorted = sortKeys;
    sorted.sort(Qt::CaseSensitive);
    require(sortKeys == sorted, "manifest files should be sorted by kind, id, path");
}

void testPartialEmitFailureStillReturnsDiagnosticsManifest() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/params.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("plugin")},
                    {QStringLiteral("kind"), QStringLiteral("plugin_hook")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "plugin_hook should fail until plugin hooks are implemented");
    require(result.manifest.files.size() == 1, "successful files should remain in partial manifest");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.plugin_unavailable")),
            "plugin hook should emit plugin unavailable diagnostic");
    require(result.manifest.toJson().value(QStringLiteral("diagnostics")).toObject()
                .value(QStringLiteral("schema")).toString() == ipcraft::schemaids::diagnosticsV1,
            "partial failure manifest should contain diagnostics schema");
}

void testDuplicateOutputPathsReturnDiagnostic() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params0")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/shared.json")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("params1")},
                    {QStringLiteral("kind"), QStringLiteral("emit_parameters")},
                    {QStringLiteral("path"), QStringLiteral("input/shared.json")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "duplicate output paths should fail deterministically");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.duplicate_output_path")),
            "duplicate output paths should emit emitter.duplicate_output_path");
}

void testCopyFileRejectsPackageSourceEscape() {
    QTemporaryDir packageRoot;
    QTemporaryDir outRoot;
    require(packageRoot.isValid(), "temporary package root should be valid");
    require(outRoot.isValid(), "temporary output root should be valid");
    writeFile(QDir(packageRoot.path()).filePath(QStringLiteral("local.txt")),
              QByteArrayLiteral("ok\n"));

    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("copy")},
                    {QStringLiteral("kind"), QStringLiteral("copy_file")},
                    {QStringLiteral("source"), QStringLiteral("../escape.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/copied.txt")}}
    });
    request.packageRoot = packageRoot.path();

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "copy_file source traversal should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.path_escape")),
            "copy_file source traversal should emit emitter.path_escape");

    ipcraft::PackageInputBuildRequest normalizedTraversal = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("copy")},
                    {QStringLiteral("kind"), QStringLiteral("copy_file")},
                    {QStringLiteral("source"), QStringLiteral("dir/../local.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/copied.txt")}}
    });
    normalizedTraversal.packageRoot = packageRoot.path();

    const ipcraft::PackageInputBuildResult normalizedTraversalResult =
        ipcraft::PackageInputBuilder::emitInputs(normalizedTraversal);
    require(!normalizedTraversalResult.ok, "copy_file embedded source traversal should fail");
    require(hasRule(normalizedTraversalResult.manifest.diagnostics,
                    QStringLiteral("emitter.path_escape")),
            "copy_file embedded source traversal should emit emitter.path_escape");
}

void testCopyFileRequiresPackageRoot() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("copy")},
                    {QStringLiteral("kind"), QStringLiteral("copy_file")},
                    {QStringLiteral("source"), QStringLiteral("local.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/copied.txt")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "copy_file should require a package root");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.source_missing")),
            "missing package root should emit emitter.source_missing");
}

void testCopyFileCopiesPackageLocalSource() {
    QTemporaryDir packageRoot;
    QTemporaryDir outRoot;
    require(packageRoot.isValid(), "temporary package root should be valid");
    require(outRoot.isValid(), "temporary output root should be valid");
    writeFile(QDir(packageRoot.path()).filePath(QStringLiteral("local.txt")),
              QByteArrayLiteral("ok\n"));

    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("copy")},
                    {QStringLiteral("kind"), QStringLiteral("copy_file")},
                    {QStringLiteral("source"), QStringLiteral("local.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/copied.txt")}}
    });
    request.packageRoot = packageRoot.path();

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(result.ok, "copy_file package-local source should succeed");
    require(readFile(QDir(outRoot.path()).filePath(QStringLiteral("input/copied.txt"))) ==
                QByteArrayLiteral("ok\n"),
            "copy_file should copy bytes exactly");
}

void testTemplateRejectsPackageSourceEscape() {
    QTemporaryDir packageRoot;
    QTemporaryDir outRoot;
    require(packageRoot.isValid(), "temporary package root should be valid");
    require(outRoot.isValid(), "temporary output root should be valid");
    writeFile(QDir(packageRoot.path()).filePath(QStringLiteral("template.txt")),
              QByteArrayLiteral("ok\n"));

    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("template")},
                    {QStringLiteral("kind"), QStringLiteral("template")},
                    {QStringLiteral("template"), QStringLiteral("../template.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/rendered.txt")}}
    });
    request.packageRoot = packageRoot.path();

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "template package source traversal should fail");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.path_escape")),
            "template package source traversal should emit emitter.path_escape");
}

void testTemplateRequiresContentOrPackageSource() {
    QTemporaryDir outRoot;
    require(outRoot.isValid(), "temporary output root should be valid");
    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("template")},
                    {QStringLiteral("kind"), QStringLiteral("template")},
                    {QStringLiteral("path"), QStringLiteral("input/rendered.txt")}}
    });

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(!result.ok, "template emitters should require content or package-local template path");
    require(hasRule(result.manifest.diagnostics, QStringLiteral("emitter.source_missing")),
            "template missing content/source should emit emitter.source_missing");
}

void testTemplateReadsPackageLocalSource() {
    QTemporaryDir packageRoot;
    QTemporaryDir outRoot;
    require(packageRoot.isValid(), "temporary package root should be valid");
    require(outRoot.isValid(), "temporary output root should be valid");
    writeFile(QDir(packageRoot.path()).filePath(QStringLiteral("template.txt")),
              QByteArrayLiteral("ok\n"));

    ipcraft::PackageInputBuildRequest request = requestFor(outRoot, QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("template")},
                    {QStringLiteral("kind"), QStringLiteral("template")},
                    {QStringLiteral("template"), QStringLiteral("template.txt")},
                    {QStringLiteral("path"), QStringLiteral("input/rendered.txt")}}
    });
    request.packageRoot = packageRoot.path();

    const ipcraft::PackageInputBuildResult result =
        ipcraft::PackageInputBuilder::emitInputs(request);
    require(result.ok, "template package-local source should succeed");
    require(readFile(QDir(outRoot.path()).filePath(QStringLiteral("input/rendered.txt"))) ==
                QByteArrayLiteral("ok\n"),
            "template should copy package-local bytes without script execution");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testEmitParametersWritesDeterministicJson();
        testEmitConfigDocumentRejectsAbsoluteOutputPath();
        testEmitConfigDocumentRejectsTraversal();
        testEmitConfigDocumentRejectsWindowsAbsoluteOutputPath();
        testMissingConfigSourceReturnsDiagnostic();
        testEmitCompositionWritesManifestEntry();
        testManifestFilesAreRelativeAndDeterministic();
        testPartialEmitFailureStillReturnsDiagnosticsManifest();
        testDuplicateOutputPathsReturnDiagnostic();
        testCopyFileRejectsPackageSourceEscape();
        testCopyFileRequiresPackageRoot();
        testCopyFileCopiesPackageLocalSource();
        testTemplateRejectsPackageSourceEscape();
        testTemplateRequiresContentOrPackageSource();
        testTemplateReadsPackageLocalSource();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_emitter_test passed\n";
    return 0;
}
