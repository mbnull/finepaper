// Shared project IP instance state records.
#pragma once

#include <QJsonObject>
#include <QString>

struct ProjectIpInstanceRecord {
    QString ipcoreId;
    QString instanceId;
    QString schema;
    QJsonObject state;
};
