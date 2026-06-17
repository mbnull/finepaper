#include "jsonschemavalidator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

bool schemaFail(QString* error, bool* schemaError, const QString& message) {
    if (schemaError) {
        *schemaError = true;
    }
    return fail(error, message);
}

QString typeName(const QJsonValue& value) {
    if (value.isObject()) {
        return QStringLiteral("object");
    }
    if (value.isArray()) {
        return QStringLiteral("array");
    }
    if (value.isString()) {
        return QStringLiteral("string");
    }
    if (value.isBool()) {
        return QStringLiteral("boolean");
    }
    if (value.isDouble()) {
        return QStringLiteral("number");
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    return QStringLiteral("undefined");
}

bool matchesType(const QJsonValue& value, const QString& type) {
    if (type == QStringLiteral("object")) {
        return value.isObject();
    }
    if (type == QStringLiteral("array")) {
        return value.isArray();
    }
    if (type == QStringLiteral("string")) {
        return value.isString();
    }
    if (type == QStringLiteral("boolean")) {
        return value.isBool();
    }
    if (type == QStringLiteral("number")) {
        return value.isDouble();
    }
    if (type == QStringLiteral("integer")) {
        if (!value.isDouble()) {
            return false;
        }
        const double number = value.toDouble();
        return std::isfinite(number) && std::floor(number) == number;
    }
    if (type == QStringLiteral("null")) {
        return value.isNull();
    }
    return true;
}

QString valueSummary(const QJsonValue& value) {
    if (value.isString()) {
        return QStringLiteral("\"%1\"").arg(value.toString());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    return typeName(value);
}

QString childPath(const QString& base, const QString& key) {
    return base == QStringLiteral("$")
        ? QStringLiteral("$.") + key
        : base + QStringLiteral(".") + key;
}

QString indexPath(const QString& base, qsizetype index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

QString unescapeJsonPointerToken(QString token) {
    return token.replace(QStringLiteral("~1"), QStringLiteral("/"))
        .replace(QStringLiteral("~0"), QStringLiteral("~"));
}

} // namespace

JsonSchemaValidator::JsonSchemaValidator(QJsonObject schema)
    : m_schema(std::move(schema)) {}

JsonSchemaValidator JsonSchemaValidator::fromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("cannot read schema: %1")
                                     .arg(path)
                                     .toStdString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error(QStringLiteral("invalid schema JSON: %1")
                                     .arg(path)
                                     .toStdString());
    }

    return JsonSchemaValidator(document.object());
}

bool JsonSchemaValidator::validate(const QJsonValue& value, QString* error) const {
    return validateAgainst(value, m_schema, QStringLiteral("$"), error);
}

