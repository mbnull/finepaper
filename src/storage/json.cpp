#include "storage/json.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace finepaper {
namespace {

void appendError(QVector<Diagnostic>& diagnostics,
                 const QString& code,
                 const QString& message,
                 const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("storage")
    });
}

QString stringValue(const QJsonObject& object,
                    const QString& key,
                    const QString& path,
                    QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_string"),
                    QStringLiteral("%1 must be a string").arg(key),
                    path + QLatin1Char('/') + key);
        return {};
    }
    return value.toString();
}

int integerValue(const QJsonObject& object,
                 const QString& key,
                 const QString& path,
                 QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_integer"),
                    QStringLiteral("%1 must be an integer").arg(key),
                    path + QLatin1Char('/') + key);
        return 0;
    }
    const int integer = value.toInt();
    if (value.toDouble() != static_cast<double>(integer)) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_integer"),
                    QStringLiteral("%1 must be an integer").arg(key),
                    path + QLatin1Char('/') + key);
    }
    return integer;
}

std::optional<RouterPosition> routerPositionFromJson(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    if (!value.isObject()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("router must be an object"),
                    path);
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    return RouterPosition{
        integerValue(object, QStringLiteral("x"), path, diagnostics),
        integerValue(object, QStringLiteral("y"), path, diagnostics)
    };
}

} // namespace

JsonObjectLoadResult loadJsonObject(const QString& path) {
    JsonObjectLoadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendError(result.diagnostics,
                    QStringLiteral("json.read_failed"),
                    QStringLiteral("could not read %1").arg(path),
                    path);
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.parse_failed"),
                    error.errorString(),
                    path);
        return result;
    }
    result.success = true;
    result.object = document.object();
    return result;
}

bool saveJsonObject(const QString& path,
                    const QJsonObject& object,
                    QVector<Diagnostic>* diagnostics) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.create_directory_failed"),
                        QStringLiteral("could not create output directory"),
                        info.absolutePath());
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not open file for writing"),
                        path);
        }
        return false;
    }
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit()) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not write JSON file"),
                        path);
        }
        return false;
    }
    return true;
}

QJsonObject designToJson(const NocDesign& design) {
    QJsonObject package{
        {QStringLiteral("id"), design.package.id},
        {QStringLiteral("version"), design.package.version}
    };
    QJsonObject topology{
        {QStringLiteral("type"), design.topology.type},
        {QStringLiteral("rows"), design.topology.rows},
        {QStringLiteral("columns"), design.topology.columns}
    };
    QJsonArray endpoints;
    for (const EndpointInstance& endpoint : design.endpoints) {
        QJsonObject router{
            {QStringLiteral("x"), endpoint.attachment.router.x},
            {QStringLiteral("y"), endpoint.attachment.router.y}
        };
        QJsonObject attachment{{QStringLiteral("router"), router}};
        if (endpoint.attachment.slot && !endpoint.attachment.slot->isEmpty()) {
            attachment.insert(QStringLiteral("slot"), *endpoint.attachment.slot);
        }
        endpoints.append(QJsonObject{
            {QStringLiteral("id"), endpoint.id},
            {QStringLiteral("type"), endpoint.type},
            {QStringLiteral("attachment"), attachment},
            {QStringLiteral("parameters"), endpoint.parameters}
        });
    }

    QJsonObject object{
        {QStringLiteral("format"), design.format},
        {QStringLiteral("formatVersion"), design.formatVersion},
        {QStringLiteral("id"), design.id},
        {QStringLiteral("name"), design.name},
        {QStringLiteral("package"), package},
        {QStringLiteral("topology"), topology},
        {QStringLiteral("parameters"), design.parameters},
        {QStringLiteral("endpoints"), endpoints}
    };
    if (!design.packageData.isEmpty()) {
        object.insert(QStringLiteral("packageData"), design.packageData);
    }
    return object;
}

