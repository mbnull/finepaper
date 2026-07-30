#include "package/design_extension_schema.h"

#include "package/package.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>
#include <array>

namespace finepaper::package_detail {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& code,
                      const QString& message,
                      const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"), code, message, path, QStringLiteral("package")
    });
}

QString jsonPointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
}

template<std::size_t Size>
bool containsKeyword(const std::array<QString, Size>& keywords,
                     const QString& candidate) {
    return std::find(keywords.cbegin(), keywords.cend(), candidate)
        != keywords.cend();
}

QString schemaReferencePath(const QString& diagnosticPath,
                            const QStringList& pointerTokens,
                            const QString& referenceKeyword) {
    qsizetype capacity = diagnosticPath.size() + referenceKeyword.size() + 2;
    for (const QString& token : pointerTokens) {
        capacity += token.size() * 2 + 1;
    }

    QString path = diagnosticPath + QLatin1Char('#');
    path.reserve(capacity);
    for (const QString& token : pointerTokens) {
        path += QLatin1Char('/');
        path += jsonPointerToken(token);
    }
    path += QLatin1Char('/');
    path += jsonPointerToken(referenceKeyword);
    return path;
}

bool validateSchemaReferences(const QJsonValue& schemaValue,
                              const QString& diagnosticPath,
                              QStringList& pointerTokens,
                              QVector<Diagnostic>& diagnostics);

bool validateChildSchema(const QJsonValue& schemaValue,
                         const QString& pointerToken,
                         const QString& diagnosticPath,
                         QStringList& pointerTokens,
                         QVector<Diagnostic>& diagnostics) {
    pointerTokens.append(pointerToken);
    const bool valid = validateSchemaReferences(
        schemaValue, diagnosticPath, pointerTokens, diagnostics);
    pointerTokens.removeLast();
    return valid;
}

bool validateSchemaReferences(const QJsonValue& schemaValue,
                              const QString& diagnosticPath,
                              QStringList& pointerTokens,
                              QVector<Diagnostic>& diagnostics) {
    if (!schemaValue.isObject()) {
        return true;
    }

    const QJsonObject schema = schemaValue.toObject();
    static const std::array<QString, 3> referenceKeywords = {
        QStringLiteral("$ref"),
        QStringLiteral("$dynamicRef"),
        QStringLiteral("$recursiveRef")
    };
    for (const QString& keyword : referenceKeywords) {
        const auto refIt = schema.constFind(keyword);
        if (refIt == schema.constEnd()) {
            continue;
        }
        const QString refPath = schemaReferencePath(
            diagnosticPath, pointerTokens, keyword);
        if (!refIt.value().isString() || refIt.value().toString().isEmpty()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("package.design_extension_schema_invalid_ref"),
                QStringLiteral(
                    "design extension schema %1 must be a non-empty string")
                    .arg(keyword),
                refPath);
            return false;
        }
        if (!refIt.value().toString().startsWith(QLatin1Char('#'))) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("package.design_extension_schema_external_ref"),
                QStringLiteral(
                    "design extension schema %1 must resolve within the same document")
                    .arg(keyword),
                refPath);
            return false;
        }
    }

    // Descend only through JSON Schema applicators. Annotation values such as
    // default/examples are arbitrary JSON and may legitimately contain a data
    // property named "$ref".
    static const std::array<QString, 5> schemaMapKeywords = {
        QStringLiteral("$defs"),
        QStringLiteral("definitions"),
        QStringLiteral("dependentSchemas"),
        QStringLiteral("patternProperties"),
        QStringLiteral("properties")
    };
    static const std::array<QString, 12> directSchemaKeywords = {
        QStringLiteral("additionalItems"),
        QStringLiteral("additionalProperties"),
        QStringLiteral("contains"),
        QStringLiteral("contentSchema"),
        QStringLiteral("else"),
        QStringLiteral("if"),
        QStringLiteral("items"),
        QStringLiteral("not"),
        QStringLiteral("propertyNames"),
        QStringLiteral("then"),
        QStringLiteral("unevaluatedItems"),
        QStringLiteral("unevaluatedProperties")
    };
    static const std::array<QString, 4> schemaArrayKeywords = {
        QStringLiteral("allOf"),
        QStringLiteral("anyOf"),
        QStringLiteral("oneOf"),
        QStringLiteral("prefixItems")
    };

    for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
        if (containsKeyword(schemaMapKeywords, it.key())
            && it.value().isObject()) {
            pointerTokens.append(it.key());
            const QJsonObject childMap = it.value().toObject();
            for (auto child = childMap.constBegin();
                 child != childMap.constEnd(); ++child) {
                if (!validateChildSchema(
                        child.value(),
                        child.key(),
                        diagnosticPath,
                        pointerTokens,
                        diagnostics)) {
                    pointerTokens.removeLast();
                    return false;
                }
            }
            pointerTokens.removeLast();
        } else if (containsKeyword(directSchemaKeywords, it.key())) {
            if (it.key() == QStringLiteral("items") && it.value().isArray()) {
                pointerTokens.append(it.key());
                const QJsonArray children = it.value().toArray();
                for (qsizetype index = 0; index < children.size(); ++index) {
                    if (!validateChildSchema(
                            children.at(index),
                            QString::number(index),
                            diagnosticPath,
                            pointerTokens,
                            diagnostics)) {
                        pointerTokens.removeLast();
                        return false;
                    }
                }
                pointerTokens.removeLast();
            } else if (!validateChildSchema(
                           it.value(),
                           it.key(),
                           diagnosticPath,
                           pointerTokens,
                           diagnostics)) {
                return false;
            }
        } else if (containsKeyword(schemaArrayKeywords, it.key())
                   && it.value().isArray()) {
            pointerTokens.append(it.key());
            const QJsonArray children = it.value().toArray();
            for (qsizetype index = 0; index < children.size(); ++index) {
                if (!validateChildSchema(
                        children.at(index),
                        QString::number(index),
                        diagnosticPath,
                        pointerTokens,
                        diagnostics)) {
                    pointerTokens.removeLast();
                    return false;
                }
            }
            pointerTokens.removeLast();
        } else if (it.key() == QStringLiteral("dependencies")
                   && it.value().isObject()) {
            pointerTokens.append(it.key());
            const QJsonObject dependencies = it.value().toObject();
            for (auto dependency = dependencies.constBegin();
                 dependency != dependencies.constEnd(); ++dependency) {
                if ((dependency.value().isObject()
                     || dependency.value().isBool())
                    && !validateChildSchema(
                        dependency.value(),
                        dependency.key(),
                        diagnosticPath,
                        pointerTokens,
                        diagnostics)) {
                    pointerTokens.removeLast();
                    return false;
                }
            }
            pointerTokens.removeLast();
        }
    }
    return true;
}

} // namespace

