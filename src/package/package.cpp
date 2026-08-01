#include "package/package.h"

#include "package/design_extension_schema.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
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

QString jsonPointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
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

QString optionalNonEmptyString(const QJsonObject& object,
                               const QString& key,
                               const QString& path,
                               QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return {};
    }
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_string"),
                         QStringLiteral("%1 must be a non-empty string").arg(key),
                         path + QLatin1Char('/') + key);
        return {};
    }
    return value.toString().trimmed();
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

bool valueMatchesParameterShape(const QJsonValue& value,
                                ParameterType type,
                                bool multiple) {
    if (!multiple) {
        return valueMatchesParameterType(value, type);
    }
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray values = value.toArray();
    return std::all_of(values.cbegin(), values.cend(), [type](const QJsonValue& item) {
        return valueMatchesParameterType(item, type);
    });
}

QJsonArray parameterDefaultItems(const ParameterDefinition& definition,
                                 bool multiple) {
    if (!definition.hasDefault) {
        return {};
    }
    if (multiple) {
        return definition.defaultValue.toArray();
    }
    return QJsonArray{definition.defaultValue};
}

struct ParameterParseOptions {
    bool multiple = false;
    bool defaultRequired = true;
};

ParameterDefinition parseParameter(const QJsonObject& object,
                                   const QString& path,
                                   QVector<Diagnostic>& diagnostics,
                                   const ParameterParseOptions& options = {}) {
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
    definition.description = optionalNonEmptyString(
        object, QStringLiteral("description"), path, diagnostics);
    definition.unit = optionalNonEmptyString(
        object, QStringLiteral("unit"), path, diagnostics);
    definition.category = optionalNonEmptyString(
        object, QStringLiteral("category"), path, diagnostics);
    definition.advanced = optionalBoolean(object,
                                          QStringLiteral("advanced"),
                                          false,
                                          path,
                                          diagnostics);
    if (definition.type == ParameterType::Invalid) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_type"),
                         QStringLiteral("unsupported parameter type %1").arg(typeId),
                         path + QStringLiteral("/type"));
    }

    if (!object.contains(QStringLiteral("default")) && options.defaultRequired) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.missing_parameter_default"),
                         QStringLiteral("parameter default is required"),
                         path + QStringLiteral("/default"));
    } else if (object.contains(QStringLiteral("default"))) {
        definition.hasDefault = true;
        definition.defaultValue = object.value(QStringLiteral("default"));
        if (!valueMatchesParameterShape(
                definition.defaultValue, definition.type, options.multiple)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter_default"),
                             options.multiple
                                 ? QStringLiteral("parameter default must be an array whose items match its type")
                                 : QStringLiteral("parameter default does not match its type"),
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
        if (definition.hasDefault) {
            const QJsonArray defaults = parameterDefaultItems(
                definition, options.multiple);
            for (const QJsonValue& defaultValue : defaults) {
                if (defaultValue.isString()
                    && !definition.values.contains(defaultValue.toString())) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("package.invalid_parameter_default"),
                                     QStringLiteral("enum default is not one of its declared values"),
                                     path + QStringLiteral("/default"));
                    break;
                }
            }
        }
    } else if (object.contains(QStringLiteral("values"))) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameter_values"),
                         QStringLiteral("values are only supported for enum parameters"),
                         path + QStringLiteral("/values"));
    }

    if (definition.hasDefault) {
        const QJsonArray defaults = parameterDefaultItems(
            definition, options.multiple);
        for (const QJsonValue& defaultValue : defaults) {
            if (!defaultValue.isDouble()) {
                continue;
            }
            const double defaultNumber = defaultValue.toDouble();
            if (definition.minimum && defaultNumber < *definition.minimum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.invalid_parameter_default"),
                                 QStringLiteral("parameter default is below minimum"),
                                 path + QStringLiteral("/default"));
                break;
            }
            if (definition.maximum && defaultNumber > *definition.maximum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.invalid_parameter_default"),
                                 QStringLiteral("parameter default is above maximum"),
                                 path + QStringLiteral("/default"));
                break;
            }
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

QVector<ElementKind> parseElementPropertyAppliesTo(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QVector<ElementKind> kinds;
    if (!value.isArray() || value.toArray().isEmpty()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_element_property_applies_to"),
            QStringLiteral("appliesTo must be a non-empty array"),
            path);
        return kinds;
    }

    const QJsonArray values = value.toArray();
    QSet<QString> seen;
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isString()
            || values.at(index).toString().trimmed().isEmpty()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_element_property_element_kind"),
                QStringLiteral("appliesTo entries must be non-empty strings"),
                itemPath);
            continue;
        }

        const QString id = values.at(index).toString().trimmed();
        if (seen.contains(id)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.duplicate_element_property_element_kind"),
                QStringLiteral("appliesTo element kind is duplicated"),
                itemPath);
            continue;
        }
        seen.insert(id);

        ElementKind kind = ElementKind::Invalid;
        if (id == QStringLiteral("router")) {
            kind = ElementKind::Router;
        } else if (id == QStringLiteral("router-link")) {
            kind = ElementKind::RouterLink;
        } else if (id == QStringLiteral("endpoint-attachment")) {
            kind = ElementKind::EndpointAttachment;
        }
        if (kind == ElementKind::Invalid) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.unsupported_element_property_element_kind"),
                QStringLiteral("element properties cannot apply to %1").arg(id),
                itemPath);
            continue;
        }
        kinds.append(kind);
    }
    return kinds;
}

QVector<ElementPropertyDefinition> parseElementProperties(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QVector<ElementPropertyDefinition> definitions;
    if (!value.isArray() || value.toArray().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_element_properties"),
                         QStringLiteral("properties must be a non-empty array"),
                         path);
        return definitions;
    }

    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_element_property"),
                             QStringLiteral("element property must be an object"),
                             itemPath);
            definitions.append(ElementPropertyDefinition{});
            continue;
        }

        const QJsonObject object = values.at(index).toObject();
        ElementPropertyDefinition definition;
        definition.multiple = optionalBoolean(object,
                                              QStringLiteral("multiple"),
                                              false,
                                              itemPath,
                                              diagnostics);
        ParameterParseOptions options;
        options.multiple = definition.multiple;
        options.defaultRequired = true;
        static_cast<ParameterDefinition&>(definition) = parseParameter(
            object, itemPath, diagnostics, options);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_element_property"),
                             QStringLiteral("element property id is duplicated"),
                             itemPath + QStringLiteral("/id"));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

QStringList parseElementPropertyEndpointTypes(
    const QJsonValue& value,
    const QVector<ElementKind>& appliesTo,
    const QSet<QString>& declaredEndpointTypeIds,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QStringList endpointTypes;
    if (value.isUndefined()) {
        return endpointTypes;
    }
    if (!appliesTo.contains(ElementKind::EndpointAttachment)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.element_property_endpoint_types_require_attachment"),
            QStringLiteral("endpointTypes is only valid when appliesTo contains endpoint-attachment"),
            path);
    }
    if (!value.isArray() || value.toArray().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_element_property_endpoint_types"),
                         QStringLiteral("endpointTypes must be a non-empty array"),
                         path);
        return endpointTypes;
    }

    const QJsonArray values = value.toArray();
    QSet<QString> seen;
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isString()
            || values.at(index).toString().trimmed().isEmpty()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_element_property_endpoint_type"),
                QStringLiteral("endpointTypes entries must be non-empty strings"),
                itemPath);
            continue;
        }
        const QString id = values.at(index).toString().trimmed();
        if (seen.contains(id)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.duplicate_element_property_endpoint_type"),
                QStringLiteral("endpoint type filter is duplicated"),
                itemPath);
            continue;
        }
        seen.insert(id);
        if (!declaredEndpointTypeIds.contains(id)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.unknown_element_property_endpoint_type"),
                QStringLiteral("endpoint type %1 is not declared").arg(id),
                itemPath);
            continue;
        }
        endpointTypes.append(id);
    }
    return endpointTypes;
}

