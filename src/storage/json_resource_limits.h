#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace finepaper {

// Resource limits apply to an already parsed JSON value. Callers must also
// bound the source byte stream before QJsonDocument parses it.
struct JsonResourceLimits {
    qsizetype maximumNestingDepth = 64;
    qsizetype maximumArrayItems = 65'536;
    qsizetype maximumObjectMembers = 65'536;
    qsizetype maximumStringCodeUnits = 65'536;
    qsizetype maximumObjectKeyCodeUnits = 4'096;
    qsizetype maximumTotalValues = 1'000'000;
};

enum class JsonResourceLimitKind {
    NestingDepth,
    ArrayItems,
    ObjectMembers,
    StringCodeUnits,
    ObjectKeyCodeUnits,
    TotalValues,
};

struct JsonResourceViolation {
    JsonResourceLimitKind kind = JsonResourceLimitKind::TotalValues;
    QString pointer;
    qsizetype actual = 0;
    qsizetype limit = 0;
};

[[nodiscard]] inline QString jsonPointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
}

// Iterative traversal avoids turning the validation pass itself into a stack
// exhaustion path for a deeply nested external document.
[[nodiscard]] inline std::optional<JsonResourceViolation>
firstJsonResourceViolation(const QJsonValue& root,
                           const JsonResourceLimits& limits = {}) {
    struct PendingValue {
        QJsonValue value;
        QString pointer;
        qsizetype depth = 0;
    };

    QVector<PendingValue> pending;
    pending.append(PendingValue{root, {}, 0});
    qsizetype visitedValues = 0;

    while (!pending.isEmpty()) {
        PendingValue current = std::move(pending.back());
        pending.removeLast();
        ++visitedValues;
        if (visitedValues > limits.maximumTotalValues) {
            return JsonResourceViolation{
                JsonResourceLimitKind::TotalValues,
                current.pointer,
                visitedValues,
                limits.maximumTotalValues};
        }
        if (current.depth > limits.maximumNestingDepth) {
            return JsonResourceViolation{
                JsonResourceLimitKind::NestingDepth,
                current.pointer,
                current.depth,
                limits.maximumNestingDepth};
        }

        if (current.value.isString()) {
            const qsizetype size = current.value.toString().size();
            if (size > limits.maximumStringCodeUnits) {
                return JsonResourceViolation{
                    JsonResourceLimitKind::StringCodeUnits,
                    current.pointer,
                    size,
                    limits.maximumStringCodeUnits};
            }
            continue;
        }

        if (current.value.isArray()) {
            const QJsonArray array = current.value.toArray();
            if (array.size() > limits.maximumArrayItems) {
                return JsonResourceViolation{
                    JsonResourceLimitKind::ArrayItems,
                    current.pointer,
                    array.size(),
                    limits.maximumArrayItems};
            }
            for (qsizetype index = array.size(); index > 0; --index) {
                const qsizetype childIndex = index - 1;
                pending.append(PendingValue{
                    array.at(childIndex),
                    current.pointer + QLatin1Char('/')
                        + QString::number(childIndex),
                    current.depth + 1});
            }
            continue;
        }

        if (!current.value.isObject()) {
            continue;
        }
        const QJsonObject object = current.value.toObject();
        if (object.size() > limits.maximumObjectMembers) {
            return JsonResourceViolation{
                JsonResourceLimitKind::ObjectMembers,
                current.pointer,
                object.size(),
                limits.maximumObjectMembers};
        }
        for (auto it = object.constEnd(); it != object.constBegin();) {
            --it;
            if (it.key().size() > limits.maximumObjectKeyCodeUnits) {
                return JsonResourceViolation{
                    JsonResourceLimitKind::ObjectKeyCodeUnits,
                    current.pointer + QLatin1Char('/')
                        + jsonPointerToken(it.key()),
                    it.key().size(),
                    limits.maximumObjectKeyCodeUnits};
            }
            pending.append(PendingValue{
                it.value(),
                current.pointer + QLatin1Char('/')
                    + jsonPointerToken(it.key()),
                current.depth + 1});
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline QString jsonResourceViolationCode(
    JsonResourceLimitKind kind) {
    switch (kind) {
    case JsonResourceLimitKind::NestingDepth:
        return QStringLiteral("depth_exceeded");
    case JsonResourceLimitKind::ArrayItems:
        return QStringLiteral("array_too_large");
    case JsonResourceLimitKind::ObjectMembers:
        return QStringLiteral("object_too_large");
    case JsonResourceLimitKind::StringCodeUnits:
        return QStringLiteral("string_too_long");
    case JsonResourceLimitKind::ObjectKeyCodeUnits:
        return QStringLiteral("object_key_too_long");
    case JsonResourceLimitKind::TotalValues:
        return QStringLiteral("value_budget_exceeded");
    }
    return QStringLiteral("resource_limit_exceeded");
}

[[nodiscard]] inline QString jsonResourceViolationMessage(
    const JsonResourceViolation& violation) {
    return QStringLiteral("JSON resource limit exceeded at %1: %2 is %3 (limit %4)")
        .arg(violation.pointer.isEmpty() ? QStringLiteral("/")
                                         : violation.pointer,
             jsonResourceViolationCode(violation.kind),
             QString::number(violation.actual),
             QString::number(violation.limit));
}

} // namespace finepaper
