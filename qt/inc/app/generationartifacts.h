// Helpers for artifacts written alongside generated Verilog output.
#pragma once

#include <QString>

class Graph;

struct GeneratedProjectSnapshotResult {
    bool success = false;
    QString path;
    QString error;
};

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName);