QVector<ElementPropertySetDefinition> parseElementPropertySets(
    const QJsonValue& value,
    int packageFormatVersion,
    const QSet<QString>& declaredEndpointTypeIds,
    QVector<Diagnostic>& diagnostics) {
    QVector<ElementPropertySetDefinition> definitions;
    if (value.isUndefined()) {
        if (formatVersionSupportsElementConfigurations(packageFormatVersion)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_element_property_sets"),
                QStringLiteral("formatVersion 3 requires elementPropertySets to be an array"),
                QStringLiteral("/elementPropertySets"));
        }
        return definitions;
    }
    if (!formatVersionSupportsElementConfigurations(packageFormatVersion)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.element_property_sets_require_v3"),
            QStringLiteral("elementPropertySets requires Package formatVersion 3"),
            QStringLiteral("/elementPropertySets"));
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_element_property_sets"),
                         QStringLiteral("elementPropertySets must be an array"),
                         QStringLiteral("/elementPropertySets"));
        return definitions;
    }

    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString path = QStringLiteral("/elementPropertySets/%1").arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_element_property_set"),
                             QStringLiteral("element property set must be an object"),
                             path);
            definitions.append(ElementPropertySetDefinition{});
            continue;
        }

        const QJsonObject object = values.at(index).toObject();
        ElementPropertySetDefinition definition;
        definition.id = requiredString(
            object, QStringLiteral("id"), path, diagnostics);
        definition.label = optionalString(object,
                                          QStringLiteral("label"),
                                          definition.id,
                                          path,
                                          diagnostics);
        definition.appliesTo = parseElementPropertyAppliesTo(
            object.value(QStringLiteral("appliesTo")),
            path + QStringLiteral("/appliesTo"),
            diagnostics);
        definition.endpointTypes = parseElementPropertyEndpointTypes(
            object.value(QStringLiteral("endpointTypes")),
            definition.appliesTo,
            declaredEndpointTypeIds,
            path + QStringLiteral("/endpointTypes"),
            diagnostics);
        definition.properties = parseElementProperties(
            object.value(QStringLiteral("properties")),
            path + QStringLiteral("/properties"),
            diagnostics);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_element_property_set"),
                             QStringLiteral("element property set id is duplicated"),
                             path + QStringLiteral("/id"));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

DomainCardinality parseDomainCardinality(const QJsonObject& object,
                                         const QString& path,
                                         QVector<Diagnostic>& diagnostics) {
    const QString id = optionalString(object,
                                      QStringLiteral("cardinality"),
                                      QStringLiteral("single"),
                                      path,
                                      diagnostics).trimmed();
    if (id == QStringLiteral("single")) {
        return DomainCardinality::Single;
    }
    if (id == QStringLiteral("multiple")) {
        return DomainCardinality::Multiple;
    }
    appendDiagnostic(diagnostics,
                     QStringLiteral("error"),
                     QStringLiteral("package.invalid_domain_cardinality"),
                     QStringLiteral("cardinality must be single or multiple"),
                     path + QStringLiteral("/cardinality"));
    return DomainCardinality::Invalid;
}

std::optional<DomainAssignmentRule> legacyDomainAssignmentRule(
    ElementKind elementKind,
    DomainCardinality cardinality,
    bool required) {
    if (!isDomainMembershipElementKind(elementKind)) {
        return std::nullopt;
    }
    if (cardinality == DomainCardinality::Single) {
        return DomainAssignmentRule{
            elementKind,
            required ? 1 : 0,
            qsizetype{1}
        };
    }
    if (cardinality == DomainCardinality::Multiple) {
        return DomainAssignmentRule{
            elementKind,
            required ? 1 : 0,
            std::nullopt
        };
    }
    return DomainAssignmentRule{elementKind, -1, std::nullopt};
}

QVector<ElementKind> parseDomainAppliesTo(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QVector<ElementKind> kinds;
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_applies_to"),
                         QStringLiteral("appliesTo must be a non-empty array"),
                         path);
        return kinds;
    }
    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isString()
            || values.at(index).toString().trimmed().isEmpty()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_element_kind"),
                             QStringLiteral("appliesTo entries must be non-empty strings"),
                             itemPath);
            continue;
        }
        const QString id = values.at(index).toString().trimmed();
        if (ids.contains(id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_domain_element_kind"),
                             QStringLiteral("appliesTo element kind is duplicated"),
                             itemPath);
            continue;
        }
        ids.insert(id);
        const ElementKind kind = elementKindFromId(id);
        if (!isDomainMembershipElementKind(kind)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.unknown_domain_element_kind"),
                             QStringLiteral("unsupported Domain element kind %1").arg(id),
                             itemPath);
            continue;
        }
        kinds.append(kind);
    }
    if (values.isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_applies_to"),
                         QStringLiteral("appliesTo must contain router or endpoint"),
                         path);
    }
    return kinds;
}

QStringList parseDomainTypeReferences(const QJsonValue& value,
                                      const QString& path,
                                      QVector<Diagnostic>& diagnostics) {
    QStringList references;
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_type_references"),
                         QStringLiteral("targetTypes must be a non-empty array"),
                         path);
        return references;
    }
    const QJsonArray values = value.toArray();
    QSet<QString> seen;
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isString()
            || values.at(index).toString().trimmed().isEmpty()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_type_reference"),
                             QStringLiteral("targetTypes entries must be non-empty strings"),
                             itemPath);
            continue;
        }
        const QString id = values.at(index).toString().trimmed();
        if (seen.contains(id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_domain_type_reference"),
                             QStringLiteral("target Domain type is duplicated"),
                             itemPath);
            continue;
        }
        seen.insert(id);
        references.append(id);
    }
    if (values.isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_type_references"),
                         QStringLiteral("targetTypes must contain at least one Domain type"),
                         path);
    }
    return references;
}

QVector<DomainPropertyDefinition> parseDomainProperties(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QVector<DomainPropertyDefinition> definitions;
    if (value.isUndefined()) {
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_properties"),
                         QStringLiteral("Domain properties must be an array"),
                         path);
        return definitions;
    }
    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_property"),
                             QStringLiteral("Domain property must be an object"),
                             itemPath);
            definitions.append(DomainPropertyDefinition{});
            continue;
        }
        const QJsonObject object = values.at(index).toObject();
        DomainPropertyDefinition definition;
        definition.required = optionalBoolean(object,
                                              QStringLiteral("required"),
                                              false,
                                              itemPath,
                                              diagnostics);
        definition.multiple = optionalBoolean(object,
                                              QStringLiteral("multiple"),
                                              false,
                                              itemPath,
                                              diagnostics);
        ParameterParseOptions parameterOptions;
        parameterOptions.multiple = definition.multiple;
        parameterOptions.defaultRequired = false;
        static_cast<ParameterDefinition&>(definition) = parseParameter(
            object, itemPath, diagnostics, parameterOptions);
        if (object.contains(QStringLiteral("referenceDomainType"))) {
            const QString reference = optionalString(
                object,
                QStringLiteral("referenceDomainType"),
                QString(),
                itemPath,
                diagnostics).trimmed();
            if (reference.isEmpty()) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.invalid_domain_property_reference"),
                                 QStringLiteral("referenceDomainType must be a non-empty string"),
                                 itemPath + QStringLiteral("/referenceDomainType"));
            } else {
                definition.referenceDomainType = reference;
            }
            if (definition.type != ParameterType::String) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.invalid_domain_property_reference_type"),
                                 QStringLiteral("a Domain reference property must have type string"),
                                 itemPath + QStringLiteral("/type"));
            }
            if (definition.hasDefault) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.domain_reference_default_unsupported"),
                    QStringLiteral("Domain reference properties cannot declare instance ids as defaults"),
                    itemPath + QStringLiteral("/default"));
            }
        }
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_domain_property"),
                             QStringLiteral("Domain property id is duplicated"),
                             itemPath + QStringLiteral("/id"));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