std::optional<QJsonObject> loadDesignExtensionSchema(
    const QString& schemaPath,
    const QString& diagnosticPath,
    QVector<Diagnostic>& diagnostics) {
    QFile schemaFile(schemaPath);
    if (!schemaFile.open(QIODevice::ReadOnly)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_read_failed"),
            QStringLiteral("could not read design extension schema"),
            diagnosticPath);
        return std::nullopt;
    }
    if (schemaFile.size() > kMaximumDesignExtensionSchemaBytes) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_too_large"),
            QStringLiteral("design extension schema exceeds the %1 byte limit")
                .arg(kMaximumDesignExtensionSchemaBytes),
            diagnosticPath);
        return std::nullopt;
    }

    const QByteArray data = schemaFile.read(
        static_cast<qint64>(kMaximumDesignExtensionSchemaBytes) + 1);
    if (schemaFile.error() != QFileDevice::NoError) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_read_failed"),
            QStringLiteral("could not read design extension schema"),
            diagnosticPath);
        return std::nullopt;
    }
    if (data.size() > kMaximumDesignExtensionSchemaBytes || !schemaFile.atEnd()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_too_large"),
            QStringLiteral("design extension schema exceeds the %1 byte limit")
                .arg(kMaximumDesignExtensionSchemaBytes),
            diagnosticPath);
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_invalid_json"),
            QStringLiteral("invalid design extension schema JSON at offset %1: %2")
                .arg(parseError.offset)
                .arg(parseError.errorString()),
            diagnosticPath);
        return std::nullopt;
    }
    if (!document.isObject()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("package.design_extension_schema_invalid_root"),
            QStringLiteral("design extension schema root must be an object"),
            diagnosticPath);
        return std::nullopt;
    }

    QJsonObject schema = document.object();
    QStringList pointerTokens;
    if (!validateSchemaReferences(
            schema, diagnosticPath, pointerTokens, diagnostics)) {
        return std::nullopt;
    }
    return schema;
}

} // namespace finepaper::package_detail
