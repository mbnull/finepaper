// ProjectDocument defines the in-memory shape of Ipcraft V1 project files.
#pragma once

#include "ipcraft/diagnostics.h"
#include "ipcraft/schemaids.h"
#include "project/ipinstancestate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct ProjectInfo {
    QString id;
    QString name;
    QString description;
    QJsonObject display;
    QJsonObject metadata;
    QJsonObject native;
};

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

struct ProjectEndpointRef {
    QString instanceId;
    QString interfaceId;
    QString portId;
    QString role;
    QJsonObject properties;
};

using ProjectConnectionInterfaceRef = ProjectEndpointRef;

struct ProjectExternalPortRecord {
    QString id;
    QString name;
    bool hasInterface = false;
    ProjectEndpointRef interfaceRef;
    QJsonObject properties;
    QJsonObject native;
};

struct ProjectConnectionRecord {
    QString id;
    QString type;
    QVector<ProjectEndpointRef> endpoints;
    QString sourceKind;
    QJsonObject properties;
    QJsonObject native;

    // Transitional compile-only fields for code that has not moved to the
    // CompositionModel shape yet.
    QVector<ProjectConnectionInterfaceRef> interfaces;
    ProjectConnectionEndpoint source;
    ProjectConnectionEndpoint target;
    QString connectionClassId;
    QString status = QStringLiteral("valid");
    QStringList alternatives;
};

struct ProjectComposition {
    QVector<ProjectConnectionRecord> connections;
    QVector<ProjectExternalPortRecord> externalPorts;
    QJsonArray groups;
    QJsonObject properties;
    QJsonObject native;
};

struct ProjectMigration {
    QString fromSchema;
    QString fromVersion;
    QJsonObject preserved;
    QJsonObject metadata;
    QJsonObject native;
};

struct ProjectDocument {
    QString schema = ipcraft::schemaids::projectV1;
    QString projectId = QStringLiteral("project_0");
    QString projectName = QStringLiteral("Untitled");
    QString projectDescription;
    QJsonObject projectDisplay;
    QJsonObject projectMetadata;
    QJsonObject projectNative;
    QVector<ProjectIpInstanceRecord> instances;
    ProjectComposition composition;
    QJsonObject layout;
    ipcraft::DiagnosticStore diagnostics;
    QJsonObject artifacts;
    ProjectMigration migration;
    QJsonObject native;

    // Transitional compile-only fields for code that has not moved to V1 root
    // model integration yet. ProjectReader/ProjectWriter no longer read/write
    // these as normal runtime fields.
    QString kind;
    QString name;
    QString version = QStringLiteral("1.0");
    QVector<ProjectIpcoreRecord> ipcores;
    QVector<ProjectIpInstanceRecord> ipcoreState;
    QVector<ProjectModuleRecord> modules;
    QVector<ProjectConnectionRecord> connections;
};
