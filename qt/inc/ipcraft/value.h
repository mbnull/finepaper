#pragma once

#include <QJsonValue>
#include <QString>

namespace ipcraft {

enum class ValueKind {
    Null,
    Bool,
    Int64,
    Double,
    String,
    Array,
    Object,
    Invalid
};

ValueKind valueKind(const QJsonValue& value);
bool isInt64Value(const QJsonValue& value);
QString valueKindName(ValueKind kind);

} // namespace ipcraft
