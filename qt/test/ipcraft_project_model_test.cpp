// Ipcraft V1 project document model smoke tests.
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"
#include "project/projectwriter.h"

#include <QCoreApplication>
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

QJsonObject diagnosticsObject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::diagnosticsV1},
        {QStringLiteral("records"), QJsonArray{}}
    };
}

QJsonObject contractProjectObject() {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::projectV1},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("project_0")},
            {QStringLiteral("name"), QStringLiteral("Contract Project")}
        }},
        {QStringLiteral("instances"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("ip0")},
                {QStringLiteral("display_name"), QStringLiteral("IP 0")},
                {QStringLiteral("package"), QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("vendor.example.simple")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")}
                }},
                {QStringLiteral("config"), QJsonObject{
                    {QStringLiteral("parameters"), QJsonObject{
                        {QStringLiteral("width"), 64}
                    }}
                }},
                {QStringLiteral("native"), QJsonObject{
                    {QStringLiteral("vendor.example"), QJsonObject{
                        {QStringLiteral("opaque"), true}
                    }}
                }}
            }
        }},
        {QStringLiteral("composition"), QJsonObject{
            {QStringLiteral("connections"), QJsonArray{}},
            {QStringLiteral("external_ports"), QJsonArray{}}
        }},
        {QStringLiteral("layout"), QJsonObject{
            {QStringLiteral("views"), QJsonArray{}}
        }},
        {QStringLiteral("diagnostics"), diagnosticsObject()},
        {QStringLiteral("artifacts"), QJsonObject{}},
        {QStringLiteral("migration"), QJsonObject{}},
        {QStringLiteral("native"), QJsonObject{
            {QStringLiteral("vendor.example"), QJsonObject{
                {QStringLiteral("project"), true}
            }}
        }}
    };
}

QString writeFixture(const QJsonObject& object, const QString& fileName) {
    static QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = tempDir.filePath(fileName);

    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "fixture should open for writing");
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    require(file.write(content) == content.size(), "fixture should write JSON");
    return path;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "file should open for reading");
    return file.readAll();
}

QJsonObject parseObject(const QByteArray& bytes) {
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    require(document.isObject(), "JSON should parse as object");
    return document.object();
}

bool hasDiagnosticRule(const ProjectReadResult& result, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : result.diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

void testProjectDocumentRoundTripsInstancesCompositionLayoutNative() {
    const QString path = writeFixture(contractProjectObject(),
                                      QStringLiteral("roundtrip.ipcraft.json"));

    const ProjectReadResult readResult = ProjectReader::readFile(path);
    require(readResult.success, "project reader should accept ipcraft.project.v1");
    require(readResult.document.projectId == QStringLiteral("project_0"),
            "project id should parse");
    require(readResult.document.projectName == QStringLiteral("Contract Project"),
            "project name should parse");
    require(readResult.document.instances.size() == 1, "instance should parse");
    require(readResult.document.instances.first().id == QStringLiteral("ip0"),
            "instance id should parse");
    require(readResult.document.instances.first().package.id ==
                QStringLiteral("vendor.example.simple"),
            "package id should parse");
    require(readResult.document.instances.first().config
                .value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 64,
            "config parameters should parse");
    require(readResult.document.instances.first().native
                .value(QStringLiteral("vendor.example")).toObject()
                .value(QStringLiteral("opaque")).toBool(),
            "instance native data should parse");
    require(readResult.document.composition.connections.isEmpty(),
            "composition connections should parse");
    require(readResult.document.composition.externalPorts.isEmpty(),
            "composition external ports should parse");
    require(readResult.document.layout.value(QStringLiteral("views")).isArray(),
            "layout should parse");
    require(readResult.document.native.value(QStringLiteral("vendor.example")).toObject()
                .value(QStringLiteral("project")).toBool(),
            "project native data should parse");

    const QString outPath = writeFixture(QJsonObject{}, QStringLiteral("roundtrip-out.json"));
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(outPath,
                                                                    readResult.document);
    require(writeResult.success, "project writer should succeed");

    const QJsonObject written = parseObject(readFile(outPath));
    require(written.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::projectV1,
            "writer should emit project V1 schema");
    require(written.value(QStringLiteral("project")).toObject()
                .value(QStringLiteral("id")).toString() == QStringLiteral("project_0"),
            "writer should preserve project id");
    require(written.value(QStringLiteral("instances")).toArray().first().toObject()
                .value(QStringLiteral("native")).toObject()
                .value(QStringLiteral("vendor.example")).toObject()
                .value(QStringLiteral("opaque")).toBool(),
            "writer should preserve instance native data");
}

void testProjectReaderRejectsUnsupportedSchema() {
    QJsonObject object = contractProjectObject();
    object.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.project.v0"));

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("unsupported-schema.json")));
    require(!result.success, "unsupported project schema should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.unsupported_schema")),
            "unsupported schema should emit stable diagnostic rule id");
}

