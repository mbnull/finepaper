#include "strictjson.h"

#include <boost/multiprecision/cpp_int.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QStringConverter>

#include <cerrno>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace noc_contract {
namespace {

class Scanner final {
public:
    explicit Scanner(QByteArrayView bytes) : m_bytes(bytes) {}

    bool run(QString *error) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        decoder(m_bytes);
        if (decoder.hasError()) {
            return fail(error, QStringLiteral("input is not valid UTF-8"));
        }
        skipWhitespace();
        if (!scanValue(error)) {
            return false;
        }
        skipWhitespace();
        if (!atEnd()) {
            return fail(error, QStringLiteral("trailing bytes after JSON value"));
        }
        return true;
    }

private:
    bool atEnd() const { return m_position >= m_bytes.size(); }

    char peek() const { return atEnd() ? '\0' : m_bytes.at(m_position); }

    void skipWhitespace() {
        while (!atEnd()) {
            const char value = peek();
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                return;
            }
            ++m_position;
        }
    }

    bool fail(QString *error, const QString &message) {
        if (error) {
            *error = QStringLiteral("offset %1: %2").arg(m_position).arg(message);
        }
        return false;
    }

    bool consume(char expected, QString *error) {
        if (peek() != expected) {
            return fail(error, QStringLiteral("expected '%1'").arg(QChar(expected)));
        }
        ++m_position;
        return true;
    }

    bool scanValue(QString *error) {
        skipWhitespace();
        switch (peek()) {
        case '{': return scanObject(error);
        case '[': return scanArray(error);
        case '"': return scanString(nullptr, error);
        case 't': return scanLiteral("true", error);
        case 'f': return scanLiteral("false", error);
        case 'n': return scanLiteral("null", error);
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                return scanNumber(error);
            }
            return fail(error, QStringLiteral("expected JSON value"));
        }
    }

    bool scanLiteral(const char *literal, QString *error) {
        const qsizetype length = static_cast<qsizetype>(std::strlen(literal));
        if (m_position + length > m_bytes.size() ||
            QByteArrayView(m_bytes.data() + m_position, length) != literal) {
            return fail(error, QStringLiteral("invalid JSON literal"));
        }
        m_position += length;
        return true;
    }

    bool scanString(QString *decoded, QString *error) {
        const qsizetype begin = m_position;
        if (!consume('"', error)) {
            return false;
        }
        bool escaped = false;
        while (!atEnd()) {
            const unsigned char byte = static_cast<unsigned char>(peek());
            ++m_position;
            if (escaped) {
                escaped = false;
                continue;
            }
            if (byte == '\\') {
                escaped = true;
                continue;
            }
            if (byte == '"') {
                const QByteArray token(m_bytes.sliced(begin, m_position - begin));
                QJsonParseError parseError;
                const QJsonDocument parsed = QJsonDocument::fromJson(
                    QByteArrayLiteral("[") + token + QByteArrayLiteral("]"), &parseError);
                if (parseError.error != QJsonParseError::NoError || !parsed.isArray() ||
                    parsed.array().size() != 1 || !parsed.array().first().isString()) {
                    return fail(error, QStringLiteral("invalid JSON string"));
                }
                const QString value = parsed.array().first().toString();
                for (qsizetype index = 0; index < value.size(); ++index) {
                    const ushort codeUnit = value.at(index).unicode();
                    if (QChar::isHighSurrogate(codeUnit)) {
                        if (index + 1 >= value.size() ||
                            !QChar::isLowSurrogate(value.at(index + 1).unicode())) {
                            return fail(error, QStringLiteral("unpaired high surrogate"));
                        }
                        ++index;
                    } else if (QChar::isLowSurrogate(codeUnit)) {
                        return fail(error, QStringLiteral("unpaired low surrogate"));
                    }
                }
                if (decoded) {
                    *decoded = value;
                }
                return true;
            }
            if (byte < 0x20) {
                return fail(error, QStringLiteral("unescaped control character in string"));
            }
        }
        return fail(error, QStringLiteral("unterminated JSON string"));
    }

    bool scanObject(QString *error) {
        if (!consume('{', error)) {
            return false;
        }
        skipWhitespace();
        QSet<QString> keys;
        if (peek() == '}') {
            ++m_position;
            return true;
        }
        while (true) {
            QString key;
            if (!scanString(&key, error)) {
                return false;
            }
            if (keys.contains(key)) {
                return fail(error, QStringLiteral("duplicate JSON object member '%1'").arg(key));
            }
            keys.insert(key);
            skipWhitespace();
            if (!consume(':', error)) {
                return false;
            }
            if (!scanValue(error)) {
                return false;
            }
            skipWhitespace();
            if (peek() == '}') {
                ++m_position;
                return true;
            }
            if (!consume(',', error)) {
                return false;
            }
            skipWhitespace();
        }
    }

    bool scanArray(QString *error) {
        if (!consume('[', error)) {
            return false;
        }
        skipWhitespace();
        if (peek() == ']') {
            ++m_position;
            return true;
        }
        while (true) {
            if (!scanValue(error)) {
                return false;
            }
            skipWhitespace();
            if (peek() == ']') {
                ++m_position;
                return true;
            }
            if (!consume(',', error)) {
                return false;
            }
            skipWhitespace();
        }
    }

    bool scanNumber(QString *error) {
        const qsizetype begin = m_position;
        if (peek() == '-') {
            ++m_position;
        }
        if (peek() == '0') {
            ++m_position;
            if (peek() >= '0' && peek() <= '9') {
                return fail(error, QStringLiteral("leading zero in number"));
            }
        } else {
            if (peek() < '1' || peek() > '9') {
                return fail(error, QStringLiteral("invalid number integer part"));
            }
            while (peek() >= '0' && peek() <= '9') {
                ++m_position;
            }
        }
        const bool integerOnly = peek() != '.' && peek() != 'e' && peek() != 'E';
        if (peek() == '.') {
            ++m_position;
            if (peek() < '0' || peek() > '9') {
                return fail(error, QStringLiteral("fraction requires a digit"));
            }
            while (peek() >= '0' && peek() <= '9') {
                ++m_position;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++m_position;
            if (peek() == '+' || peek() == '-') {
                ++m_position;
            }
            if (peek() < '0' || peek() > '9') {
                return fail(error, QStringLiteral("exponent requires a digit"));
            }
            while (peek() >= '0' && peek() <= '9') {
                ++m_position;
            }
        }
        const QByteArray token(m_bytes.sliced(begin, m_position - begin));
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(
            QByteArrayLiteral("[") + token + QByteArrayLiteral("]"), &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isArray() ||
            parsed.array().size() != 1 || !parsed.array().first().isDouble()) {
            return fail(error, QStringLiteral("invalid JSON number"));
        }
        const double value = parsed.array().first().toDouble();
        if (!std::isfinite(value)) {
            return fail(error, QStringLiteral("number is not finite binary64"));
        }
        if (integerOnly) {
            if (!integerTokenMatchesBinary64(token, value)) {
                return fail(error, QStringLiteral("integer is not represented exactly by binary64; use a string"));
            }
        }
        return true;
    }

    static bool integerTokenMatchesBinary64(const QByteArray &token, double value) {
        using boost::multiprecision::cpp_int;

        const auto bits = std::bit_cast<std::uint64_t>(value);
        const bool valueNegative = (bits >> 63) != 0;
        const std::uint64_t fraction = bits & ((std::uint64_t{1} << 52) - 1);
        const unsigned exponentField = static_cast<unsigned>((bits >> 52) & 0x7ff);
        if (exponentField == 0x7ff) {
            return false;
        }

        const std::uint64_t significand = exponentField == 0
                                               ? fraction
                                               : ((std::uint64_t{1} << 52) | fraction);
        const int binaryShift = exponentField == 0
                                    ? -1074
                                    : static_cast<int>(exponentField) - 1023 - 52;
        cpp_int exactMagnitude = significand;
        if (binaryShift >= 0) {
            exactMagnitude <<= binaryShift;
        } else {
            const int divisorBits = -binaryShift;
            if (divisorBits >= 64) {
                if (significand != 0) {
                    return false;
                }
                exactMagnitude = 0;
            } else {
                const std::uint64_t remainderMask =
                    (std::uint64_t{1} << divisorBits) - 1;
                if ((significand & remainderMask) != 0) {
                    return false;
                }
                exactMagnitude >>= divisorBits;
            }
        }

        const bool tokenNegative = token.startsWith('-');
        cpp_int tokenMagnitude = 0;
        const qsizetype begin = tokenNegative ? 1 : 0;
        for (qsizetype index = begin; index < token.size(); ++index) {
            const char digit = token.at(index);
            if (digit < '0' || digit > '9') {
                return false;
            }
            tokenMagnitude *= 10;
            tokenMagnitude += digit - '0';
        }
        return tokenMagnitude == exactMagnitude &&
               (tokenMagnitude == 0 || tokenNegative == valueNegative);
    }

    QByteArrayView m_bytes;
    qsizetype m_position = 0;
};

} // namespace

bool validateStrictJson(QByteArrayView bytes, QString *error) {
    return Scanner(bytes).run(error);
}

} // namespace noc_contract
