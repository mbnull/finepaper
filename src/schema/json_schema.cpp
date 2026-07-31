#include "schema/json_schema.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <queue>
#include <utility>

namespace finepaper::json_schema {
namespace {

using NodeId = quint32;

QString pointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
}

QString appendPointer(const QString& base, const QString& token) {
    return base + QLatin1Char('/') + pointerToken(token);
}

QString keywordPointer(const QString& schemaPointer,
                       const QString& keyword) {
    return appendPointer(schemaPointer, keyword);
}

bool isFiniteNumber(const QJsonValue& value) {
    return value.isDouble() && std::isfinite(value.toDouble());
}

std::optional<qint64> exactInteger(const QJsonValue& value) {
    if (!isFiniteNumber(value)) {
        return std::nullopt;
    }
    const qint64 lowerDefault = (std::numeric_limits<qint64>::min)();
    const qint64 upperDefault = (std::numeric_limits<qint64>::max)();
    const qint64 withLowerDefault = value.toInteger(lowerDefault);
    const qint64 withUpperDefault = value.toInteger(upperDefault);
    if (withLowerDefault != withUpperDefault) {
        return std::nullopt;
    }
    return withLowerDefault;
}

enum class NumberOrder {
    Less,
    Equal,
    Greater
};

NumberOrder compareIntegerToDouble(qint64 integer, double floating) {
    const double integerLimit = std::ldexp(1.0, 63);
    if (floating < -integerLimit) {
        return NumberOrder::Greater;
    }
    if (floating >= integerLimit) {
        return NumberOrder::Less;
    }

    const double floored = std::floor(floating);
    const qint64 flooredInteger = static_cast<qint64>(floored);
    if (integer < flooredInteger) {
        return NumberOrder::Less;
    }
    if (integer > flooredInteger) {
        return NumberOrder::Greater;
    }
    return floating == floored ? NumberOrder::Equal : NumberOrder::Less;
}

NumberOrder compareNumbers(const QJsonValue& left,
                           const QJsonValue& right) {
    const std::optional<qint64> leftInteger = exactInteger(left);
    const std::optional<qint64> rightInteger = exactInteger(right);
    if (leftInteger && rightInteger) {
        if (*leftInteger < *rightInteger) return NumberOrder::Less;
        if (*leftInteger > *rightInteger) return NumberOrder::Greater;
        return NumberOrder::Equal;
    }
    if (leftInteger) {
        return compareIntegerToDouble(*leftInteger, right.toDouble());
    }
    if (rightInteger) {
        const NumberOrder reverse = compareIntegerToDouble(
            *rightInteger, left.toDouble());
        if (reverse == NumberOrder::Less) return NumberOrder::Greater;
        if (reverse == NumberOrder::Greater) return NumberOrder::Less;
        return NumberOrder::Equal;
    }
    if (left.toDouble() < right.toDouble()) return NumberOrder::Less;
    if (left.toDouble() > right.toDouble()) return NumberOrder::Greater;
    return NumberOrder::Equal;
}

QString valueTypeName(const QJsonValue& value) {
    if (value.isNull()) return QStringLiteral("null");
    if (value.isBool()) return QStringLiteral("boolean");
    if (value.isDouble()) return QStringLiteral("number");
    if (value.isString()) return QStringLiteral("string");
    if (value.isArray()) return QStringLiteral("array");
    if (value.isObject()) return QStringLiteral("object");
    return QStringLiteral("undefined");
}

bool isHexDigit(char16_t value) {
    const ushort code = static_cast<ushort>(value);
    return (code >= '0' && code <= '9')
        || (code >= 'a' && code <= 'f')
        || (code >= 'A' && code <= 'F');
}

uint hexValue(char16_t value) {
    const ushort code = static_cast<ushort>(value);
    if (code >= '0' && code <= '9') return code - '0';
    if (code >= 'a' && code <= 'f') return code - 'a' + 10;
    return code - 'A' + 10;
}

std::optional<uint> escapedCodeUnit(const QString& pattern,
                                    qsizetype slashIndex) {
    if (slashIndex + 5 >= pattern.size()
        || pattern.at(slashIndex) != QLatin1Char('\\')
        || pattern.at(slashIndex + 1) != QLatin1Char('u')) {
        return std::nullopt;
    }
    uint value = 0;
    for (qsizetype offset = 2; offset <= 5; ++offset) {
        const char16_t character = static_cast<char16_t>(
            pattern.at(slashIndex + offset).unicode());
        if (!isHexDigit(character)) {
            return std::nullopt;
        }
        value = value * 16 + hexValue(character);
    }
    return value;
}

enum class PatternTranslationStatus {
    Invalid,
    Unsupported,
    Ready
};

struct PatternTranslation {
    PatternTranslationStatus status = PatternTranslationStatus::Ready;
    QString pattern;
    QString message;
};

struct QuantifierInfo {
    bool recognized = false;
    bool variable = false;
    qsizetype end = 0;
    quint64 maximum = 0;
};

QuantifierInfo braceQuantifier(const QString& pattern, qsizetype start) {
    QuantifierInfo result;
    const qsizetype end = pattern.indexOf(QLatin1Char('}'), start + 1);
    if (end < 0) {
        return result;
    }
    const QString body = pattern.mid(start + 1, end - start - 1);
    const qsizetype comma = body.indexOf(QLatin1Char(','));
    const QString minimumText = comma < 0 ? body : body.left(comma);
    const QString maximumText = comma < 0 ? body : body.mid(comma + 1);
    if (minimumText.isEmpty()
        || std::any_of(
            minimumText.cbegin(), minimumText.cend(), [](const auto value) {
                return !value.isDigit();
            })
        || std::any_of(
            maximumText.cbegin(), maximumText.cend(), [](const auto value) {
                return !value.isDigit();
            })) {
        return result;
    }

    bool minimumOk = false;
    const quint64 minimum = minimumText.toULongLong(&minimumOk);
    bool maximumOk = true;
    const quint64 maximum = maximumText.isEmpty()
        ? (std::numeric_limits<quint64>::max)()
        : maximumText.toULongLong(&maximumOk);
    if (!minimumOk || !maximumOk || maximum < minimum) {
        return result;
    }
    result.recognized = true;
    result.variable = comma >= 0 && maximum != minimum;
    result.end = end;
    result.maximum = maximum;
    return result;
}

std::optional<QString> unsafeEcmaPatternReason(const QString& pattern) {
    enum class GroupKind {
        Root,
        Consuming,
        Assertion
    };
    struct GroupFrame {
        GroupKind kind = GroupKind::Consuming;
        int variableQuantifiersInBranch = 0;
        bool hasVariableQuantifier = false;
        bool hasAlternation = false;
    };

    QVector<GroupFrame> groups{GroupFrame{GroupKind::Root}};
    bool inCharacterClass = false;
    bool hasVariableQuantifier = false;
    const auto addVariableQuantifier = [&]() -> std::optional<QString> {
        hasVariableQuantifier = true;
        GroupFrame& group = groups.last();
        group.hasVariableQuantifier = true;
        ++group.variableQuantifiersInBranch;
        if (group.variableQuantifiersInBranch > 1) {
            return QStringLiteral(
                "multiple variable repetitions in one branch are outside the safe pattern profile");
        }
        return std::nullopt;
    };
    for (qsizetype index = 0; index < pattern.size(); ++index) {
        const char16_t character = static_cast<char16_t>(
            pattern.at(index).unicode());
        if (character == u'\\') {
            if (index + 1 < pattern.size()) {
                const char16_t escaped = static_cast<char16_t>(
                    pattern.at(index + 1).unicode());
                if ((escaped >= u'1' && escaped <= u'9')
                    || escaped == u'k' || escaped == u'g') {
                    return QStringLiteral(
                        "backreferences are outside the safe pattern profile");
                }
                const bool asciiLetter = (escaped >= u'a' && escaped <= u'z')
                    || (escaped >= u'A' && escaped <= u'Z');
                constexpr char16_t supportedLetterEscapes[] = {
                    u'b', u'B', u'c', u'd', u'D', u'f', u'n', u'r',
                    u's', u'S', u't', u'u', u'v', u'w', u'W', u'x'
                };
                if (asciiLetter
                    && std::find(
                        std::cbegin(supportedLetterEscapes),
                        std::cend(supportedLetterEscapes),
                        escaped) == std::cend(supportedLetterEscapes)) {
                    return QStringLiteral(
                        "non-ECMA escapes are outside the safe pattern profile");
                }
                ++index;
            }
            continue;
        }
        if (character == u'[' && !inCharacterClass) {
            inCharacterClass = true;
            continue;
        }
        if (character == u']' && inCharacterClass) {
            inCharacterClass = false;
            continue;
        }
        if (inCharacterClass) {
            if (character == u'[' && index + 1 < pattern.size()) {
                const char16_t marker = static_cast<char16_t>(
                    pattern.at(index + 1).unicode());
                if (marker == u':' || marker == u'.' || marker == u'=') {
                    return QStringLiteral(
                        "PCRE character-class extensions are outside the ECMA pattern profile");
                }
            }
            continue;
        }
        if (character == u'(') {
            GroupKind kind = GroupKind::Consuming;
            if (index + 1 < pattern.size()
                && pattern.at(index + 1) == QLatin1Char('*')) {
                return QStringLiteral(
                    "PCRE control verbs are outside the ECMA pattern profile");
            }
            if (index + 1 < pattern.size()
                && pattern.at(index + 1) == QLatin1Char('?')) {
                if (pattern.mid(index, 3) == QStringLiteral("(?:")) {
                    index += 2;
                } else if (pattern.mid(index, 3) == QStringLiteral("(?=")
                           || pattern.mid(index, 3)
                               == QStringLiteral("(?!")) {
                    kind = GroupKind::Assertion;
                    index += 2;
                } else if (pattern.mid(index, 4)
                               == QStringLiteral("(?<=")
                           || pattern.mid(index, 4)
                               == QStringLiteral("(?<!")) {
                    return QStringLiteral(
                        "lookbehind is outside the safe pattern profile");
                } else {
                    return QStringLiteral(
                        "non-ECMA group extensions are outside the safe pattern profile");
                }
            }
            groups.append(GroupFrame{kind});
            continue;
        }
        if (character == u'|') {
            GroupFrame& group = groups.last();
            group.hasAlternation = true;
            group.variableQuantifiersInBranch = 0;
            continue;
        }
        if (character == u')') {
            if (groups.size() <= 1) {
                continue;
            }
            const GroupFrame closed = groups.takeLast();
            if (index + 1 < pattern.size()) {
                const char16_t next = static_cast<char16_t>(
                    pattern.at(index + 1).unicode());
                if (next == u'*' || next == u'+' || next == u'?') {
                    return QStringLiteral(
                        "quantified groups are outside the safe pattern profile");
                }
                if (next == u'{') {
                    const QuantifierInfo quantifier = braceQuantifier(
                        pattern, index + 1);
                    if (quantifier.recognized
                        && (quantifier.variable
                            || quantifier.maximum != 1)) {
                        return QStringLiteral(
                            "quantified groups are outside the safe pattern profile");
                    }
                }
            }
            if (closed.kind == GroupKind::Consuming
                && (closed.hasVariableQuantifier
                    || closed.hasAlternation)) {
                if (const std::optional<QString> reason =
                        addVariableQuantifier()) {
                    return reason;
                }
            }
            continue;
        }

        bool quantifierRecognized = character == u'*' || character == u'+'
            || character == u'?';
        bool variable = quantifierRecognized;
        qsizetype quantifierEnd = index;
        if (character == u'{') {
            const QuantifierInfo quantifier = braceQuantifier(pattern, index);
            if (!quantifier.recognized) {
                continue;
            }
            quantifierRecognized = true;
            if (quantifier.maximum > 1'024) {
                return QStringLiteral(
                    "pattern repetition exceeds the safe limit");
            }
            variable = quantifier.variable;
            quantifierEnd = quantifier.end;
        }
        if (quantifierRecognized
            && quantifierEnd + 1 < pattern.size()) {
            const char16_t modifier = static_cast<char16_t>(
                pattern.at(quantifierEnd + 1).unicode());
            if (modifier == u'?') {
                ++quantifierEnd;
            } else if (modifier == u'+') {
                return QStringLiteral(
                    "possessive quantifiers are outside the ECMA pattern profile");
            }
        }
        if (!variable) {
            index = quantifierEnd;
            continue;
        }
        if (const std::optional<QString> reason = addVariableQuantifier()) {
            return reason;
        }
        index = quantifierEnd;
    }
    if (groups.constFirst().hasAlternation && hasVariableQuantifier) {
        return QStringLiteral(
            "top-level alternation with variable repetition is outside the safe pattern profile");
    }
    if (hasVariableQuantifier
        && (pattern.isEmpty() || pattern.front() != QLatin1Char('^'))) {
        return QStringLiteral(
            "unanchored variable repetition is outside the safe pattern profile");
    }
    return std::nullopt;
}

// JSON Schema patterns use ECMA-262 syntax. Qt uses PCRE2; the bundled schema
// relies on ECMA's Unicode escapes and whitespace classes. Translate the
// constructs whose PCRE2 defaults differ, including exact end-of-input `$`.
PatternTranslation translateEcmaPattern(const QString& source) {
    const QString ecmaWhitespace = QStringLiteral(
        "\\x{0009}-\\x{000D}\\x{0020}\\x{00A0}\\x{1680}"
        "\\x{2000}-\\x{200A}\\x{2028}\\x{2029}\\x{202F}"
        "\\x{205F}\\x{3000}\\x{FEFF}");
    PatternTranslation result;
    result.pattern.reserve(source.size() + 16);
    bool inCharacterClass = false;
    bool characterClassAtStart = false;
    bool characterClassNegated = false;
    bool characterClassHasContent = false;
    for (qsizetype index = 0; index < source.size(); ++index) {
        const char16_t character = static_cast<char16_t>(
            source.at(index).unicode());
        if (character == u'[' && !inCharacterClass) {
            inCharacterClass = true;
            characterClassAtStart = true;
            characterClassNegated = false;
            characterClassHasContent = false;
            result.pattern += QLatin1Char('[');
            continue;
        }
        if (inCharacterClass) {
            if (characterClassAtStart && character == u'^') {
                characterClassAtStart = false;
                characterClassNegated = true;
                result.pattern += QLatin1Char('^');
                continue;
            }
            if (character == u']') {
                if (!characterClassHasContent) {
                    result.pattern.chop(characterClassNegated ? 2 : 1);
                    result.pattern += characterClassNegated
                        ? QStringLiteral("[\\s\\S]")
                        : QStringLiteral("(?!)");
                } else {
                    result.pattern += QLatin1Char(']');
                }
                inCharacterClass = false;
                characterClassAtStart = false;
                continue;
            }
            characterClassAtStart = false;
            characterClassHasContent = true;
        }
        if (character == u'$' && !inCharacterClass) {
            result.pattern += QStringLiteral("\\z");
            continue;
        }
        if (character == u'.' && !inCharacterClass) {
            result.pattern += QStringLiteral("[^\\r\\n\\x{2028}\\x{2029}]");
            continue;
        }
        if (character != u'\\') {
            result.pattern += QChar(character);
            continue;
        }

        if (index + 1 >= source.size()) {
            result.pattern += QLatin1Char('\\');
            continue;
        }
        if (source.at(index + 1) == QLatin1Char('\\')) {
            result.pattern += QStringLiteral("\\\\");
            ++index;
            continue;
        }
        const char16_t escaped = static_cast<char16_t>(
            source.at(index + 1).unicode());
        if (escaped == u'w' || escaped == u'd' || escaped == u's') {
            if (inCharacterClass) {
                if (escaped == u'w') {
                    result.pattern += QStringLiteral("A-Za-z0-9_");
                } else if (escaped == u'd') {
                    result.pattern += QStringLiteral("0-9");
                } else {
                    result.pattern += ecmaWhitespace;
                }
            } else if (escaped == u'w') {
                result.pattern += QStringLiteral("[A-Za-z0-9_]");
            } else if (escaped == u'd') {
                result.pattern += QStringLiteral("[0-9]");
            } else {
                result.pattern += QLatin1Char('[') + ecmaWhitespace
                    + QLatin1Char(']');
            }
            ++index;
            continue;
        }
        if (escaped == u'W' || escaped == u'D' || escaped == u'S') {
            if (inCharacterClass) {
                result.status = PatternTranslationStatus::Unsupported;
                result.message = QStringLiteral(
                    "negated ECMA character classes inside [...] are not supported");
                return result;
            }
            const QString body = escaped == u'W'
                ? QStringLiteral("A-Za-z0-9_")
                : escaped == u'D' ? QStringLiteral("0-9") : ecmaWhitespace;
            result.pattern += QStringLiteral("[^") + body
                + QLatin1Char(']');
            ++index;
            continue;
        }
        if (escaped == u'v') {
            result.pattern += QStringLiteral("\\x{000B}");
            ++index;
            continue;
        }
        if (escaped == u'x') {
            if (index + 3 >= source.size()
                || !isHexDigit(static_cast<char16_t>(
                    source.at(index + 2).unicode()))
                || !isHexDigit(static_cast<char16_t>(
                    source.at(index + 3).unicode()))) {
                result.status = PatternTranslationStatus::Invalid;
                result.message = QStringLiteral(
                    "pattern contains an invalid ECMA \\xHH escape");
                return result;
            }
            result.pattern += source.mid(index, 4);
            index += 3;
            continue;
        }
        if (escaped == u'c') {
            if (index + 2 >= source.size()) {
                result.status = PatternTranslationStatus::Invalid;
                result.message = QStringLiteral(
                    "pattern contains an incomplete ECMA control escape");
                return result;
            }
            const char16_t control = static_cast<char16_t>(
                source.at(index + 2).unicode());
            if (!((control >= u'a' && control <= u'z')
                  || (control >= u'A' && control <= u'Z'))) {
                result.status = PatternTranslationStatus::Invalid;
                result.message = QStringLiteral(
                    "ECMA control escapes require an ASCII letter");
                return result;
            }
            result.pattern += source.mid(index, 3);
            index += 2;
            continue;
        }
        if (escaped != u'u') {
            result.pattern += QLatin1Char('\\');
            result.pattern += QChar(escaped);
            ++index;
            continue;
        }

        const std::optional<uint> first = escapedCodeUnit(source, index);
        if (!first) {
            result.status = PatternTranslationStatus::Invalid;
            result.message = QStringLiteral(
                "pattern contains an incomplete ECMA \\uXXXX escape");
            return result;
        }

        uint codePoint = *first;
        qsizetype consumed = 5;
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            const qsizetype secondSlash = index + 6;
            const std::optional<uint> second = escapedCodeUnit(
                source, secondSlash);
            if (!second || *second < 0xDC00 || *second > 0xDFFF) {
                result.status = PatternTranslationStatus::Invalid;
                result.message = QStringLiteral(
                    "pattern contains an unpaired high surrogate escape");
                return result;
            }
            codePoint = 0x10000
                + ((codePoint - 0xD800) << 10)
                + (*second - 0xDC00);
            consumed = 11;
        } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
            result.status = PatternTranslationStatus::Invalid;
            result.message = QStringLiteral(
                "pattern contains an unpaired low surrogate escape");
            return result;
        }

