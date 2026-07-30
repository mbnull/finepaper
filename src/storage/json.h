#pragma once

#include "noc/model.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace finepaper {

struct JsonObjectLoadResult {
    bool success = false;
    QJsonObject object;
    QVector<Diagnostic> diagnostics;
};

struct DesignLoadResult {
    bool success = false;
    NocDesign design;
    QVector<Diagnostic> diagnostics;
};

struct ElementConfigurationsParseResult {
    bool success = false;
    QVector<ElementConfiguration> configurations;
    QVector<Diagnostic> diagnostics;
};

JsonObjectLoadResult loadJsonObject(const QString& path);
bool saveJsonObject(const QString& path,
                    const QJsonObject& object,
                    QVector<Diagnostic>* diagnostics = nullptr);

QJsonObject designToJson(const NocDesign& design);
ElementConfigurationsParseResult parseElementConfigurations(
    const QJsonValue& value,
    const QString& basePath = QStringLiteral("/elementConfigurations"));
DesignLoadResult designFromJson(const QJsonObject& object);
DesignLoadResult loadDesign(const QString& path);
bool saveDesign(const QString& path,
                const NocDesign& design,
                QVector<Diagnostic>* diagnostics = nullptr);

QJsonObject diagnosticToJson(const Diagnostic& diagnostic);
QJsonArray diagnosticsToJson(const QVector<Diagnostic>& diagnostics);

} // namespace finepaper
