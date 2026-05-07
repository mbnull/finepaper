// Writes project snapshots that make generated output directories reproducible.
#include "app/generationartifacts.h"

#include "graph/graph.h"
#include "project/graphprojectserializer.h"
#include "project/projectwriter.h"

#include <QDir>
#include <QSet>

namespace {

void addPluginStateDependencies(ProjectDocument& document,
                                const QVector<ProjectPluginStateRecord>& pluginStates) {
    QSet<QString> pluginIds;
    for (const ProjectPluginRecord& plugin : document.plugins) {
        pluginIds.insert(plugin.id);
    }
    for (const ProjectPluginStateRecord& state : pluginStates) {
        if (state.pluginId.isEmpty() || pluginIds.contains(state.pluginId)) {
            continue;
        }
        document.plugins.push_back(ProjectPluginRecord{state.pluginId, QStringLiteral("1.0")});
        pluginIds.insert(state.pluginId);
    }
}

} // namespace

QJsonArray pluginStateArray(const QVector<ProjectPluginStateRecord>& records) {
    QJsonArray array;
    for (const ProjectPluginStateRecord& record : records) {
        QJsonObject object;
        object.insert(QStringLiteral("plugin"), record.pluginId);
        object.insert(QStringLiteral("instance"), record.instanceId);
        object.insert(QStringLiteral("schema"), record.schema);
        object.insert(QStringLiteral("state"), record.state);
        array.append(object);
    }
    return array;
}

void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records) {
    root.insert(QStringLiteral("plugin_state"), pluginStateArray(records));
}

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectPluginStateRecord>& pluginStates) {
    QDir outputDir(outputDirectory);
    const QString projectPath = outputDir.filePath(designName + QStringLiteral(".fpproj"));
    ProjectDocument document = GraphProjectSerializer::toProject(graph, designName);
    document.pluginStates = pluginStates;
    addPluginStateDependencies(document, pluginStates);
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    if (!writeResult.success) {
        return {false, projectPath, writeResult.error};
    }

    return {true, projectPath, {}};
}
