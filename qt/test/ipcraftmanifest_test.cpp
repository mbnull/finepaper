// Hard-cutover bridge tests for the legacy IpcraftManifestReader API.
//
// The reader name is retained for in-tree callers that still depend on
// IpcraftPackageManifest at compile time. Runtime loading is V1 package spec
// loading only: ipcraft.manifest.v1 is intentionally rejected.
#include "ipcraft/ipcraftmanifestreader.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "failed to open fixture for writing");
    require(file.write(content) == content.size(), "failed to write fixture");
}

void writePackage(QDir& root, const QJsonObject& object) {
    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QJsonObject minimalPackageSpec() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), QStringLiteral("vendor.example.simple")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("name"), QStringLiteral("Simple IP")},
        {QStringLiteral("extensions"), QJsonArray{
            QStringLiteral("ipcraft.config.params")
        }},
        {QStringLiteral("config_schema"), QJsonObject{
            {QStringLiteral("parameters"), QJsonArray{}}
        }},
        {QStringLiteral("plugin"), QJsonObject{
            {QStringLiteral("library"), QStringLiteral("plugins/libdemo.so")},
            {QStringLiteral("entrypoint"), QStringLiteral("DemoPlugin")}
        }}
    };
}

void testBridgeLoadsPackageV1Only() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    writePackage(root, minimalPackageSpec());

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(root.absolutePath());
    require(result.ok, "ipcraft.package.v1 should load through compatibility reader");
    require(result.manifest.schema == ipcraft::schemaids::packageV1,
            "compatibility manifest should expose package v1 schema");
    require(result.manifest.id == QStringLiteral("vendor.example.simple"),
            "package id should bridge");
    require(result.manifest.version == QStringLiteral("1.0.0"),
            "package version should bridge");
    require(result.manifest.extensions.contains(QStringLiteral("ipcraft.config.params")),
            "declared extensions should bridge");
    require(result.manifest.plugin.has_value(), "plugin metadata should bridge separately");
    require(result.manifest.plugin->libraryPath == QStringLiteral("plugins/libdemo.so"),
            "plugin library should bridge");
    require(result.manifest.plugin->entrypoint == QStringLiteral("DemoPlugin"),
            "plugin entrypoint should bridge");
}

void testBridgeRejectsOldManifestSchema() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.manifest.v1"));
    writePackage(root, spec);

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(root.absolutePath());
    require(!result.ok, "ipcraft.manifest.v1 should be rejected after hard cutover");
    require(!result.diagnostics.isEmpty(), "unsupported schema should emit diagnostics");
    require(result.diagnostics.first().ruleId == QStringLiteral("package.unsupported_schema"),
            "unsupported schema diagnostic should expose stable rule id");
    require(result.diagnostics.first().source == QStringLiteral("package.parser"),
            "unsupported schema diagnostic should expose stable source");
    require(result.diagnostics.first().path == QStringLiteral("$.schema"),
            "unsupported schema diagnostic should point at schema path");
}

void testBridgeDoesNotReadIpcoreYml() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    writePackage(root, minimalPackageSpec());
    writeFile(root.filePath(QStringLiteral("ipcore.yml")),
              QByteArrayLiteral("schema: authoring-file-that-runtime-must-ignore\n"));

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(root.absolutePath());
    require(result.ok, "runtime compatibility reader should ignore ipcore.yml");
    require(result.manifest.id == QStringLiteral("vendor.example.simple"),
            "runtime compatibility reader should read ipcraft.json");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testBridgeLoadsPackageV1Only();
        testBridgeRejectsOldManifestSchema();
        testBridgeDoesNotReadIpcoreYml();
    } catch (const std::exception& exception) {
        std::cerr << "ipcraftmanifest_test failed: " << exception.what() << '\n';
        return 1;
    }

    std::cout << "ipcraftmanifest_test passed\n";
    return 0;
}
