// ProjectDocument defines the in-memory shape of Finepaper .fpproj files.
#pragma once

#include "project/pluginstate.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

struct ProjectIpcoreRecord {
    QString id;
    QString version;
};

struct ProjectModuleRecord {
    QString id;
    QString ipcoreId;
    QString type;
    QJsonObject parameters;
};

struct ProjectConnectionEndpoint {
    QString moduleId;
    QString portId;
};

struct ProjectConnectionRecord {
    QString id;
    ProjectConnectionEndpoint source;
    ProjectConnectionEndpoint target;
};

struct ProjectDocument {
    QString schema = QStringLiteral("v1");
    QString kind = QStringLiteral("finepaper-project");
    QString name = QStringLiteral("Untitled");
    QString version = QStringLiteral("1.0");
    QVector<ProjectIpcoreRecord> ipcores;
    QVector<ProjectIpInstanceRecord> ipcoreState;
    QVector<ProjectModuleRecord> modules;
    QVector<ProjectConnectionRecord> connections;
};
