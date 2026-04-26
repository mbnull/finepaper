// ProjectDocument defines the in-memory shape of Finepaper .fpproj files.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct ProjectPluginRecord {
    QString id;
    QString version;
};

struct ProjectModuleRecord {
    QString id;
    QString pluginId;
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
    QVector<ProjectPluginRecord> plugins;
    QVector<ProjectModuleRecord> modules;
    QVector<ProjectConnectionRecord> connections;
};
