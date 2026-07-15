#include "canonicaljson.h"

#include <QCryptographicHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::runtime_error(message.toStdString());
}

QString pointerToken(QString token) {
    token.replace(QStringLiteral("~"), QStringLiteral("~0"));
    token.replace(QStringLiteral("/"), QStringLiteral("~1"));
    return token;
}

QString childPointer(const QString &parent, const QString &token) {
    return parent + QStringLiteral("/") + pointerToken(token);
}

QString wildcardPointer(QString pointer) {
    const auto parts = pointer.split(QLatin1Char('/'));
    QStringList normalized;
    normalized.reserve(parts.size());
    for (const auto &part : parts) {
        bool isIndex = false;
        part.toLongLong(&isIndex);
        normalized.append(isIndex && !part.isEmpty() ? QStringLiteral("*") : part);
    }
    return normalized.join(QLatin1Char('/'));
}

QByteArray encodeString(const QString &value, const QString &location) {
    QByteArray result(1, '"');
    for (qsizetype index = 0; index < value.size(); ++index) {
        const ushort codeUnit = value.at(index).unicode();
        switch (codeUnit) {
        case '"': result += "\\\""; continue;
        case '\\': result += "\\\\"; continue;
        case '\b': result += "\\b"; continue;
        case '\t': result += "\\t"; continue;
        case '\n': result += "\\n"; continue;
        case '\f': result += "\\f"; continue;
        case '\r': result += "\\r"; continue;
        default: break;
        }
        if (codeUnit < 0x20) {
            result += QByteArrayLiteral("\\u00");
            result += QByteArray::number(codeUnit, 16).rightJustified(2, '0');
            continue;
        }
        if (QChar::isHighSurrogate(codeUnit)) {
            if (index + 1 >= value.size() ||
                !QChar::isLowSurrogate(value.at(index + 1).unicode())) {
                fail(location + QStringLiteral(": unpaired high surrogate"));
            }
            result += value.mid(index, 2).toUtf8();
            ++index;
            continue;
        }
        if (QChar::isLowSurrogate(codeUnit)) {
            fail(location + QStringLiteral(": unpaired low surrogate"));
        }
        result += QString(value.at(index)).toUtf8();
    }
    result += '"';
    return result;
}

class CanonicalWriter final {
public:
    explicit CanonicalWriter(const CanonicalRuleSet &rules) : m_rules(rules) {}

    QByteArray write(const QJsonValue &value, const QString &pointer = {}) const {
        switch (value.type()) {
        case QJsonValue::Null:
            return QByteArrayLiteral("null");
        case QJsonValue::Bool:
            return value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
        case QJsonValue::Double:
            return writeNumber(value.toDouble(), displayPointer(pointer));
        case QJsonValue::String:
            return encodeString(value.toString(), displayPointer(pointer));
        case QJsonValue::Array:
            return writeArray(value.toArray(), pointer);
        case QJsonValue::Object:
            return writeObject(value.toObject(), pointer);
        case QJsonValue::Undefined:
            fail(displayPointer(pointer) + QStringLiteral(": undefined is not JSON"));
        }
        fail(displayPointer(pointer) + QStringLiteral(": unsupported JSON value"));
    }

private:
    static QString displayPointer(const QString &pointer) {
        return pointer.isEmpty() ? QStringLiteral("/") : pointer;
    }

    static QByteArray writeNumber(double value, const QString &location) {
        constexpr double maximumSafeInteger = 9007199254740991.0;
        if (!std::isfinite(value) || std::trunc(value) != value ||
            std::abs(value) > maximumSafeInteger) {
            fail(location +
                 QStringLiteral(": number is outside the frozen safe-integer JSON domain"));
        }
        if (value == 0.0) {
            return QByteArrayLiteral("0");
        }
        return QByteArray::number(static_cast<qint64>(value));
    }

