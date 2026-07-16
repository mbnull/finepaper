#include "canonicaljson.h"

#include <QCryptographicHash>
#include <QJsonArray>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

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

    QByteArray write(const QJsonValue &value,
                     const QString &instancePointer = {},
                     const QString &rulePointer = {}) const {
        switch (value.type()) {
        case QJsonValue::Null:
            return QByteArrayLiteral("null");
        case QJsonValue::Bool:
            return value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
        case QJsonValue::Double:
            return writeNumber(value.toDouble(), displayPointer(instancePointer));
        case QJsonValue::String:
            return encodeString(value.toString(), displayPointer(instancePointer));
        case QJsonValue::Array:
            return writeArray(value.toArray(), instancePointer, rulePointer);
        case QJsonValue::Object:
            return writeObject(value.toObject(), instancePointer, rulePointer);
        case QJsonValue::Undefined:
            fail(displayPointer(instancePointer) + QStringLiteral(": undefined is not JSON"));
        }
        fail(displayPointer(instancePointer) + QStringLiteral(": unsupported JSON value"));
    }

private:
    struct SortAtom final {
        enum class Kind {
            Null,
            Boolean,
            Number,
            UnicodeString,
            UnicodeScalarString,
            CanonicalBytes,
        };

        Kind kind = Kind::Null;
        bool boolean = false;
        double number = 0;
        QString string;
        QByteArray bytes;

        static SortAtom fromJson(const QJsonValue &value, const QString &location) {
            SortAtom atom;
            if (value.isNull()) {
                return atom;
            }
            if (value.isBool()) {
                atom.kind = Kind::Boolean;
                atom.boolean = value.toBool();
                return atom;
            }
            if (value.isDouble()) {
                const double number = value.toDouble();
                if (!std::isfinite(number)) {
                    fail(location + QStringLiteral(": non-finite numeric sort component"));
                }
                atom.kind = Kind::Number;
                atom.number = number;
                return atom;
            }
            if (value.isString()) {
                atom.kind = Kind::UnicodeString;
                atom.string = value.toString();
                return atom;
            }
            fail(location + QStringLiteral(": sort component must be scalar"));
        }

        static SortAtom unicode(QString value) {
            SortAtom atom;
            atom.kind = Kind::UnicodeString;
            atom.string = std::move(value);
            return atom;
        }

        static SortAtom unicodeScalar(QString value) {
            SortAtom atom;
            atom.kind = Kind::UnicodeScalarString;
            atom.string = std::move(value);
            return atom;
        }

        static SortAtom integer(double value) {
            SortAtom atom;
            atom.kind = Kind::Number;
            atom.number = value;
            return atom;
        }

        static SortAtom canonical(QByteArray value) {
            SortAtom atom;
            atom.kind = Kind::CanonicalBytes;
            atom.bytes = std::move(value);
            return atom;
        }
    };

    static QString displayPointer(const QString &pointer) {
        return pointer.isEmpty() ? QStringLiteral("/") : pointer;
    }

    static QByteArray writeNumber(double value, const QString &location) {
        if (!std::isfinite(value)) {
            fail(location + QStringLiteral(": number is not finite binary64"));
        }
        if (value == 0.0) {
            return QByteArrayLiteral("0");
        }

        std::array<char, 128> buffer{};
        const auto conversion = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                               value, std::chars_format::general);
        if (conversion.ec != std::errc{}) {
            fail(location + QStringLiteral(": binary64 number cannot be serialized"));
        }
        QByteArray token(buffer.data(), static_cast<qsizetype>(conversion.ptr - buffer.data()));
        token = token.toLower();
        const qsizetype exponentMarker = token.indexOf('e');
        if (exponentMarker < 0) {
            return token;
        }

        const QByteArray mantissa = token.left(exponentMarker);
        const int exponent = token.mid(exponentMarker + 1).toInt();
        QByteArray digits = mantissa;
        const bool negative = digits.startsWith('-');
        if (negative) {
            digits.remove(0, 1);
        }
        const qsizetype decimalPoint = digits.indexOf('.');
        const qsizetype point = decimalPoint < 0 ? digits.size() + exponent
                                                 : decimalPoint + exponent;
        digits.remove(decimalPoint, 1);
        const int adjustedExponent = static_cast<int>(point) - 1;

        if (adjustedExponent >= -6 && adjustedExponent < 21) {
            QByteArray result;
            if (negative) {
                result += '-';
            }
            if (point <= 0) {
                result += "0.";
                result += QByteArray(point == 0 ? 0 : -point, '0');
                result += digits;
            } else if (point >= digits.size()) {
                result += digits;
                result += QByteArray(point - digits.size(), '0');
            } else {
                result += digits.left(point);
                result += '.';
                result += digits.mid(point);
            }
            return result;
        }

        QByteArray normalizedMantissa = digits.left(1);
        if (digits.size() > 1) {
            normalizedMantissa += '.';
            normalizedMantissa += digits.mid(1);
        }
        QByteArray result;
        if (negative) {
            result += '-';
        }
        result += normalizedMantissa;
        result += 'e';
        if (adjustedExponent >= 0) {
            result += '+';
        }
        result += QByteArray::number(adjustedExponent);
        return result;
    }

    QByteArray writeObject(const QJsonObject &object,
                           const QString &instancePointer,
                           const QString &rulePointer) const {
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
            result += encodeString(key, displayPointer(instancePointer));
            result += ':';
            result += write(object.value(key),
                            childPointer(instancePointer, key),
                            childPointer(rulePointer, key));
        }
        result += '}';
        return result;
    }

    const CanonicalRuleSet::Rule &ruleFor(const QString &rulePointer,
                                          const QString &instancePointer) const {
        const auto &rules = m_rules.rules();
        const auto iterator = rules.constFind(rulePointer);
        if (iterator == rules.cend()) {
            fail(displayPointer(instancePointer) +
                 QStringLiteral(": array has no explicit canonical collection rule"));
        }
        return iterator.value();
    }

    QList<SortAtom> sortComponents(const QString &key,
                                   const QJsonValue &item,
                                   const QString &itemInstancePointer,
                                   const QString &itemRulePointer) const {
        if (key == QStringLiteral("unicodeScalarValue")) {
            if (!item.isString()) {
                fail(displayPointer(itemInstancePointer) +
                     QStringLiteral(": unicodeScalarValue requires a string item"));
            }
            return {SortAtom::unicodeScalar(item.toString())};
        }
        if (key == QStringLiteral("canonicalJson")) {
            return {SortAtom::canonical(
                write(item, itemInstancePointer, itemRulePointer))};
        }
        if (!item.isObject()) {
            fail(displayPointer(itemInstancePointer) +
                 QStringLiteral(": object sort key requires an object item"));
        }
        const auto object = item.toObject();
        if (key == QStringLiteral("persistedEndpointCanonicalKey") ||
            key == QStringLiteral("patchEndpointCanonicalKey")) {
            const bool patch = key.startsWith(QStringLiteral("patch"));
            const QString state = object.value(QStringLiteral("state")).toString();
            const bool resolved = state == QStringLiteral("resolved");
            if (!resolved && state != QStringLiteral("unresolved")) {
                fail(displayPointer(itemInstancePointer) +
                     QStringLiteral(": endpoint sort key requires resolved/unresolved state"));
            }
            const QString subjectProperty = resolved ? QStringLiteral("subject")
                                                     : QStringLiteral("intendedSubject");
            const auto subject = object.value(subjectProperty).toObject();
            const QString subjectKind = subject.value(QStringLiteral("kind")).toString();
            if (subjectKind.isEmpty()) {
                fail(displayPointer(itemInstancePointer) +
                     QStringLiteral(": endpoint sort key requires subject kind"));
            }

            QString referenceKind;
            QString referenceValue;
            if (patch) {
                const auto reference = subject.value(QStringLiteral("ref")).toObject();
                if (reference.size() != 1 ||
                    (!reference.contains(QStringLiteral("id")) &&
                     !reference.contains(QStringLiteral("localRef")))) {
                    fail(displayPointer(itemInstancePointer) +
                         QStringLiteral(": patch endpoint requires exactly id or localRef"));
                }
                referenceKind = reference.contains(QStringLiteral("id"))
                                    ? QStringLiteral("id")
                                    : QStringLiteral("localRef");
                referenceValue = reference.value(referenceKind).toString();
                if (referenceValue.isEmpty()) {
                    fail(displayPointer(itemInstancePointer) +
                         QStringLiteral(": patch endpoint reference must be a string"));
                }
            } else {
                referenceKind = QStringLiteral("id");
                referenceValue = subject.value(QStringLiteral("id")).toString();
                if (referenceValue.isEmpty()) {
                    fail(displayPointer(itemInstancePointer) +
                         QStringLiteral(": persisted endpoint requires subject id"));
                }
            }

            QList<SortAtom> tuple{SortAtom::integer(resolved ? 0 : 1),
                                  SortAtom::unicode(subjectKind),
                                  SortAtom::unicode(referenceKind),
                                  SortAtom::unicode(referenceValue)};
            if (!resolved) {
                const QString reason = object.value(QStringLiteral("reasonCode")).toString();
                if (reason.isEmpty()) {
                    fail(displayPointer(itemInstancePointer) +
                         QStringLiteral(": unresolved endpoint requires reasonCode"));
                }
                tuple.append(SortAtom::unicode(reason));
            }
            return tuple;
        }
        if (key == QStringLiteral("subjectsCanonicalJson") ||
            key == QStringLiteral("detailsCanonicalJson")) {
            const QString property = key.startsWith(QStringLiteral("subjects"))
                                         ? QStringLiteral("subjects")
                                         : QStringLiteral("details");
            if (!object.contains(property)) {
                fail(displayPointer(itemInstancePointer) +
                     QStringLiteral(": missing sort key ") + key);
            }
            return {SortAtom::canonical(
                write(object.value(property),
                      childPointer(itemInstancePointer, property),
                      childPointer(itemRulePointer, property)))};
        }
        if (!object.contains(key)) {
            fail(displayPointer(itemInstancePointer) +
                 QStringLiteral(": missing sort key ") + key);
        }
        return {SortAtom::fromJson(object.value(key),
                                   displayPointer(itemInstancePointer))};
    }

    static int compareAtom(const SortAtom &left, const SortAtom &right) {
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind) ? -1 : 1;
        }
        switch (left.kind) {
        case SortAtom::Kind::Null:
            return 0;
        case SortAtom::Kind::Boolean:
            return left.boolean == right.boolean ? 0 : (left.boolean ? 1 : -1);
        case SortAtom::Kind::Number:
            if (left.number < right.number) return -1;
            if (left.number > right.number) return 1;
            return 0;
        case SortAtom::Kind::UnicodeString:
            return left.string.toUtf8().compare(right.string.toUtf8());
        case SortAtom::Kind::UnicodeScalarString: {
            const auto codePointAt = [](const QString &value, qsizetype &index) -> uint {
                const ushort high = value.at(index).unicode();
                if (QChar::isHighSurrogate(high) && index + 1 < value.size()) {
                    const ushort low = value.at(++index).unicode();
                    return QChar::surrogateToUcs4(high, low);
                }
                return high;
            };
            qsizetype leftIndex = 0;
            qsizetype rightIndex = 0;
            while (leftIndex < left.string.size() && rightIndex < right.string.size()) {
                const uint leftPoint = codePointAt(left.string, leftIndex);
                const uint rightPoint = codePointAt(right.string, rightIndex);
                if (leftPoint < rightPoint) return -1;
                if (leftPoint > rightPoint) return 1;
                ++leftIndex;
                ++rightIndex;
            }
            if (leftIndex < left.string.size()) return 1;
            if (rightIndex < right.string.size()) return -1;
            return 0;
        }
        case SortAtom::Kind::CanonicalBytes:
            return left.bytes.compare(right.bytes);
        }
        return 0;
    }

    static int compareComponentLists(const QList<SortAtom> &left,
                                     const QList<SortAtom> &right) {
        const qsizetype common = std::min(left.size(), right.size());
        for (qsizetype index = 0; index < common; ++index) {
            const int comparison = compareAtom(left.at(index), right.at(index));
            if (comparison != 0) {
                return comparison;
            }
        }
        if (left.size() < right.size()) return -1;
        if (left.size() > right.size()) return 1;
        return 0;
    }

    int compareItems(const QJsonValue &left,
                     const QJsonValue &right,
                     const QStringList &sortKeys,
                     const QString &itemInstancePointer,
                     const QString &itemRulePointer) const {
        for (const auto &key : sortKeys) {
            const int comparison = compareComponentLists(
                sortComponents(key, left, itemInstancePointer, itemRulePointer),
                sortComponents(key, right, itemInstancePointer, itemRulePointer));
            if (comparison != 0) {
                return comparison;
            }
        }
        return 0;
    }

    QByteArray writeArray(const QJsonArray &array,
                         const QString &instancePointer,
                         const QString &rulePointer) const {
        const auto &rule = ruleFor(rulePointer, instancePointer);
        QList<QJsonValue> values;
        values.reserve(array.size());
        for (const auto &value : array) {
            values.append(value);
        }

        if (rule.kind != CanonicalRuleSet::CollectionKind::Ordered) {
            if (rule.sortKey.isEmpty()) {
                fail(displayPointer(instancePointer) +
                     QStringLiteral(": sortable rule has no sort key"));
            }
            const QString itemInstancePointer = instancePointer + QStringLiteral("/*");
            const QString itemRulePointer = childPointer(rulePointer, QStringLiteral("*"));
            const auto less = [&](const QJsonValue &left, const QJsonValue &right) {
                return compareItems(left,
                                    right,
                                    rule.sortKey,
                                    itemInstancePointer,
                                    itemRulePointer) < 0;
            };
            QList<QJsonValue> sorted = values;
            std::stable_sort(sorted.begin(), sorted.end(), less);
            for (qsizetype index = 1; index < sorted.size(); ++index) {
                if (compareItems(sorted.at(index - 1), sorted.at(index), rule.sortKey,
                                 itemInstancePointer, itemRulePointer) == 0) {
                    fail(displayPointer(instancePointer) +
                         QStringLiteral(": ambiguous or duplicate collection sort key"));
                }
            }
            if (rule.kind == CanonicalRuleSet::CollectionKind::DerivedOrdered) {
                for (qsizetype index = 0; index < values.size(); ++index) {
                    const QString itemDisplayPointer =
                        childPointer(instancePointer, QString::number(index));
                    if (write(values.at(index), itemDisplayPointer, itemRulePointer) !=
                        write(sorted.at(index), itemDisplayPointer, itemRulePointer)) {
                        fail(displayPointer(instancePointer) +
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
            result += write(values.at(index),
                            childPointer(instancePointer, QString::number(index)),
                            childPointer(rulePointer, QStringLiteral("*")));
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
