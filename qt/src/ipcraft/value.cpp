#include "ipcraft/value.h"

#include <limits>

namespace ipcraft {

bool isInt64Value(const QJsonValue& value) {
    if (!value.isDouble()) {
        return false;
    }

    const qint64 sentinel = (std::numeric_limits<qint64>::min)();
    const qint64 integer = value.toInteger(sentinel);
    if (integer == sentinel) {
        // Qt's JSON parser rounds values below int64 min to int64 min, so the
        // boundary value is rejected to avoid accepting underflow as valid.
        return false;
    }
    return QJsonValue(integer).toDouble() == value.toDouble();
}

ValueKind valueKind(const QJsonValue& value) {
    if (value.isUndefined()) {
        return ValueKind::Invalid;
    }
    if (value.isNull()) {
        return ValueKind::Null;
    }
    if (value.isBool()) {
        return ValueKind::Bool;
    }
    if (value.isString()) {
        return ValueKind::String;
    }
    if (value.isArray()) {
        return ValueKind::Array;
    }
    if (value.isObject()) {
        return ValueKind::Object;
    }
    if (value.isDouble()) {
        return isInt64Value(value) ? ValueKind::Int64 : ValueKind::Double;
    }
    return ValueKind::Invalid;
}

QString valueKindName(ValueKind kind) {
    switch (kind) {
    case ValueKind::Null:
        return QStringLiteral("null");
    case ValueKind::Bool:
        return QStringLiteral("bool");
    case ValueKind::Int64:
        return QStringLiteral("int64");
    case ValueKind::Double:
        return QStringLiteral("double");
    case ValueKind::String:
        return QStringLiteral("string");
    case ValueKind::Array:
        return QStringLiteral("array");
    case ValueKind::Object:
        return QStringLiteral("object");
    case ValueKind::Invalid:
        break;
    }
    return QStringLiteral("invalid");
}

} // namespace ipcraft
