// Ipcraft V1 diagnostics model smoke tests.
#include "ipcraft/diagnostics.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testDiagnosticStoreRoundTrip() {
    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("core");
    diagnostic.ruleId = QStringLiteral("composition.unknown_interface");
    diagnostic.category = QStringLiteral("composition");
    diagnostic.message = QStringLiteral("Unknown interface");
    diagnostic.details.insert(QStringLiteral("interface"), QStringLiteral("m_axi"));

    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("interface");
    location.instanceId = QStringLiteral("ip0");
    location.interfaceId = QStringLiteral("m_axi");
    diagnostic.locations.append(location);

    ipcraft::DiagnosticStore store;
    store.records.append(diagnostic);

    const QJsonObject object = store.toJson();
    require(object.value(QStringLiteral("schema")).toString() == ipcraft::schemaids::diagnosticsV1,
            "diagnostics schema should be canonical V1");
    require(object.value(QStringLiteral("records")).toArray().size() == 1,
            "diagnostics records should serialize");

    const ipcraft::DiagnosticStore parsed = ipcraft::DiagnosticStore::fromJson(object);
    require(parsed.records.size() == 1, "diagnostics records should parse");
    require(parsed.records.first().ruleId == QStringLiteral("composition.unknown_interface"),
            "diagnostic rule_id should round-trip");
    require(parsed.records.first().locations.size() == 1,
            "diagnostic locations should parse");
    require(parsed.records.first().locations.first().kind == QStringLiteral("interface"),
            "diagnostic location kind should round-trip");
}

ipcraft::Diagnostic makeDiagnostic(const QString& severity,
                                   const QString& source,
                                   const QString& ruleId,
                                   const QString& instanceId) {
    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.source = source;
    diagnostic.ruleId = ruleId;
    diagnostic.message = QStringLiteral("message");

    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("instance");
    location.instanceId = instanceId;
    diagnostic.locations.append(location);
    return diagnostic;
}

void testDiagnosticStoreSerializesRecordsInDeterministicOrder() {
    ipcraft::DiagnosticStore store;
    store.records.append(makeDiagnostic(QStringLiteral("warning"),
                                        QStringLiteral("validator"),
                                        QStringLiteral("z.rule"),
                                        QStringLiteral("ip1")));
    store.records.append(makeDiagnostic(QStringLiteral("error"),
                                        QStringLiteral("builder"),
                                        QStringLiteral("a.rule"),
                                        QStringLiteral("ip0")));

    const QJsonArray records = store.toJson().value(QStringLiteral("records")).toArray();
    require(records.size() == 2, "diagnostics records should serialize");
    require(records.at(0).toObject().value(QStringLiteral("severity")).toString() ==
                QStringLiteral("error"),
            "diagnostics records should sort by severity");
    require(records.at(0).toObject().value(QStringLiteral("source")).toString() ==
                QStringLiteral("builder"),
            "diagnostics records should keep sorted diagnostic content");
    require(records.at(1).toObject().value(QStringLiteral("severity")).toString() ==
                QStringLiteral("warning"),
            "diagnostics records should place lower-priority severity after errors");
}

void testDefaultDiagnosticSerializesErrorSeverity() {
    const QJsonObject object = ipcraft::Diagnostic().toJson();
    require(object.value(QStringLiteral("severity")).toString() == QStringLiteral("error"),
            "default diagnostic severity should serialize as error");
}

void testDefaultLocationSerializesProjectKind() {
    const QJsonObject object = ipcraft::DiagnosticLocation().toJson();
    require(object.value(QStringLiteral("kind")).toString() == QStringLiteral("project"),
            "default diagnostic location kind should serialize as project");
}

void testDiagnosticLocationSkipsInvalidOptionalIntegers() {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("file");
    location.row = -1;
    location.column = -2;
    location.line = -3;
    location.columnNumber = 0;

    const QJsonObject object = location.toJson();
    require(!object.contains(QStringLiteral("row")), "negative row should not serialize");
    require(!object.contains(QStringLiteral("column")), "negative column should not serialize");
    require(!object.contains(QStringLiteral("line")), "negative line should not serialize");
    require(!object.contains(QStringLiteral("column_number")),
            "non-positive column_number should not serialize");
}

void testDiagnosticLocationRoundTripsStringColumn() {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("table_cell");
    location.column = QStringLiteral("latency");

    const QJsonObject object = location.toJson();
    require(object.value(QStringLiteral("column")).toString() == QStringLiteral("latency"),
            "string column should serialize");

    const ipcraft::DiagnosticLocation parsed = ipcraft::DiagnosticLocation::fromJson(object);
    require(parsed.column.toString() == QStringLiteral("latency"),
            "string column should round-trip");
}

void testDiagnosticLocationRejectsInvalidColumnOnParse() {
    QJsonObject object;
    object.insert(QStringLiteral("kind"), QStringLiteral("table_cell"));
    object.insert(QStringLiteral("column"), -1);

    const ipcraft::DiagnosticLocation parsed = ipcraft::DiagnosticLocation::fromJson(object);
    require(!parsed.toJson().contains(QStringLiteral("column")),
            "negative parsed column should not re-serialize");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testDiagnosticStoreRoundTrip();
        testDiagnosticStoreSerializesRecordsInDeterministicOrder();
        testDefaultDiagnosticSerializesErrorSeverity();
        testDefaultLocationSerializesProjectKind();
        testDiagnosticLocationSkipsInvalidOptionalIntegers();
        testDiagnosticLocationRoundTripsStringColumn();
        testDiagnosticLocationRejectsInvalidColumnOnParse();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "ipcraft_diagnostics_test passed\n";
    return 0;
}