        result.pattern += QStringLiteral("\\x{%1}")
            .arg(codePoint, 0, 16);
        index += consumed;
    }
    return result;
}

enum TypeFlag : quint16 {
    NullType = 1 << 0,
    BooleanType = 1 << 1,
    ObjectType = 1 << 2,
    ArrayType = 1 << 3,
    NumberType = 1 << 4,
    IntegerType = 1 << 5,
    StringType = 1 << 6
};

std::optional<quint16> typeFlag(const QString& type) {
    if (type == QStringLiteral("null")) return NullType;
    if (type == QStringLiteral("boolean")) return BooleanType;
    if (type == QStringLiteral("object")) return ObjectType;
    if (type == QStringLiteral("array")) return ArrayType;
    if (type == QStringLiteral("number")) return NumberType;
    if (type == QStringLiteral("integer")) return IntegerType;
    if (type == QStringLiteral("string")) return StringType;
    return std::nullopt;
}

bool matchesType(const QJsonValue& value, quint16 types) {
    if (value.isNull() && (types & NullType)) return true;
    if (value.isBool() && (types & BooleanType)) return true;
    if (value.isObject() && (types & ObjectType)) return true;
    if (value.isArray() && (types & ArrayType)) return true;
    if (value.isString() && (types & StringType)) return true;
    if (!isFiniteNumber(value)) return false;
    if (types & NumberType) return true;
    return (types & IntegerType)
        && std::floor(value.toDouble()) == value.toDouble();
}