QVector<DomainRelationDefinition> parseDomainRelations(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    QVector<DomainRelationDefinition> definitions;
    if (value.isUndefined()) {
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_relations"),
                         QStringLiteral("relations must be an array"),
                         path);
        return definitions;
    }
    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_relation"),
                             QStringLiteral("Domain relation must be an object"),
                             itemPath);
            definitions.append(DomainRelationDefinition{});
            continue;
        }
        const QJsonObject object = values.at(index).toObject();
        DomainRelationDefinition definition;
        definition.id = requiredString(
            object, QStringLiteral("id"), itemPath, diagnostics);
        definition.label = optionalString(object,
                                          QStringLiteral("label"),
                                          definition.id,
                                          itemPath,
                                          diagnostics);
        definition.targetTypes = parseDomainTypeReferences(
            object.value(QStringLiteral("targetTypes")),
            itemPath + QStringLiteral("/targetTypes"),
            diagnostics);
        definition.cardinality = parseDomainCardinality(
            object, itemPath, diagnostics);
        definition.required = optionalBoolean(object,
                                              QStringLiteral("required"),
                                              false,
                                              itemPath,
                                              diagnostics);
        definition.properties = parseDomainProperties(
            object.value(QStringLiteral("properties")),
            itemPath + QStringLiteral("/properties"),
            diagnostics);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_domain_relation"),
                             QStringLiteral("Domain relation id is duplicated"),
                             itemPath + QStringLiteral("/id"));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

std::optional<DomainDefaultInstanceDefinition> parseDomainDefaultInstance(
    const QJsonObject& domainType,
    bool required,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    const QString key = QStringLiteral("defaultInstance");
    if (!domainType.contains(key)) {
        return std::nullopt;
    }
    const QString instancePath = path + QLatin1Char('/') + key;
    const QJsonValue value = domainType.value(key);
    if (!value.isObject()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_default_domain_instance"),
            QStringLiteral("defaultInstance must be an object"),
            instancePath);
        return DomainDefaultInstanceDefinition{};
    }
    if (!required) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.default_domain_instance_requires_required_type"),
            QStringLiteral("defaultInstance is only meaningful for a required Domain type"),
            instancePath);
    }

    const QJsonObject object = value.toObject();
    const QSet<QString> allowed{
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("properties")
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.unknown_default_domain_instance_field"),
                QStringLiteral("defaultInstance field %1 is not supported")
                    .arg(it.key()),
                instancePath + QLatin1Char('/') + it.key());
        }
    }

    DomainDefaultInstanceDefinition definition;
    if (object.contains(QStringLiteral("id"))) {
        definition.id = requiredString(
            object, QStringLiteral("id"), instancePath, diagnostics);
    }
    if (object.contains(QStringLiteral("name"))) {
        definition.name = requiredString(
            object, QStringLiteral("name"), instancePath, diagnostics);
    }
    if (object.contains(QStringLiteral("properties"))) {
        if (!object.value(QStringLiteral("properties")).isObject()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_default_domain_instance_properties"),
                QStringLiteral("defaultInstance properties must be an object"),
                instancePath + QStringLiteral("/properties"));
        } else {
            definition.properties =
                object.value(QStringLiteral("properties")).toObject();
        }
    }
    return definition;
}

QJsonObject defaultDomainProperties(const DomainTypeDefinition& type) {
    QJsonObject result;
    for (const DomainPropertyDefinition& property : type.properties) {
        if (property.hasDefault) {
            result.insert(property.id, property.defaultValue);
        }
    }
    if (type.defaultInstance) {
        for (auto it = type.defaultInstance->properties.constBegin();
             it != type.defaultInstance->properties.constEnd(); ++it) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

void validateDefaultDomainPropertyValue(
    const QJsonValue& value,
    const DomainPropertyDefinition& property,
    const QHash<QString, QString>& defaultDomainTypesById,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    if (!valueMatchesParameterShape(value, property.type, property.multiple)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_default_domain_property"),
            QStringLiteral("default Domain property %1 has the wrong type or shape")
                .arg(property.id),
            path);
        return;
    }

    const QJsonArray items = property.multiple
        ? value.toArray() : QJsonArray{value};
    if (property.required && property.multiple && items.isEmpty()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.empty_required_default_domain_property"),
            QStringLiteral("required default Domain property %1 must not be empty")
                .arg(property.id),
            path);
    }
    for (qsizetype index = 0; index < items.size(); ++index) {
        const QJsonValue item = items.at(index);
        const QString itemPath = property.multiple
            ? QStringLiteral("%1/%2").arg(path).arg(index) : path;
        if (item.isDouble()) {
            const double number = item.toDouble();
            if (property.minimum && number < *property.minimum) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.default_domain_property_below_minimum"),
                    QStringLiteral("default Domain property %1 is below its minimum")
                        .arg(property.id),
                    itemPath);
            }
            if (property.maximum && number > *property.maximum) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.default_domain_property_above_maximum"),
                    QStringLiteral("default Domain property %1 is above its maximum")
                        .arg(property.id),
                    itemPath);
            }
        }
        if (property.type == ParameterType::Enumeration
            && !property.values.contains(item.toString())) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_default_domain_property_enum"),
                QStringLiteral("default Domain property %1 has an unsupported value")
                    .arg(property.id),
                itemPath);
        }
        if (property.referenceDomainType) {
            const auto target = defaultDomainTypesById.constFind(item.toString());
            if (target == defaultDomainTypesById.cend()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.unknown_default_domain_reference"),
                    QStringLiteral("default Domain property %1 references an instance that is not materialized")
                        .arg(property.id),
                    itemPath);
            } else if (target.value() != *property.referenceDomainType) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.default_domain_reference_type_mismatch"),
                    QStringLiteral("default Domain property %1 references the wrong Domain type")
                        .arg(property.id),
                    itemPath);
            }
        }
    }
}

