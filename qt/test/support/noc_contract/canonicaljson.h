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

// Gate 0's Qt JSON domain is intentionally lossless: null, booleans, strings,
// objects, explicitly catalogued arrays, and integers in [-2^53+1, 2^53-1].
// Fractional/non-finite/unsafe doubles are rejected rather than rounded.
QByteArray canonicalJson(const QJsonValue &value, const CanonicalRuleSet &rules);
QString sha256Digest(QByteArrayView bytes);