bool JsonSchemaValidator::validateAgainst(const QJsonValue& value,
                                          const QJsonValue& schema,
                                          const QString& path,
                                          QString* error,
                                          bool* schemaError) const {
    if (schema.isBool()) {
        return schema.toBool()
            ? true
            : fail(error, QStringLiteral("%1: schema is false").arg(path));
    }
    if (!schema.isObject()) {
        return schemaFail(error, schemaError, QStringLiteral("%1: invalid schema").arg(path));
    }

    const QJsonObject schemaObject = schema.toObject();
    const QJsonValue refValue = schemaObject.value(QStringLiteral("$ref"));
    if (refValue.isString()) {
        const QString ref = refValue.toString();
        const QJsonValue resolved = resolveRef(ref);
        if (resolved.isUndefined()) {
            return schemaFail(error,
                              schemaError,
                              QStringLiteral("%1: unresolved schema reference %2")
                                  .arg(path, ref));
        }
        if (!validateAgainst(value, resolved, path, error, schemaError)) {
            return false;
        }
    }

    const QJsonValue constValue = schemaObject.value(QStringLiteral("const"));
    if (!constValue.isUndefined() && value != constValue) {
        return fail(error, QStringLiteral("%1: expected %2")
                              .arg(path, valueSummary(constValue)));
    }

    const QJsonValue enumValue = schemaObject.value(QStringLiteral("enum"));
    if (enumValue.isArray()) {
        bool matched = false;
        for (const QJsonValue& candidate : enumValue.toArray()) {
            if (value == candidate) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return fail(error, QStringLiteral("%1: value is not in enum").arg(path));
        }
    }

    const QJsonValue typeValue = schemaObject.value(QStringLiteral("type"));
    if (typeValue.isString()) {
        const QString expectedType = typeValue.toString();
        if (!matchesType(value, expectedType)) {
            return fail(error, QStringLiteral("%1: expected %2, got %3")
                                  .arg(path, expectedType, typeName(value)));
        }
    } else if (typeValue.isArray()) {
        bool matched = false;
        QStringList expectedTypes;
        for (const QJsonValue& typeCandidate : typeValue.toArray()) {
            if (!typeCandidate.isString()) {
                continue;
            }
            const QString expectedType = typeCandidate.toString();
            expectedTypes.append(expectedType);
            if (matchesType(value, expectedType)) {
                matched = true;
            }
        }
        if (!matched) {
            return fail(error, QStringLiteral("%1: expected %2, got %3")
                                  .arg(path, expectedTypes.join(QStringLiteral(" or ")), typeName(value)));
        }
    }

    const QJsonValue allOfValue = schemaObject.value(QStringLiteral("allOf"));
    if (allOfValue.isArray()) {
        for (const QJsonValue& candidate : allOfValue.toArray()) {
            if (!validateAgainst(value, candidate, path, error)) {
                return false;
            }
        }
    }

    const QJsonValue anyOfValue = schemaObject.value(QStringLiteral("anyOf"));
    if (anyOfValue.isArray()) {
        QString firstError;
        bool matched = false;
        for (const QJsonValue& candidate : anyOfValue.toArray()) {
            QString candidateError;
            bool candidateSchemaError = false;
            if (validateAgainst(value, candidate, path, &candidateError, &candidateSchemaError)) {
                matched = true;
                break;
            }
            if (candidateSchemaError) {
                return schemaFail(error, schemaError, candidateError);
            }
            if (firstError.isEmpty()) {
                firstError = candidateError;
            }
        }
        if (!matched) {
            const QString detail = firstError.isEmpty()
                ? QString()
                : QStringLiteral(" First mismatch: %1").arg(firstError);
            return fail(error,
                        QStringLiteral("%1: expected at least one schema in anyOf to match.%2")
                            .arg(path, detail));
        }
    }

    const QJsonValue oneOfValue = schemaObject.value(QStringLiteral("oneOf"));
    if (oneOfValue.isArray()) {
        qsizetype matchCount = 0;
        QString firstError;
        for (const QJsonValue& candidate : oneOfValue.toArray()) {
            QString candidateError;
            bool candidateSchemaError = false;
            if (validateAgainst(value, candidate, path, &candidateError, &candidateSchemaError)) {
                ++matchCount;
            } else if (candidateSchemaError) {
                return schemaFail(error, schemaError, candidateError);
            } else if (firstError.isEmpty()) {
                firstError = candidateError;
            }
        }
        if (matchCount != 1) {
            const QString detail = matchCount == 0 && !firstError.isEmpty()
                ? QStringLiteral(" First mismatch: %1").arg(firstError)
                : QString();
            return fail(error,
                        QStringLiteral("%1: expected exactly one schema in oneOf to match.%2")
                            .arg(path, detail));
        }
    }

    const QJsonValue notValue = schemaObject.value(QStringLiteral("not"));
    if (!notValue.isUndefined()) {
        QString notError;
        bool notSchemaError = false;
        if (validateAgainst(value, notValue, path, &notError, &notSchemaError)) {
            return fail(error, QStringLiteral("%1: must not match forbidden schema").arg(path));
        }
        if (notSchemaError) {
            return schemaFail(error, schemaError, notError);
        }
    }

    const QJsonValue ifValue = schemaObject.value(QStringLiteral("if"));
    if (!ifValue.isUndefined()) {
        const QJsonValue thenValue = schemaObject.value(QStringLiteral("then"));
        QString ifError;
        bool ifSchemaError = false;
        if (!thenValue.isUndefined() &&
            validateAgainst(value, ifValue, path, &ifError, &ifSchemaError)) {
            if (!validateAgainst(value, thenValue, path, error, schemaError)) {
                return false;
            }
        }
        if (ifSchemaError) {
            return schemaFail(error, schemaError, ifError);
        }
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QJsonValue requiredValue = schemaObject.value(QStringLiteral("required"));
        if (requiredValue.isArray()) {
            for (const QJsonValue& requiredEntry : requiredValue.toArray()) {
                if (!requiredEntry.isString()) {
                    continue;
                }
                const QString key = requiredEntry.toString();
                if (!object.contains(key)) {
                    return fail(error, QStringLiteral("%1: missing required property")
                                          .arg(childPath(path, key)));
                }
            }
        }

        const QJsonObject properties = schemaObject.value(QStringLiteral("properties")).toObject();
        for (auto property = properties.constBegin(); property != properties.constEnd(); ++property) {
            if (!object.contains(property.key())) {
                continue;
            }
            if (!validateAgainst(object.value(property.key()),
                                 property.value(),
                                 childPath(path, property.key()),
                                 error)) {
                return false;
            }
        }

        const QJsonValue additionalProperties =
            schemaObject.value(QStringLiteral("additionalProperties"));
        if (additionalProperties.isBool() && !additionalProperties.toBool()) {
            for (auto member = object.constBegin(); member != object.constEnd(); ++member) {
                if (!properties.contains(member.key())) {
                    return fail(error, QStringLiteral("%1: unexpected property")
                                          .arg(childPath(path, member.key())));
                }
            }
        } else if (additionalProperties.isObject() || additionalProperties.isBool()) {
            for (auto member = object.constBegin(); member != object.constEnd(); ++member) {
                if (properties.contains(member.key())) {
                    continue;
                }
                if (!validateAgainst(member.value(),
                                     additionalProperties,
                                     childPath(path, member.key()),
                                     error)) {
                    return false;
                }
            }
        }
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        const QJsonValue minItemsValue = schemaObject.value(QStringLiteral("minItems"));
        if (minItemsValue.isDouble() && array.size() < minItemsValue.toInt()) {
            return fail(error, QStringLiteral("%1: expected at least %2 items")
                                  .arg(path)
                                  .arg(minItemsValue.toInt()));
        }

        const QJsonValue itemsValue = schemaObject.value(QStringLiteral("items"));
        if (!itemsValue.isUndefined()) {
            for (qsizetype index = 0; index < array.size(); ++index) {
                if (!validateAgainst(array.at(index), itemsValue, indexPath(path, index), error)) {
                    return false;
                }
            }
        }

        const QJsonValue containsValue = schemaObject.value(QStringLiteral("contains"));
        if (!containsValue.isUndefined()) {
            bool containsMatch = false;
            for (qsizetype index = 0; index < array.size(); ++index) {
                QString itemError;
                bool itemSchemaError = false;
                if (validateAgainst(array.at(index),
                                    containsValue,
                                    indexPath(path, index),
                                    &itemError,
                                    &itemSchemaError)) {
                    containsMatch = true;
                    break;
                }
                if (itemSchemaError) {
                    return schemaFail(error, schemaError, itemError);
                }
            }
            if (!containsMatch) {
                return fail(error, QStringLiteral("%1: expected at least one matching item")
                                      .arg(path));
            }
        }
    }

    if (value.isString()) {
        const QJsonValue minLengthValue = schemaObject.value(QStringLiteral("minLength"));
        if (minLengthValue.isDouble() && value.toString().size() < minLengthValue.toInt()) {
            return fail(error, QStringLiteral("%1: expected string length at least %2")
                                  .arg(path)
                                  .arg(minLengthValue.toInt()));
        }
    }

    if (value.isDouble()) {
        const QJsonValue minimumValue = schemaObject.value(QStringLiteral("minimum"));
        if (minimumValue.isDouble() && value.toDouble() < minimumValue.toDouble()) {
            return fail(error, QStringLiteral("%1: expected value >= %2")
                                  .arg(path)
                                  .arg(minimumValue.toDouble()));
        }
    }

    return true;
}

QJsonValue JsonSchemaValidator::resolveRef(const QString& ref) const {
    if (ref == QStringLiteral("#")) {
        return m_schema;
    }
    if (!ref.startsWith(QStringLiteral("#/"))) {
        return QJsonValue(QJsonValue::Undefined);
    }

    QJsonValue current(m_schema);
    const QStringList tokens = ref.mid(2).split(QLatin1Char('/'));
    for (const QString& rawToken : tokens) {
        if (!current.isObject()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        const QString token = unescapeJsonPointerToken(rawToken);
        const QJsonObject object = current.toObject();
        if (!object.contains(token)) {
            return QJsonValue(QJsonValue::Undefined);
        }
        current = object.value(token);
    }
    return current;
}
