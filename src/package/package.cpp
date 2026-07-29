#include "package/package.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path,
                      const QString& source = QStringLiteral("package")) {
    diagnostics.append(Diagnostic{severity, code, message, path, source});
}

std::optional<QJsonObject> readObject(const QString& path, QVector<Diagnostic>& diagnostics) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.read_failed"),
                         QStringLiteral("could not read %1").arg(path),
                         path);
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_json"),
                         error.errorString(),
                         path);
        return std::nullopt;
    }
    return document.object();
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.missing_field"),
                         QStringLiteral("%1 must be a non-empty string").arg(key),
                         path + QLatin1Char('/') + key);
        return {};
    }
    return value.toString().trimmed();
}

int requiredInteger(const QJsonObject& object,
                    const QString& key,
                    int fallback,
                    const QString& path,
                    QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble() ||
        !std::isfinite(value.toDouble()) ||
        std::floor(value.toDouble()) != value.toDouble() ||
        value.toDouble() < static_cast<double>(std::numeric_limits<int>::min()) ||
        value.toDouble() > static_cast<double>(std::numeric_limits<int>::max())) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_integer"),
                         QStringLiteral("%1 must be an integer").arg(key),
                         path + QLatin1Char('/') + key);
        return fallback;
    }
    return value.toInt();
}

QJsonObject requiredObject(const QJsonObject& object,
                           const QString& key,
                           const QString& path,
                           QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_object"),
                         QStringLiteral("%1 must be an object").arg(key),
                         path + QLatin1Char('/') + key);
        return {};
    }
    return value.toObject();
}

QString optionalString(const QJsonObject& object,
                       const QString& key,
                       const QString& fallback,
                       const QString& path,
                       QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_string"),
                         QStringLiteral("%1 must be a string").arg(key),
                         path + QLatin1Char('/') + key);
        return fallback;
    }
    return value.toString();
}

bool optionalBoolean(const QJsonObject& object,
                     const QString& key,
                     bool fallback,
                     const QString& path,
                     QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_boolean"),
                         QStringLiteral("%1 must be a boolean").arg(key),
                         path + QLatin1Char('/') + key);
        return fallback;
    }
    return value.toBool();
}

int optionalTimeoutSeconds(const QJsonObject& object,
                           const QString& key,
                           int fallback,
                           const QString& path,
                           QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return fallback;
    }
    const int timeout = requiredInteger(object, key, fallback, path, diagnostics);
    if (timeout <= 0 || timeout > kMaximumPackageTimeoutSeconds) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_timeout"),
            QStringLiteral("%1 must be between 1 and %2 seconds")
                .arg(key)
                .arg(kMaximumPackageTimeoutSeconds),
            path + QLatin1Char('/') + key);
    }
    return timeout;
}

ParameterType parameterTypeFromId(const QString& type) {
    if (type == QStringLiteral("integer")) return ParameterType::Integer;
    if (type == QStringLiteral("number")) return ParameterType::Number;
    if (type == QStringLiteral("boolean")) return ParameterType::Boolean;
    if (type == QStringLiteral("string")) return ParameterType::String;
    if (type == QStringLiteral("enum")) return ParameterType::Enumeration;
    return ParameterType::Invalid;
}

bool valueMatchesParameterType(const QJsonValue& value, ParameterType type) {
    if (type == ParameterType::Integer) {
        return value.isDouble() &&
               std::isfinite(value.toDouble()) &&
               std::floor(value.toDouble()) == value.toDouble();
    }
    if (type == ParameterType::Number) {
        return value.isDouble() && std::isfinite(value.toDouble());
    }
    if (type == ParameterType::Boolean) {
        return value.isBool();
    }
    if (type == ParameterType::String || type == ParameterType::Enumeration) {
        return value.isString();
    }
    return false;
}

