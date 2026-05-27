// Ipcraft V1 ArtifactCollector contract tests.
#include "ipcraft/artifactmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
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

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId && diagnostic.source == QStringLiteral("core")) {
            return true;
        }
    }
    return false;
}

void writeFile(const QString& path, const QByteArray& bytes = "data\n") {
    const QFileInfo info(path);
    require(QDir().mkpath(info.absolutePath()), "file directory should be created");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "file should open");
    require(file.write(bytes) == bytes.size(), "file should write fully");
}

ipcraft::PackageSpec packageWithArtifacts(QJsonArray artifacts) {
    ipcraft::PackageSpec package;
    package.id = QStringLiteral("vendor.example.artifacts");
    package.version = QStringLiteral("1.0.0");
    package.artifacts = std::move(artifacts);
    return package;
}

ipcraft::ArtifactCollectRequest requestFor(QTemporaryDir& runRoot, QJsonArray artifacts) {
    ipcraft::ArtifactCollectRequest request;
    request.runRoot = runRoot.path();
    request.flowRunId = QStringLiteral("run0");
    request.sourceInstanceId = QStringLiteral("ip0");
    request.package = packageWithArtifacts(std::move(artifacts));
    return request;
}

void testArtifactGlobCollectsInsideRunRoot() {
    QTemporaryDir runRoot;
    require(runRoot.isValid(), "run root should be valid");
    writeFile(QDir(runRoot.path()).filePath(QStringLiteral("out/a.v")));
    writeFile(QDir(runRoot.path()).filePath(QStringLiteral("out/b.v")));
    writeFile(QDir(runRoot.path()).filePath(QStringLiteral("out/skip.txt")));

    const ipcraft::ArtifactCollectResult result = ipcraft::ArtifactCollector::collect(
        requestFor(runRoot,
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("rtl")},
                                          {QStringLiteral("type"), QStringLiteral("rtl")},
                                          {QStringLiteral("glob"), QStringLiteral("out/*.v")},
                                          {QStringLiteral("required"), true}}}));

    require(result.ok, "inside-root artifact glob should succeed");
    require(result.index.records.size() == 2, "glob should collect matching files only");
    require(result.index.records.at(0).path == QStringLiteral("out/a.v"),
            "artifact records should be deterministically sorted");
    require(result.index.records.at(1).path == QStringLiteral("out/b.v"),
            "artifact records should include second match");
}

void testArtifactGlobRejectsTraversal() {
    QTemporaryDir runRoot;
    require(runRoot.isValid(), "run root should be valid");

    const ipcraft::ArtifactCollectResult result = ipcraft::ArtifactCollector::collect(
        requestFor(runRoot,
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("bad")},
                                          {QStringLiteral("type"), QStringLiteral("report")},
                                          {QStringLiteral("glob"), QStringLiteral("../*.log")}}}));

    require(!result.ok, "artifact traversal glob should fail");
    require(hasRule(result.diagnostics, QStringLiteral("artifact.glob_escape")),
            "artifact traversal should emit artifact.glob_escape");
}

void testArtifactGlobRejectsSymlinkEscape() {
    QTemporaryDir runRoot;
    QTemporaryDir outside;
    require(runRoot.isValid(), "run root should be valid");
    require(outside.isValid(), "outside dir should be valid");
    const QString outsideFile = QDir(outside.path()).filePath(QStringLiteral("leak.log"));
    writeFile(outsideFile, "secret\n");
    require(QDir().mkpath(QDir(runRoot.path()).filePath(QStringLiteral("logs"))),
            "logs dir should be created");
    const QString linkPath = QDir(runRoot.path()).filePath(QStringLiteral("logs/leak.log"));
    require(QFile::link(outsideFile, linkPath), "test symlink should be created");

    const ipcraft::ArtifactCollectResult result = ipcraft::ArtifactCollector::collect(
        requestFor(runRoot,
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("logs")},
                                          {QStringLiteral("type"), QStringLiteral("log")},
                                          {QStringLiteral("glob"), QStringLiteral("logs/*.log")}}}));

    require(!result.ok, "artifact symlink escape should fail");
    require(hasRule(result.diagnostics, QStringLiteral("artifact.glob_escape")),
            "artifact symlink escape should emit artifact.glob_escape");
}

void testRequiredArtifactMissingReturnsDiagnostic() {
    QTemporaryDir runRoot;
    require(runRoot.isValid(), "run root should be valid");

    const ipcraft::ArtifactCollectResult result = ipcraft::ArtifactCollector::collect(
        requestFor(runRoot,
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("rtl")},
                                          {QStringLiteral("type"), QStringLiteral("rtl")},
                                          {QStringLiteral("glob"), QStringLiteral("out/*.v")},
                                          {QStringLiteral("required"), true}}}));

    require(!result.ok, "missing required artifact should fail");
    require(hasRule(result.diagnostics, QStringLiteral("artifact.required_missing")),
            "missing required artifact should emit artifact.required_missing");
}

void testArtifactIndexRecordsTypeSizeModifiedTime() {
    QTemporaryDir runRoot;
    require(runRoot.isValid(), "run root should be valid");
    writeFile(QDir(runRoot.path()).filePath(QStringLiteral("reports/summary.txt")),
              "summary\n");

    const ipcraft::ArtifactCollectResult result = ipcraft::ArtifactCollector::collect(
        requestFor(runRoot,
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("summary")},
                                          {QStringLiteral("type"), QStringLiteral("report")},
                                          {QStringLiteral("glob"),
                                           QStringLiteral("reports/*.txt")}}}));

    require(result.ok, "artifact collection should succeed");
    require(result.index.records.size() == 1, "one artifact should be collected");
    const ipcraft::ArtifactRecord& record = result.index.records.first();
    require(record.id == QStringLiteral("summary"), "artifact id should be recorded");
    require(record.type == QStringLiteral("report"), "artifact type should be recorded");
    require(record.size == 8, "artifact size should be recorded");
    require(!record.modifiedTime.isEmpty(), "artifact modified time should be recorded");
    require(record.sourceInstanceId == QStringLiteral("ip0"),
            "source instance should be recorded");
    require(record.flowRunId == QStringLiteral("run0"), "flow run id should be recorded");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testArtifactGlobCollectsInsideRunRoot();
        testArtifactGlobRejectsTraversal();
        testArtifactGlobRejectsSymlinkEscape();
        testRequiredArtifactMissingReturnsDiagnostic();
        testArtifactIndexRecordsTypeSizeModifiedTime();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "ipcraft_artifact_test passed\n";
    return 0;
}
