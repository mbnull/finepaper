// ProjectDocument defines the in-memory shape of Finepaper .fpproj files.
#pragma once

#include "project/ipinstancestate.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct ProjectIpcoreRecord {
    QString id;
    QString version;
};

struct ProjectModuleRecord {
    QString id;
    QString ipcoreId;
    QString instanceId;
    QString type;
    QJsonObject parameters;
};

struct ProjectConnectionEndpoint {
    QString moduleId;
    QString portId;
};

struct ProjectConnectionInterfaceRef {
    QString instanceId;
    QString interfaceId;
};

struct ProjectConnectionRecord {
    QString id;
    // Legacy port endpoints remain internal for the Task 6 migration path.
    ProjectConnectionEndpoint source;
    ProjectConnectionEndpoint target;
    QString connectionClassId;
    QVector<ProjectConnectionInterfaceRef> interfaces;
    QString status = QStringLiteral("valid");
    QStringList alternatives;
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