void resolveAndValidateDomainDefaultInstances(
    QVector<DomainTypeDefinition>& definitions,
    QVector<Diagnostic>& diagnostics) {
    QSet<qsizetype> explicitlyDeclared;
    for (qsizetype index = 0; index < definitions.size(); ++index) {
        DomainTypeDefinition& type = definitions[index];
        const bool declared = type.defaultInstance.has_value();
        if (declared) {
            explicitlyDeclared.insert(index);
        }
        if (!type.required) {
            continue;
        }

        DomainDefaultInstanceDefinition resolved =
            type.defaultInstance.value_or(DomainDefaultInstanceDefinition{});
        if (resolved.id.isEmpty()) {
            resolved.id = type.id + QStringLiteral("-default");
        }
        if (resolved.name.isEmpty()) {
            resolved.name = type.label.trimmed().isEmpty()
                ? type.id : type.label.trimmed();
        }
        QJsonObject properties;
        for (const DomainPropertyDefinition& property : type.properties) {
            if (property.hasDefault) {
                properties.insert(property.id, property.defaultValue);
            }
        }
        for (auto it = resolved.properties.constBegin();
             it != resolved.properties.constEnd(); ++it) {
            properties.insert(it.key(), it.value());
        }
        resolved.properties = std::move(properties);
        type.defaultInstance = std::move(resolved);
    }

    QHash<QString, QString> defaultDomainTypesById;
    for (qsizetype index = 0; index < definitions.size(); ++index) {
        const DomainTypeDefinition& type = definitions.at(index);
        if (!type.required) {
            continue;
        }
        const QString id = type.defaultInstance->id;
        const QString path = QStringLiteral("/domainTypes/%1/defaultInstance/id")
                                 .arg(index);
        if (defaultDomainTypesById.contains(id)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.duplicate_default_domain_instance_id"),
                QStringLiteral("materialized default Domain id %1 is duplicated")
                    .arg(id),
                path);
        } else {
            defaultDomainTypesById.insert(id, type.id);
        }
    }

    for (qsizetype typeIndex = 0; typeIndex < definitions.size(); ++typeIndex) {
        const DomainTypeDefinition& type = definitions.at(typeIndex);
        if (!type.required || !explicitlyDeclared.contains(typeIndex)) {
            continue;
        }
        const QString base = QStringLiteral("/domainTypes/%1/defaultInstance/properties")
                                 .arg(typeIndex);
        QHash<QString, const DomainPropertyDefinition*> propertiesById;
        for (const DomainPropertyDefinition& property : type.properties) {
            propertiesById.insert(property.id, &property);
        }
        for (auto it = type.defaultInstance->properties.constBegin();
             it != type.defaultInstance->properties.constEnd(); ++it) {
            if (!propertiesById.contains(it.key())) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.unknown_default_domain_property"),
                    QStringLiteral("default Domain property %1 is not declared by the Domain Type")
                        .arg(it.key()),
                    base + QLatin1Char('/') + it.key());
            }
        }

        const QJsonObject values = defaultDomainProperties(type);
        for (const DomainPropertyDefinition& property : type.properties) {
            const QString path = base + QLatin1Char('/') + property.id;
            if (!values.contains(property.id)) {
                if (property.required) {
                    appendDiagnostic(
                        diagnostics,
                        QStringLiteral("error"),
                        QStringLiteral("package.missing_required_default_domain_property"),
                        QStringLiteral("defaultInstance must provide required Domain property %1")
                            .arg(property.id),
                        path);
                }
                continue;
            }
            validateDefaultDomainPropertyValue(
                values.value(property.id), property,
                defaultDomainTypesById, path, diagnostics);
        }
    }
}

void validateDomainPropertyReferences(
    const QVector<DomainPropertyDefinition>& properties,
    const QSet<QString>& domainTypeIds,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    for (qsizetype index = 0; index < properties.size(); ++index) {
        const auto& reference = properties.at(index).referenceDomainType;
        if (reference && !domainTypeIds.contains(*reference)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.unknown_domain_property_reference"),
                             QStringLiteral("referenced Domain type %1 is not declared")
                                 .arg(*reference),
                             QStringLiteral("%1/%2/referenceDomainType")
                                 .arg(path).arg(index));
        }
    }
}

QVector<DomainTypeDefinition> parseDomainTypes(
    const QJsonValue& value,
    int packageFormatVersion,
    QVector<Diagnostic>& diagnostics) {
    QVector<DomainTypeDefinition> definitions;
    if (value.isUndefined()) {
        if (formatVersionSupportsDomains(packageFormatVersion)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_types"),
                             QStringLiteral("formatVersion %1 requires domainTypes to be an array")
                                 .arg(packageFormatVersion),
                             QStringLiteral("/domainTypes"));
        }
        return definitions;
    }
    if (!formatVersionSupportsDomains(packageFormatVersion)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.domain_types_require_v2"),
                         QStringLiteral("domainTypes requires Package formatVersion 2"),
                         QStringLiteral("/domainTypes"));
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_domain_types"),
                         QStringLiteral("domainTypes must be an array"),
                         QStringLiteral("/domainTypes"));
        return definitions;
    }

    const QJsonArray values = value.toArray();
    QSet<QString> ids;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString path = QStringLiteral("/domainTypes/%1").arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_domain_type"),
                             QStringLiteral("Domain type must be an object"),
                             path);
            definitions.append(DomainTypeDefinition{});
            continue;
        }
        const QJsonObject object = values.at(index).toObject();
        if (object.contains(QStringLiteral("assignmentRules"))) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.assignment_rules_require_v4"),
                QStringLiteral(
                    "assignmentRules requires Package formatVersion 4"),
                path + QStringLiteral("/assignmentRules"));
        }
        DomainTypeDefinition definition;
        definition.id = requiredString(
            object, QStringLiteral("id"), path, diagnostics);
        definition.label = optionalString(object,
                                          QStringLiteral("label"),
                                          definition.id,
                                          path,
                                          diagnostics);
        definition.appliesTo = parseDomainAppliesTo(
            object.value(QStringLiteral("appliesTo")),
            path + QStringLiteral("/appliesTo"),
            diagnostics);
        definition.cardinality = parseDomainCardinality(
            object, path, diagnostics);
        definition.required = optionalBoolean(object,
                                              QStringLiteral("required"),
                                              false,
                                              path,
                                              diagnostics);
        for (const ElementKind elementKind : definition.appliesTo) {
            const std::optional<DomainAssignmentRule> rule =
                legacyDomainAssignmentRule(
                    elementKind, definition.cardinality, definition.required);
            if (rule) {
                definition.assignmentRules.append(*rule);
            }
        }
        definition.defaultInstance = parseDomainDefaultInstance(
            object, definition.required, path, diagnostics);
        definition.properties = parseDomainProperties(
            object.value(QStringLiteral("properties")),
            path + QStringLiteral("/properties"),
            diagnostics);
        definition.relations = parseDomainRelations(
            object.value(QStringLiteral("relations")),
            path + QStringLiteral("/relations"),
            diagnostics);
        definition.crossingProperties = parseDomainProperties(
            object.value(QStringLiteral("crossingProperties")),
            path + QStringLiteral("/crossingProperties"),
            diagnostics);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_domain_type"),
                             QStringLiteral("Domain type id is duplicated"),
                             path + QStringLiteral("/id"));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }

    for (qsizetype typeIndex = 0; typeIndex < definitions.size(); ++typeIndex) {
        const DomainTypeDefinition& definition = definitions.at(typeIndex);
        const QString typePath = QStringLiteral("/domainTypes/%1").arg(typeIndex);
        validateDomainPropertyReferences(
            definition.properties,
            ids,
            typePath + QStringLiteral("/properties"),
            diagnostics);
        validateDomainPropertyReferences(
            definition.crossingProperties,
            ids,
            typePath + QStringLiteral("/crossingProperties"),
            diagnostics);
        for (qsizetype relationIndex = 0;
             relationIndex < definition.relations.size(); ++relationIndex) {
            const DomainRelationDefinition& relation =
                definition.relations.at(relationIndex);
            const QString relationPath = QStringLiteral("%1/relations/%2")
                                             .arg(typePath).arg(relationIndex);
            for (qsizetype targetIndex = 0;
                 targetIndex < relation.targetTypes.size(); ++targetIndex) {
                if (!ids.contains(relation.targetTypes.at(targetIndex))) {
                    appendDiagnostic(diagnostics,
                                     QStringLiteral("error"),
                                     QStringLiteral("package.unknown_domain_relation_target"),
                                     QStringLiteral("target Domain type %1 is not declared")
                                         .arg(relation.targetTypes.at(targetIndex)),
                                     QStringLiteral("%1/targetTypes/%2")
                                         .arg(relationPath).arg(targetIndex));
                }
            }
            validateDomainPropertyReferences(
                relation.properties,
                ids,
                relationPath + QStringLiteral("/properties"),
                diagnostics);
        }
    }
    resolveAndValidateDomainDefaultInstances(definitions, diagnostics);
    return definitions;
}

