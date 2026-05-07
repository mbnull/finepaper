// ProjectWriter serializes Finepaper project documents as stable JSON.
#include "project/projectwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QJsonObject endpointObject(const ProjectConnectionEndpoint& endpoint) {
    QJsonObject object;
    object.insert(QStringLiteral("module"), endpoint.moduleId);
    object.insert(QStringLiteral("port"), endpoint.portId);
    return object;
}

QJsonObject toJson(const ProjectDocument& document) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), document.schema);
    root.insert(QStringLiteral("kind"), document.kind);

    QJsonObject project;
    project.insert(QStringLiteral("name"), document.name);
    project.insert(QStringLiteral("version"), document.version);
    root.insert(QStringLiteral("project"), project);

    QJsonArray plugins;
    QVector<ProjectPluginRecord> sortedPlugins = document.plugins;
    std::sort(sortedPlugins.begin(), sortedPlugins.end(), [](const ProjectPluginRecord& lhs,
                                                             const ProjectPluginRecord& rhs) {
        return lhs.id < rhs.id;
    });
    for (const ProjectPluginRecord& plugin : sortedPlugins) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), plugin.id);
        object.insert(QStringLiteral("version"), plugin.version);
        plugins.append(object);
    }
    root.insert(QStringLiteral("plugins"), plugins);

    QJsonArray pluginStates;
    for (const ProjectPluginStateRecord& state : document.pluginStates) {
        QJsonObject object;
        object.insert(QStringLiteral("plugin"), state.pluginId);
        object.insert(QStringLiteral("instance"), state.instanceId);
        object.insert(QStringLiteral("schema"), state.schema);
        object.insert(QStringLiteral("state"), sortedObject(state.state));
        pluginStates.append(object);
    }
    root.insert(QStringLiteral("plugin_state"), pluginStates);

    QJsonObject graph;
    QJsonArray modules;
    for (const ProjectModuleRecord& module : document.modules) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), module.id);
        object.insert(QStringLiteral("plugin"), module.pluginId);
        object.insert(QStringLiteral("type"), module.type);
        object.insert(QStringLiteral("parameters"), sortedObject(module.parameters));
        modules.append(object);
    }
    graph.insert(QStringLiteral("modules"), modules);

    QJsonArray connections;
    for (const ProjectConnectionRecord& connection : document.connections) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), connection.id);
        object.insert(QStringLiteral("source"), endpointObject(connection.source));
        object.insert(QStringLiteral("target"), endpointObject(connection.target));
        connections.append(object);
    }
    graph.insert(QStringLiteral("connections"), connections);
    root.insert(QStringLiteral("graph"), graph);

    return root;
}

} // namespace

ProjectWriteResult ProjectWriter::writeFile(const QString& path, const ProjectDocument& document) {
    QFile file(path);
    const QFileInfo fileInfo(path);
    if (!fileInfo.absoluteDir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
        return {false, QStringLiteral("Could not create project directory: %1").arg(fileInfo.absolutePath())};
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("Could not open project file for writing: %1").arg(path)};
    }

    const QJsonDocument json(toJson(document));
    if (file.write(json.toJson(QJsonDocument::Indented)) < 0) {
        return {false, QStringLiteral("Could not write project file: %1").arg(path)};
    }

    return {true, {}};
}
