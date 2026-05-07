// ProjectReader parses Finepaper project JSON into ProjectDocument records.
#include "project/projectreader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

ProjectReadResult failure(const QString& error) {
    ProjectReadResult result;
    result.error = error;
    return result;
}

ProjectConnectionEndpoint endpointFromObject(const QJsonObject& object) {
    ProjectConnectionEndpoint endpoint;
    endpoint.moduleId = object.value(QStringLiteral("module")).toString();
    endpoint.portId = object.value(QStringLiteral("port")).toString();
    return endpoint;
}

} // namespace

ProjectFileKind ProjectReader::detectKind(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ProjectFileKind::Unknown;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return ProjectFileKind::Unknown;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("kind")).toString() == QStringLiteral("finepaper-project")) {
        return ProjectFileKind::Project;
    }
    if (root.contains(QStringLiteral("xps")) ||
        root.contains(QStringLiteral("endpoints")) ||
        root.contains(QStringLiteral("connections"))) {
        return ProjectFileKind::LegacyJson;
    }

    return ProjectFileKind::Unknown;
}

ProjectReadResult ProjectReader::readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(QStringLiteral("Could not open project file: %1").arg(path));
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return failure(QStringLiteral("Invalid project JSON: %1").arg(parseError.errorString()));
    }

    const QJsonObject root = json.object();
    ProjectDocument document;
    document.schema = root.value(QStringLiteral("schema")).toString();
    document.kind = root.value(QStringLiteral("kind")).toString();
    if (document.schema != QStringLiteral("v1")) {
        return failure(QStringLiteral("Unsupported project schema: %1").arg(document.schema));
    }
    if (document.kind != QStringLiteral("finepaper-project")) {
        return failure(QStringLiteral("Unsupported project kind: %1").arg(document.kind));
    }

    const QJsonObject project = root.value(QStringLiteral("project")).toObject();
    document.name = project.value(QStringLiteral("name")).toString(QStringLiteral("Untitled"));
    document.version = project.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));

    const QJsonArray plugins = root.value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue& value : plugins) {
        const QJsonObject object = value.toObject();
        document.plugins.push_back(ProjectPluginRecord{
            object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("version")).toString()
        });
    }

    const QJsonValue pluginStateValue = root.value(QStringLiteral("plugin_state"));
    if (!pluginStateValue.isUndefined() && !pluginStateValue.isArray()) {
        return failure(QStringLiteral("Project plugin_state must be an array"));
    }
    const QJsonArray pluginStates = pluginStateValue.toArray();
    for (const QJsonValue& value : pluginStates) {
        if (!value.isObject()) {
            return failure(QStringLiteral("Project plugin_state entries must be objects"));
        }
        const QJsonObject object = value.toObject();
        const QJsonValue stateValue = object.value(QStringLiteral("state"));
        if (!stateValue.isObject()) {
            return failure(QStringLiteral("Project plugin_state.state must be an object"));
        }
        ProjectPluginStateRecord state;
        state.pluginId = object.value(QStringLiteral("plugin")).toString();
        state.instanceId = object.value(QStringLiteral("instance")).toString();
        state.schema = object.value(QStringLiteral("schema")).toString();
        state.state = stateValue.toObject();
        document.pluginStates.push_back(state);
    }

    if (root.contains(QStringLiteral("ip_instances"))) {
        return failure(QStringLiteral("Project ip_instances is a pre-v1 field and is not supported"));
    }

    const QJsonValue graphValue = root.value(QStringLiteral("graph"));
    if (!graphValue.isObject()) {
        return failure(QStringLiteral("Project graph must be an object"));
    }

    const QJsonObject graph = graphValue.toObject();
    const QJsonValue modulesValue = graph.value(QStringLiteral("modules"));
    if (!modulesValue.isArray()) {
        return failure(QStringLiteral("Project graph.modules must be an array"));
    }

    const QJsonArray modules = modulesValue.toArray();
    for (const QJsonValue& value : modules) {
        const QJsonObject object = value.toObject();
        ProjectModuleRecord module;
        module.id = object.value(QStringLiteral("id")).toString();
        module.pluginId = object.value(QStringLiteral("plugin")).toString();
        module.type = object.value(QStringLiteral("type")).toString();
        module.parameters = object.value(QStringLiteral("parameters")).toObject();
        document.modules.push_back(module);
    }

    const QJsonValue connectionsValue = graph.value(QStringLiteral("connections"));
    if (!connectionsValue.isArray()) {
        return failure(QStringLiteral("Project graph.connections must be an array"));
    }

    const QJsonArray connections = connectionsValue.toArray();
    for (const QJsonValue& value : connections) {
        const QJsonObject object = value.toObject();
        ProjectConnectionRecord connection;
        connection.id = object.value(QStringLiteral("id")).toString();
        connection.source = endpointFromObject(object.value(QStringLiteral("source")).toObject());
        connection.target = endpointFromObject(object.value(QStringLiteral("target")).toObject());
        document.connections.push_back(connection);
    }

    ProjectReadResult result;
    result.success = true;
    result.document = document;
    return result;
}
