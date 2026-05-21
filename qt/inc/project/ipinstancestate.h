// Shared project IP instance state records.
#pragma once

#include <QJsonObject>
#include <QString>

struct ProjectPackageRef {
    QString id;
    QString version;
};

struct ProjectIpInstanceRecord {
    QString id;
    QString displayName;
    ProjectPackageRef package;
    QJsonObject config;
    bool hasGraphConfig = false;
    bool graphConfigIsNull = false;
    QJsonObject graphConfig;
    QJsonObject native;
    QJsonObject lastRuns;
    QJsonObject artifacts;
    QJsonObject diagnostics;
    QJsonObject view;

    // Transitional compile-only aliases for code that has not moved to
    // ProjectDocument.instances yet. ProjectReader/ProjectWriter no longer use
    // the legacy ipcore_state schema as a normal runtime path.
    QString ipcoreId;
    QString instanceId;
    QString schema;
    QJsonObject state;
};