QStringList sortedKeys(const QJsonObject& object) {
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace

namespace detail {

enum class AdditionalPropertiesKind {
    Allow,
    Deny,
    Schema
};

struct PropertyRule {
    QString name;
    NodeId schema = 0;
};

struct Node {
    QString pointer;
    bool booleanSchema = false;
    bool booleanValue = true;

    bool hasTypes = false;
    quint16 types = 0;
    bool hasConst = false;
    QJsonValue constValue;
    QVector<QJsonValue> enumValues;
    std::optional<QRegularExpression> pattern = std::nullopt;
    qsizetype patternCodeUnits = 0;
    std::optional<QJsonValue> minimum = std::nullopt;
    std::optional<QJsonValue> exclusiveMinimum = std::nullopt;

    QStringList required;
    QVector<PropertyRule> properties;
    AdditionalPropertiesKind additionalProperties =
        AdditionalPropertiesKind::Allow;
    std::optional<NodeId> additionalPropertiesSchema = std::nullopt;

    std::optional<NodeId> items = std::nullopt;
    std::optional<qsizetype> minimumItems = std::nullopt;
    std::optional<qsizetype> maximumItems = std::nullopt;
    bool uniqueItems = false;

    std::optional<NodeId> reference = std::nullopt;
    QVector<NodeId> allOf;
    std::optional<NodeId> ifSchema = std::nullopt;
    std::optional<NodeId> thenSchema = std::nullopt;
    std::optional<NodeId> elseSchema = std::nullopt;
    std::optional<NodeId> notSchema = std::nullopt;
};

struct Program {
    QVector<Node> nodes;
    QHash<QString, NodeId> nodeByPointer;
    NodeId root = 0;
};

} // namespace detail

namespace {

class Compiler final {
public:
    Compiler(const QJsonObject& root, Limits limits)
        : m_root(root), m_limits(limits),
          m_program(std::make_shared<detail::Program>()) {}

    CompileResult run() {
        const std::optional<NodeId> root = collectSchema(
            QJsonValue(m_root), QString(), 0);
        if (root) {
            m_program->root = *root;
        }
        resolveReferences();
        rejectZeroProgressCycles();

        CompileResult result;
        result.status = m_status;
        result.issues = std::move(m_issues);
        return result;
    }

    std::shared_ptr<const detail::Program> program() const {
        return m_program;
    }

private:
    struct PendingReference {
        NodeId source = 0;
        QString reference;
        QString schemaPointer;
    };

    struct ZeroEdge {
        NodeId target = 0;
        QString schemaPointer;
        QString keyword;
    };

    void markInvalid(const QString& code,
                     const QString& message,
                     const QString& schemaPointer,
                     const QString& keyword) {
        m_status = CompileStatus::Invalid;
        appendCompileIssue(code, message, schemaPointer, keyword);
    }

    void markUnsupported(const QString& code,
                         const QString& message,
                         const QString& schemaPointer,
                         const QString& keyword) {
        if (m_status == CompileStatus::Ready) {
            m_status = CompileStatus::Unsupported;
        }
        appendCompileIssue(code, message, schemaPointer, keyword);
    }

    void appendCompileIssue(const QString& code,
                            const QString& message,
                            const QString& schemaPointer,
                            const QString& keyword) {
        if (m_compileIssuesTruncated) {
            return;
        }
        const qsizetype limit = std::max<qsizetype>(
            1, m_limits.maximumDiagnostics);
        if (m_issues.size() >= limit) {
            m_issues.last() = Issue{
                QStringLiteral("json_schema.compile_diagnostics_truncated"),
                QStringLiteral("schema diagnostics exceeded the configured limit"),
                {}, {}, {}};
            m_compileIssuesTruncated = true;
            return;
        }
        m_issues.append(Issue{code, message, {}, schemaPointer, keyword});
    }

    bool validateKnownKeywords(const QJsonObject& schema,
                               const QString& path) {
        static const QSet<QString> supported = {
            QStringLiteral("$schema"),
            QStringLiteral("$id"),
            QStringLiteral("$defs"),
            QStringLiteral("$ref"),
            QStringLiteral("title"),
            QStringLiteral("description"),
            QStringLiteral("default"),
            QStringLiteral("type"),
            QStringLiteral("const"),
            QStringLiteral("enum"),
            QStringLiteral("pattern"),
            QStringLiteral("minimum"),
            QStringLiteral("exclusiveMinimum"),
            QStringLiteral("minItems"),
            QStringLiteral("maxItems"),
            QStringLiteral("uniqueItems"),
            QStringLiteral("properties"),
            QStringLiteral("required"),
            QStringLiteral("additionalProperties"),
            QStringLiteral("items"),
            QStringLiteral("allOf"),
            QStringLiteral("if"),
            QStringLiteral("then"),
            QStringLiteral("else"),
            QStringLiteral("not")
        };

        bool valid = true;
        for (const QString& key : sortedKeys(schema)) {
            if (supported.contains(key)) {
                continue;
            }
            valid = false;
            markUnsupported(
                QStringLiteral("json_schema.unsupported_keyword"),
                QStringLiteral("keyword %1 is outside the supported Draft 2020-12 profile")
                    .arg(key),
                keywordPointer(path, key),
                key);
        }
        return valid;
    }

    std::optional<NodeId> allocateNode(const QString& path, int depth) {
        if (depth > m_limits.maximumSchemaDepth) {
            if (!m_schemaBudgetReported) {
                m_schemaBudgetReported = true;
                markUnsupported(
                    QStringLiteral("json_schema.schema_depth_exceeded"),
                    QStringLiteral("schema nesting exceeds the configured depth limit"),
                    path,
                    {});
            }
            return std::nullopt;
        }
        if (m_program->nodes.size() >= m_limits.maximumSchemaNodes) {
            if (!m_schemaBudgetReported) {
                m_schemaBudgetReported = true;
                markUnsupported(
                    QStringLiteral("json_schema.schema_node_budget_exceeded"),
                    QStringLiteral("schema node count exceeds the configured limit"),
                    path,
                    {});
            }
            return std::nullopt;
        }

        const NodeId id = static_cast<NodeId>(m_program->nodes.size());
        detail::Node placeholder;
        placeholder.pointer = path;
        m_program->nodes.append(std::move(placeholder));
        m_program->nodeByPointer.insert(path, id);
        return id;
    }

    void parseDialectAndAnnotations(const QJsonObject& schema,
                                    const QString& path) {
        if (schema.contains(QStringLiteral("$schema"))) {
            const QJsonValue value = schema.value(QStringLiteral("$schema"));
            const QString keywordPath = keywordPointer(
                path, QStringLiteral("$schema"));
            if (!value.isString() || value.toString().isEmpty()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_keyword_value"),
                    QStringLiteral("$schema must be a non-empty string"),
                    keywordPath,
                    QStringLiteral("$schema"));
            } else if (!path.isEmpty()) {
                markUnsupported(
                    QStringLiteral("json_schema.unsupported_embedded_dialect"),
                    QStringLiteral("embedded $schema declarations are not supported"),
                    keywordPath,
                    QStringLiteral("$schema"));
            } else if (value.toString()
                       != QStringLiteral(
                           "https://json-schema.org/draft/2020-12/schema")) {
                markUnsupported(
                    QStringLiteral("json_schema.unsupported_dialect"),
                    QStringLiteral("only JSON Schema Draft 2020-12 is supported"),
                    keywordPath,
                    QStringLiteral("$schema"));
            }
        }

        if (schema.contains(QStringLiteral("$id"))) {
            const QJsonValue value = schema.value(QStringLiteral("$id"));
            const QString keywordPath = keywordPointer(
                path, QStringLiteral("$id"));
            if (!value.isString() || value.toString().isEmpty()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_keyword_value"),
                    QStringLiteral("$id must be a non-empty string"),
                    keywordPath,
                    QStringLiteral("$id"));
            } else if (!path.isEmpty()) {
                markUnsupported(
                    QStringLiteral("json_schema.unsupported_embedded_resource"),
                    QStringLiteral("embedded $id resources are not supported"),
                    keywordPath,
                    QStringLiteral("$id"));
            } else if (value.toString().contains(QLatin1Char('#'))) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_id"),
                    QStringLiteral("$id must not contain a non-empty fragment"),
                    keywordPath,
                    QStringLiteral("$id"));
            }
        }

        for (const QString& keyword : {
                 QStringLiteral("title"),
                 QStringLiteral("description")}) {
            if (schema.contains(keyword) && !schema.value(keyword).isString()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_keyword_value"),
                    QStringLiteral("%1 must be a string").arg(keyword),
                    keywordPointer(path, keyword),
                    keyword);
            }
        }
    }

    void parseTypes(const QJsonObject& schema,
                    const QString& path,
                    detail::Node& node) {
        if (!schema.contains(QStringLiteral("type"))) {
            return;
        }
        const QJsonValue value = schema.value(QStringLiteral("type"));
        const QString typePath = keywordPointer(path, QStringLiteral("type"));
        QStringList names;
        if (value.isString()) {
            names.append(value.toString());
        } else if (value.isArray() && !value.toArray().isEmpty()) {
            const QJsonArray values = value.toArray();
            for (qsizetype index = 0; index < values.size(); ++index) {
                if (!values.at(index).isString()) {
                    markInvalid(
                        QStringLiteral("json_schema.invalid_type"),
                        QStringLiteral("type array entries must be strings"),
                        appendPointer(typePath, QString::number(index)),
                        QStringLiteral("type"));
                    continue;
                }
                names.append(values.at(index).toString());
            }
        } else {
            markInvalid(
                QStringLiteral("json_schema.invalid_type"),
                QStringLiteral("type must be a string or a non-empty array of strings"),
                typePath,
                QStringLiteral("type"));
            return;
        }

        QSet<QString> seen;
        for (qsizetype index = 0; index < names.size(); ++index) {
            const QString& name = names.at(index);
            const std::optional<quint16> flag = typeFlag(name);
            if (!flag) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_type"),
                    QStringLiteral("unknown JSON type %1").arg(name),
                    value.isArray()
                        ? appendPointer(typePath, QString::number(index))
                        : typePath,
                    QStringLiteral("type"));
                continue;
            }
            if (seen.contains(name)) {
                markInvalid(
                    QStringLiteral("json_schema.duplicate_type"),
                    QStringLiteral("type array entries must be unique"),
                    appendPointer(typePath, QString::number(index)),
                    QStringLiteral("type"));
                continue;
            }
            seen.insert(name);
            node.types |= *flag;
        }
        node.hasTypes = true;
    }

    bool accountSchemaData(const QJsonValue& root,
                           const QString& schemaPointer,
                           const QString& keyword) {
        struct PendingValue {
            QJsonValue value;
            int depth = 0;
        };

        QVector<PendingValue> pending{
            PendingValue{root, 0}
        };
        while (!pending.isEmpty()) {
            const PendingValue current = pending.takeLast();
            if (current.depth > m_limits.maximumSchemaDataDepth) {
                if (!m_schemaDataBudgetReported) {
                    m_schemaDataBudgetReported = true;
                    markUnsupported(
                        QStringLiteral("json_schema.schema_data_depth_exceeded"),
                        QStringLiteral(
                            "const/enum data nesting exceeds the configured depth limit"),
                        schemaPointer,
                        keyword);
                }
                return false;
            }
            if (m_schemaDataNodes >= m_limits.maximumSchemaDataNodes) {
                if (!m_schemaDataBudgetReported) {
                    m_schemaDataBudgetReported = true;
                    markUnsupported(
                        QStringLiteral(
                            "json_schema.schema_data_budget_exceeded"),
                        QStringLiteral(
                            "const/enum data exceeds the configured node budget"),
                        schemaPointer,
                        keyword);
                }
                return false;
            }
            ++m_schemaDataNodes;

            if (current.value.isArray()) {
                const QJsonArray values = current.value.toArray();
                for (qsizetype index = values.size(); index > 0; --index) {
                    pending.append(PendingValue{
                        values.at(index - 1), current.depth + 1});
                }
            } else if (current.value.isObject()) {
                const QJsonObject values = current.value.toObject();
                const QStringList keys = sortedKeys(values);
                for (qsizetype index = keys.size(); index > 0; --index) {
                    pending.append(PendingValue{
                        values.value(keys.at(index - 1)), current.depth + 1});
                }
            }
        }
        return true;
    }

    void parseScalarAssertions(const QJsonObject& schema,
                               const QString& path,
                               detail::Node& node) {
        if (schema.contains(QStringLiteral("const"))) {
            const QJsonValue value = schema.value(QStringLiteral("const"));
            if (accountSchemaData(
                    value,
                    keywordPointer(path, QStringLiteral("const")),
                    QStringLiteral("const"))) {
                node.hasConst = true;
                node.constValue = value;
            }
        }

        if (schema.contains(QStringLiteral("enum"))) {
            const QJsonValue value = schema.value(QStringLiteral("enum"));
            const QString enumPath = keywordPointer(path, QStringLiteral("enum"));
            if (!value.isArray() || value.toArray().isEmpty()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_enum"),
                    QStringLiteral("enum must be a non-empty array"),
                    enumPath,
                    QStringLiteral("enum"));
            } else {
                const QJsonArray values = value.toArray();
                if (values.size() > m_limits.maximumEnumValues) {
                    markUnsupported(
                        QStringLiteral(
                            "json_schema.enum_value_budget_exceeded"),
                        QStringLiteral(
                            "enum contains more entries than the configured limit"),
                        enumPath,
                        QStringLiteral("enum"));
                } else {
                    QSet<QJsonValue> seen;
                    seen.reserve(values.size());
                    for (qsizetype index = 0; index < values.size(); ++index) {
                        const QJsonValue candidate = values.at(index);
                        if (!accountSchemaData(
                                candidate,
                                appendPointer(
                                    enumPath, QString::number(index)),
                                QStringLiteral("enum"))) {
                            break;
                        }
                        if (seen.contains(candidate)) {
                            markInvalid(
                                QStringLiteral(
                                    "json_schema.duplicate_enum_value"),
                                QStringLiteral("enum entries must be unique"),
                                appendPointer(
                                    enumPath, QString::number(index)),
                                QStringLiteral("enum"));
                        } else {
                            seen.insert(candidate);
                            node.enumValues.append(candidate);
                        }
                    }
                }
            }
        }

        if (schema.contains(QStringLiteral("pattern"))) {
            const QJsonValue value = schema.value(QStringLiteral("pattern"));
            const QString patternPath = keywordPointer(
                path, QStringLiteral("pattern"));
            if (!value.isString()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_pattern"),
                    QStringLiteral("pattern must be a string"),
                    patternPath,
                    QStringLiteral("pattern"));
            } else if (value.toString().size()
                       > m_limits.maximumPatternLength) {
                markUnsupported(
                    QStringLiteral("json_schema.pattern_budget_exceeded"),
                    QStringLiteral("pattern exceeds the configured length limit"),
                    patternPath,
                    QStringLiteral("pattern"));
            } else {
                const PatternTranslation translated = translateEcmaPattern(
                    value.toString());
                if (translated.status == PatternTranslationStatus::Invalid) {
                    markInvalid(
                        QStringLiteral("json_schema.invalid_pattern"),
                        translated.message,
                        patternPath,
                        QStringLiteral("pattern"));
                } else if (translated.status
                           == PatternTranslationStatus::Unsupported) {
                    markUnsupported(
                        QStringLiteral("json_schema.unsupported_pattern"),
                        translated.message,
                        patternPath,
                        QStringLiteral("pattern"));
                } else {
                    QRegularExpression expression(translated.pattern);
                    if (!expression.isValid()) {
                        markInvalid(
                            QStringLiteral("json_schema.invalid_pattern"),
                            QStringLiteral("invalid pattern at offset %1: %2")
                                .arg(expression.patternErrorOffset())
                                .arg(expression.errorString()),
                            patternPath,
                            QStringLiteral("pattern"));
                    } else if (const std::optional<QString> unsafeReason =
                                   unsafeEcmaPatternReason(
                                       value.toString())) {
                        markUnsupported(
                            QStringLiteral(
                                "json_schema.unsafe_pattern_unsupported"),
                            *unsafeReason,
                            patternPath,
                            QStringLiteral("pattern"));
                    } else {
                        node.patternCodeUnits = translated.pattern.size();
                        node.pattern = std::move(expression);
                    }
                }
            }
        }

        const auto parseBoundary = [&](const QString& keyword,
                                       std::optional<QJsonValue>& target) {
            if (!schema.contains(keyword)) {
                return;
            }
            const QJsonValue value = schema.value(keyword);
            if (!isFiniteNumber(value)) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_numeric_boundary"),
                    QStringLiteral("%1 must be a finite number").arg(keyword),
                    keywordPointer(path, keyword),
                    keyword);
            } else {
                target = value;
            }
        };
        parseBoundary(QStringLiteral("minimum"), node.minimum);
        parseBoundary(
            QStringLiteral("exclusiveMinimum"), node.exclusiveMinimum);
    }

    void parseArrayAssertions(const QJsonObject& schema,
                              const QString& path,
                              detail::Node& node) {
        const auto parseItemCount = [&](const QString& keyword,
                                        std::optional<qsizetype>& target) {
            if (!schema.contains(keyword)) {
                return;
            }
            const QJsonValue value = schema.value(keyword);
            const std::optional<qint64> integer = exactInteger(value);
            const qint64 maximum = static_cast<qint64>(
                (std::numeric_limits<qsizetype>::max)());
            if (!integer || *integer < 0 || *integer > maximum) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_item_limit"),
                    QStringLiteral("%1 must be a non-negative integer")
                        .arg(keyword),
                    keywordPointer(path, keyword),
                    keyword);
            } else {
                target = static_cast<qsizetype>(*integer);
            }
        };
        parseItemCount(QStringLiteral("minItems"), node.minimumItems);
        parseItemCount(QStringLiteral("maxItems"), node.maximumItems);
        if (node.minimumItems && node.maximumItems
            && *node.minimumItems > *node.maximumItems) {
            markInvalid(
                QStringLiteral("json_schema.invalid_item_range"),
                QStringLiteral("minItems must not exceed maxItems"),
                keywordPointer(path, QStringLiteral("maxItems")),
                QStringLiteral("maxItems"));
        }

        if (schema.contains(QStringLiteral("uniqueItems"))) {
            const QJsonValue value = schema.value(
                QStringLiteral("uniqueItems"));
            if (!value.isBool()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_keyword_value"),
                    QStringLiteral("uniqueItems must be a boolean"),
                    keywordPointer(path, QStringLiteral("uniqueItems")),
                    QStringLiteral("uniqueItems"));
            } else {
                node.uniqueItems = value.toBool();
            }
        }
    }

    void parseRequired(const QJsonObject& schema,
                       const QString& path,
                       detail::Node& node) {
        if (!schema.contains(QStringLiteral("required"))) {
            return;
        }
        const QJsonValue value = schema.value(QStringLiteral("required"));
        const QString requiredPath = keywordPointer(
            path, QStringLiteral("required"));
        if (!value.isArray()) {
            markInvalid(
                QStringLiteral("json_schema.invalid_required"),
                QStringLiteral("required must be an array of unique strings"),
                requiredPath,
                QStringLiteral("required"));
            return;
        }

        QSet<QString> seen;
        const QJsonArray values = value.toArray();
        for (qsizetype index = 0; index < values.size(); ++index) {
            if (!values.at(index).isString()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_required"),
                    QStringLiteral("required entries must be strings"),
                    appendPointer(requiredPath, QString::number(index)),
                    QStringLiteral("required"));
                continue;
            }
            const QString name = values.at(index).toString();
            if (seen.contains(name)) {
                markInvalid(
                    QStringLiteral("json_schema.duplicate_required"),
                    QStringLiteral("required entries must be unique"),
                    appendPointer(requiredPath, QString::number(index)),
                    QStringLiteral("required"));
                continue;
            }
            seen.insert(name);
            node.required.append(name);
        }
    }

    std::optional<NodeId> collectChild(const QJsonValue& value,
                                       const QString& path,
                                       int depth) {
        if (!value.isObject() && !value.isBool()) {
            markInvalid(
                QStringLiteral("json_schema.expected_schema"),
                QStringLiteral("schema value must be an object or boolean"),
                path,
                {});
            return std::nullopt;
        }
        return collectSchema(value, path, depth);
    }

    void collectSchemaMap(const QJsonObject& schema,
                          const QString& path,
                          const QString& keyword,
                          int depth,
                          QVector<detail::PropertyRule>* properties = nullptr) {
        if (!schema.contains(keyword)) {
            return;
        }
        const QJsonValue value = schema.value(keyword);
        const QString mapPath = keywordPointer(path, keyword);
        if (!value.isObject()) {
            markInvalid(
                QStringLiteral("json_schema.invalid_keyword_value"),
                QStringLiteral("%1 must be an object of schemas").arg(keyword),
                mapPath,
                keyword);
            return;
        }
        const QJsonObject map = value.toObject();
        for (const QString& name : sortedKeys(map)) {
            const std::optional<NodeId> child = collectChild(
                map.value(name), appendPointer(mapPath, name), depth + 1);
            if (child && properties) {
                properties->append(detail::PropertyRule{name, *child});
            }
        }
    }

    void collectObjectChildren(const QJsonObject& schema,
                               const QString& path,
                               int depth,
                               detail::Node& node) {
        collectSchemaMap(
            schema, path, QStringLiteral("$defs"), depth, nullptr);
        collectSchemaMap(
            schema, path, QStringLiteral("properties"), depth,
            &node.properties);

        if (schema.contains(QStringLiteral("additionalProperties"))) {
            const QJsonValue value = schema.value(
                QStringLiteral("additionalProperties"));
            const QString childPath = keywordPointer(
                path, QStringLiteral("additionalProperties"));
            if (value.isBool()) {
                // Boolean schemas are still addressable by a local $ref even
                // though the object evaluator can use a cheaper allow/deny
                // representation for this keyword.
                collectChild(value, childPath, depth + 1);
                node.additionalProperties = value.toBool()
                    ? detail::AdditionalPropertiesKind::Allow
                    : detail::AdditionalPropertiesKind::Deny;
            } else {
                const std::optional<NodeId> child = collectChild(
                    value, childPath, depth + 1);
                if (child) {
                    node.additionalProperties =
                        detail::AdditionalPropertiesKind::Schema;
                    node.additionalPropertiesSchema = *child;
                }
            }
        }

        if (schema.contains(QStringLiteral("items"))) {
            node.items = collectChild(
                schema.value(QStringLiteral("items")),
                keywordPointer(path, QStringLiteral("items")),
                depth + 1);
        }
    }

    void collectApplicators(const QJsonObject& schema,
                            const QString& path,
                            int depth,
                            detail::Node& node) {
        if (schema.contains(QStringLiteral("allOf"))) {
            const QJsonValue value = schema.value(QStringLiteral("allOf"));
            const QString allOfPath = keywordPointer(
                path, QStringLiteral("allOf"));
            if (!value.isArray() || value.toArray().isEmpty()) {
                markInvalid(
                    QStringLiteral("json_schema.invalid_all_of"),
                    QStringLiteral("allOf must be a non-empty array of schemas"),
                    allOfPath,
                    QStringLiteral("allOf"));
            } else {
                const QJsonArray values = value.toArray();
                for (qsizetype index = 0; index < values.size(); ++index) {
                    const std::optional<NodeId> child = collectChild(
                        values.at(index),
                        appendPointer(allOfPath, QString::number(index)),
                        depth + 1);
                    if (child) {
                        node.allOf.append(*child);
                    }
                }
            }
        }

        const auto collectDirect = [&](const QString& keyword,
                                       std::optional<NodeId>& target) {
            if (!schema.contains(keyword)) {
                return;
            }
            target = collectChild(
                schema.value(keyword), keywordPointer(path, keyword), depth + 1);
        };
        collectDirect(QStringLiteral("if"), node.ifSchema);
        collectDirect(QStringLiteral("then"), node.thenSchema);
        collectDirect(QStringLiteral("else"), node.elseSchema);
        collectDirect(QStringLiteral("not"), node.notSchema);
    }

    void collectReference(const QJsonObject& schema,
                          const QString& path,
                          NodeId node) {
        if (!schema.contains(QStringLiteral("$ref"))) {
            return;
        }
        const QJsonValue value = schema.value(QStringLiteral("$ref"));
        const QString refPath = keywordPointer(path, QStringLiteral("$ref"));
        if (!value.isString() || value.toString().isEmpty()) {
            markInvalid(
                QStringLiteral("json_schema.invalid_ref"),
                QStringLiteral("$ref must be a non-empty string"),
                refPath,
                QStringLiteral("$ref"));
            return;
        }
        if (m_pendingReferences.size() >= m_limits.maximumReferences) {
            if (!m_referenceBudgetReported) {
                m_referenceBudgetReported = true;
                markUnsupported(
                    QStringLiteral("json_schema.reference_budget_exceeded"),
                    QStringLiteral("schema references exceed the configured limit"),
                    refPath,
                    QStringLiteral("$ref"));
            }
            return;
        }
        m_pendingReferences.append(
            PendingReference{node, value.toString(), refPath});
    }

    std::optional<NodeId> collectSchema(const QJsonValue& value,
                                        const QString& path,
                                        int depth) {
        const std::optional<NodeId> allocated = allocateNode(path, depth);
        if (!allocated) {
            return std::nullopt;
        }
        const NodeId id = *allocated;
        detail::Node node;
        node.pointer = path;

        if (value.isBool()) {
            node.booleanSchema = true;
            node.booleanValue = value.toBool();
            m_program->nodes[static_cast<qsizetype>(id)] = std::move(node);
            return id;
        }
        if (!value.isObject()) {
            markInvalid(
                QStringLiteral("json_schema.expected_schema"),
                QStringLiteral("schema value must be an object or boolean"),
                path,
                {});
            return id;
        }

        const QJsonObject schema = value.toObject();
        validateKnownKeywords(schema, path);
        parseDialectAndAnnotations(schema, path);
        parseTypes(schema, path, node);
        parseScalarAssertions(schema, path, node);
        parseArrayAssertions(schema, path, node);
        parseRequired(schema, path, node);
        collectObjectChildren(schema, path, depth, node);
        collectApplicators(schema, path, depth, node);
        collectReference(schema, path, id);
        m_program->nodes[static_cast<qsizetype>(id)] = std::move(node);
        return id;
    }

    static std::optional<QString> canonicalLocalPointer(
        const QString& reference) {
        if (!reference.startsWith(QLatin1Char('#'))) {
            return std::nullopt;
        }
        const QByteArray encoded = reference.mid(1).toUtf8();
        for (qsizetype index = 0; index < encoded.size(); ++index) {
            if (encoded.at(index) != '%') {
                continue;
            }
            if (index + 2 >= encoded.size()
                || !isHexDigit(static_cast<char16_t>(
                    QChar::fromLatin1(encoded.at(index + 1)).unicode()))
                || !isHexDigit(static_cast<char16_t>(
                    QChar::fromLatin1(encoded.at(index + 2)).unicode()))) {
                return std::nullopt;
            }
            index += 2;
        }

        const QByteArray decodedBytes = QByteArray::fromPercentEncoding(encoded);
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString fragment = decoder.decode(decodedBytes);
        if (decoder.hasError()) {
            return std::nullopt;
        }
        if (fragment.isEmpty()) {
            return QString();
        }
        if (!fragment.startsWith(QLatin1Char('/'))) {
            return std::nullopt;
        }

        QString canonical;
        const QStringList rawTokens = fragment.mid(1).split(
            QLatin1Char('/'), Qt::KeepEmptyParts);
        for (const QString& rawToken : rawTokens) {
            QString token;
            token.reserve(rawToken.size());
            for (qsizetype index = 0; index < rawToken.size(); ++index) {
                if (rawToken.at(index) != QLatin1Char('~')) {
                    token += rawToken.at(index);
                    continue;
                }
                if (index + 1 >= rawToken.size()) {
                    return std::nullopt;
                }
                const char16_t escape = static_cast<char16_t>(
                    rawToken.at(++index).unicode());
                if (escape == u'0') {
                    token += QLatin1Char('~');
                } else if (escape == u'1') {
                    token += QLatin1Char('/');
                } else {
                    return std::nullopt;
                }
            }
            canonical = appendPointer(canonical, token);
        }
        return canonical;
    }

    void resolveReferences() {
        for (const PendingReference& pending : std::as_const(m_pendingReferences)) {
            if (!pending.reference.startsWith(QLatin1Char('#'))) {
                markUnsupported(
                    QStringLiteral("json_schema.external_ref_unsupported"),
                    QStringLiteral("only same-document JSON Pointer references are supported"),
                    pending.schemaPointer,
                    QStringLiteral("$ref"));
                continue;
            }
            const std::optional<QString> pointer = canonicalLocalPointer(
                pending.reference);
            if (!pointer) {
                markUnsupported(
                    QStringLiteral("json_schema.non_pointer_ref_unsupported"),
                    QStringLiteral("$ref must be # or a valid same-document JSON Pointer"),
                    pending.schemaPointer,
                    QStringLiteral("$ref"));
                continue;
            }
            const auto target = m_program->nodeByPointer.constFind(*pointer);
            if (target == m_program->nodeByPointer.constEnd()) {
                markInvalid(
                    QStringLiteral("json_schema.unresolved_ref"),
                    QStringLiteral("$ref does not resolve to a schema node"),
                    pending.schemaPointer,
                    QStringLiteral("$ref"));
                continue;
            }
            m_program->nodes[static_cast<qsizetype>(pending.source)].reference =
                target.value();
        }
    }

    QVector<ZeroEdge> zeroEdges(NodeId id) const {
        const detail::Node& node = m_program->nodes.at(
            static_cast<qsizetype>(id));
        QVector<ZeroEdge> edges;
        if (node.reference) {
            edges.append(ZeroEdge{
                *node.reference,
                keywordPointer(node.pointer, QStringLiteral("$ref")),
                QStringLiteral("$ref")});
        }
        for (qsizetype index = 0; index < node.allOf.size(); ++index) {
            edges.append(ZeroEdge{
                node.allOf.at(index),
                appendPointer(
                    keywordPointer(node.pointer, QStringLiteral("allOf")),
                    QString::number(index)),
                QStringLiteral("allOf")});
        }
        const auto appendDirect = [&](const std::optional<NodeId>& target,
                                      const QString& keyword) {
            if (target) {
                edges.append(ZeroEdge{
                    *target, keywordPointer(node.pointer, keyword), keyword});
            }
        };
        appendDirect(node.ifSchema, QStringLiteral("if"));
        // Draft 2020-12 defines then/else without if as no-ops. They cannot
        // participate in an evaluation cycle unless this node has an if.
        if (node.ifSchema) {
            appendDirect(node.thenSchema, QStringLiteral("then"));
            appendDirect(node.elseSchema, QStringLiteral("else"));
        }
        appendDirect(node.notSchema, QStringLiteral("not"));
        std::sort(
            edges.begin(), edges.end(), [](const ZeroEdge& left,
                                           const ZeroEdge& right) {
                return left.schemaPointer < right.schemaPointer;
            });
        return edges;
    }

    void rejectZeroProgressCycles() {
        const qsizetype count = m_program->nodes.size();
        QVector<qsizetype> indegree(count, 0);
        QVector<QVector<ZeroEdge>> graph;
        graph.reserve(count);
        for (qsizetype index = 0; index < count; ++index) {
            graph.append(zeroEdges(static_cast<NodeId>(index)));
            for (const ZeroEdge& edge : graph.last()) {
                ++indegree[static_cast<qsizetype>(edge.target)];
            }
        }

        std::priority_queue<NodeId, QVector<NodeId>, std::greater<>> ready;
        for (qsizetype index = 0; index < count; ++index) {
            if (indegree.at(index) == 0) {
                ready.push(static_cast<NodeId>(index));
            }
        }
        qsizetype processed = 0;
        while (!ready.empty()) {
            const NodeId node = ready.top();
            ready.pop();
            ++processed;
            for (const ZeroEdge& edge : graph.at(
                     static_cast<qsizetype>(node))) {
                qsizetype& targetIndegree = indegree[
                    static_cast<qsizetype>(edge.target)];
                --targetIndegree;
                if (targetIndegree == 0) {
                    ready.push(edge.target);
                }
            }
        }
        if (processed == count) {
            return;
        }

        std::optional<ZeroEdge> representative = std::nullopt;
        for (qsizetype index = 0; index < count; ++index) {
            if (indegree.at(index) == 0) {
                continue;
            }
            for (const ZeroEdge& edge : graph.at(index)) {
                if (indegree.at(static_cast<qsizetype>(edge.target)) == 0) {
                    continue;
                }
                if (!representative
                    || edge.schemaPointer < representative->schemaPointer) {
                    representative = edge;
                }
            }
        }
        markUnsupported(
            QStringLiteral("json_schema.zero_progress_cycle_unsupported"),
            QStringLiteral("schema contains a reference/applicator cycle that does not descend into the instance"),
            representative ? representative->schemaPointer : QString(),
            representative ? representative->keyword : QString());
    }

    QJsonObject m_root;
    Limits m_limits;
    std::shared_ptr<detail::Program> m_program;
    CompileStatus m_status = CompileStatus::Ready;
    QVector<Issue> m_issues;
    QVector<PendingReference> m_pendingReferences;
    qsizetype m_schemaDataNodes = 0;
    bool m_schemaBudgetReported = false;
    bool m_schemaDataBudgetReported = false;
    bool m_referenceBudgetReported = false;
    bool m_compileIssuesTruncated = false;
};

