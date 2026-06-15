#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct ExtensionContribution {
    QString id;
    QString extensionPoint;
    QString ownerPluginId;
    QString label;
    QJsonObject descriptor;
};

class ExtensionPointRegistry {
public:
    static constexpr int kMaxContributions = 1024;

    bool registerContribution(const ExtensionContribution& contribution);
    QVector<ExtensionContribution> contributions(const QString& extensionPoint) const;
    QVector<ExtensionContribution> allContributions() const;

private:
    QHash<QString, ExtensionContribution> m_byId;
    QStringList m_contributionOrder;
};
