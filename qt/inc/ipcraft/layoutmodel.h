#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ipcraft {

struct LayoutView {
    QString id;
    QJsonObject canvas;
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static LayoutView fromJson(const QJsonObject& object);
};

struct LayoutModel {
    QVector<LayoutView> views;
    QJsonObject properties;
    QJsonObject native;

    QJsonObject toJson() const;
    static LayoutModel fromJson(const QJsonObject& object);
};

} // namespace ipcraft
