#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

struct CanonicalRuleBinding final {
    // JSON Pointer in the value passed to canonicalJson. Use '*' for array items.
    QString instancePointer;
    QString schemaId;
    QString schemaPointer;
};

class CanonicalRuleSet final {
public:
    enum class CollectionKind { Set, Ordered, DerivedOrdered };

    struct Rule final {
        CollectionKind kind = CollectionKind::Ordered;
        QStringList sortKey;
        QString schemaId;
        QString schemaPointer;
    };

    static CanonicalRuleSet fromCatalog(
        const QJsonObject &canonicalVectorCatalog,
        const QList<CanonicalRuleBinding> &bindings);

    const QHash<QString, Rule> &rules() const { return m_rules; }

private:
    QHash<QString, Rule> m_rules;

};

// Gate 0's Qt JSON domain is strict UTF-8 JSON represented by null, booleans,
// strings, objects, explicitly catalogued arrays, and finite IEEE-754 binary64
// numbers. Number spelling is semantic rather than lexical.
QByteArray canonicalJson(const QJsonValue &value, const CanonicalRuleSet &rules);
QString sha256Digest(QByteArrayView bytes);