void testProjectReaderRejectsDuplicateInstanceIds() {
    QJsonObject object = contractProjectObject();
    QJsonArray instances = object.value(QStringLiteral("instances")).toArray();
    instances.append(instances.first());
    object.insert(QStringLiteral("instances"), instances);

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("duplicate-instances.json")));
    require(!result.success, "duplicate instance ids should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.duplicate_id")),
            "duplicate instance ids should emit stable diagnostic rule id");
}

void testProjectWriterUsesDeterministicJson() {
    const ProjectReadResult readResult = ProjectReader::readFile(
        writeFixture(contractProjectObject(), QStringLiteral("deterministic.json")));
    require(readResult.success, "input project should parse");

    const QString firstPath = writeFixture(QJsonObject{}, QStringLiteral("first-out.json"));
    const QString secondPath = writeFixture(QJsonObject{}, QStringLiteral("second-out.json"));
    require(ProjectWriter::writeFile(firstPath, readResult.document).success,
            "first write should succeed");
    require(ProjectWriter::writeFile(secondPath, readResult.document).success,
            "second write should succeed");
    require(readFile(firstPath) == readFile(secondPath),
            "project writer output should be deterministic");
}

void testProjectReaderRejectsUnknownInstanceField() {
    QJsonObject object = contractProjectObject();
    QJsonArray instances = object.value(QStringLiteral("instances")).toArray();
    QJsonObject instance = instances.first().toObject();
    instance.insert(QStringLiteral("unexpected"), true);
    instances.replace(0, instance);
    object.insert(QStringLiteral("instances"), instances);

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("unknown-instance-field.json")));
    require(!result.success, "unknown instance fields should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.unknown_field")),
            "unknown instance fields should emit a stable diagnostic rule id");
}

void testProjectReaderRejectsConnectionWithTooFewEndpoints() {
    QJsonObject object = contractProjectObject();
    object.insert(QStringLiteral("composition"), QJsonObject{
        {QStringLiteral("connections"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("conn0")},
                {QStringLiteral("type"), QStringLiteral("interface")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("instance"), QStringLiteral("ip0")},
                        {QStringLiteral("interface"), QStringLiteral("m_axi")}
                    }
                }}
            }
        }},
        {QStringLiteral("external_ports"), QJsonArray{}}
    });

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("too-few-endpoints.json")));
    require(!result.success, "connection with too few endpoints should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.missing_required")),
            "malformed connection should emit a stable diagnostic rule id");
}

void testProjectReaderRejectsMalformedDiagnosticsStore() {
    QJsonObject object = contractProjectObject();
    object.insert(QStringLiteral("diagnostics"), QJsonObject{});

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("malformed-diagnostics.json")));
    require(!result.success, "malformed diagnostics store should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.invalid_value")) ||
                hasDiagnosticRule(result, QStringLiteral("project.type_mismatch")),
            "malformed diagnostics should emit a stable diagnostic rule id");
}

void testProjectReaderRejectsMalformedDiagnosticRecord() {
    QJsonObject object = contractProjectObject();
    object.insert(QStringLiteral("diagnostics"), QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::diagnosticsV1},
        {QStringLiteral("records"), QJsonArray{QStringLiteral("not-an-object")}}
    });

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("malformed-diagnostic-record.json")));
    require(!result.success, "malformed diagnostic records should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.type_mismatch")),
            "malformed diagnostic records should emit a stable diagnostic rule id");
}

void testProjectReaderRejectsNonStringOptionalFields() {
    QJsonObject object = contractProjectObject();
    QJsonArray instances = object.value(QStringLiteral("instances")).toArray();
    QJsonObject instance = instances.first().toObject();
    instance.insert(QStringLiteral("display_name"), 7);
    instances.replace(0, instance);
    object.insert(QStringLiteral("instances"), instances);

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("non-string-display-name.json")));
    require(!result.success, "non-string optional string fields should fail");
    require(hasDiagnosticRule(result, QStringLiteral("project.type_mismatch")),
            "non-string optional field should emit a stable diagnostic rule id");
}