enum class Evaluation {
    Pass,
    Fail,
    Aborted
};

class Validator final {
public:
    Validator(const detail::Program& program, Limits limits)
        : m_program(program), m_limits(limits) {}

    ValidationResult run(const QJsonValue& instance) {
        if (instance.isUndefined()) {
            return ValidationResult{
                false,
                {Issue{
                    QStringLiteral("json_schema.undefined_instance"),
                    QStringLiteral(
                        "undefined is not a value in the JSON data model"),
                    {}, {}, {}}}};
        }
        const Evaluation evaluation = evaluate(
            m_program.root, instance, QString(), 0, 0, true);
        return ValidationResult{
            evaluation == Evaluation::Pass && !m_aborted,
            std::move(m_issues)};
    }

private:
    struct ActiveEvaluation {
        NodeId schema = 0;
        QString instancePointer;

        bool operator==(const ActiveEvaluation& other) const {
            return schema == other.schema
                && instancePointer == other.instancePointer;
        }

        friend size_t qHash(const ActiveEvaluation& value,
                            size_t seed = 0) noexcept {
            return qHash(
                value.instancePointer, qHash(value.schema, seed));
        }
    };

    bool consumeStep(const QString& instancePointer,
                     const QString& schemaPointer) {
        return consumeSteps(1, instancePointer, schemaPointer);
    }

