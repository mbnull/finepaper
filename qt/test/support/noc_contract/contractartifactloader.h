#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class ContractArtifactLoader final {
public:
    static QString repositoryRoot();
    static QJsonObject loadObject(const QString &relativePath);
    static QJsonArray loadArray(const QString &relativePath);
    static QByteArray loadBytes(const QString &relativePath);
};