void testProjectReaderRejectsUnknownEndpointInstance() {
    QJsonObject object = contractProjectObject();
    object.insert(QStringLiteral("composition"), QJsonObject{
        {QStringLiteral("connections"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("conn0")},
                {QStringLiteral("type"), QStringLiteral("interface")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("instance"), QStringLiteral("ip0")},
                        {QStringLiteral("interface"), QStringLiteral("m_axi")}
                    },
                    QJsonObject{
                        {QStringLiteral("instance"), QStringLiteral("missing")},
                        {QStringLiteral("interface"), QStringLiteral("s_axi")}
                    }
                }}
            }
        }},
        {QStringLiteral("external_ports"), QJsonArray{}}
    });

    const ProjectReadResult result = ProjectReader::readFile(
        writeFixture(object, QStringLiteral("unknown-endpoint-instance.json")));
    require(!result.success, "connections should reference declared instances");
    require(hasDiagnosticRule(result, QStringLiteral("project.unknown_instance")),
            "unknown endpoint instance should emit a stable diagnostic rule id");
}

void testProjectWriterUsesCanonicalSchemaAndRejectsLegacyAliases() {
    ProjectDocument document;
    document.schema = QStringLiteral("legacy");
    document.projectId = QStringLiteral("project_0");
    document.projectName = QStringLiteral("Contract Project");

    const QString outPath = writeFixture(QJsonObject{}, QStringLiteral("canonical-schema.json"));
    require(ProjectWriter::writeFile(outPath, document).success,
            "writer should accept a valid minimal document");
    const QJsonObject written = parseObject(readFile(outPath));
    require(written.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::projectV1,
            "writer should always emit canonical project schema");

    ProjectIpInstanceRecord legacyOnlyInstance;
    legacyOnlyInstance.instanceId = QStringLiteral("legacy_ip0");
    legacyOnlyInstance.ipcoreId = QStringLiteral("legacy.package");
    legacyOnlyInstance.package.version = QStringLiteral("1.0.0");
    document.instances.append(legacyOnlyInstance);

    const ProjectWriteResult result = ProjectWriter::writeFile(
        writeFixture(QJsonObject{}, QStringLiteral("legacy-alias-output.json")),
        document);
    require(!result.success, "writer should not synthesize V1 fields from legacy aliases");
}

void testProjectWriterRejectsUnknownEndpointInstance() {
    ProjectDocument document;
    document.projectId = QStringLiteral("project_0");
    document.projectName = QStringLiteral("Contract Project");

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("ip0");
    instance.package.id = QStringLiteral("vendor.example.simple");
    instance.package.version = QStringLiteral("1.0.0");
    document.instances.append(instance);

    ProjectConnectionRecord connection;
    connection.id = QStringLiteral("conn0");
    connection.type = QStringLiteral("interface");
    connection.endpoints = {
        ProjectEndpointRef{QStringLiteral("ip0"), QStringLiteral("m_axi")},
        ProjectEndpointRef{QStringLiteral("missing"), QStringLiteral("s_axi")}
    };
    document.composition.connections.append(connection);

    const ProjectWriteResult result = ProjectWriter::writeFile(
        writeFixture(QJsonObject{}, QStringLiteral("writer-unknown-endpoint.json")),
        document);
    require(!result.success, "writer should reject undeclared endpoint instances");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testProjectDocumentRoundTripsInstancesCompositionLayoutNative();
        testProjectReaderRejectsUnsupportedSchema();
        testProjectReaderRejectsDuplicateInstanceIds();
        testProjectWriterUsesDeterministicJson();
        testProjectReaderRejectsUnknownInstanceField();
        testProjectReaderRejectsConnectionWithTooFewEndpoints();
        testProjectReaderRejectsMalformedDiagnosticsStore();
        testProjectReaderRejectsMalformedDiagnosticRecord();
        testProjectReaderRejectsNonStringOptionalFields();
        testProjectReaderRejectsUnknownEndpointInstance();
        testProjectWriterUsesCanonicalSchemaAndRejectsLegacyAliases();
        testProjectWriterRejectsUnknownEndpointInstance();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "ipcraft_project_model_test passed\n";
    return 0;
}