    bool consumeSteps(quint64 count,
                      const QString& instancePointer,
                      const QString& schemaPointer) {
        if (m_aborted) {
            return false;
        }
        if (m_steps > m_limits.maximumEvaluationSteps
            || count > m_limits.maximumEvaluationSteps - m_steps) {
            return rejectEvaluationBudget(instancePointer, schemaPointer);
        }
        m_steps += count;
        return true;
    }

    bool reportFailure(const QString& code,
                       const QString& message,
                       const QString& instancePointer,
                       const QString& schemaPointer,
                       const QString& keyword,
                       bool emitIssues) {
        if (!emitIssues) {
            return true;
        }
        const qsizetype limit = std::max<qsizetype>(
            1, m_limits.maximumDiagnostics);
        if (m_issues.size() >= limit) {
            terminalIssue(
                QStringLiteral("json_schema.diagnostics_truncated"),
                QStringLiteral("validation diagnostics exceeded the configured limit"),
                instancePointer,
                schemaPointer,
                keyword);
            return false;
        }
        m_issues.append(Issue{
            code, message, instancePointer, schemaPointer, keyword});
        return true;
    }

    void terminalIssue(const QString& code,
                       const QString& message,
                       const QString& instancePointer,
                       const QString& schemaPointer,
                       const QString& keyword) {
        if (m_aborted) {
            return;
        }
        m_aborted = true;
        const qsizetype limit = std::max<qsizetype>(
            1, m_limits.maximumDiagnostics);
        const Issue issue{
            code, message, instancePointer, schemaPointer, keyword};
        if (m_issues.size() < limit) {
            m_issues.append(issue);
        } else {
            m_issues.last() = issue;
        }
    }

    bool rejectEvaluationBudget(const QString& instancePointer,
                                const QString& schemaPointer) {
        terminalIssue(
            QStringLiteral("json_schema.validation_budget_exceeded"),
            QStringLiteral(
                "validation exceeded the configured evaluation budget"),
            instancePointer,
            schemaPointer,
            {});
        return false;
    }

    bool ensureStepCapacity(quint64 count,
                            const QString& instancePointer,
                            const QString& schemaPointer) {
        if (m_aborted) {
            return false;
        }
        if (m_steps > m_limits.maximumEvaluationSteps
            || count > m_limits.maximumEvaluationSteps - m_steps) {
            return rejectEvaluationBudget(instancePointer, schemaPointer);
        }
        return true;
    }