std::optional<bool> requiredDomainRuntimeCapability(
    const QJsonObject& object,
    const QString& key,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    const QString valuePath = path + QLatin1Char('/') + key;
    if (!object.contains(key)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.missing_domain_runtime_capability"),
            QStringLiteral("Domain runtime capability %1 must be declared explicitly")
                .arg(key),
            valuePath);
        return std::nullopt;
    }
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_domain_runtime_capability"),
            QStringLiteral("Domain runtime capability %1 must be a boolean")
                .arg(key),
            valuePath);
        return std::nullopt;
    }
    return value.toBool();
}

RuntimeCapabilitiesDefinition parseRuntimeCapabilities(
    const QJsonValue& value,
    int packageFormatVersion,
    bool domainTypesDeclared,
    QVector<Diagnostic>& diagnostics) {
    RuntimeCapabilitiesDefinition definition;
    const QString path = QStringLiteral("/runtimeCapabilities");

    if (!formatVersionSupportsDomains(packageFormatVersion)) {
        if (!value.isUndefined()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.runtime_capabilities_require_v2"),
                QStringLiteral(
                    "runtimeCapabilities.domainConfiguration requires Package formatVersion 2"),
                path);
        }
        return definition;
    }

    if (value.isUndefined()) {
        if (domainTypesDeclared) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.missing_runtime_capabilities"),
                QStringLiteral(
                    "Packages that declare domainTypes must declare runtimeCapabilities"),
                path);
        }
        return definition;
    }
    if (!value.isObject()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_runtime_capabilities"),
                         QStringLiteral("runtimeCapabilities must be an object"),
                         path);
        return definition;
    }

    const QJsonObject object = value.toObject();
    const QSet<QString> allowed{QStringLiteral("domainConfiguration")};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.unknown_runtime_capability"),
                QStringLiteral("runtime capability %1 is not supported").arg(it.key()),
                path + QLatin1Char('/') + it.key());
        }
    }

    const QString domainPath = path + QStringLiteral("/domainConfiguration");
    if (!object.contains(QStringLiteral("domainConfiguration"))) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.missing_domain_runtime_capabilities"),
            QStringLiteral(
                "runtimeCapabilities.domainConfiguration must be declared explicitly"),
            domainPath);
        return definition;
    }
    const QJsonValue domainValue = object.value(QStringLiteral("domainConfiguration"));
    if (!domainValue.isObject()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_domain_runtime_capabilities"),
            QStringLiteral("runtimeCapabilities.domainConfiguration must be an object"),
            domainPath);
        return definition;
    }

    const QJsonObject domainObject = domainValue.toObject();
    const QSet<QString> domainAllowed{
        QStringLiteral("domains"),
        QStringLiteral("memberships"),
        QStringLiteral("relations"),
        QStringLiteral("crossingPolicies"),
        QStringLiteral("edgeOverrides")
    };
    for (auto it = domainObject.constBegin(); it != domainObject.constEnd(); ++it) {
        if (!domainAllowed.contains(it.key())) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.unknown_domain_runtime_capability"),
                QStringLiteral("Domain runtime capability %1 is not supported")
                    .arg(it.key()),
                domainPath + QLatin1Char('/') + it.key());
        }
    }

    const std::optional<bool> domains = requiredDomainRuntimeCapability(
        domainObject, QStringLiteral("domains"), domainPath, diagnostics);
    const std::optional<bool> memberships = requiredDomainRuntimeCapability(
        domainObject, QStringLiteral("memberships"), domainPath, diagnostics);
    const std::optional<bool> relations = requiredDomainRuntimeCapability(
        domainObject, QStringLiteral("relations"), domainPath, diagnostics);
    const std::optional<bool> crossingPolicies = requiredDomainRuntimeCapability(
        domainObject, QStringLiteral("crossingPolicies"), domainPath, diagnostics);
    const std::optional<bool> edgeOverrides = requiredDomainRuntimeCapability(
        domainObject, QStringLiteral("edgeOverrides"), domainPath, diagnostics);
    if (!domains || !memberships || !relations || !crossingPolicies || !edgeOverrides) {
        return definition;
    }

    const auto requireCapability = [&](bool enabled,
                                       const QString& capability,
                                       bool dependencyEnabled,
                                       const QString& dependency) {
        if (!enabled || dependencyEnabled) {
            return;
        }
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
            QStringLiteral("Domain runtime capability %1 requires %2")
                .arg(capability, dependency),
            domainPath + QLatin1Char('/') + capability);
    };
    requireCapability(*memberships,
                      QStringLiteral("memberships"),
                      *domains,
                      QStringLiteral("domains"));
    requireCapability(*relations,
                      QStringLiteral("relations"),
                      *domains,
                      QStringLiteral("domains"));
    requireCapability(*crossingPolicies,
                      QStringLiteral("crossingPolicies"),
                      *domains,
                      QStringLiteral("domains"));
    requireCapability(*edgeOverrides,
                      QStringLiteral("edgeOverrides"),
                      *domains,
                      QStringLiteral("domains"));
    requireCapability(*edgeOverrides,
                      QStringLiteral("edgeOverrides"),
                      *memberships,
                      QStringLiteral("memberships"));
    requireCapability(*edgeOverrides,
                      QStringLiteral("edgeOverrides"),
                      *crossingPolicies,
                      QStringLiteral("crossingPolicies"));

    definition.domainConfiguration = DomainConfigurationRuntimeCapabilities{
        *domains, *memberships, *relations, *crossingPolicies, *edgeOverrides
    };
    return definition;
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

void appendDesignExtensionSchemaProfileDiagnostics(
    const DesignExtensionDefinition& definition,
    const QString& itemPath,
    QVector<Diagnostic>& diagnostics) {
    if (definition.schemaStatus == json_schema::CompileStatus::Ready
        && definition.compiledSchema) {
        return;
    }
    const bool unsupported = definition.schemaStatus
        == json_schema::CompileStatus::Unsupported;
    const QString code = unsupported
        ? QStringLiteral("package.design_extension_schema_unsupported")
        : QStringLiteral("package.design_extension_schema_invalid");
    const QString severity = unsupported
        ? QStringLiteral("warning")
        : QStringLiteral("error");
    if (definition.schemaIssues.isEmpty()) {
        appendDiagnostic(
            diagnostics,
            severity,
            code,
            unsupported
                ? QStringLiteral(
                    "design extension schema uses an unsupported feature")
                : QStringLiteral(
                    "design extension schema could not be compiled"),
            itemPath + QStringLiteral("/schema"));
        return;
    }
    for (const json_schema::Issue& issue : definition.schemaIssues) {
        appendDiagnostic(
            diagnostics,
            severity,
            code,
            issue.message,
            itemPath + QStringLiteral("/schema#") + issue.schemaPointer);
    }
}