ParameterDefinition parseParameter(const QJsonObject& object,
                                   const QString& path,
                                   QVector<Diagnostic>& diagnostics) {
    ParameterDefinition definition;
    definition.id = requiredString(object, QStringLiteral("id"), path, diagnostics);
    const QString typeId = requiredString(
        object, QStringLiteral("type"), path, diagnostics);
    definition.type = parameterTypeFromId(typeId);
    definition.label = optionalString(object,
                                      QStringLiteral("label"),
                                      definition.id,
                                      path,
                                      diagnostics);
    if (definition.type == ParameterType::Invalid) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_type"),
                         QStringLiteral("unsupported parameter type %1").arg(typeId),
                         path + QStringLiteral("/type"));
    }

    if (!object.contains(QStringLiteral("default"))) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.missing_parameter_default"),
                         QStringLiteral("parameter default is required"),
                         path + QStringLiteral("/default"));
    } else {
        definition.hasDefault = true;
        definition.defaultValue = object.value(QStringLiteral("default"));
        if (!valueMatchesParameterType(definition.defaultValue, definition.type)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_default"),
                             QStringLiteral("parameter default does not match its type"),
                             path + QStringLiteral("/default"));
        }
    }

    if (object.contains(QStringLiteral("minimum"))) {
        const QJsonValue minimum = object.value(QStringLiteral("minimum"));
        if (!minimum.isDouble() || !std::isfinite(minimum.toDouble())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_minimum"),
                             QStringLiteral("minimum must be a finite number"),
                             path + QStringLiteral("/minimum"));
        } else {
            definition.minimum = minimum.toDouble();
        }
    }
    if (object.contains(QStringLiteral("maximum"))) {
        const QJsonValue maximum = object.value(QStringLiteral("maximum"));
        if (!maximum.isDouble() || !std::isfinite(maximum.toDouble())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_maximum"),
                             QStringLiteral("maximum must be a finite number"),
                             path + QStringLiteral("/maximum"));
        } else {
            definition.maximum = maximum.toDouble();
        }
    }

    if ((definition.minimum || definition.maximum) &&
        definition.type != ParameterType::Integer &&
        definition.type != ParameterType::Number) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_range"),
                         QStringLiteral("minimum and maximum require an integer or number parameter"),
                         path);
    }
    if (definition.type == ParameterType::Integer) {
        if (definition.minimum && std::floor(*definition.minimum) != *definition.minimum) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_minimum"),
                             QStringLiteral("integer parameter minimum must be an integer"),
                             path + QStringLiteral("/minimum"));
        }
        if (definition.maximum && std::floor(*definition.maximum) != *definition.maximum) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_maximum"),
                             QStringLiteral("integer parameter maximum must be an integer"),
                             path + QStringLiteral("/maximum"));
        }
    }
    if (definition.minimum && definition.maximum &&
        *definition.minimum > *definition.maximum) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_range"),
                         QStringLiteral("minimum must not exceed maximum"),
                         path);
    }

    if (object.contains(QStringLiteral("values"))) {
        const QJsonValue valuesValue = object.value(QStringLiteral("values"));
        if (!valuesValue.isArray()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_values"),
                             QStringLiteral("values must be an array"),
                             path + QStringLiteral("/values"));
        } else {
            const QJsonArray values = valuesValue.toArray();
            QSet<QString> seenValues;
            for (qsizetype index = 0; index < values.size(); ++index) {
                const QJsonValue value = values.at(index);
                if (!value.isString() || value.toString().isEmpty()) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("package.invalid_parameter_value"),
                                     QStringLiteral("enum value must be a non-empty string"),
                                     QStringLiteral("%1/values/%2").arg(path).arg(index));
                    continue;
                }
                if (seenValues.contains(value.toString())) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("package.duplicate_parameter_value"),
                                     QStringLiteral("enum value is duplicated"),
                                     QStringLiteral("%1/values/%2").arg(path).arg(index));
                    continue;
                }
                seenValues.insert(value.toString());
                definition.values.append(value.toString());
            }
        }
    }

    if (definition.type == ParameterType::Enumeration) {
        if (definition.values.isEmpty()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.missing_parameter_values"),
                             QStringLiteral("enum parameter requires at least one value"),
                             path + QStringLiteral("/values"));
        }
        if (definition.hasDefault && definition.defaultValue.isString() &&
            !definition.values.contains(definition.defaultValue.toString())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_default"),
                             QStringLiteral("enum default is not one of its declared values"),
                             path + QStringLiteral("/default"));
        }
    } else if (object.contains(QStringLiteral("values"))) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_values"),
                         QStringLiteral("values are only supported for enum parameters"),
                         path + QStringLiteral("/values"));
    }

    if (definition.hasDefault && definition.defaultValue.isDouble()) {
        const double defaultNumber = definition.defaultValue.toDouble();
        if (definition.minimum && defaultNumber < *definition.minimum) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_default"),
                             QStringLiteral("parameter default is below minimum"),
                             path + QStringLiteral("/default"));
        }
        if (definition.maximum && defaultNumber > *definition.maximum) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_default"),
                             QStringLiteral("parameter default is above maximum"),
                             path + QStringLiteral("/default"));
        }
    }
    return definition;
}

