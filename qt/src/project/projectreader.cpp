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

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
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

    if (root.contains(QStringLiteral("plugins"))) {
        return failure(QStringLiteral("Project plugins is a pre-v1 field and is not supported"));
    }
    if (root.contains(QStringLiteral("plugin_state"))) {
        return failure(QStringLiteral("Project plugin_state is a pre-v1 field and is not supported"));
    }
    if (root.contains(QStringLiteral("ip_instances"))) {
        return failure(QStringLiteral("Project ip_instances is a pre-v1 field and is not supported"));
    }

    const QJsonArray ipcores = root.value(QStringLiteral("ipcores")).toArray();
    for (const QJsonValue& value : ipcores) {
        const QJsonObject object = value.toObject();
        document.ipcores.push_back(ProjectIpcoreRecord{
            object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("version")).toString()
        });
    }

    const QJsonValue ipcoreStateValue = root.value(QStringLiteral("ipcore_state"));
    if (!ipcoreStateValue.isUndefined() && !ipcoreStateValue.isArray()) {
        return failure(QStringLiteral("Project ipcore_state must be an array"));
    }
    const QJsonArray ipcoreState = ipcoreStateValue.toArray();
    for (const QJsonValue& value : ipcoreState) {
        if (!value.isObject()) {
            return failure(QStringLiteral("Project ipcore_state entries must be objects"));
        }
        const QJsonObject object = value.toObject();
        const QJsonValue stateValue = object.value(QStringLiteral("state"));
        if (!stateValue.isObject()) {
            return failure(QStringLiteral("Project ipcore_state.state must be an object"));
        }
        ProjectIpInstanceRecord state;
        state.ipcoreId = object.value(QStringLiteral("ipcore")).toString();
        state.instanceId = object.value(QStringLiteral("instance")).toString();
        state.schema = object.value(QStringLiteral("schema")).toString();
        state.state = stateValue.toObject();
        document.ipcoreState.push_back(state);
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
        if (object.contains(QStringLiteral("plugin"))) {
            return failure(QStringLiteral("Project graph.modules.plugin is a pre-v1 field and is not supported"));
        }
        ProjectModuleRecord module;
        module.id = object.value(QStringLiteral("id")).toString();
        module.ipcoreId = object.value(QStringLiteral("ipcore")).toString();
        module.instanceId = object.value(QStringLiteral("instance")).toString();
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
        if (!value.isObject()) {
            return failure(QStringLiteral("Project graph.connections entries must be objects"));
        }
        const QJsonObject object = value.toObject();
        ProjectConnectionRecord connection;
        connection.id = object.value(QStringLiteral("id")).toString();
        const QJsonValue interfacesValue = object.value(QStringLiteral("interfaces"));
        if (!interfacesValue.isUndefined()) {
            if (!interfacesValue.isArray()) {
                return failure(QStringLiteral("Project connection %1 interfaces must be an array")
                                   .arg(connection.id));
            }
            connection.connectionClassId = object.value(QStringLiteral("class")).toString();
            connection.status = object.value(QStringLiteral("status")).toString(QStringLiteral("valid"));
            const QJsonArray interfaces = interfacesValue.toArray();
            if (interfaces.size() != 2) {
                return failure(QStringLiteral("Project connection %1 interfaces must contain exactly two participants")
                                   .arg(connection.id));
            }
            for (const QJsonValue& participantValue : interfaces) {
                if (!participantValue.isObject()) {
                    return failure(QStringLiteral("Project connection %1 interface participants must be objects")
                                       .arg(connection.id));
                }
                const QJsonObject participant = participantValue.toObject();
                if (!isNonEmptyString(participant.value(QStringLiteral("instance"))) ||
                    !isNonEmptyString(participant.value(QStringLiteral("interface")))) {
                    return failure(QStringLiteral("Project connection %1 interface participant must include instance and interface")
                                       .arg(connection.id));
                }
                connection.interfaces.push_back(ProjectConnectionInterfaceRef{
                    participant.value(QStringLiteral("instance")).toString(),
                    participant.value(QStringLiteral("interface")).toString()
                });
            }

            const QJsonValue alternativesValue = object.value(QStringLiteral("alternatives"));
            if (!alternativesValue.isUndefined()) {
                if (!alternativesValue.isArray()) {
                    return failure(QStringLiteral("Project connection %1 alternatives must be an array")
                                       .arg(connection.id));
                }
                for (const QJsonValue& alternative : alternativesValue.toArray()) {
                    if (!alternative.isString()) {
                        return failure(QStringLiteral("Project connection %1 alternatives must contain strings")
                                           .arg(connection.id));
                    }
                    connection.alternatives.append(alternative.toString());
                }
            }
        } else {
            connection.source = endpointFromObject(object.value(QStringLiteral("source")).toObject());
            connection.target = endpointFromObject(object.value(QStringLiteral("target")).toObject());
        }
        document.connections.push_back(connection);
    }

    ProjectReadResult result;
    result.success = true;
    result.document = document;
    return result;
}