std::optional<QStringList> decodeDesignExtensionDomainReferencePointer(
    const QString& pointer,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    // RFC 6901 uses the empty string for the whole document. Here that means
    // the root value of the Design Extension namespace.
    if (pointer.isEmpty()) {
        return QStringList{};
    }
    if (pointer.size()
        > kMaximumDesignExtensionDomainReferencePointerCharacters) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral(
                "package.design_extension_domain_reference_pointer_too_long"),
            QStringLiteral(
                "domain reference pointer cannot contain more than %1 characters")
                .arg(
                    kMaximumDesignExtensionDomainReferencePointerCharacters),
            path);
        return std::nullopt;
    }
    if (!pointer.startsWith(QLatin1Char('/'))) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral(
                "package.invalid_design_extension_domain_reference_pointer"),
            QStringLiteral(
                "domain reference pointer must be relative to the extension root and begin with /"),
            path);
        return std::nullopt;
    }

    QStringList encodedTokens = pointer.split(
        QLatin1Char('/'), Qt::KeepEmptyParts);
    encodedTokens.removeFirst();
    if (encodedTokens.size()
        > kMaximumDesignExtensionDomainReferencePointerTokens) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral(
                "package.design_extension_domain_reference_pointer_too_deep"),
            QStringLiteral(
                "domain reference pointer cannot contain more than %1 tokens")
                .arg(kMaximumDesignExtensionDomainReferencePointerTokens),
            path);
        return std::nullopt;
    }

    QStringList decodedTokens;
    decodedTokens.reserve(encodedTokens.size());
    for (const QString& encodedToken : encodedTokens) {
        QString decodedToken;
        decodedToken.reserve(encodedToken.size());
        for (qsizetype index = 0; index < encodedToken.size(); ++index) {
            const char16_t character = encodedToken.at(index).unicode();
            if (character != u'~') {
                decodedToken.append(character);
                continue;
            }
            if (index + 1 >= encodedToken.size()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral(
                        "package.invalid_design_extension_domain_reference_pointer"),
                    QStringLiteral(
                        "domain reference pointer contains an incomplete RFC 6901 escape"),
                    path);
                return std::nullopt;
            }
            const char16_t escape = encodedToken.at(++index).unicode();
            if (escape == u'0') {
                decodedToken.append(QLatin1Char('~'));
            } else if (escape == u'1') {
                decodedToken.append(QLatin1Char('/'));
            } else {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral(
                        "package.invalid_design_extension_domain_reference_pointer"),
                    QStringLiteral(
                        "domain reference pointer uses an invalid RFC 6901 escape"),
                    path);
                return std::nullopt;
            }
        }
        decodedTokens.append(std::move(decodedToken));
    }
    return decodedTokens;
}

void parseDesignExtensionDomainReferences(
    const QJsonValue& value,
    const QSet<QString>& domainTypeIds,
    const QString& path,
    DesignExtensionDefinition& definition,
    QVector<Diagnostic>& diagnostics) {
    if (value.isUndefined()) {
        return;
    }
    if (!value.isArray()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral(
                "package.invalid_design_extension_domain_references"),
            QStringLiteral("design extension domainReferences must be an array"),
            path);
        return;
    }

    const QJsonArray values = value.toArray();
    if (values.size() > kMaximumDesignExtensionDomainReferences) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral(
                "package.too_many_design_extension_domain_references"),
            QStringLiteral(
                "design extension domainReferences cannot contain more than %1 entries")
                .arg(kMaximumDesignExtensionDomainReferences),
            path);
        return;
    }

    QSet<QString> pointers;
    definition.domainReferences.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral(
                    "package.invalid_design_extension_domain_reference"),
                QStringLiteral("design extension domain reference must be an object"),
                itemPath);
            continue;
        }

        const QJsonObject object = values.at(index).toObject();
        static const std::array<QString, 2> allowed = {
            QStringLiteral("pointer"),
            QStringLiteral("domainType")
        };
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (std::find(allowed.cbegin(), allowed.cend(), it.key())
                == allowed.cend()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral(
                        "package.unknown_design_extension_domain_reference_field"),
                    QStringLiteral(
                        "design extension domain reference field %1 is not supported")
                        .arg(it.key()),
                    itemPath + QLatin1Char('/') + jsonPointerToken(it.key()));
            }
        }

        const QString pointerPath = itemPath + QStringLiteral("/pointer");
        const QJsonValue pointerValue = object.value(QStringLiteral("pointer"));
        QString pointer;
        std::optional<QStringList> pointerTokens = std::nullopt;
        if (!pointerValue.isString()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral(
                    "package.invalid_design_extension_domain_reference_pointer"),
                QStringLiteral("domain reference pointer must be a string"),
                pointerPath);
        } else {
            pointer = pointerValue.toString();
            pointerTokens = decodeDesignExtensionDomainReferencePointer(
                pointer, pointerPath, diagnostics);
        }

        const QString domainTypePath = itemPath + QStringLiteral("/domainType");
        const QJsonValue domainTypeValue = object.value(
            QStringLiteral("domainType"));
        QString domainType;
        if (!domainTypeValue.isString()
            || domainTypeValue.toString().trimmed().isEmpty()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral(
                    "package.invalid_design_extension_domain_reference_domain_type"),
                QStringLiteral("domain reference domainType must be a non-empty string"),
                domainTypePath);
        } else {
            domainType = domainTypeValue.toString().trimmed();
            if (!domainTypeIds.contains(domainType)) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral(
                        "package.unknown_design_extension_domain_reference_type"),
                    QStringLiteral(
                        "domain reference domainType is not declared by this Package"),
                    domainTypePath);
                domainType.clear();
            }
        }

        if (pointerTokens) {
            if (pointers.contains(pointer)) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral(
                        "package.duplicate_design_extension_domain_reference"),
                    QStringLiteral("domain reference pointer is duplicated"),
                    pointerPath);
                pointerTokens.reset();
            } else {
                pointers.insert(pointer);
            }
        }

        if (pointerTokens && !domainType.isEmpty()) {
            definition.domainReferences.append(
                DesignExtensionDomainReferenceDefinition{
                    std::move(*pointerTokens),
                    std::move(domainType)
                });
        }
    }
}