    bool checkObjectKeyLimits(const QJsonObject& object,
                              const QString& instancePointer,
                              const QString& schemaPointer) {
        quint64 totalCodeUnits = 0;
        const quint64 maximumTotal = m_limits.maximumObjectKeyCodeUnitsTotal < 0
            ? 0
            : static_cast<quint64>(
                m_limits.maximumObjectKeyCodeUnitsTotal);
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const qsizetype keySize = it.keyView().size();
            if (keySize > m_limits.maximumObjectKeyCodeUnits) {
                terminalIssue(
                    QStringLiteral(
                        "json_schema.instance_object_key_too_large"),
                    QStringLiteral(
                        "instance object key exceeds the configured length limit"),
                    instancePointer,
                    schemaPointer,
                    {});
                return false;
            }
            const quint64 unsignedSize = static_cast<quint64>(keySize);
            if (unsignedSize > maximumTotal - totalCodeUnits) {
                terminalIssue(
                    QStringLiteral(
                        "json_schema.instance_object_keys_too_large"),
                    QStringLiteral(
                        "instance object keys exceed the configured total length limit"),
                    instancePointer,
                    schemaPointer,
                    {});
                return false;
            }
            totalCodeUnits += unsignedSize;
        }
        return true;
    }

    bool checkValueLimits(const QJsonValue& value,
                          int depth,
                          const QString& instancePointer,
                          const QString& schemaPointer,
                          bool cacheObjectKeys = true) {
        if (depth > m_limits.maximumInstanceDepth) {
            terminalIssue(
                QStringLiteral("json_schema.instance_depth_exceeded"),
                QStringLiteral(
                    "instance nesting exceeds the configured depth limit"),
                instancePointer,
                schemaPointer,
                {});
            return false;
        }
        if (value.isString()
            && value.toString().size() > m_limits.maximumStringCodeUnits) {
            terminalIssue(
                QStringLiteral("json_schema.instance_string_too_large"),
                QStringLiteral(
                    "instance string exceeds the configured length limit"),
                instancePointer,
                schemaPointer,
                {});
            return false;
        }
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            if (object.size() > m_limits.maximumObjectProperties) {
                terminalIssue(
                    QStringLiteral("json_schema.instance_object_too_large"),
                    QStringLiteral(
                        "instance object exceeds the configured property limit"),
                    instancePointer,
                    schemaPointer,
                    {});
                return false;
            }
            if (!cacheObjectKeys) {
                if (!checkObjectKeyLimits(
                        object, instancePointer, schemaPointer)) {
                    return false;
                }
            } else if (!m_checkedObjectKeyPaths.contains(instancePointer)) {
                if (!checkObjectKeyLimits(
                        object, instancePointer, schemaPointer)) {
                    return false;
                }
                m_checkedObjectKeyPaths.insert(instancePointer);
            }
        }
        if (value.isArray()
            && value.toArray().size() > m_limits.maximumArrayItems) {
            terminalIssue(
                QStringLiteral("json_schema.instance_array_too_large"),
                QStringLiteral(
                    "instance array exceeds the configured item limit"),
                instancePointer,
                schemaPointer,
                {});
            return false;
        }
        return true;
    }

    bool consumeStringWork(qsizetype size,
                           const QString& instancePointer,
                           const QString& schemaPointer) {
        if (size <= 0) {
            return true;
        }
        constexpr quint64 codeUnitsPerStep = 64;
        const quint64 unsignedSize = static_cast<quint64>(size);
        const quint64 steps = unsignedSize / codeUnitsPerStep
            + (unsignedSize % codeUnitsPerStep == 0 ? 0 : 1);
        return consumeSteps(steps, instancePointer, schemaPointer);
    }

    bool consumePointerCodeUnits(quint64 codeUnits,
                                 const QString& instancePointer,
                                 const QString& schemaPointer) {
        constexpr quint64 codeUnitsPerStep = 16;
        const quint64 steps = codeUnits / codeUnitsPerStep
            + (codeUnits % codeUnitsPerStep == 0 ? 0 : 1);
        return consumeSteps(steps, instancePointer, schemaPointer);
    }

    bool consumeChildPointerWork(const QString& parent,
                                 quint64 childCount,
                                 quint64 suffixCodeUnits,
                                 const QString& schemaPointer) {
        const quint64 parentCodeUnits = static_cast<quint64>(parent.size());
        if (childCount != 0
            && parentCodeUnits
                > ((std::numeric_limits<quint64>::max)()
                   - suffixCodeUnits) / childCount) {
            return rejectEvaluationBudget(parent, schemaPointer);
        }
        return consumePointerCodeUnits(
            parentCodeUnits * childCount + suffixCodeUnits,
            parent,
            schemaPointer);
    }

    bool consumeArrayChildPointerWork(const QString& parent,
                                      qsizetype childCount,
                                      const QString& schemaPointer) {
        if (childCount <= 0) {
            return true;
        }
        constexpr quint64 maximumIndexCodeUnits = 20;
        const quint64 count = static_cast<quint64>(childCount);
        if (count > (std::numeric_limits<quint64>::max)()
                / (maximumIndexCodeUnits + 1)) {
            return rejectEvaluationBudget(parent, schemaPointer);
        }
        const quint64 suffixCodeUnits = count
            * (maximumIndexCodeUnits + 1);
        return consumeChildPointerWork(
            parent, count, suffixCodeUnits, schemaPointer);
    }

    bool consumeObjectChildPointerWork(const QString& parent,
                                       const QStringList& keys,
                                       const QString& schemaPointer) {
        quint64 suffixCodeUnits = static_cast<quint64>(keys.size());
        for (const QString& key : keys) {
            const quint64 keyCodeUnits = static_cast<quint64>(key.size());
            if (keyCodeUnits
                > ((std::numeric_limits<quint64>::max)()
                   - suffixCodeUnits) / 2) {
                return rejectEvaluationBudget(parent, schemaPointer);
            }
            suffixCodeUnits += keyCodeUnits * 2;
        }
        return consumeChildPointerWork(
            parent,
            static_cast<quint64>(keys.size()),
            suffixCodeUnits,
            schemaPointer);
    }

    std::optional<QString> makeChildInstancePointer(
        const QString& parent,
        const QString& token,
        const QString& schemaPointer) {
        const quint64 tokenCodeUnits = static_cast<quint64>(token.size());
        if (tokenCodeUnits
            > ((std::numeric_limits<quint64>::max)() - 1) / 2) {
            rejectEvaluationBudget(parent, schemaPointer);
            return std::nullopt;
        }
        if (!consumeChildPointerWork(
                parent,
                1,
                tokenCodeUnits * 2 + 1,
                schemaPointer)) {
            return std::nullopt;
        }
        return appendPointer(parent, token);
    }

    std::optional<QString> cachedSchemaKeywordPointer(
        const detail::Node& node,
        const QString& keyword,
        const QString& instancePointer) {
        const auto nodeCache = m_schemaKeywordPointers.constFind(&node);
        if (nodeCache != m_schemaKeywordPointers.constEnd()) {
            const auto cached = nodeCache->constFind(keyword);
            if (cached != nodeCache->constEnd()) {
                return cached.value();
            }
        }

        const quint64 parentCodeUnits = static_cast<quint64>(
            node.pointer.size());
        const quint64 keywordCodeUnits = static_cast<quint64>(keyword.size());
        if (keywordCodeUnits
            > ((std::numeric_limits<quint64>::max)() - 1) / 2
            || parentCodeUnits
                > (std::numeric_limits<quint64>::max)()
                    - keywordCodeUnits * 2 - 1) {
            rejectEvaluationBudget(instancePointer, node.pointer);
            return std::nullopt;
        }
        if (!consumePointerCodeUnits(
                parentCodeUnits + keywordCodeUnits * 2 + 1,
                instancePointer,
                node.pointer)) {
            return std::nullopt;
        }

        const QString result = appendPointer(node.pointer, keyword);
        m_schemaKeywordPointers[&node].insert(keyword, result);
        return result;
    }

    bool consumeCartesianStringWork(qsizetype leftSize,
                                    qsizetype rightSize,
                                    const QString& instancePointer,
                                    const QString& schemaPointer) {
        if (leftSize <= 0 || rightSize <= 0) {
            return true;
        }
        constexpr quint64 codeUnitsPerStep = 64;
        const quint64 left = static_cast<quint64>(leftSize);
        const quint64 right = static_cast<quint64>(rightSize);
        if (left > (std::numeric_limits<quint64>::max)() / right) {
            return rejectEvaluationBudget(instancePointer, schemaPointer);
        }
        const quint64 codeUnits = left * right;
        const quint64 steps = codeUnits / codeUnitsPerStep
            + (codeUnits % codeUnitsPerStep == 0 ? 0 : 1);
        return consumeSteps(steps, instancePointer, schemaPointer);
    }

    bool accountObjectKeysBeforeSort(
        const QJsonObject& object,
        const QString& instancePointer,
        const QString& schemaPointer) {
        if (!checkObjectKeyLimits(
                object, instancePointer, schemaPointer)) {
            return false;
        }
        quint64 totalCodeUnits = 0;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            totalCodeUnits += static_cast<quint64>(it.keyView().size());
        }

        quint64 sortPasses = 0;
        quint64 remaining = static_cast<quint64>(object.size());
        while (remaining > 1) {
            remaining = remaining / 2 + (remaining % 2 == 0 ? 0 : 1);
            ++sortPasses;
        }
        const quint64 scanSteps = totalCodeUnits / 64
            + (totalCodeUnits % 64 == 0 ? 0 : 1);
        const quint64 multiplier = sortPasses * 2 + 1;
        if (scanSteps != 0
            && multiplier
                > (std::numeric_limits<quint64>::max)() / scanSteps) {
            return rejectEvaluationBudget(instancePointer, schemaPointer);
        }
        return consumeSteps(
            scanSteps * multiplier, instancePointer, schemaPointer);
    }

    Evaluation compareValues(const QJsonValue& left,
                             const QJsonValue& right,
                             const QString& instancePointer,
                             const QString& schemaPointer) {
        struct PendingComparison {
            QJsonValue left;
            QJsonValue right;
            QString instancePointer;
            int depth = 0;
        };

        QVector<PendingComparison> pending{
            PendingComparison{left, right, instancePointer, 0}
        };
        while (!pending.isEmpty()) {
            const PendingComparison current = pending.takeLast();
            if (!consumeStep(current.instancePointer, schemaPointer)
                || !checkValueLimits(
                    current.left,
                    current.depth,
                    current.instancePointer,
                    schemaPointer)
                || !checkValueLimits(
                    current.right,
                    current.depth,
                    current.instancePointer,
                    schemaPointer,
                    false)) {
                return Evaluation::Aborted;
            }
            if (current.left.isDouble() && current.right.isDouble()) {
                if (!isFiniteNumber(current.left)
                    || !isFiniteNumber(current.right)
                    || compareNumbers(current.left, current.right)
                        != NumberOrder::Equal) {
                    return Evaluation::Fail;
                }
                continue;
            }
            if (current.left.type() != current.right.type()) {
                return Evaluation::Fail;
            }
            if (current.left.isString()) {
                const QString leftString = current.left.toString();
                const QString rightString = current.right.toString();
                if (!consumeStringWork(
                        (std::max)(leftString.size(), rightString.size()),
                        current.instancePointer,
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
                if (leftString != rightString) return Evaluation::Fail;
                continue;
            }
            if (current.left.isArray()) {
                const QJsonArray leftArray = current.left.toArray();
                const QJsonArray rightArray = current.right.toArray();
                if (leftArray.size() != rightArray.size()) {
                    return Evaluation::Fail;
                }
                if (!ensureStepCapacity(
                        static_cast<quint64>(leftArray.size()),
                        current.instancePointer,
                        schemaPointer)
                    || !consumeArrayChildPointerWork(
                        current.instancePointer,
                        leftArray.size(),
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
                for (qsizetype index = leftArray.size(); index > 0; --index) {
                    pending.append(PendingComparison{
                        leftArray.at(index - 1),
                        rightArray.at(index - 1),
                        appendPointer(
                            current.instancePointer,
                            QString::number(index - 1)),
                        current.depth + 1});
                }
                continue;
            }
            if (current.left.isObject()) {
                const QJsonObject leftObject = current.left.toObject();
                const QJsonObject rightObject = current.right.toObject();
                if (leftObject.size() != rightObject.size()
                    || !accountObjectKeysBeforeSort(
                        leftObject, current.instancePointer, schemaPointer)
                    || !accountObjectKeysBeforeSort(
                        rightObject, current.instancePointer, schemaPointer)
                    || !ensureStepCapacity(
                        static_cast<quint64>(leftObject.size()),
                        current.instancePointer,
                        schemaPointer)) {
                    return m_aborted ? Evaluation::Aborted : Evaluation::Fail;
                }
                const QStringList leftKeys = sortedKeys(leftObject);
                const QStringList rightKeys = sortedKeys(rightObject);
                if (leftKeys != rightKeys) return Evaluation::Fail;
                if (!consumeObjectChildPointerWork(
                        current.instancePointer,
                        leftKeys,
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
                for (qsizetype index = leftKeys.size(); index > 0; --index) {
                    const QString& key = leftKeys.at(index - 1);
                    if (!consumeStringWork(
                            key.size(),
                            current.instancePointer,
                            schemaPointer)) {
                        return Evaluation::Aborted;
                    }
                    pending.append(PendingComparison{
                        leftObject.value(key),
                        rightObject.value(key),
                        appendPointer(current.instancePointer, key),
                        current.depth + 1});
                }
                continue;
            }
            if (current.left != current.right) return Evaluation::Fail;
        }
        return Evaluation::Pass;
    }

    Evaluation accountValueForHash(const QJsonValue& root,
                                   int rootDepth,
                                   const QString& instancePointer,
                                   const QString& schemaPointer) {
        struct PendingValue {
            QJsonValue value;
            QString instancePointer;
            int depth = 0;
        };

        QVector<PendingValue> pending{
            PendingValue{root, instancePointer, rootDepth}
        };
        while (!pending.isEmpty()) {
            const PendingValue current = pending.takeLast();
            if (!consumeStep(current.instancePointer, schemaPointer)
                || !checkValueLimits(
                    current.value,
                    current.depth,
                    current.instancePointer,
                    schemaPointer)) {
                return Evaluation::Aborted;
            }
            if (current.value.isString()) {
                if (!consumeStringWork(
                        current.value.toString().size(),
                        current.instancePointer,
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
            } else if (current.value.isArray()) {
                const QJsonArray values = current.value.toArray();
                if (!ensureStepCapacity(
                        static_cast<quint64>(values.size()),
                        current.instancePointer,
                        schemaPointer)
                    || !consumeArrayChildPointerWork(
                        current.instancePointer,
                        values.size(),
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
                for (qsizetype index = values.size(); index > 0; --index) {
                    pending.append(PendingValue{
                        values.at(index - 1),
                        appendPointer(
                            current.instancePointer,
                            QString::number(index - 1)),
                        current.depth + 1});
                }
            } else if (current.value.isObject()) {
                const QJsonObject values = current.value.toObject();
                if (!accountObjectKeysBeforeSort(
                        values, current.instancePointer, schemaPointer)
                    || !ensureStepCapacity(
                        static_cast<quint64>(values.size()),
                        current.instancePointer,
                        schemaPointer)) {
                    return Evaluation::Aborted;
                }
                const QStringList keys = sortedKeys(values);
                if (!consumeObjectChildPointerWork(
                        current.instancePointer, keys, schemaPointer)) {
                    return Evaluation::Aborted;
                }
                for (qsizetype index = keys.size(); index > 0; --index) {
                    const QString& key = keys.at(index - 1);
                    if (!consumeStringWork(
                            key.size(),
                            current.instancePointer,
                            schemaPointer)) {
                        return Evaluation::Aborted;
                    }
                    pending.append(PendingValue{
                        values.value(key),
                        appendPointer(current.instancePointer, key),
                        current.depth + 1});
                }
            }
        }
        return Evaluation::Pass;
    }

    bool merge(Evaluation evaluation, bool& valid) {
        if (evaluation == Evaluation::Aborted) {
            return false;
        }
        if (evaluation == Evaluation::Fail) {
            valid = false;
        }
        return true;
    }

    bool fail(bool& valid,
              const QString& code,
              const QString& message,
              const QString& instancePointer,
              const QString& schemaPointer,
              const QString& keyword,
              bool emitIssues) {
        valid = false;
        return reportFailure(
            code, message, instancePointer, schemaPointer, keyword, emitIssues);
    }

    Evaluation evaluate(NodeId id,
                        const QJsonValue& instance,
                        const QString& instancePointer,
                        int instanceDepth,
                        int evaluationDepth,
                        bool emitIssues) {
        const detail::Node& node = m_program.nodes.at(
            static_cast<qsizetype>(id));
        if (evaluationDepth > m_limits.maximumEvaluationDepth) {
            terminalIssue(
                QStringLiteral("json_schema.evaluation_depth_exceeded"),
                QStringLiteral(
                    "schema evaluation exceeds the configured recursion depth limit"),
                instancePointer,
                node.pointer,
                {});
            return Evaluation::Aborted;
        }
        if (!checkValueLimits(
                instance, instanceDepth, instancePointer, node.pointer)
            || !consumeStep(instancePointer, node.pointer)) {
            return Evaluation::Aborted;
        }

        const ActiveEvaluation activeKey{id, instancePointer};
        if (m_active.contains(activeKey)) {
            terminalIssue(
                QStringLiteral("json_schema.evaluation_cycle"),
                QStringLiteral("validation encountered a non-progressing schema cycle"),
                instancePointer,
                node.pointer,
                {});
            return Evaluation::Aborted;
        }
        m_active.insert(activeKey);
        const Evaluation result = evaluateBody(
            node,
            instance,
            instancePointer,
            instanceDepth,
            evaluationDepth,
            emitIssues);
        m_active.remove(activeKey);
        return result;
    }

    Evaluation evaluateBody(const detail::Node& node,
                            const QJsonValue& instance,
                            const QString& instancePointer,
                            int instanceDepth,
                            int evaluationDepth,
                            bool emitIssues) {
        if (node.booleanSchema) {
            if (node.booleanValue) {
                return Evaluation::Pass;
            }
            bool valid = true;
            if (!fail(
                    valid,
                    QStringLiteral("json_schema.false_schema"),
                    QStringLiteral("instance is rejected by a false schema"),
                    instancePointer,
                    node.pointer,
                    {},
                    emitIssues)) {
                return Evaluation::Aborted;
            }
            return Evaluation::Fail;
        }

        bool valid = true;
        if (node.reference
            && !merge(evaluate(
                          *node.reference,
                          instance,
                          instancePointer,
                          instanceDepth,
                          evaluationDepth + 1,
                          emitIssues),
                      valid)) {
            return Evaluation::Aborted;
        }
        if (!emitIssues && !valid) return Evaluation::Fail;

        if (node.hasTypes && !matchesType(instance, node.types)) {
            const std::optional<QString> typePath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("type"), instancePointer);
            if (!typePath) {
                return Evaluation::Aborted;
            }
            if (!fail(
                    valid,
                    QStringLiteral("json_schema.type"),
                    QStringLiteral("expected a declared JSON type, got %1")
                        .arg(valueTypeName(instance)),
                    instancePointer,
                    *typePath,
                    QStringLiteral("type"),
                    emitIssues)) {
                return Evaluation::Aborted;
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;
        if (node.hasConst) {
            const std::optional<QString> constPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("const"), instancePointer);
            if (!constPath) {
                return Evaluation::Aborted;
            }
            const Evaluation equality = compareValues(
                instance,
                node.constValue,
                instancePointer,
                *constPath);
            if (equality == Evaluation::Aborted) {
                return Evaluation::Aborted;
            }
            if (equality == Evaluation::Fail && !fail(
                    valid,
                    QStringLiteral("json_schema.const"),
                    QStringLiteral("value does not match const"),
                    instancePointer,
                    *constPath,
                    QStringLiteral("const"),
                    emitIssues)) {
                return Evaluation::Aborted;
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;
        if (!node.enumValues.isEmpty()) {
            const std::optional<QString> enumPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("enum"), instancePointer);
            if (!enumPath) {
                return Evaluation::Aborted;
            }
            bool matches = false;
            for (const QJsonValue& candidate : node.enumValues) {
                const Evaluation equality = compareValues(
                    instance,
                    candidate,
                    instancePointer,
                    *enumPath);
                if (equality == Evaluation::Aborted) {
                    return Evaluation::Aborted;
                }
                if (equality == Evaluation::Pass) {
                    matches = true;
                    break;
                }
            }
            if (!matches
                && !fail(
                    valid,
                    QStringLiteral("json_schema.enum"),
                    QStringLiteral("value is not one of the enum entries"),
                    instancePointer,
                    *enumPath,
                    QStringLiteral("enum"),
                    emitIssues)) {
                return Evaluation::Aborted;
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;
        if (node.pattern && instance.isString()) {
            const std::optional<QString> patternPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("pattern"), instancePointer);
            if (!patternPath) {
                return Evaluation::Aborted;
            }
            const QString text = instance.toString();
            if (!consumeCartesianStringWork(
                    node.patternCodeUnits,
                    text.size(),
                    instancePointer,
                    *patternPath)) {
                return Evaluation::Aborted;
            }
            if (!node.pattern->match(text).hasMatch()
                && !fail(
                    valid,
                    QStringLiteral("json_schema.pattern"),
                    QStringLiteral("string does not match pattern"),
                    instancePointer,
                    *patternPath,
                    QStringLiteral("pattern"),
                    emitIssues)) {
                return Evaluation::Aborted;
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;

        if (isFiniteNumber(instance)) {
            if (node.minimum
                && compareNumbers(instance, *node.minimum)
                    == NumberOrder::Less) {
                const std::optional<QString> minimumPath =
                    cachedSchemaKeywordPointer(
                        node, QStringLiteral("minimum"), instancePointer);
                if (!minimumPath) {
                    return Evaluation::Aborted;
                }
                if (!fail(
                        valid,
                        QStringLiteral("json_schema.minimum"),
                        QStringLiteral("number is less than minimum"),
                        instancePointer,
                        *minimumPath,
                        QStringLiteral("minimum"),
                        emitIssues)) {
                    return Evaluation::Aborted;
                }
            }
            if (node.exclusiveMinimum
                && compareNumbers(instance, *node.exclusiveMinimum)
                    != NumberOrder::Greater) {
                const std::optional<QString> minimumPath =
                    cachedSchemaKeywordPointer(
                        node,
                        QStringLiteral("exclusiveMinimum"),
                        instancePointer);
                if (!minimumPath) {
                    return Evaluation::Aborted;
                }
                if (!fail(
                        valid,
                        QStringLiteral("json_schema.exclusive_minimum"),
                        QStringLiteral("number is not greater than exclusiveMinimum"),
                        instancePointer,
                        *minimumPath,
                        QStringLiteral("exclusiveMinimum"),
                        emitIssues)) {
                    return Evaluation::Aborted;
                }
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;

        if (instance.isObject()
            && !evaluateObject(
                node,
                instance.toObject(),
                instancePointer,
                instanceDepth,
                evaluationDepth,
                emitIssues,
                valid)) {
            return Evaluation::Aborted;
        }
        if (!emitIssues && !valid) return Evaluation::Fail;
        if (instance.isArray()
            && !evaluateArray(
                node,
                instance.toArray(),
                instancePointer,
                instanceDepth,
                evaluationDepth,
                emitIssues,
                valid)) {
            return Evaluation::Aborted;
        }
        if (!emitIssues && !valid) return Evaluation::Fail;

        for (NodeId child : node.allOf) {
            if (!merge(evaluate(
                          child,
                          instance,
                          instancePointer,
                          instanceDepth,
                          evaluationDepth + 1,
                          emitIssues),
                      valid)) {
                return Evaluation::Aborted;
            }
            if (!emitIssues && !valid) return Evaluation::Fail;
        }

        if (node.ifSchema) {
            const Evaluation condition = evaluate(
                *node.ifSchema,
                instance,
                instancePointer,
                instanceDepth,
                evaluationDepth + 1,
                false);
            if (condition == Evaluation::Aborted) {
                return Evaluation::Aborted;
            }
            if (!emitIssues && !valid) return Evaluation::Fail;
            const std::optional<NodeId>& branch = condition == Evaluation::Pass
                ? node.thenSchema : node.elseSchema;
            if (branch
                && !merge(evaluate(
                              *branch,
                              instance,
                              instancePointer,
                              instanceDepth,
                              evaluationDepth + 1,
                              emitIssues),
                          valid)) {
                return Evaluation::Aborted;
            }
        }
        if (!emitIssues && !valid) return Evaluation::Fail;

        if (node.notSchema) {
            const Evaluation negated = evaluate(
                *node.notSchema,
                instance,
                instancePointer,
                instanceDepth,
                evaluationDepth + 1,
                false);
            if (negated == Evaluation::Aborted) {
                return Evaluation::Aborted;
            }
            if (negated == Evaluation::Pass) {
                const std::optional<QString> notPath =
                    cachedSchemaKeywordPointer(
                        node, QStringLiteral("not"), instancePointer);
                if (!notPath) {
                    return Evaluation::Aborted;
                }
                if (!fail(
                    valid,
                    QStringLiteral("json_schema.not"),
                    QStringLiteral("instance matches the schema forbidden by not"),
                    instancePointer,
                    *notPath,
                    QStringLiteral("not"),
                    emitIssues)) {
                    return Evaluation::Aborted;
                }
            }
        }

        return valid ? Evaluation::Pass : Evaluation::Fail;
    }

    bool evaluateObject(const detail::Node& node,
                        const QJsonObject& object,
                        const QString& instancePointer,
                        int instanceDepth,
                        int evaluationDepth,
                        bool emitIssues,
                        bool& valid) {
        std::optional<QString> requiredPath = std::nullopt;
        if (!node.required.isEmpty()) {
            requiredPath = cachedSchemaKeywordPointer(
                node, QStringLiteral("required"), instancePointer);
            if (!requiredPath) {
                return false;
            }
        }
        for (const QString& name : node.required) {
            if (!consumeStep(
                    instancePointer, *requiredPath)) {
                return false;
            }
            if (!object.contains(name)) {
                const std::optional<QString> childInstance =
                    makeChildInstancePointer(
                        instancePointer, name, *requiredPath);
                if (!childInstance) {
                    return false;
                }
                if (!fail(
                    valid,
                    QStringLiteral("json_schema.required"),
                    QStringLiteral("required property %1 is missing").arg(name),
                    *childInstance,
                    *requiredPath,
                    QStringLiteral("required"),
                    emitIssues)) {
                    return false;
                }
            }
            if (!emitIssues && !valid) return true;
        }

        QSet<QString> declared;
        qsizetype presentDeclared = 0;
        std::optional<QString> propertiesPath = std::nullopt;
        if (!node.properties.isEmpty()) {
            propertiesPath = cachedSchemaKeywordPointer(
                node, QStringLiteral("properties"), instancePointer);
            if (!propertiesPath) {
                return false;
            }
        }
        for (const detail::PropertyRule& property : node.properties) {
            declared.insert(property.name);
            if (!object.contains(property.name)) {
                continue;
            }
            ++presentDeclared;
            const std::optional<QString> childInstance =
                makeChildInstancePointer(
                    instancePointer, property.name, *propertiesPath);
            if (!childInstance
                || !consumeStep(*childInstance, *propertiesPath)) {
                return false;
            }
            if (!merge(evaluate(
                          property.schema,
                          object.value(property.name),
                          *childInstance,
                          instanceDepth + 1,
                          evaluationDepth + 1,
                          emitIssues),
                      valid)) {
                return false;
            }
            if (!emitIssues && !valid) return true;
        }

        if (node.additionalProperties
            == detail::AdditionalPropertiesKind::Allow) {
            return true;
        }
        const qsizetype additionalCount = object.size() - presentDeclared;
        const std::optional<QString> additionalPath =
            cachedSchemaKeywordPointer(
                node,
                QStringLiteral("additionalProperties"),
                instancePointer);
        if (!additionalPath) {
            return false;
        }
        if (!ensureStepCapacity(
                static_cast<quint64>(additionalCount),
                instancePointer,
                *additionalPath)) {
            return false;
        }
        if (!accountObjectKeysBeforeSort(
                object,
                instancePointer,
                *additionalPath)) {
            return false;
        }
        const QStringList objectKeys = sortedKeys(object);
        QStringList additionalKeys;
        additionalKeys.reserve(additionalCount);
        for (const QString& name : objectKeys) {
            if (!declared.contains(name)) {
                additionalKeys.append(name);
            }
        }
        if (!consumeObjectChildPointerWork(
                instancePointer, additionalKeys, *additionalPath)) {
            return false;
        }
        for (const QString& name : additionalKeys) {
            const QString childInstance = appendPointer(instancePointer, name);
            if (!consumeStep(childInstance, *additionalPath)) {
                return false;
            }
            if (node.additionalProperties
                == detail::AdditionalPropertiesKind::Deny) {
                if (!fail(
                        valid,
                        QStringLiteral("json_schema.additional_property"),
                        QStringLiteral("property %1 is not allowed").arg(name),
                        childInstance,
                        *additionalPath,
                        QStringLiteral("additionalProperties"),
                        emitIssues)) {
                    return false;
                }
                if (!emitIssues && !valid) return true;
            } else if (node.additionalProperties
                       == detail::AdditionalPropertiesKind::Schema) {
                if (!merge(evaluate(
                              *node.additionalPropertiesSchema,
                              object.value(name),
                              childInstance,
                              instanceDepth + 1,
                              evaluationDepth + 1,
                              emitIssues),
                          valid)) {
                    return false;
                }
                if (!emitIssues && !valid) return true;
            }
        }
        return true;
    }

    bool evaluateArray(const detail::Node& node,
                       const QJsonArray& array,
                       const QString& instancePointer,
                       int instanceDepth,
                       int evaluationDepth,
                       bool emitIssues,
                       bool& valid) {
        if (node.minimumItems && array.size() < *node.minimumItems) {
            const std::optional<QString> minimumPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("minItems"), instancePointer);
            if (!minimumPath) {
                return false;
            }
            if (!fail(
                    valid,
                    QStringLiteral("json_schema.min_items"),
                    QStringLiteral("array contains fewer than minItems entries"),
                    instancePointer,
                    *minimumPath,
                    QStringLiteral("minItems"),
                    emitIssues)) {
                return false;
            }
            if (!emitIssues && !valid) return true;
        }
        if (node.maximumItems && array.size() > *node.maximumItems) {
            const std::optional<QString> maximumPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("maxItems"), instancePointer);
            if (!maximumPath) {
                return false;
            }
            if (!fail(
                    valid,
                    QStringLiteral("json_schema.max_items"),
                    QStringLiteral("array contains more than maxItems entries"),
                    instancePointer,
                    *maximumPath,
                    QStringLiteral("maxItems"),
                    emitIssues)) {
                return false;
            }
            if (!emitIssues && !valid) return true;
        }

        if (node.items) {
            const std::optional<QString> itemsPath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("items"), instancePointer);
            if (!itemsPath) {
                return false;
            }
            if (!consumeArrayChildPointerWork(
                    instancePointer, array.size(), *itemsPath)) {
                return false;
            }
            for (qsizetype index = 0; index < array.size(); ++index) {
                const QString childInstance = appendPointer(
                    instancePointer, QString::number(index));
                if (!consumeStep(
                        childInstance, *itemsPath)) {
                    return false;
                }
                if (!merge(evaluate(
                              *node.items,
                              array.at(index),
                              childInstance,
                              instanceDepth + 1,
                              evaluationDepth + 1,
                              emitIssues),
                          valid)) {
                    return false;
                }
                if (!emitIssues && !valid) return true;
            }
        }

        if (node.uniqueItems) {
            QHash<QJsonValue, qsizetype> firstIndex;
            firstIndex.reserve(array.size());
            const std::optional<QString> uniquePath =
                cachedSchemaKeywordPointer(
                    node, QStringLiteral("uniqueItems"), instancePointer);
            if (!uniquePath) {
                return false;
            }
            if (!consumeArrayChildPointerWork(
                    instancePointer, array.size(), *uniquePath)) {
                return false;
            }
            for (qsizetype index = 0; index < array.size(); ++index) {
                const QString childInstance = appendPointer(
                    instancePointer, QString::number(index));
                if (accountValueForHash(
                        array.at(index),
                        instanceDepth + 1,
                        childInstance,
                        *uniquePath) == Evaluation::Aborted) {
                    return false;
                }
                const auto existing = firstIndex.constFind(array.at(index));
                if (existing == firstIndex.constEnd()) {
                    firstIndex.insert(array.at(index), index);
                    continue;
                }
                if (!fail(
                        valid,
                        QStringLiteral("json_schema.unique_items"),
                        QStringLiteral("array entry duplicates index %1")
                            .arg(existing.value()),
                        childInstance,
                        *uniquePath,
                        QStringLiteral("uniqueItems"),
                        emitIssues)) {
                    return false;
                }
                if (!emitIssues && !valid) return true;
            }
        }
        return true;
    }

    const detail::Program& m_program;
    Limits m_limits;
    quint64 m_steps = 0;
    QVector<Issue> m_issues;
    QSet<ActiveEvaluation> m_active;
    QSet<QString> m_checkedObjectKeyPaths;
    QHash<const detail::Node*, QHash<QString, QString>> m_schemaKeywordPointers;
    bool m_aborted = false;
};

} // namespace

CompiledSchema::CompiledSchema(
    std::shared_ptr<const detail::Program> program)
    : m_program(std::move(program)) {}

CompiledSchema::~CompiledSchema() = default;

CompileResult compile(const QJsonObject& schema, Limits limits) {
    Compiler compiler(schema, limits);
    CompileResult result = compiler.run();
    if (result.status == CompileStatus::Ready) {
        result.schema = std::shared_ptr<const CompiledSchema>(
            new CompiledSchema(compiler.program()));
    }
    return result;
}

ValidationResult validate(const CompiledSchema& schema,
                          const QJsonValue& instance,
                          Limits limits) {
    if (!schema.m_program || schema.m_program->nodes.isEmpty()) {
        return ValidationResult{
            false,
            {Issue{
                QStringLiteral("json_schema.uninitialized_schema"),
                QStringLiteral("compiled schema is not initialized"),
                {}, {}, {}}}};
    }
    return Validator(*schema.m_program, limits).run(instance);
}

} // namespace finepaper::json_schema
