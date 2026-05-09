// Writes project snapshots that make generated output directories reproducible.
#include "app/generationartifacts.h"

#include "graph/graph.h"
#include "project/graphprojectserializer.h"
#include "project/projectwriter.h"

#include <QDir>
#include <QSet>

namespace {

void addIpcoreStateDependencies(ProjectDocument& document,
                                const QVector<ProjectIpInstanceRecord>& records) {
    QSet<QString> ipcoreIds;
    for (const ProjectIpcoreRecord& ipcore : document.ipcores) {
        ipcoreIds.insert(ipcore.id);
    }
    for (const ProjectIpInstanceRecord& state : records) {
        if (state.ipcoreId.isEmpty() || ipcoreIds.contains(state.ipcoreId)) {
            continue;
        }
        document.ipcores.push_back(ProjectIpcoreRecord{state.ipcoreId, QStringLiteral("1.0")});
        ipcoreIds.insert(state.ipcoreId);
    }
}

} // namespace

QJsonArray ipcoreStateArray(const QVector<ProjectIpInstanceRecord>& records) {
    QJsonArray array;
    for (const ProjectIpInstanceRecord& record : records) {
        QJsonObject object;
        object.insert(QStringLiteral("ipcore"), record.ipcoreId);
        object.insert(QStringLiteral("instance"), record.instanceId);
        object.insert(QStringLiteral("schema"), record.schema);
        object.insert(QStringLiteral("state"), record.state);
        array.append(object);
    }
    return array;
}

void attachIpcoreState(QJsonObject& root,
                       const QVector<ProjectIpInstanceRecord>& records) {
    root.insert(QStringLiteral("ipcore_state"), ipcoreStateArray(records));
}

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const Graph& graph,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectIpInstanceRecord>& ipcoreState) {
    QDir outputDir(outputDirectory);
    const QString projectPath = outputDir.filePath(designName + QStringLiteral(".fpproj"));
    ProjectDocument document = GraphProjectSerializer::toProject(graph, designName);
    document.ipcoreState = ipcoreState;
    addIpcoreStateDependencies(document, ipcoreState);
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    if (!writeResult.success) {
        return {false, projectPath, writeResult.error};
    }

    return {true, projectPath, {}};
}
