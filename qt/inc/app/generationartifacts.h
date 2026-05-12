// Helpers for artifacts written alongside generated Verilog output.
#pragma once

#include "project/ipinstancestate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

class Graph;

struct GeneratedProjectSnapshotResult {
    bool success = false;
    QString path;
    QString error;
};

QJsonArray ipcoreStateArray(const QVector<ProjectIpInstanceRecord>& records);
void attachIpcoreState(QJsonObject& root,
                       const QVector<ProjectIpInstanceRecord>& records);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectIpInstanceRecord>& ipcoreState);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotFile(const Graph& graph,
                                                                 const QString& projectPath,
                                                                 const QString& designName,
                                                                 const QVector<ProjectIpInstanceRecord>& ipcoreState);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotInOutputRoot(const Graph& graph,
                                                                         const QString& outputRoot,
                                                                         const QString& designName,
                                                                         const QVector<ProjectIpInstanceRecord>& ipcoreState);
