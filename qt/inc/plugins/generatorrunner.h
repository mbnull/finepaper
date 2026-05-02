// GeneratorRunner selects and builds plugin generator process commands.
#pragma once

#include "plugins/plugindescriptor.h"

#include <QList>
#include <QString>
#include <QStringList>

class Graph;

struct GeneratorCommand {
    bool valid = false;
    QString errorMessage;
    QString pluginId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("legacy_noc_json");
    QStringList arguments;
};

class GeneratorRunner {
public:
    static GeneratorCommand resolveForGraph(const Graph* graph,
                                            const QString& inputPath,
                                            const QString& outputDirectory);
    static GeneratorCommand resolveForGraph(const Graph* graph,
                                            const QList<PluginDescriptor>& plugins,
                                            const QString& inputPath,
                                            const QString& outputDirectory);
    static GeneratorCommand resolveDrcForGraph(const Graph* graph,
                                               const QString& inputPath,
                                               const QString& outputDirectory);
    static GeneratorCommand resolveDrcForGraph(const Graph* graph,
                                               const QList<PluginDescriptor>& plugins,
                                               const QString& inputPath,
                                               const QString& outputDirectory);
};
