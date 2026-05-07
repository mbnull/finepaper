// Helpers for artifacts written alongside generated Verilog output.
#pragma once

#include "project/pluginstate.h"

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

QJsonArray pluginStateArray(const QVector<ProjectPluginStateRecord>& records);
void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records);

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectPluginStateRecord>& pluginStates);
