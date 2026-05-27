#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace ipcraft {

QJsonObject sortedJsonObject(const QJsonObject& object);
QJsonValue sortedJsonValue(const QJsonValue& value);

QByteArray toDeterministicJson(
    const QJsonObject& object,
    QJsonDocument::JsonFormat format = QJsonDocument::Indented);

} // namespace ipcraft
