// Writes project snapshots that make generated output directories reproducible.
#include "app/generationartifacts.h"

#include "graph/graph.h"
#include "project/graphprojectserializer.h"
#include "project/projectwriter.h"

#include <QDir>

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

QJsonObject legacyIpInstanceObject(const QVector<ProjectPluginStateRecord>& records,
                                   const QString& pluginId) {
    for (const ProjectPluginStateRecord& record : records) {
        if (!pluginId.isEmpty() && record.pluginId != pluginId) {
            continue;
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), record.instanceId);
        object.insert(QStringLiteral("plugin"), record.pluginId);
        const QJsonValue kind = record.state.value(QStringLiteral("kind"));
        if (kind.isString()) {
            object.insert(QStringLiteral("kind"), kind);
        }
        const QJsonValue type = record.state.value(QStringLiteral("type"));
        if (type.isString()) {
            object.insert(QStringLiteral("type"), type);
        }
        object.insert(QStringLiteral("parameters"),
                      record.state.value(QStringLiteral("global_parameters")).toObject());
        return object;
    }

    return {};
}

void attachPluginState(QJsonObject& root,
                       const QVector<ProjectPluginStateRecord>& records,
                       const QString& legacyPluginId) {
    root.insert(QStringLiteral("plugin_state"), pluginStateArray(records));

    const QJsonObject legacyIpInstance = legacyIpInstanceObject(records, legacyPluginId);
    if (!legacyIpInstance.isEmpty()) {
        root.insert(QStringLiteral("ip_instance"), legacyIpInstance);
    }
}

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectPluginStateRecord>& pluginStates) {
    QDir outputDir(outputDirectory);
    const QString projectPath = outputDir.filePath(designName + QStringLiteral(".fpproj"));
    ProjectDocument document = GraphProjectSerializer::toProject(graph, designName);
    document.pluginStates = pluginStates;
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    if (!writeResult.success) {
        return {false, projectPath, writeResult.error};
    }

    return {true, projectPath, {}};
}