QVector<DesignExtensionDefinition> parseDesignExtensions(
    const QJsonValue& value,
    const QString& packageRoot,
    const QVector<DomainTypeDefinition>& domainTypes,
    QVector<Diagnostic>& diagnostics) {
    QVector<DesignExtensionDefinition> definitions;
    const QString path = QStringLiteral("/designExtensions");
    if (value.isUndefined()) {
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_design_extensions"),
                         QStringLiteral("designExtensions must be an array"),
                         path);
        return definitions;
    }

    const QJsonArray values = value.toArray();
    if (values.size() > kMaximumDesignExtensionsPerPackage) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("package.too_many_design_extensions"),
            QStringLiteral("designExtensions cannot contain more than %1 entries")
                .arg(kMaximumDesignExtensionsPerPackage),
            path);
        return definitions;
    }

    struct CachedSchema {
        QJsonObject document;
        json_schema::CompileStatus status = json_schema::CompileStatus::Invalid;
        std::shared_ptr<const json_schema::CompiledSchema> compiled;
        QVector<json_schema::Issue> issues;
    };

    QSet<QString> ids;
    QSet<QString> domainTypeIds;
    domainTypeIds.reserve(domainTypes.size());
    for (const DomainTypeDefinition& domainType : domainTypes) {
        domainTypeIds.insert(domainType.id);
    }
    QHash<QString, CachedSchema> schemasByCanonicalPath;
    QSet<QString> attemptedSchemaPaths;
    qint64 accountedSchemaBytes = 0;
    definitions.reserve(values.size());
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString itemPath = QStringLiteral("%1/%2").arg(path).arg(index);
        if (!values.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_design_extension"),
                             QStringLiteral("design extension must be an object"),
                             itemPath);
            continue;
        }

        const qsizetype itemDiagnosticStart = diagnostics.size();
        const QJsonObject object = values.at(index).toObject();
        static const std::array<QString, 5> allowed = {
            QStringLiteral("id"),
            QStringLiteral("schema"),
            QStringLiteral("version"),
            QStringLiteral("editor"),
            QStringLiteral("domainReferences")
        };
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (std::find(allowed.cbegin(), allowed.cend(), it.key())
                == allowed.cend()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.unknown_design_extension_field"),
                    QStringLiteral("design extension field %1 is not supported")
                        .arg(it.key()),
                    itemPath + QLatin1Char('/') + jsonPointerToken(it.key()));
            }
        }

        DesignExtensionDefinition definition;
        definition.id = requiredString(
            object, QStringLiteral("id"), itemPath, diagnostics);
        definition.schema = requiredString(
            object, QStringLiteral("schema"), itemPath, diagnostics);
        definition.version = requiredInteger(
            object, QStringLiteral("version"), 0, itemPath, diagnostics);
        if (object.contains(QStringLiteral("editor"))) {
            const QJsonValue editorValue = object.value(QStringLiteral("editor"));
            const QString editorPath = itemPath + QStringLiteral("/editor");
            if (!editorValue.isObject()) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.invalid_design_extension_editor"),
                    QStringLiteral("design extension editor must be an object"),
                    editorPath);
            } else {
                const QJsonObject editorObject = editorValue.toObject();
                for (auto it = editorObject.constBegin();
                     it != editorObject.constEnd(); ++it) {
                    if (it.key() != QStringLiteral("kind")) {
                        appendDiagnostic(
                            diagnostics,
                            QStringLiteral("error"),
                            QStringLiteral(
                                "package.unknown_design_extension_editor_field"),
                            QStringLiteral("design extension editor field %1 is not supported")
                                .arg(it.key()),
                            editorPath + QLatin1Char('/')
                                + jsonPointerToken(it.key()));
                    }
                }
                definition.editor = DesignExtensionEditorDefinition{
                    requiredString(
                        editorObject,
                        QStringLiteral("kind"),
                        editorPath,
                        diagnostics)
                };
            }
        }
        if (object.contains(QStringLiteral("domainReferences"))) {
            parseDesignExtensionDomainReferences(
                object.value(QStringLiteral("domainReferences")),
                domainTypeIds,
                itemPath + QStringLiteral("/domainReferences"),
                definition,
                diagnostics);
        }

        const auto isAsciiLetterOrDigit = [](const auto character) {
            const ushort value = character.unicode();
            return (value >= 'A' && value <= 'Z')
                || (value >= 'a' && value <= 'z')
                || (value >= '0' && value <= '9');
        };
        const bool validId = !definition.id.isEmpty()
            && isAsciiLetterOrDigit(definition.id.front())
            && std::all_of(
                std::next(definition.id.cbegin()),
                definition.id.cend(),
                [&](const auto character) {
                    return isAsciiLetterOrDigit(character)
                        || character == QLatin1Char('.')
                        || character == QLatin1Char('_')
                        || character == QLatin1Char('-');
                });
        if (!definition.id.isEmpty() && !validId) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.invalid_design_extension_id"),
                QStringLiteral(
                    "design extension id must match [A-Za-z0-9][A-Za-z0-9._-]*"),
                itemPath + QStringLiteral("/id"));
        }

        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_design_extension"),
                             QStringLiteral("design extension id is duplicated"),
                             itemPath + QStringLiteral("/id"));
        }
        ids.insert(definition.id);

        if (definition.version <= 0) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_design_extension_version"),
                             QStringLiteral("design extension version must be a positive integer"),
                             itemPath + QStringLiteral("/version"));
        }

        if (!definition.schema.isEmpty()) {
            const QStringList components = definition.schema.split(
                QLatin1Char('/'), Qt::KeepEmptyParts);
            const bool invalidRelativePath = QFileInfo(definition.schema).isAbsolute()
                || definition.schema.contains(QLatin1Char('\\'))
                || std::any_of(
                    components.cbegin(), components.cend(), [](const QString& component) {
                        return component.isEmpty()
                            || component == QStringLiteral(".")
                            || component == QStringLiteral("..");
                    });
            const QString schemaPath = QDir(packageRoot).filePath(definition.schema);
            const QFileInfo schemaInfo(schemaPath);
            if (invalidRelativePath) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.design_extension_schema_escape"),
                    QStringLiteral(
                        "design extension schema must be a contained relative POSIX path"),
                    itemPath + QStringLiteral("/schema"));
            } else if (!schemaInfo.isFile()) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.design_extension_schema_missing"),
                                 QStringLiteral("design extension schema does not exist or is not a file"),
                                 itemPath + QStringLiteral("/schema"));
            } else if (!pathIsInside(packageRoot, schemaPath)) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("error"),
                    QStringLiteral("package.design_extension_schema_escape"),
                    QStringLiteral("design extension schema escapes the Package root"),
                    itemPath + QStringLiteral("/schema"));
            } else if (diagnostics.size() == itemDiagnosticStart) {
                const QString canonicalSchemaPath = schemaInfo.canonicalFilePath();
                const auto cachedSchema = schemasByCanonicalPath.constFind(
                    canonicalSchemaPath);
                if (cachedSchema != schemasByCanonicalPath.constEnd()) {
                    definition.schemaDocument = cachedSchema->document;
                    definition.schemaStatus = cachedSchema->status;
                    definition.compiledSchema = cachedSchema->compiled;
                    definition.schemaIssues = cachedSchema->issues;
                    appendDesignExtensionSchemaProfileDiagnostics(
                        definition, itemPath, diagnostics);
                } else if (attemptedSchemaPaths.contains(canonicalSchemaPath)) {
                    // The first attempt already produced the authoritative
                    // diagnostic. Do not re-read an invalid shared schema.
                } else {
                    attemptedSchemaPaths.insert(canonicalSchemaPath);
                    const qint64 schemaBytes = schemaInfo.size();
                    if (schemaBytes < 0) {
                        appendDiagnostic(
                            diagnostics,
                            QStringLiteral("error"),
                            QStringLiteral(
                                "package.design_extension_schema_read_failed"),
                            QStringLiteral(
                                "could not determine design extension schema size"),
                            itemPath + QStringLiteral("/schema"));
                    } else if (schemaBytes
                               > kMaximumDesignExtensionSchemaBytes) {
                        appendDiagnostic(
                            diagnostics,
                            QStringLiteral("error"),
                            QStringLiteral(
                                "package.design_extension_schema_too_large"),
                            QStringLiteral(
                                "design extension schema exceeds the %1 byte limit")
                                .arg(kMaximumDesignExtensionSchemaBytes),
                            itemPath + QStringLiteral("/schema"));
                    } else if (schemaBytes
                               > kMaximumDesignExtensionSchemaTotalBytes
                                   - accountedSchemaBytes) {
                        appendDiagnostic(
                            diagnostics,
                            QStringLiteral("error"),
                            QStringLiteral(
                                "package.design_extension_schema_budget_exceeded"),
                            QStringLiteral(
                                "unique design extension schemas exceed the %1 byte Package budget")
                                .arg(kMaximumDesignExtensionSchemaTotalBytes),
                            itemPath + QStringLiteral("/schema"));
                    } else {
                        accountedSchemaBytes += schemaBytes;
                        std::optional<QJsonObject> schema =
                            package_detail::loadDesignExtensionSchema(
                                schemaPath,
                                itemPath + QStringLiteral("/schema"),
                                diagnostics);
                        if (schema) {
                            definition.schemaDocument = std::move(*schema);
                            json_schema::CompileResult compilation =
                                json_schema::compile(
                                    definition.schemaDocument);
                            definition.schemaStatus = compilation.status;
                            definition.compiledSchema = std::move(
                                compilation.schema);
                            definition.schemaIssues = std::move(
                                compilation.issues);
                            appendDesignExtensionSchemaProfileDiagnostics(
                                definition, itemPath, diagnostics);
                            schemasByCanonicalPath.insert(
                                canonicalSchemaPath,
                                CachedSchema{
                                    definition.schemaDocument,
                                    definition.schemaStatus,
                                    definition.compiledSchema,
                                    definition.schemaIssues
                                });
                        }
                    }
                }
            }
        }

        definitions.append(std::move(definition));
    }
    return definitions;
}