QVector<ParameterDefinition> parseParameters(const QJsonValue& value,
                                             const QString& path,
                                             QVector<Diagnostic>& diagnostics) {
    QVector<ParameterDefinition> definitions;
    if (value.isUndefined()) {
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameters"),
                         QStringLiteral("parameters must be an array"),
                         path);
        return definitions;
    }
    const QJsonArray array = value.toArray();
    QSet<QString> ids;
    definitions.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter"),
                             QStringLiteral("parameter must be an object"),
                             QStringLiteral("%1/%2").arg(path).arg(index));
            continue;
        }
        ParameterDefinition definition = parseParameter(
            array.at(index).toObject(),
            QStringLiteral("%1/%2").arg(path).arg(index),
            diagnostics);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_parameter"),
                             QStringLiteral("parameter id is duplicated"),
                             QStringLiteral("%1/%2/id").arg(path).arg(index));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

bool pathIsInside(const QString& rootPath, const QString& candidatePath) {
    const QString root = QFileInfo(rootPath).canonicalFilePath();
    const QString candidate = QFileInfo(candidatePath).canonicalFilePath();
    if (root.isEmpty() || candidate.isEmpty()) {
        return false;
    }
    return candidate == root || candidate.startsWith(root + QDir::separator());
}

void validateExecutablePath(const QString& packageRoot,
                            const QString& relativePath,
                            const QString& jsonPath,
                            QVector<Diagnostic>& diagnostics) {
    if (relativePath.isEmpty()) {
        return;
    }
    if (QFileInfo(relativePath).isAbsolute()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.executable_escape"),
                         QStringLiteral("executable path must be relative to the package root"),
                         jsonPath);
        return;
    }
    const QString absolutePath = QDir(packageRoot).filePath(relativePath);
    const QFileInfo info(absolutePath);
    if (!info.isFile() || !info.isExecutable()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.executable_missing"),
                         QStringLiteral("executable does not exist or is not executable"),
                         jsonPath);
        return;
    }
    if (!pathIsInside(packageRoot, absolutePath)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.executable_escape"),
                         QStringLiteral("executable path escapes package root"),
                         jsonPath);
        return;
    }
}

bool valueMatchesType(const QJsonValue& value, const ParameterDefinition& definition) {
    return valueMatchesParameterType(value, definition.type);
}

} // namespace

QString PackageDefinition::key() const {
    return id + QLatin1Char('@') + version;
}

const ParameterDefinition* PackageDefinition::parameter(const QString& id) const {
    const auto it = std::find_if(parameters.cbegin(), parameters.cend(), [&](const auto& value) {
        return value.id == id;
    });
    return it == parameters.cend() ? nullptr : &(*it);
}

