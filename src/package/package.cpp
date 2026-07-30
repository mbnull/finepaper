#include "package/package.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

const ElementPropertySetDefinition* PackageDefinition::elementPropertySet(
    const QString& id) const {
    const auto it = std::find_if(
        elementPropertySets.cbegin(), elementPropertySets.cend(),
        [&](const auto& value) { return value.id == id; });
    return it == elementPropertySets.cend() ? nullptr : &(*it);
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

    package.domainTypes = parseDomainTypes(
        rootObject->value(QStringLiteral("domainTypes")),
        package.formatVersion,
        result.diagnostics);
    package.elementPropertySets = parseElementPropertySets(
        rootObject->value(QStringLiteral("elementPropertySets")),
        package.formatVersion,
        endpointIds,
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
    QSet<QString> discoveredPackagePaths;
    QHash<QString, QString> packagePathByKey;
    bool foundUsableRoot = false;

    for (const QString& rootValue : roots) {
        const QString rootPath = QDir::cleanPath(QFileInfo(rootValue).absoluteFilePath());
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("warning"),
                             QStringLiteral("package.root_missing"),
                             QStringLiteral("package root does not exist and was skipped"),
                             rootPath);
            continue;
        }
        foundUsableRoot = true;

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
            const QFileInfo packageInfo(packagePath);
            const QString canonicalPath = packageInfo.canonicalFilePath();
            const QString canonicalPackagePath = canonicalPath.isEmpty()
                ? QDir::cleanPath(packageInfo.absoluteFilePath())
                : canonicalPath;
            if (discoveredPackagePaths.contains(canonicalPackagePath)) {
                continue;
            }
            discoveredPackagePaths.insert(canonicalPackagePath);

            PackageLoadResult loadResult = loadPackage(canonicalPackagePath);
            diagnostics += loadResult.diagnostics;
            if (!loadResult.success || !loadResult.package) {
                continue;
            }
            if (keys.contains(loadResult.package->key())) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("warning"),
                                 QStringLiteral("package.duplicate_ignored"),
                                 QStringLiteral("Package %1 from %2 was ignored; the same id and "
                                                "version are already loaded from %3")
                                     .arg(loadResult.package->key(),
                                          canonicalPackagePath,
                                          packagePathByKey.value(loadResult.package->key())),
                                 canonicalPackagePath);
                continue;
            }
            keys.insert(loadResult.package->key());
            packagePathByKey.insert(loadResult.package->key(), canonicalPackagePath);
            loaded.append(std::move(*loadResult.package));
        }
    }

    if (!roots.isEmpty() && !foundUsableRoot) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.no_usable_roots"),
                         QStringLiteral("none of the configured Package roots are available"),
                         QString());
    }

    std::sort(loaded.begin(), loaded.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key() < rhs.key();
    });
    if (!hasErrors(diagnostics) && (foundUsableRoot || roots.isEmpty())) {
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
