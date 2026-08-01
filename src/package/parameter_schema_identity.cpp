#include "package/parameter_schema_identity.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace finepaper {
namespace {

QJsonObject parameterSemantics(const ParameterDefinition& definition) {
    QJsonObject semantics = {
        {QStringLiteral("id"), definition.id},
        {QStringLiteral("type"), static_cast<int>(definition.type)},
        {QStringLiteral("hasDefault"), definition.hasDefault},
        {QStringLiteral("values"),
         QJsonArray::fromStringList(definition.values)},
    };
    if (definition.hasDefault) {
        semantics.insert(
            QStringLiteral("default"), definition.defaultValue);
    }
    if (definition.minimum) {
        semantics.insert(QStringLiteral("minimum"), *definition.minimum);
    }
    if (definition.maximum) {
        semantics.insert(QStringLiteral("maximum"), *definition.maximum);
    }
    return semantics;
}

template <typename Definition, typename AppendSpecificSemantics>
QString schemaIdentity(
    const QVector<Definition>& definitions,
    AppendSpecificSemantics appendSpecificSemantics) {
    QVector<const Definition*> ordered;
    ordered.reserve(definitions.size());
    for (const Definition& definition : definitions) {
        ordered.append(&definition);
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [](const Definition* left, const Definition* right) {
            return left->id < right->id;
        });

    QJsonArray schema;
    for (const Definition* definition : ordered) {
        QJsonObject semantics = parameterSemantics(*definition);
        appendSpecificSemantics(*definition, semantics);
        schema.append(semantics);
    }
    const QByteArray canonical = QJsonDocument(schema).toJson(
        QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(
        canonical, QCryptographicHash::Sha256).toHex());
}

} // namespace

QString parameterSchemaIdentity(
    const QVector<ParameterDefinition>& definitions) {
    return schemaIdentity(
        definitions,
        [](const ParameterDefinition&, QJsonObject&) {});
}

QString elementPropertySchemaIdentity(
    const QVector<ElementPropertyDefinition>& definitions) {
    return schemaIdentity(
        definitions,
        [](const ElementPropertyDefinition& definition,
           QJsonObject& semantics) {
            semantics.insert(
                QStringLiteral("multiple"), definition.multiple);
        });
}

} // namespace finepaper
