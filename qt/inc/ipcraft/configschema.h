#pragma once

#include "ipcraft/diagnostics.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace ipcraft {

struct ParameterDef {
    QString id;
    QString type;
    bool required = false;
    QString valueType;
    QJsonArray enumValues;
    bool hasRangeMin = false;
    bool hasRangeMax = false;
    double rangeMin = 0.0;
    double rangeMax = 0.0;
    QJsonValue defaultValue;
    QJsonObject defaultWhen;
    QJsonObject visibleWhen;
    QJsonObject enabledWhen;
    QJsonObject requiredWhen;
};

struct TableColumnDef {
    QString id;
    QString type;
    bool required = false;
};

struct TableDef {
    QString id;
    QVector<TableColumnDef> columns;
    bool allowAddRemove = true;
    bool preserveUnknownColumns = false;
};

struct ConfigDocumentDef {
    QString id;
    QString format;
    QString outputPath;
    bool editable = true;
    bool preserveUnknownFields = false;
};

struct FileInputDef {
    QString id;
    QString kind;
    QStringList allowedExtensions;
    bool required = false;
};

struct ConfigBundle {
    QJsonObject parameters;
    QJsonObject tables;
    QJsonObject documents;
    QJsonObject files;
    QJsonObject preserved;

    static ConfigBundle fromJson(const QJsonObject& object);
    QJsonObject toJson() const;
};

struct ConfigSchemaReadResult;

struct ConfigSchema {
    QVector<ParameterDef> parameters;
    QVector<TableDef> tables;
    QVector<ConfigDocumentDef> documents;
    QVector<FileInputDef> files;
    QJsonObject metadata;
    QJsonObject native;

    static ConfigSchemaReadResult fromJson(const QJsonObject& object);
};

struct ConfigSchemaReadResult {
    bool ok = false;
    ConfigSchema schema;
    DiagnosticStore diagnostics;
};

struct ConfigValidationOptions {
    QString projectRootPath;
};

struct ConfigValidationResult {
    bool ok = false;
    ConfigBundle normalized;
    DiagnosticStore diagnostics;
};

bool evaluateConfigExpression(const QJsonObject& expression,
                              const ConfigBundle& bundle,
                              DiagnosticStore* diagnostics,
                              const QString& path);

ConfigValidationResult validateConfigBundle(
    const ConfigSchema& schema,
    const ConfigBundle& bundle,
    const ConfigValidationOptions& options = {});

} // namespace ipcraft
