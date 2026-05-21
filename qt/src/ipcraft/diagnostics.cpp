#include "ipcraft/diagnostics.h"

#include "ipcraft/jsonhelpers.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>

namespace {

void insertString(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

void insertOptionalInt(QJsonObject& object,
                       const QString& key,
                       const std::optional<int>& value,
                       int minimum) {
    if (value.has_value() && *value >= minimum) {
        object.insert(key, *value);
    }
}

void insertColumn(QJsonObject& object, const QJsonValue& value) {
    if (value.isString()) {
        const QString stringValue = value.toString();
        if (!stringValue.isEmpty()) {
            object.insert(QStringLiteral("column"), stringValue);
        }
        return;
    }
    if (!value.isDouble()) {
        return;
    }

    const double doubleValue = value.toDouble();
    const int intValue = value.toInt();
    if (intValue >= 0 && static_cast<double>(intValue) == doubleValue) {
        object.insert(QStringLiteral("column"), intValue);
    }
}

int severityRank(const QString& severity) {
    if (severity == QStringLiteral("error")) {
        return 0;
    }
    if (severity == QStringLiteral("warning")) {
        return 1;
    }
    if (severity == QStringLiteral("info")) {
        return 2;
    }
    return 3;
}

QByteArray firstLocationSortKey(const ipcraft::Diagnostic& diagnostic) {
    if (diagnostic.locations.isEmpty()) {
        return QByteArray{};
    }

    return QJsonDocument(diagnostic.locations.first().toJson()).toJson(QJsonDocument::Compact);
}

QByteArray diagnosticSortKey(const ipcraft::Diagnostic& diagnostic) {
    return QJsonDocument(diagnostic.toJson()).toJson(QJsonDocument::Compact);
}

bool diagnosticLess(const ipcraft::Diagnostic& left,
                    const ipcraft::Diagnostic& right) {
    const int leftSeverityRank = severityRank(left.severity);
    const int rightSeverityRank = severityRank(right.severity);
    if (leftSeverityRank != rightSeverityRank) {
        return leftSeverityRank < rightSeverityRank;
    }

    const int severityCompare = QString::compare(left.severity, right.severity, Qt::CaseSensitive);
    if (severityCompare != 0) {
        return severityCompare < 0;
    }

    const int sourceCompare = QString::compare(left.source, right.source, Qt::CaseSensitive);
    if (sourceCompare != 0) {
        return sourceCompare < 0;
    }

    const int ruleCompare = QString::compare(left.ruleId, right.ruleId, Qt::CaseSensitive);
    if (ruleCompare != 0) {
        return ruleCompare < 0;
    }

    const QByteArray leftLocationKey = firstLocationSortKey(left);
    const QByteArray rightLocationKey = firstLocationSortKey(right);
    if (leftLocationKey != rightLocationKey) {
        return leftLocationKey < rightLocationKey;
    }

    return diagnosticSortKey(left) < diagnosticSortKey(right);
}

QString stringValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString{};
}

std::optional<int> optionalIntValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }

    const double doubleValue = value.toDouble();
    const int intValue = value.toInt();
    if (static_cast<double>(intValue) != doubleValue) {
        return std::nullopt;
    }
    return intValue;
}

QJsonValue columnValue(const QJsonObject& object) {
    const QJsonValue value = object.value(QStringLiteral("column"));
    if (value.isString() && !value.toString().isEmpty()) {
        return value;
    }
    if (!value.isDouble()) {
        return {};
    }

    const double doubleValue = value.toDouble();
    const int intValue = value.toInt();
    if (intValue >= 0 && static_cast<double>(intValue) == doubleValue) {
        return intValue;
    }
    return {};
}

QString requiredStringValue(const QJsonObject& object,
                            const QString& key,
                            const QString& fallback) {
    const QString value = stringValue(object, key);
    return value.isEmpty() ? fallback : value;
}

QJsonObject objectValue(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

} // namespace

namespace ipcraft {

QJsonObject DiagnosticLocation::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("kind"),
                  kind.isEmpty() ? QStringLiteral("project") : kind);
    insertString(object, QStringLiteral("instance_id"), instanceId);
    insertString(object, QStringLiteral("interface_id"), interfaceId);
    insertString(object, QStringLiteral("connection_id"), connectionId);
    insertString(object, QStringLiteral("parameter_id"), parameterId);
    insertString(object, QStringLiteral("table_id"), tableId);
    insertOptionalInt(object, QStringLiteral("row"), row, 0);
    insertColumn(object, column);
    insertString(object, QStringLiteral("document_id"), documentId);
    insertString(object, QStringLiteral("path"), path);
    insertString(object, QStringLiteral("file"), file);
    insertOptionalInt(object, QStringLiteral("line"), line, 1);
    insertOptionalInt(object, QStringLiteral("column_number"), columnNumber, 1);
    insertString(object, QStringLiteral("artifact_id"), artifactId);
    insertString(object, QStringLiteral("graph_object_id"), graphObjectId);
    if (!details.isEmpty()) {
        object.insert(QStringLiteral("details"), details);
    }
    return sortedJsonObject(object);
}

