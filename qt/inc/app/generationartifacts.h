// Helpers for artifacts written alongside generated Verilog output.
#pragma once

#include "project/ipinstancestate.h"
#include "ipcraft/core/project_design.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct GeneratedProjectSnapshotResult {
    bool success = false;
    QString path;
    QString error;
};

QJsonArray ipcoreStateArray(const QVector<ProjectIpInstanceRecord>& records);
void attachIpcoreState(QJsonObject& root,
                       const QVector<ProjectIpInstanceRecord>& records);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const ipcraft::core::ProjectDesign* projectDesign,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectIpInstanceRecord>& ipcoreState);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotFile(const ipcraft::core::ProjectDesign* projectDesign,
                                                                 const QString& projectPath,
                                                                 const QString& designName,
                                                                 const QVector<ProjectIpInstanceRecord>& ipcoreState);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotInOutputRoot(const ipcraft::core::ProjectDesign* projectDesign,
                                                                         const QString& outputRoot,
                                                                         const QString& designName,
                                                                         const QVector<ProjectIpInstanceRecord>& ipcoreState);