    QByteArray writeObject(const QJsonObject &object, const QString &pointer) const {
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
            return QString::compare(left, right, Qt::CaseSensitive) < 0;
        });

        QByteArray result(1, '{');
        bool first = true;
        for (const auto &key : keys) {
            if (!first) {
                result += ',';
            }
            first = false;
            result += encodeString(key, displayPointer(pointer));
            result += ':';
            result += write(object.value(key), childPointer(pointer, key));
        }
        result += '}';
        return result;
    }

    const CanonicalRuleSet::Rule &ruleFor(const QString &pointer) const {
        const auto &rules = m_rules.rules();
        auto iterator = rules.constFind(pointer);
        if (iterator == rules.cend()) {
            iterator = rules.constFind(wildcardPointer(pointer));
        }
        if (iterator == rules.cend()) {
            fail(displayPointer(pointer) +
                 QStringLiteral(": array has no explicit canonical collection rule"));
        }
        return iterator.value();
    }

    QJsonValue sortComponent(const QString &key,
                             const QJsonValue &item,
                             const QString &itemPointer) const {
        if (key == QStringLiteral("unicodeScalarValue")) {
            if (!item.isString()) {
                fail(displayPointer(itemPointer) +
                     QStringLiteral(": unicodeScalarValue requires a string item"));
            }
            return item;
        }
        if (key == QStringLiteral("canonicalJson")) {
            return QString::fromUtf8(write(item, itemPointer));
        }
        if (!item.isObject()) {
            fail(displayPointer(itemPointer) +
                 QStringLiteral(": object sort key requires an object item"));
        }
        const auto object = item.toObject();
        if (key == QStringLiteral("persistedEndpointCanonicalKey") ||
            key == QStringLiteral("patchEndpointCanonicalKey")) {
            const bool patch = key.startsWith(QStringLiteral("patch"));
            const QString state = object.value(QStringLiteral("state")).toString();
            const bool resolved = state == QStringLiteral("resolved");
            if (!resolved && state != QStringLiteral("unresolved")) {
                fail(displayPointer(itemPointer) +
                     QStringLiteral(": endpoint sort key requires resolved/unresolved state"));
            }
            const QString subjectProperty = resolved ? QStringLiteral("subject")
                                                     : QStringLiteral("intendedSubject");
            const auto subject = object.value(subjectProperty).toObject();
            const QString subjectKind = subject.value(QStringLiteral("kind")).toString();
            if (subjectKind.isEmpty()) {
                fail(displayPointer(itemPointer) +
                     QStringLiteral(": endpoint sort key requires subject kind"));
            }

            QString referenceToken;
            if (patch) {
                const auto reference = subject.value(QStringLiteral("ref")).toObject();
                if (reference.size() != 1 ||
                    (!reference.contains(QStringLiteral("id")) &&
                     !reference.contains(QStringLiteral("localRef")))) {
                    fail(displayPointer(itemPointer) +
                         QStringLiteral(": patch endpoint requires exactly id or localRef"));
                }
                const QString referenceKind = reference.contains(QStringLiteral("id"))
                                                  ? QStringLiteral("id")
                                                  : QStringLiteral("localRef");
                const QString referenceValue = reference.value(referenceKind).toString();
                if (referenceValue.isEmpty()) {
                    fail(displayPointer(itemPointer) +
                         QStringLiteral(": patch endpoint reference must be a string"));
                }
                referenceToken = referenceKind + QLatin1Char(':') + referenceValue;
            } else {
                const QString id = subject.value(QStringLiteral("id")).toString();
                if (id.isEmpty()) {
                    fail(displayPointer(itemPointer) +
                         QStringLiteral(": persisted endpoint requires subject id"));
                }
                referenceToken = QStringLiteral("id:") + id;
            }

            QStringList tuple{resolved ? QStringLiteral("0") : QStringLiteral("1"),
                              subjectKind,
                              referenceToken};
            if (!resolved) {
                const QString reason = object.value(QStringLiteral("reasonCode")).toString();
                if (reason.isEmpty()) {
                    fail(displayPointer(itemPointer) +
                         QStringLiteral(": unresolved endpoint requires reasonCode"));
                }
                tuple.append(reason);
            }
            return tuple.join(QChar(0x001f));
        }
        if (key == QStringLiteral("subjectsCanonicalJson") ||
            key == QStringLiteral("detailsCanonicalJson")) {
            const QString property = key.startsWith(QStringLiteral("subjects"))
                                         ? QStringLiteral("subjects")
                                         : QStringLiteral("details");
            if (!object.contains(property)) {
                fail(displayPointer(itemPointer) + QStringLiteral(": missing sort key ") + key);
            }
            return QString::fromUtf8(
                write(object.value(property), childPointer(itemPointer, property)));
        }
        if (!object.contains(key)) {
            fail(displayPointer(itemPointer) + QStringLiteral(": missing sort key ") + key);
        }
        return object.value(key);
    }

    static int compareScalar(const QJsonValue &left, const QJsonValue &right) {
        if (left.type() != right.type()) {
            return static_cast<int>(left.type()) < static_cast<int>(right.type()) ? -1 : 1;
        }
        if (left.isString()) {
            const QByteArray leftBytes = left.toString().toUtf8();
            const QByteArray rightBytes = right.toString().toUtf8();
            return leftBytes.compare(rightBytes);
        }
        if (left.isDouble()) {
            if (left.toDouble() < right.toDouble()) return -1;
            if (left.toDouble() > right.toDouble()) return 1;
            return 0;
        }
        if (left.isBool()) {
            return left.toBool() == right.toBool() ? 0 : (left.toBool() ? 1 : -1);
        }
        return left == right ? 0 : -1;
    }

    int compareItems(const QJsonValue &left,
                     const QJsonValue &right,
                     const QStringList &sortKeys,
                     const QString &pointer) const {
        for (const auto &key : sortKeys) {
            const int comparison = compareScalar(
                sortComponent(key, left, pointer),
                sortComponent(key, right, pointer));
            if (comparison != 0) {
                return comparison;
            }
        }
        return 0;
    }

    QByteArray writeArray(const QJsonArray &array, const QString &pointer) const {
        const auto &rule = ruleFor(pointer);
        QList<QJsonValue> values;
        values.reserve(array.size());
        for (const auto &value : array) {
            values.append(value);
        }

        if (rule.kind != CanonicalRuleSet::CollectionKind::Ordered) {
            if (rule.sortKey.isEmpty()) {
                fail(displayPointer(pointer) + QStringLiteral(": sortable rule has no sort key"));
            }
            const auto less = [&](const QJsonValue &left, const QJsonValue &right) {
                return compareItems(left, right, rule.sortKey, pointer + QStringLiteral("/*")) < 0;
            };
            QList<QJsonValue> sorted = values;
            std::stable_sort(sorted.begin(), sorted.end(), less);
            for (qsizetype index = 1; index < sorted.size(); ++index) {
                if (compareItems(sorted.at(index - 1), sorted.at(index), rule.sortKey,
                                 pointer + QStringLiteral("/*")) == 0) {
                    fail(displayPointer(pointer) +
                         QStringLiteral(": ambiguous or duplicate collection sort key"));
                }
            }
            if (rule.kind == CanonicalRuleSet::CollectionKind::DerivedOrdered) {
                for (qsizetype index = 0; index < values.size(); ++index) {
                    if (write(values.at(index), childPointer(pointer, QString::number(index))) !=
                        write(sorted.at(index), childPointer(pointer, QString::number(index)))) {
                        fail(displayPointer(pointer) +
                             QStringLiteral(": derived-ordered collection is not canonical"));
                    }
                }
            } else {
                values = std::move(sorted);
            }
        }

        QByteArray result(1, '[');
        for (qsizetype index = 0; index < values.size(); ++index) {
            if (index != 0) {
                result += ',';
            }
            result += write(values.at(index), childPointer(pointer, QString::number(index)));
        }
        result += ']';
        return result;
    }

    const CanonicalRuleSet &m_rules;
};

} // namespace