DesignLoadResult designFromJson(const QJsonObject& object) {
    DesignLoadResult result;
    NocDesign design;
    design.format = stringValue(object,
                                QStringLiteral("format"),
                                QString(),
                                result.diagnostics);
    design.formatVersion = integerValue(object,
                                        QStringLiteral("formatVersion"),
                                        QString(),
                                        result.diagnostics);
    design.id = stringValue(object, QStringLiteral("id"), QString(), result.diagnostics);
    design.name = stringValue(object, QStringLiteral("name"), QString(), result.diagnostics);

    const QJsonValue packageValue = object.value(QStringLiteral("package"));
    if (!packageValue.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("package must be an object"),
                    QStringLiteral("/package"));
    } else {
        const QJsonObject package = packageValue.toObject();
        design.package.id = stringValue(package,
                                        QStringLiteral("id"),
                                        QStringLiteral("/package"),
                                        result.diagnostics);
        design.package.version = stringValue(package,
                                             QStringLiteral("version"),
                                             QStringLiteral("/package"),
                                             result.diagnostics);
    }

    const QJsonValue topologyValue = object.value(QStringLiteral("topology"));
    if (!topologyValue.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("topology must be an object"),
                    QStringLiteral("/topology"));
    } else {
        const QJsonObject topology = topologyValue.toObject();
        design.topology.type = stringValue(topology,
                                           QStringLiteral("type"),
                                           QStringLiteral("/topology"),
                                           result.diagnostics);
        design.topology.rows = integerValue(topology,
                                            QStringLiteral("rows"),
                                            QStringLiteral("/topology"),
                                            result.diagnostics);
        design.topology.columns = integerValue(topology,
                                               QStringLiteral("columns"),
                                               QStringLiteral("/topology"),
                                               result.diagnostics);
    }

    if (!object.value(QStringLiteral("parameters")).isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("parameters must be an object"),
                    QStringLiteral("/parameters"));
    } else {
        design.parameters = object.value(QStringLiteral("parameters")).toObject();
    }

    const QJsonValue endpointsValue = object.value(QStringLiteral("endpoints"));
    if (!endpointsValue.isArray()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_array"),
                    QStringLiteral("endpoints must be an array"),
                    QStringLiteral("/endpoints"));
    } else {
        const QJsonArray endpoints = endpointsValue.toArray();
        for (qsizetype index = 0; index < endpoints.size(); ++index) {
            const QString base = QStringLiteral("/endpoints/%1").arg(index);
            if (!endpoints.at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("endpoint must be an object"),
                            base);
                continue;
            }
            const QJsonObject object = endpoints.at(index).toObject();
            EndpointInstance endpoint;
            endpoint.id = stringValue(object,
                                      QStringLiteral("id"),
                                      base,
                                      result.diagnostics);
            endpoint.type = stringValue(object,
                                        QStringLiteral("type"),
                                        base,
                                        result.diagnostics);
            const QJsonValue attachmentValue = object.value(QStringLiteral("attachment"));
            if (!attachmentValue.isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("attachment must be an object"),
                            base + QStringLiteral("/attachment"));
            } else {
                const QJsonObject attachment = attachmentValue.toObject();
                const auto router = routerPositionFromJson(
                    attachment.value(QStringLiteral("router")),
                    base + QStringLiteral("/attachment/router"),
                    result.diagnostics);
                if (router) {
                    endpoint.attachment.router = *router;
                }
                if (attachment.contains(QStringLiteral("slot"))) {
                    if (!attachment.value(QStringLiteral("slot")).isString()) {
                        appendError(result.diagnostics,
                                    QStringLiteral("json.expected_string"),
                                    QStringLiteral("slot must be a string"),
                                    base + QStringLiteral("/attachment/slot"));
                    } else {
                        endpoint.attachment.slot = attachment.value(QStringLiteral("slot")).toString();
                    }
                }
            }
            if (!object.value(QStringLiteral("parameters")).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("parameters must be an object"),
                            base + QStringLiteral("/parameters"));
            } else {
                endpoint.parameters = object.value(QStringLiteral("parameters")).toObject();
            }
            design.endpoints.append(std::move(endpoint));
        }
    }

    if (object.contains(QStringLiteral("packageData"))) {
        if (!object.value(QStringLiteral("packageData")).isObject()) {
            appendError(result.diagnostics,
                        QStringLiteral("json.expected_object"),
                        QStringLiteral("packageData must be an object"),
                        QStringLiteral("/packageData"));
        } else {
            design.packageData = object.value(QStringLiteral("packageData")).toObject();
        }
    }

    result.diagnostics += validateDesignStructure(design);
    result.success = !hasErrors(result.diagnostics);
    result.design = std::move(design);
    return result;
}

DesignLoadResult loadDesign(const QString& path) {
    const JsonObjectLoadResult json = loadJsonObject(path);
    if (!json.success) {
        DesignLoadResult result;
        result.diagnostics = json.diagnostics;
        return result;
    }
    return designFromJson(json.object);
}

bool saveDesign(const QString& path,
                const NocDesign& design,
                QVector<Diagnostic>* diagnostics) {
    return saveJsonObject(path, designToJson(design), diagnostics);
}

QJsonObject diagnosticToJson(const Diagnostic& diagnostic) {
    return QJsonObject{
        {QStringLiteral("severity"), diagnostic.severity},
        {QStringLiteral("code"), diagnostic.code},
        {QStringLiteral("message"), diagnostic.message},
        {QStringLiteral("path"), diagnostic.path},
        {QStringLiteral("source"), diagnostic.source}
    };
}

QJsonArray diagnosticsToJson(const QVector<Diagnostic>& diagnostics) {
    QJsonArray array;
    for (const Diagnostic& diagnostic : diagnostics) {
        array.append(diagnosticToJson(diagnostic));
    }
    return array;
}

} // namespace finepaper
