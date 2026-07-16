#include "strictjson.h"
#include "canonicaljson.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString &message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void rejects(QByteArrayView bytes) {
    QString error;
    require(!noc_contract::validateStrictJson(bytes, &error),
            QStringLiteral("input must be rejected: %1").arg(QString::fromUtf8(bytes)));
    require(!error.isEmpty(), QStringLiteral("rejection must include an error"));
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QString error;
        require(noc_contract::validateStrictJson("{\"a\":1,\"b\":1.0,\"c\":1e0}", &error), error);
        require(noc_contract::validateStrictJson("[1e-7,1e-6,1e20,1e21,-0]", &error), error);
        CanonicalRuleSet rules;
        const auto number = QJsonDocument::fromJson("[1e-7]").array().first();
        require(canonicalJson(number, rules) == QByteArrayLiteral("1e-7"),
                QStringLiteral("RFC 8785 must preserve the 1e-7 threshold"));
        const auto oneMillionth = QJsonDocument::fromJson("[1e-6]").array().first();
        require(canonicalJson(oneMillionth, rules) == QByteArrayLiteral("0.000001"),
                QStringLiteral("RFC 8785 must use fixed notation at 1e-6"));
        const auto oneE20 = QJsonDocument::fromJson("[1e20]").array().first();
        require(canonicalJson(oneE20, rules) == QByteArrayLiteral("100000000000000000000"),
                QStringLiteral("RFC 8785 must use fixed notation below 1e21"));
        const auto oneE21 = QJsonDocument::fromJson("[1e21]").array().first();
        require(canonicalJson(oneE21, rules) == QByteArrayLiteral("1e+21"),
                QStringLiteral("RFC 8785 must use exponent notation at 1e21"));
        const auto negativeOneE21 = QJsonDocument::fromJson("[-1e21]").array().first();
        require(canonicalJson(negativeOneE21, rules) == QByteArrayLiteral("-1e+21"),
                QStringLiteral("RFC 8785 must preserve negative exponent sign"));
        const auto largeRoundTrip =
            QJsonDocument::fromJson("[1000000000000000100]").array().first();
        const auto largeRoundTripCanonical = canonicalJson(largeRoundTrip, rules);
        require(largeRoundTripCanonical == QByteArrayLiteral("1000000000000000100"),
                QStringLiteral("RFC 8785 large-number spelling was %1")
                    .arg(QString::fromUtf8(largeRoundTripCanonical)));
        const auto smallest = QJsonDocument::fromJson("[5e-324]").array().first();
        require(canonicalJson(smallest, rules) == QByteArrayLiteral("5e-324"),
                QStringLiteral("RFC 8785 must preserve the smallest binary64 value"));
        const auto largest =
            QJsonDocument::fromJson("[1.7976931348623157e308]").array().first();
        require(canonicalJson(largest, rules) ==
                    QByteArrayLiteral("1.7976931348623157e+308"),
                QStringLiteral("RFC 8785 must preserve the largest binary64 value"));
        QJsonObject unicodeKeys;
        unicodeKeys.insert(QStringLiteral("\ue000"), 1);
        unicodeKeys.insert(QStringLiteral("😀"), 2);
        require(canonicalJson(unicodeKeys, rules) == QByteArrayLiteral("{\"😀\":2,\"\":1}"),
                QStringLiteral("object keys must use RFC 8785 UTF-16 order"));
        rejects("{\"a\":1,\"a\":2}");
        rejects("{\"a\":1,\"\\u0061\":2}");
        rejects("{\"x\":9007199254740993}");
        rejects("{\"x\":NaN}");
        rejects("{\"x\":\"\\ud800\"}");
        rejects(QByteArray("{\"x\":\xC3\x28}"));
        std::cout << "noc_strict_json_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
