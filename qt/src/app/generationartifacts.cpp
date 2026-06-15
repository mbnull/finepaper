// Writes project snapshots that make generated output directories reproducible.
#include "app/generationartifacts.h"

#include "project/projectdesignserializer.h"
#include "project/projectwriter.h"

#include <QDir>
#include <QHash>
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

QVector<ProjectIpInstanceRecord> projectInstancesFromGenerationState(
    const QVector<ProjectIpInstanceRecord>& records) {
    QVector<ProjectIpInstanceRecord> instances;
    instances.reserve(records.size());
    for (ProjectIpInstanceRecord record : records) {
        if (record.id.trimmed().isEmpty()) {
            record.id = record.instanceId;
        }
        if (record.package.id.trimmed().isEmpty()) {
            record.package.id = record.ipcoreId;
        }
        if (record.package.version.trimmed().isEmpty()) {
            record.package.version = QStringLiteral("1.0");
        }
        if (record.displayName.trimmed().isEmpty()) {
            record.displayName = record.id;
        }
        const QJsonValue globalParameters = record.state.value(QStringLiteral("global_parameters"));
        if (globalParameters.isObject() &&
            !record.config.contains(QStringLiteral("parameters"))) {
            record.config.insert(QStringLiteral("parameters"), globalParameters.toObject());
        }
        if (record.native.isEmpty() && !record.state.isEmpty()) {
            record.native = QJsonObject{{QStringLiteral("legacy_state"), record.state}};
        }
        instances.append(record);
    }
    return instances;
}

ProjectDocument documentFromDesign(const ipcraft::core::ProjectDesign* projectDesign,
                                   const QString& designName) {
    ProjectDocument document = projectDesign
        ? ProjectDesignSerializer::toDocument(*projectDesign)
        : ProjectDocument{};
    if (!projectDesign || document.projectName.trimmed().isEmpty()) {
        document.projectName = designName;
        document.name = designName;
    }
    if (!projectDesign || document.projectId.trimmed().isEmpty()) {
        document.projectId = designName;
    }
    return document;
}

QString instanceDocumentKey(const ProjectIpInstanceRecord& record) {
    const QString id = record.id.trimmed();
    return id.isEmpty() ? record.instanceId.trimmed() : id;
}

void mergeGenerationInstances(ProjectDocument& document,
                              const QVector<ProjectIpInstanceRecord>& generationRecords) {
    QHash<QString, qsizetype> instanceIndexes;
    for (qsizetype index = 0; index < document.instances.size(); ++index) {
        const QString key = instanceDocumentKey(document.instances.at(index));
        if (!key.isEmpty() && !instanceIndexes.contains(key)) {
            instanceIndexes.insert(key, index);
        }
    }

    for (const ProjectIpInstanceRecord& record : generationRecords) {
        const QString key = instanceDocumentKey(record);
        if (!key.isEmpty() && instanceIndexes.contains(key)) {
            document.instances[instanceIndexes.value(key)] = record;
            continue;
        }
        if (!key.isEmpty()) {
            instanceIndexes.insert(key, document.instances.size());
        }
        document.instances.append(record);
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

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshot(const ipcraft::core::ProjectDesign* projectDesign,
                                                             const QString& outputDirectory,
                                                             const QString& designName,
                                                             const QVector<ProjectIpInstanceRecord>& ipcoreState) {
    QDir outputDir(outputDirectory);
    const QString projectPath = outputDir.filePath(designName + QStringLiteral(".fpproj"));
    return writeGeneratedProjectSnapshotFile(projectDesign, projectPath, designName, ipcoreState);
}

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotFile(const ipcraft::core::ProjectDesign* projectDesign,
                                                                 const QString& projectPath,
                                                                 const QString& designName,
                                                                 const QVector<ProjectIpInstanceRecord>& ipcoreState) {
    ProjectDocument document = documentFromDesign(projectDesign, designName);
    const QVector<ProjectIpInstanceRecord> projectInstances =
        projectInstancesFromGenerationState(ipcoreState);
    mergeGenerationInstances(document, projectInstances);
    document.ipcoreState = projectInstances;
    addIpcoreStateDependencies(document, ipcoreState);
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(projectPath, document);
    if (!writeResult.success) {
        return {false, projectPath, writeResult.error};
    }

    return {true, projectPath, {}};
}

GeneratedProjectSnapshotResult writeGeneratedProjectSnapshotInOutputRoot(const ipcraft::core::ProjectDesign* projectDesign,
                                                                         const QString& outputRoot,
                                                                         const QString& designName,
                                                                         const QVector<ProjectIpInstanceRecord>& ipcoreState) {
    const QString projectPath = QDir(outputRoot).filePath(QStringLiteral("project-snapshot.fpproj"));
    return writeGeneratedProjectSnapshotFile(projectDesign, projectPath, designName, ipcoreState);
}