bool valueMatchesType(const QJsonValue& value, const ParameterDefinition& definition) {
    return valueMatchesParameterType(value, definition.type);
}

} // namespace

QVector<AttachmentSlotDefinition> effectiveExplicitAttachmentSlots(
    const AttachmentDefinition& definition) {
    if (definition.slotMode != AttachmentSlotMode::Explicit) {
        return {};
    }
    if (definition.maxPerRouter <= 0
        || definition.maxPerRouter > kMaximumEndpointAttachmentsPerRouter
        || definition.positions.size()
            > kMaximumEndpointAttachmentsPerRouter) {
        return {};
    }
    if (!definition.positions.isEmpty()) {
        return definition.positions;
    }

    QVector<AttachmentSlotDefinition> effectiveSlots;
    effectiveSlots.reserve(definition.maxPerRouter);
    for (int index = 0; index < definition.maxPerRouter; ++index) {
        const QString id = QString::number(index);
        effectiveSlots.append({
            id, QStringLiteral("Local port %1").arg(index)});
    }
    return effectiveSlots;
}

QString PackageDefinition::key() const {
    return PackageReference{id, version}.key();
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

const DomainTypeDefinition* PackageDefinition::domainType(const QString& id) const {
    const auto it = std::find_if(domainTypes.cbegin(), domainTypes.cend(), [&](const auto& value) {
        return value.id == id;
    });
    return it == domainTypes.cend() ? nullptr : &(*it);
}

const ElementPropertyDefinition* ElementPropertySetDefinition::property(
    const QString& id) const {
    const auto it = std::find_if(
        properties.cbegin(), properties.cend(), [&](const auto& value) {
            return value.id == id;
        });
    return it == properties.cend() ? nullptr : &(*it);
}

std::optional<DomainAssignmentRule> DomainTypeDefinition::assignmentRule(
    ElementKind elementKind) const {
    const auto rule = std::find_if(
        assignmentRules.cbegin(), assignmentRules.cend(),
        [&](const DomainAssignmentRule& candidate) {
            return candidate.elementKind == elementKind;
        });
    if (rule != assignmentRules.cend()) {
        return *rule;
    }
    if (!assignmentRules.isEmpty() || !appliesTo.contains(elementKind)) {
        return std::nullopt;
    }
    return legacyDomainAssignmentRule(elementKind, cardinality, required);
}

const ElementPropertySetDefinition* PackageDefinition::elementPropertySet(
    const QString& id) const {
    const auto it = std::find_if(
        elementPropertySets.cbegin(), elementPropertySets.cend(),
        [&](const auto& value) { return value.id == id; });
    return it == elementPropertySets.cend() ? nullptr : &(*it);
}

const DesignExtensionDefinition* PackageDefinition::designExtension(
    const QString& id) const {
    const auto it = std::find_if(
        designExtensions.cbegin(), designExtensions.cend(),
        [&](const DesignExtensionDefinition& value) { return value.id == id; });
    return it == designExtensions.cend() ? nullptr : &(*it);
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
    if (package.formatVersion < kMinimumPackageFormatVersion
        || package.formatVersion > kMaximumPackageFormatVersion) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.unsupported_version"),
                         QStringLiteral("formatVersion must be between %1 and %2")
                             .arg(kMinimumPackageFormatVersion)
                             .arg(kMaximumPackageFormatVersion),
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

    const QJsonValue domainTypesValue = rootObject->value(
        QStringLiteral("domainTypes"));
    package.domainTypes = parseDomainTypes(
        domainTypesValue, package.formatVersion, result.diagnostics);
    package.runtimeCapabilities = parseRuntimeCapabilities(
        rootObject->value(QStringLiteral("runtimeCapabilities")),
        package.formatVersion,
        !domainTypesValue.isUndefined(),
        result.diagnostics);
    package.elementPropertySets = parseElementPropertySets(
        rootObject->value(QStringLiteral("elementPropertySets")),
        package.formatVersion,
        endpointIds,
        result.diagnostics);
    package.designExtensionsDeclared = rootObject->contains(
        QStringLiteral("designExtensions"));
    package.designExtensions = parseDesignExtensions(
        rootObject->value(QStringLiteral("designExtensions")),
        package.rootPath,
        package.domainTypes,
        result.diagnostics);

    const QJsonObject attachment = requiredObject(*rootObject,
                                                  QStringLiteral("attachment"),
                                                  QString(),
                                                  result.diagnostics);
    package.attachment.maxPerRouter = requiredInteger(attachment,
                                                      QStringLiteral("maxPerRouter"),
                                                      1,
                                                      QStringLiteral("/attachment"),
                                                      result.diagnostics);
    if (package.attachment.maxPerRouter <= 0
        || package.attachment.maxPerRouter
            > kMaximumEndpointAttachmentsPerRouter) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_attachment_capacity"),
                         QStringLiteral("maxPerRouter must be between 1 and %1")
                             .arg(kMaximumEndpointAttachmentsPerRouter),
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
        if (slotArray.size() > kMaximumEndpointAttachmentsPerRouter) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("package.too_many_attachment_slots"),
                QStringLiteral("attachment slots cannot exceed %1")
                    .arg(kMaximumEndpointAttachmentsPerRouter),
                QStringLiteral("/attachment/slots"));
        }
        const qsizetype slotCount = (std::min)(
            slotArray.size(),
            static_cast<qsizetype>(
                kMaximumEndpointAttachmentsPerRouter));
        QSet<QString> slotIds;
        for (qsizetype index = 0; index < slotCount; ++index) {
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

} // namespace finepaper