const EndpointTypeDefinition* PackageDefinition::endpointType(const QString& id) const {
    const auto it = std::find_if(endpointTypes.cbegin(), endpointTypes.cend(), [&](const auto& value) {
        return value.id == id;
    });
    return it == endpointTypes.cend() ? nullptr : &(*it);
}

PackageLoadResult loadPackage(const QString& packageRoot) {
    PackageLoadResult result;
    const QString absoluteRoot = QDir::cleanPath(QFileInfo(packageRoot).absoluteFilePath());
    const QString manifestPath = QDir(absoluteRoot).filePath(QStringLiteral("package.json"));
    const auto rootObject = readObject(manifestPath, result.diagnostics);
    if (!rootObject) {
        return result;
    }

    PackageDefinition package;
    package.rootPath = QFileInfo(absoluteRoot).canonicalFilePath();
    if (package.rootPath.isEmpty()) {
        package.rootPath = absoluteRoot;
    }
    package.format = requiredString(*rootObject,
                                    QStringLiteral("format"),
                                    QString(),
                                    result.diagnostics);
    package.formatVersion = requiredInteger(*rootObject,
                                            QStringLiteral("formatVersion"),
                                            0,
                                            QString(),
                                            result.diagnostics);
    package.id = requiredString(*rootObject, QStringLiteral("id"), QString(), result.diagnostics);
    package.name = requiredString(*rootObject, QStringLiteral("name"), QString(), result.diagnostics);
    package.version = requiredString(*rootObject,
                                     QStringLiteral("version"),
                                     QString(),
                                     result.diagnostics);
    if (package.format != QStringLiteral("finepaper.noc-package")) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.unsupported_format"),
                         QStringLiteral("format must be finepaper.noc-package"),
                         QStringLiteral("/format"));
    }
    if (package.formatVersion != 1) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.unsupported_version"),
                         QStringLiteral("formatVersion must be 1"),
                         QStringLiteral("/formatVersion"));
    }

    const QJsonObject mesh = requiredObject(*rootObject,
                                            QStringLiteral("mesh"),
                                            QString(),
                                            result.diagnostics);
    const QJsonObject rows = requiredObject(mesh,
                                            QStringLiteral("rows"),
                                            QStringLiteral("/mesh"),
                                            result.diagnostics);
    const QJsonObject columns = requiredObject(mesh,
                                               QStringLiteral("columns"),
                                               QStringLiteral("/mesh"),
                                               result.diagnostics);
    package.mesh.minimumRows = requiredInteger(rows,
                                               QStringLiteral("min"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.maximumRows = requiredInteger(rows,
                                               QStringLiteral("max"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.defaultRows = requiredInteger(rows,
                                               QStringLiteral("default"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.minimumColumns = requiredInteger(columns,
                                                  QStringLiteral("min"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);
    package.mesh.maximumColumns = requiredInteger(columns,
                                                  QStringLiteral("max"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);
    package.mesh.defaultColumns = requiredInteger(columns,
                                                  QStringLiteral("default"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);
    const bool rowsValid = package.mesh.minimumRows > 0 &&
        package.mesh.minimumRows <= package.mesh.defaultRows &&
        package.mesh.defaultRows <= package.mesh.maximumRows;
    if (!rowsValid) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_mesh_rows"),
                         QStringLiteral("mesh rows must satisfy 0 < min <= default <= max"),
                         QStringLiteral("/mesh/rows"));
    }
    const bool columnsValid = package.mesh.minimumColumns > 0 &&
        package.mesh.minimumColumns <= package.mesh.defaultColumns &&
        package.mesh.defaultColumns <= package.mesh.maximumColumns;
    if (!columnsValid) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_mesh_columns"),
                         QStringLiteral("mesh columns must satisfy 0 < min <= default <= max"),
                         QStringLiteral("/mesh/columns"));
    }
    if (package.mesh.maximumRows > kMaximumMeshDimension ||
        package.mesh.maximumColumns > kMaximumMeshDimension ||
        (package.mesh.maximumRows > 0 && package.mesh.maximumColumns > 0 &&
         static_cast<qint64>(package.mesh.maximumRows)
                 * static_cast<qint64>(package.mesh.maximumColumns)
             > kMaximumProjectedRouterCount)) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.mesh_projection_too_large"),
                         QStringLiteral("Package mesh bounds exceed Finepaper's safe projection limit"),
                         QStringLiteral("/mesh"));
    }

    package.parameters = parseParameters(rootObject->value(QStringLiteral("parameters")),
                                         QStringLiteral("/parameters"),
                                         result.diagnostics);

    QJsonArray endpointTypes;
    const QJsonValue endpointTypesValue = rootObject->value(QStringLiteral("endpointTypes"));
    if (!endpointTypesValue.isUndefined()) {
        if (!endpointTypesValue.isArray()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_endpoint_types"),
                             QStringLiteral("endpointTypes must be an array"),
                             QStringLiteral("/endpointTypes"));
        } else {
            endpointTypes = endpointTypesValue.toArray();
        }
    }
    QSet<QString> endpointIds;
    for (qsizetype index = 0; index < endpointTypes.size(); ++index) {
        const QString base = QStringLiteral("/endpointTypes/%1").arg(index);
        if (!endpointTypes.at(index).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_endpoint_type"),
                             QStringLiteral("endpoint type must be an object"),
                             base);
            continue;
        }
        const QJsonObject object = endpointTypes.at(index).toObject();
        EndpointTypeDefinition definition;
        definition.id = requiredString(object, QStringLiteral("id"), base, result.diagnostics);
        definition.label = optionalString(object,
                                          QStringLiteral("label"),
                                          definition.id,
                                          base,
                                          result.diagnostics);
        definition.icon = optionalString(object,
                                         QStringLiteral("icon"),
                                         QString(),
                                         base,
                                         result.diagnostics);
        definition.parameters = parseParameters(object.value(QStringLiteral("parameters")),
                                                base + QStringLiteral("/parameters"),
                                                result.diagnostics);
        if (!definition.id.isEmpty() && endpointIds.contains(definition.id)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_endpoint_type"),
                             QStringLiteral("endpoint type id is duplicated"),
                             base + QStringLiteral("/id"));
        }
        endpointIds.insert(definition.id);
        package.endpointTypes.append(std::move(definition));
    }

    const QJsonObject attachment = requiredObject(*rootObject,
                                                  QStringLiteral("attachment"),
                                                  QString(),
                                                  result.diagnostics);
    package.attachment.maxPerRouter = requiredInteger(attachment,
                                                      QStringLiteral("maxPerRouter"),
                                                      1,
                                                      QStringLiteral("/attachment"),
                                                      result.diagnostics);
    if (package.attachment.maxPerRouter <= 0) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_attachment_capacity"),
                         QStringLiteral("maxPerRouter must be greater than zero"),
                         QStringLiteral("/attachment/maxPerRouter"));
    }
    const QString slotModeId = optionalString(attachment,
                                              QStringLiteral("slotMode"),
                                              QStringLiteral("automatic"),
                                              QStringLiteral("/attachment"),
                                              result.diagnostics);
    if (slotModeId == QStringLiteral("automatic")) {
        package.attachment.slotMode = AttachmentSlotMode::Automatic;
    } else if (slotModeId == QStringLiteral("explicit")) {
        package.attachment.slotMode = AttachmentSlotMode::Explicit;
    } else {
        package.attachment.slotMode = AttachmentSlotMode::Invalid;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_slot_mode"),
                         QStringLiteral("slotMode must be automatic or explicit"),
                         QStringLiteral("/attachment/slotMode"));
    }
    const QJsonValue attachmentSlots = attachment.value(QStringLiteral("slots"));
    if (!attachmentSlots.isUndefined() && !attachmentSlots.isArray()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_attachment_slots"),
                         QStringLiteral("attachment slots must be an array"),
                         QStringLiteral("/attachment/slots"));
    } else {
        const QJsonArray slotArray = attachmentSlots.toArray();
        QSet<QString> slotIds;
        for (qsizetype index = 0; index < slotArray.size(); ++index) {
            const QString base = QStringLiteral("/attachment/slots/%1").arg(index);
            if (!slotArray.at(index).isObject()) {
                appendDiagnostic(result.diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.invalid_attachment_slot"),
                                 QStringLiteral("attachment slot must be an object"),
                                 base);
                continue;
            }
            const QJsonObject object = slotArray.at(index).toObject();
            AttachmentSlotDefinition definition;
            definition.id = requiredString(
                object, QStringLiteral("id"), base, result.diagnostics);
            definition.label = optionalString(object,
                                              QStringLiteral("label"),
                                              definition.id,
                                              base,
                                              result.diagnostics);
            if (!definition.id.isEmpty() && slotIds.contains(definition.id)) {
                appendDiagnostic(result.diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.duplicate_attachment_slot"),
                                 QStringLiteral("attachment slot id is duplicated"),
                                 base + QStringLiteral("/id"));
            }
            slotIds.insert(definition.id);
            package.attachment.positions.append(std::move(definition));
        }
    }

    const QJsonObject generator = requiredObject(*rootObject,
                                                 QStringLiteral("generator"),
                                                 QString(),
                                                 result.diagnostics);
    package.generator.name = requiredString(generator,
                                            QStringLiteral("name"),
                                            QStringLiteral("/generator"),
                                            result.diagnostics);
    package.generator.version = requiredString(generator,
                                               QStringLiteral("version"),
                                               QStringLiteral("/generator"),
                                               result.diagnostics);
    package.generator.executable = requiredString(generator,
                                                  QStringLiteral("executable"),
                                                  QStringLiteral("/generator"),
                                                  result.diagnostics);
    package.generator.supportsValidate = optionalBoolean(generator,
                                                         QStringLiteral("supportsValidate"),
                                                         false,
                                                         QStringLiteral("/generator"),
                                                         result.diagnostics);
    package.generator.timeoutSeconds = optionalTimeoutSeconds(
        generator,
        QStringLiteral("timeoutSeconds"),
        GeneratorDefinition{}.timeoutSeconds,
        QStringLiteral("/generator"),
        result.diagnostics);
    validateExecutablePath(package.rootPath,
                           package.generator.executable,
                           QStringLiteral("/generator/executable"),
                           result.diagnostics);

    if (rootObject->contains(QStringLiteral("engine"))) {
        const QJsonValue engineValue = rootObject->value(QStringLiteral("engine"));
        if (!engineValue.isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_engine"),
                             QStringLiteral("engine must be an object"),
                             QStringLiteral("/engine"));
        } else {
            const QJsonObject engineObject = engineValue.toObject();
            EngineDefinition engine;
            engine.executable = requiredString(engineObject,
                                               QStringLiteral("executable"),
                                               QStringLiteral("/engine"),
                                               result.diagnostics);
            engine.providesValidation = optionalBoolean(engineObject,
                                                        QStringLiteral("providesValidation"),
                                                        false,
                                                        QStringLiteral("/engine"),
                                                        result.diagnostics);
            engine.timeoutSeconds = optionalTimeoutSeconds(
                engineObject,
                QStringLiteral("timeoutSeconds"),
                EngineDefinition{}.timeoutSeconds,
                QStringLiteral("/engine"),
                result.diagnostics);
            validateExecutablePath(package.rootPath,
                                   engine.executable,
                                   QStringLiteral("/engine/executable"),
                                   result.diagnostics);
            package.engine = std::move(engine);
        }
    }

    result.success = !hasErrors(result.diagnostics);
    if (result.success) {
        result.package = std::move(package);
    }
    return result;
}

