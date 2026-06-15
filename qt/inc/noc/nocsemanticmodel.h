#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

class NoCSemanticModel {
public:
    static NoCSemanticModel fromJson(const QJsonObject& descriptor);

    QString semanticRoleForModule(const QString& moduleId) const;
    bool isEmpty() const;

private:
    QHash<QString, QString> m_rolesByModule;
};
