// ProjectWriter serializes Finepaper project documents as stable JSON.
#include "project/projectwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {

QJsonObject sortedObject(const QJsonObject& object) {
    QStringList keys = object.keys();
    keys.sort();

    QJsonObject sorted;
    for (const QString& key : keys) {
        sorted.insert(key, object.value(key));
    }
    return sorted;
}

QJsonObject interfaceObject(const ProjectConnectionInterfaceRef& interfaceRef) {
    QJsonObject object;
    object.insert(QStringLiteral("instance"), interfaceRef.instanceId);
    object.insert(QStringLiteral("interface"), interfaceRef.interfaceId);
    return object;
}

QVector<ProjectConnectionInterfaceRef> writableInterfaces(const ProjectConnectionRecord& connection) {
    if (!connection.interfaces.isEmpty()) {
        return connection.interfaces;
    }

    QVector<ProjectConnectionInterfaceRef> interfaces;
    if (!connection.source.moduleId.isEmpty() || !connection.source.portId.isEmpty()) {
        interfaces.push_back(ProjectConnectionInterfaceRef{
            connection.source.moduleId,
            connection.source.portId
        });
    }
    if (!connection.target.moduleId.isEmpty() || !connection.target.portId.isEmpty()) {
        interfaces.push_back(ProjectConnectionInterfaceRef{
            connection.target.moduleId,
            connection.target.portId
        });
    }
    return interfaces;
}

QJsonObject toJson(const ProjectDocument& document) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), document.schema);
    root.insert(QStringLiteral("kind"), document.kind);

    QJsonObject project;
    project.insert(QStringLiteral("name"), document.name);
    project.insert(QStringLiteral("version"), document.version);
    root.insert(QStringLiteral("project"), project);

    QJsonArray ipcores;
    QVector<ProjectIpcoreRecord> sortedIpcoreRecords = document.ipcores;
    std::sort(sortedIpcoreRecords.begin(), sortedIpcoreRecords.end(), [](const ProjectIpcoreRecord& lhs,
                                                                         const ProjectIpcoreRecord& rhs) {
        return lhs.id < rhs.id;
    });
    for (const ProjectIpcoreRecord& ipcore : sortedIpcoreRecords) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), ipcore.id);
        object.insert(QStringLiteral("version"), ipcore.version);
        ipcores.append(object);
    }
    root.insert(QStringLiteral("ipcores"), ipcores);

    QJsonArray ipcoreState;
    for (const ProjectIpInstanceRecord& state : document.ipcoreState) {
        QJsonObject object;
        object.insert(QStringLiteral("ipcore"), state.ipcoreId);
        object.insert(QStringLiteral("instance"), state.instanceId);
        object.insert(QStringLiteral("schema"), state.schema);
        object.insert(QStringLiteral("state"), sortedObject(state.state));
        ipcoreState.append(object);
    }
    root.insert(QStringLiteral("ipcore_state"), ipcoreState);

    QJsonObject graph;
    QJsonArray modules;
    for (const ProjectModuleRecord& module : document.modules) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), module.id);
        object.insert(QStringLiteral("ipcore"), module.ipcoreId);
        object.insert(QStringLiteral("instance"), module.instanceId);
        object.insert(QStringLiteral("type"), module.type);
        object.insert(QStringLiteral("parameters"), sortedObject(module.parameters));
        modules.append(object);
    }
    graph.insert(QStringLiteral("modules"), modules);

    QJsonArray connections;
    for (const ProjectConnectionRecord& connection : document.connections) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), connection.id);
        object.insert(QStringLiteral("class"), connection.connectionClassId);
        QJsonArray interfaces;
        for (const ProjectConnectionInterfaceRef& interfaceRef : writableInterfaces(connection)) {
            interfaces.append(interfaceObject(interfaceRef));
        }
        object.insert(QStringLiteral("interfaces"), interfaces);
        object.insert(QStringLiteral("status"),
                      connection.status.isEmpty() ? QStringLiteral("valid") : connection.status);
        if (!connection.alternatives.isEmpty()) {
            QJsonArray alternatives;
            for (const QString& alternative : connection.alternatives) {
                alternatives.append(alternative);
            }
            object.insert(QStringLiteral("alternatives"), alternatives);
        }
        connections.append(object);
    }
    graph.insert(QStringLiteral("connections"), connections);
    root.insert(QStringLiteral("graph"), graph);

    return root;
}

} // namespace

ProjectWriteResult ProjectWriter::writeFile(const QString& path, const ProjectDocument& document) {
    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
        return {false, QStringLiteral("Could not create project directory: %1").arg(fileInfo.absolutePath())};
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("Could not open project file for writing: %1").arg(path)};
    }

    const QJsonDocument json(toJson(document));
    const QByteArray content = json.toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size()) {
        return {false, QStringLiteral("Could not write project file: %1").arg(path)};
    }
    if (!file.commit()) {
        return {false, QStringLiteral("Could not commit project file: %1").arg(path)};
    }

    return {true, {}};
}