QVector<Diagnostic> validateParameterObject(
    const QJsonObject& values,
    const QVector<ParameterDefinition>& definitions,
    const QString& basePath,
    const QString& source) {
    QVector<Diagnostic> diagnostics;
    QSet<QString> knownIds;
    for (const ParameterDefinition& definition : definitions) {
        knownIds.insert(definition.id);
        if (!values.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.missing"),
                             QStringLiteral("parameter %1 is required").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
            continue;
        }
        const QJsonValue value = values.value(definition.id);
        if (!valueMatchesType(value, definition)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.invalid_type"),
                             QStringLiteral("parameter %1 has the wrong type").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
            continue;
        }
        if (value.isDouble()) {
            const double number = value.toDouble();
            if (definition.minimum && number < *definition.minimum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("parameter.below_minimum"),
                                 QStringLiteral("parameter %1 is below its minimum").arg(definition.id),
                                 basePath + QLatin1Char('/') + definition.id,
                                 source);
            }
            if (definition.maximum && number > *definition.maximum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("parameter.above_maximum"),
                                 QStringLiteral("parameter %1 is above its maximum").arg(definition.id),
                                 basePath + QLatin1Char('/') + definition.id,
                                 source);
            }
        }
        if (definition.type == ParameterType::Enumeration &&
            !definition.values.contains(value.toString())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.invalid_enum"),
                             QStringLiteral("parameter %1 has an unsupported value").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
        }
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!knownIds.contains(it.key())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.unknown"),
                             QStringLiteral("parameter %1 is not declared by the Package").arg(it.key()),
                             basePath + QLatin1Char('/') + it.key(),
                             source);
        }
    }
    return diagnostics;
}

