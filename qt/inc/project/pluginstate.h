// Shared project plugin state records.
#pragma once

#include <QJsonObject>
#include <QString>

struct ProjectPluginStateRecord {
    QString pluginId;
    QString instanceId;
    QString schema;
    QJsonObject state;
};
