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
QJsonObject legacyIpInstanceObject(const QVector<ProjectPluginStateRecord>& records,
                                   const QString& pluginId = QString());
void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records,
                       const QString& legacyPluginId = QString());

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectPluginStateRecord>& pluginStates);