QVector<Diagnostic> PackageCatalog::reload(const QStringList& roots) {
    QVector<Diagnostic> diagnostics;
    QVector<PackageDefinition> loaded;
    QSet<QString> keys;

    for (const QString& rootValue : roots) {
        const QString rootPath = QDir::cleanPath(QFileInfo(rootValue).absoluteFilePath());
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.root_missing"),
                             QStringLiteral("package root does not exist"),
                             rootPath);
            continue;
        }

        QStringList packagePaths;
        if (QFileInfo(QDir(rootPath).filePath(QStringLiteral("package.json"))).isFile()) {
            packagePaths.append(rootPath);
        } else {
            const QFileInfoList children = QDir(rootPath).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo& child : children) {
                if (QFileInfo(QDir(child.absoluteFilePath())
                                  .filePath(QStringLiteral("package.json"))).isFile()) {
                    packagePaths.append(child.absoluteFilePath());
                }
            }
        }

        for (const QString& packagePath : packagePaths) {
            PackageLoadResult loadResult = loadPackage(packagePath);
            diagnostics += loadResult.diagnostics;
            if (!loadResult.success || !loadResult.package) {
                continue;
            }
            if (keys.contains(loadResult.package->key())) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.duplicate"),
                                 QStringLiteral("duplicate Package %1").arg(loadResult.package->key()),
                                 packagePath);
                continue;
            }
            keys.insert(loadResult.package->key());
            loaded.append(std::move(*loadResult.package));
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key() < rhs.key();
    });
    if (!hasErrors(diagnostics)) {
        m_packages = std::move(loaded);
    }
    return diagnostics;
}

const QVector<PackageDefinition>& PackageCatalog::packages() const {
    return m_packages;
}

std::optional<PackageDefinition> PackageCatalog::resolve(
    const PackageReference& reference) const {
    const auto it = std::find_if(m_packages.cbegin(), m_packages.cend(), [&](const auto& package) {
        return package.id == reference.id && package.version == reference.version;
    });
    if (it == m_packages.cend()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace finepaper