DiagnosticLocation DiagnosticLocation::fromJson(const QJsonObject& object) {
    DiagnosticLocation location;
    location.kind = requiredStringValue(object,
                                        QStringLiteral("kind"),
                                        QStringLiteral("project"));
    location.instanceId = stringValue(object, QStringLiteral("instance_id"));
    location.interfaceId = stringValue(object, QStringLiteral("interface_id"));
    location.connectionId = stringValue(object, QStringLiteral("connection_id"));
    location.parameterId = stringValue(object, QStringLiteral("parameter_id"));
    location.tableId = stringValue(object, QStringLiteral("table_id"));
    location.row = optionalIntValue(object, QStringLiteral("row"));
    location.column = columnValue(object);
    location.documentId = stringValue(object, QStringLiteral("document_id"));
    location.path = stringValue(object, QStringLiteral("path"));
    location.file = stringValue(object, QStringLiteral("file"));
    location.line = optionalIntValue(object, QStringLiteral("line"));
    location.columnNumber = optionalIntValue(object, QStringLiteral("column_number"));
    location.artifactId = stringValue(object, QStringLiteral("artifact_id"));
    location.graphObjectId = stringValue(object, QStringLiteral("graph_object_id"));
    location.details = objectValue(object, QStringLiteral("details"));
    return location;
}

QJsonObject Diagnostic::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("severity"), severity);
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("rule_id"), ruleId);
    insertString(object, QStringLiteral("category"), category);
    object.insert(QStringLiteral("message"), message);
    if (!details.isEmpty()) {
        object.insert(QStringLiteral("details"), details);
    }

    QJsonArray locationArray;
    for (const DiagnosticLocation& location : locations) {
        locationArray.append(location.toJson());
    }
    object.insert(QStringLiteral("locations"), locationArray);
    return sortedJsonObject(object);
}

Diagnostic Diagnostic::fromJson(const QJsonObject& object) {
    Diagnostic diagnostic;
    diagnostic.severity = requiredStringValue(object,
                                              QStringLiteral("severity"),
                                              QStringLiteral("error"));
    diagnostic.source = requiredStringValue(object,
                                            QStringLiteral("source"),
                                            QStringLiteral("core"));
    diagnostic.ruleId = requiredStringValue(object,
                                            QStringLiteral("rule_id"),
                                            QStringLiteral("diagnostic.unspecified"));
    diagnostic.category = stringValue(object, QStringLiteral("category"));
    diagnostic.message = stringValue(object, QStringLiteral("message"));
    diagnostic.details = objectValue(object, QStringLiteral("details"));

    const QJsonValue locationsValue = object.value(QStringLiteral("locations"));
    if (locationsValue.isArray()) {
        const QJsonArray locationsArray = locationsValue.toArray();
        for (const QJsonValue& locationValue : locationsArray) {
            if (locationValue.isObject()) {
                diagnostic.locations.append(
                    DiagnosticLocation::fromJson(locationValue.toObject()));
            }
        }
    }

    return diagnostic;
}

QJsonObject DiagnosticStore::toJson() const {
    QJsonArray recordsArray;
    QVector<Diagnostic> sortedRecords = records;
    std::stable_sort(sortedRecords.begin(), sortedRecords.end(), diagnosticLess);
    for (const Diagnostic& diagnostic : sortedRecords) {
        recordsArray.append(diagnostic.toJson());
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), schema.isEmpty() ? schemaids::diagnosticsV1 : schema);
    object.insert(QStringLiteral("records"), recordsArray);
    return sortedJsonObject(object);
}

DiagnosticStore DiagnosticStore::fromJson(const QJsonObject& object) {
    DiagnosticStore store;
    store.schema = stringValue(object, QStringLiteral("schema"));
    if (store.schema.isEmpty()) {
        store.schema = schemaids::diagnosticsV1;
    }

    const QJsonValue recordsValue = object.value(QStringLiteral("records"));
    if (recordsValue.isArray()) {
        const QJsonArray recordsArray = recordsValue.toArray();
        for (const QJsonValue& recordValue : recordsArray) {
            if (recordValue.isObject()) {
                store.records.append(Diagnostic::fromJson(recordValue.toObject()));
            }
        }
    }

    return store;
}

} // namespace ipcraft