CanonicalRuleSet CanonicalRuleSet::fromCatalog(
    const QJsonObject &canonicalVectorCatalog,
    const QList<CanonicalRuleBinding> &bindings) {
    const QJsonArray catalog =
        canonicalVectorCatalog.value(QStringLiteral("canonicalCollections")).toArray();
    if (catalog.isEmpty()) {
        fail(QStringLiteral("canonicalCollections: missing explicit rule table"));
    }

    CanonicalRuleSet result;
    for (const auto &binding : bindings) {
        if (result.m_rules.contains(binding.instancePointer)) {
            fail(binding.instancePointer + QStringLiteral(": ambiguous canonical rule binding"));
        }
        QList<QJsonObject> matches;
        for (const auto &value : catalog) {
            const auto object = value.toObject();
            if (object.value(QStringLiteral("schemaId")).toString() == binding.schemaId &&
                object.value(QStringLiteral("schemaPointer")).toString() ==
                    binding.schemaPointer) {
                matches.append(object);
            }
        }
        if (matches.size() != 1) {
            fail(binding.schemaId + binding.schemaPointer +
                 QStringLiteral(": canonical rule must resolve exactly once"));
        }

        const auto match = matches.first();
        const QString kind = match.value(QStringLiteral("kind")).toString();
        Rule rule;
        if (kind == QStringLiteral("set")) {
            rule.kind = CollectionKind::Set;
        } else if (kind == QStringLiteral("ordered")) {
            rule.kind = CollectionKind::Ordered;
        } else if (kind == QStringLiteral("derived-ordered")) {
            rule.kind = CollectionKind::DerivedOrdered;
        } else {
            fail(binding.schemaId + binding.schemaPointer +
                 QStringLiteral(": unknown canonical collection kind"));
        }
        for (const auto &key : match.value(QStringLiteral("sortKey")).toArray()) {
            if (!key.isString() || key.toString().isEmpty()) {
                fail(binding.schemaId + binding.schemaPointer +
                     QStringLiteral(": invalid canonical sort key"));
            }
            rule.sortKey.append(key.toString());
        }
        if (rule.kind != CollectionKind::Ordered && rule.sortKey.isEmpty()) {
            fail(binding.schemaId + binding.schemaPointer +
                 QStringLiteral(": sortable collection requires sort keys"));
        }
        rule.schemaId = binding.schemaId;
        rule.schemaPointer = binding.schemaPointer;
        result.m_rules.insert(binding.instancePointer, rule);
    }
    return result;
}

QByteArray canonicalJson(const QJsonValue &value, const CanonicalRuleSet &rules) {
    return CanonicalWriter(rules).write(value);
}

QString sha256Digest(QByteArrayView bytes) {
    return QStringLiteral("sha256:") +
           QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                                   .toHex());
}
