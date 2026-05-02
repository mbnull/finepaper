#pragma once

#include "plugins/plugindescriptor.h"

#include <QHash>
#include <QString>
#include <QStringList>

class Graph;
class ModuleRegistry;

struct TopologyPresetRequest {
    QString pluginId;
    TopologyPresetDescriptor preset;
    QHash<QString, int> parameters;
};

struct TopologyPresetResult {
    bool success = false;
    QString error;
    QStringList moduleIds;
    QStringList connectionIds;
};

class TopologyPresetBuilder {
public:
    static TopologyPresetResult apply(Graph* graph,
                                      const ModuleRegistry& registry,
                                      const TopologyPresetRequest& request);
};
