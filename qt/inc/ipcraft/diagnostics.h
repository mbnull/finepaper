#pragma once

#include "ipcraft/schemaids.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <optional>

namespace ipcraft {

struct DiagnosticLocation {
    QString kind = QStringLiteral("project");
    QString instanceId;
    QString interfaceId;
    QString connectionId;
    QString parameterId;
    QString tableId;
    std::optional<int> row = std::nullopt;
    QJsonValue column;
    QString documentId;
    QString path;
    QString file;
    std::optional<int> line = std::nullopt;
    std::optional<int> columnNumber = std::nullopt;
    QString artifactId;
    QString graphObjectId;
    QJsonObject details;

    QJsonObject toJson() const;
    static DiagnosticLocation fromJson(const QJsonObject& object);
};

struct Diagnostic {
    QString severity = QStringLiteral("error");
    QString source = QStringLiteral("core");
    QString ruleId = QStringLiteral("diagnostic.unspecified");
    QString category;
    QString message;
    QJsonObject details;
    QVector<DiagnosticLocation> locations;

    QJsonObject toJson() const;
    static Diagnostic fromJson(const QJsonObject& object);
};

struct DiagnosticStore {
    QString schema = schemaids::diagnosticsV1;
    QVector<Diagnostic> records;

    QJsonObject toJson() const;
    static DiagnosticStore fromJson(const QJsonObject& object);
};

} // namespace ipcraft
