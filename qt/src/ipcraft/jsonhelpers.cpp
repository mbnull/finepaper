#include "ipcraft/jsonhelpers.h"

#include <QJsonArray>
#include <QStringList>

namespace ipcraft {

QJsonValue sortedJsonValue(const QJsonValue& value) {
    if (value.isObject()) {
        return sortedJsonObject(value.toObject());
    }

    if (value.isArray()) {
        QJsonArray sortedArray;
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) {
            sortedArray.append(sortedJsonValue(item));
        }
        return sortedArray;
    }

    return value;
}

QJsonObject sortedJsonObject(const QJsonObject& object) {
    QStringList keys = object.keys();
    keys.sort(Qt::CaseSensitive);

    QJsonObject sorted;
    for (const QString& key : keys) {
        sorted.insert(key, sortedJsonValue(object.value(key)));
    }
    return sorted;
}

QByteArray toDeterministicJson(const QJsonObject& object,
                               QJsonDocument::JsonFormat format) {
    QByteArray bytes = QJsonDocument(sortedJsonObject(object)).toJson(format);
    while (!bytes.isEmpty() &&
           (bytes.endsWith('\n') || bytes.endsWith('\r'))) {
        bytes.chop(1);
    }
    bytes.append('\n');
    return bytes;
}

} // namespace ipcraft
