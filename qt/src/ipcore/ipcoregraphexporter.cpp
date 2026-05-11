// IpCoreGraphExporter implementation.
#include "ipcore/ipcoregraphexporter.h"

#include "graph/graph.h"
#include "modules/modulelabels.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <variant>

namespace {

QJsonValue parameterToJson(const Parameter::Value& value) {
    if (std::holds_alternative<QString>(value)) return QJsonValue(std::get<QString>(value));
    if (std::holds_alternative<int>(value)) return QJsonValue(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return QJsonValue(std::get<double>(value));
    if (std::holds_alternative<bool>(value)) return QJsonValue(std::get<bool>(value));
    return QJsonValue();
}

QString directionToJsonString(Port::Direction direction) {
    if (direction == Port::Direction::Input) return QStringLiteral("input");
    if (direction == Port::Direction::Output) return QStringLiteral("output");
    return QStringLiteral("inout");
}

QJsonObject portToIpcoreJson(const Port& port) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), port.id());
    object.insert(QStringLiteral("direction"), directionToJsonString(port.direction()));
    object.insert(QStringLiteral("type"), port.type());
    object.insert(QStringLiteral("name"), port.name());
    if (!port.description().isEmpty()) object.insert(QStringLiteral("description"), port.description());
    if (!port.role().isEmpty()) object.insert(QStringLiteral("role"), port.role());
    if (!port.busType().isEmpty()) object.insert(QStringLiteral("bus_type"), port.busType());
    if (!port.interfaceId().isEmpty()) object.insert(QStringLiteral("interface"), port.interfaceId());
    return object;
}

QJsonObject parametersToIpcoreJson(const Module* module) {
    QJsonObject parameters;
    if (!module) return parameters;
    for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
        parameters.insert(it.key(), parameterToJson(it.value().value()));
    }
    return parameters;
}

QJsonObject ipcoreStateObject(const ProjectIpInstanceRecord& record) {
    QJsonObject object;
    object.insert(QStringLiteral("ipcore"), record.ipcoreId);
    object.insert(QStringLiteral("instance"), record.instanceId);
    object.insert(QStringLiteral("schema"), record.schema);
    object.insert(QStringLiteral("state"), record.state);
    return object;
}

QString safeArtifactToken(QString token, const QString& defaultToken) {
    token = token.trimmed();
    if (token.isEmpty()) token = defaultToken.trimmed();
    token.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_$]+")), QStringLiteral("_"));
    token.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    token = token.trimmed();
    while (token.startsWith(QStringLiteral("_"))) token.remove(0, 1);
    while (token.endsWith(QStringLiteral("_"))) token.chop(1);
    if (token.isEmpty()) token = QStringLiteral("module");
    if (!token.front().isLetter() && token.front() != QLatin1Char('_')) {
        token.prepend(QStringLiteral("m_"));
    }
    return token;
}

QString uniqueArtifactToken(const QString& token, QSet<QString>& usedTokens) {
    QString candidate = token;
    int suffix = 1;
    while (usedTokens.contains(candidate)) {
        candidate = QStringLiteral("%1_%2").arg(token).arg(suffix++);
    }
    usedTokens.insert(candidate);
    return candidate;
}

QString moduleArtifactId(const Module* module, QSet<QString>& usedModuleIds) {
    const QString defaultToken = module ? module->type().toLower() : QStringLiteral("module");
    return uniqueArtifactToken(safeArtifactToken(ModuleLabels::externalId(module), defaultToken), usedModuleIds);
}

QString connectionArtifactId(const QString& sourceModuleId,
                             const QString& sourcePortId,
                             const QString& targetModuleId,
                             const QString& targetPortId,
                             QSet<QString>& usedConnectionIds) {
    const QString raw = QStringLiteral("%1_%2_to_%3_%4")
                            .arg(sourceModuleId, sourcePortId, targetModuleId, targetPortId);
    return uniqueArtifactToken(safeArtifactToken(raw, QStringLiteral("connection")), usedConnectionIds);
}

} // namespace

QString IpCoreGraphExporter::schemaName() {
    return QStringLiteral("finepaper-ipcore-graph-v1");
}

IpCoreGraphExportResult IpCoreGraphExporter::exportGraph(const IpCoreGraphExportRequest& request) {
    if (!request.graph) {
        return {false, {}, QStringLiteral("Graph is not available.")};
    }
    if (request.ipcore.id.trimmed().isEmpty()) {
        return {false, {}, QStringLiteral("Selected IP core is required.")};
    }
    if (request.instance.ipcoreId != request.ipcore.id || request.instance.instanceId.trimmed().isEmpty()) {
        return {false, {},
                QStringLiteral("Selected IP instance does not match IP core '%1'.").arg(request.ipcore.id)};
    }
    if (request.externalToInternalIds) {
        request.externalToInternalIds->clear();
    }

    QJsonArray modules;
    QJsonArray connections;
    QHash<QString, QString> runtimeToArtifactIds;
    QSet<QString> usedModuleIds;

    for (const auto& module : request.graph->modules()) {
        if (module->ipcoreId() != request.ipcore.id ||
            module->instanceId() != request.instance.instanceId) {
            continue;
        }

        const QString artifactId = moduleArtifactId(module.get(), usedModuleIds);
        runtimeToArtifactIds.insert(module->id(), artifactId);
        if (request.externalToInternalIds) {
            request.externalToInternalIds->insert(artifactId, module->id());
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), artifactId);
        object.insert(QStringLiteral("ipcore"), request.ipcore.id);
        object.insert(QStringLiteral("instance"), request.instance.instanceId);
        object.insert(QStringLiteral("type"), module->type());
        object.insert(QStringLiteral("parameters"), parametersToIpcoreJson(module.get()));

        QJsonArray ports;
        for (const Port& port : module->ports()) {
            ports.append(portToIpcoreJson(port));
        }
        object.insert(QStringLiteral("ports"), ports);
        modules.append(object);
    }

    QSet<QString> usedConnectionIds;
    for (const auto& connection : request.graph->connections()) {
        const bool sourceSelected = runtimeToArtifactIds.contains(connection->source().moduleId);
        const bool targetSelected = runtimeToArtifactIds.contains(connection->target().moduleId);
        if (!sourceSelected && !targetSelected) {
            continue;
        }
        if (sourceSelected != targetSelected) {
            return {false, {},
                    QStringLiteral("Connection '%1' crosses selected instance '%2'.")
                        .arg(connection->id(), request.instance.instanceId)};
        }
        const QString sourceModuleId = runtimeToArtifactIds.value(connection->source().moduleId);
        const QString targetModuleId = runtimeToArtifactIds.value(connection->target().moduleId);

        QJsonObject object;
        object.insert(QStringLiteral("id"),
                      connectionArtifactId(sourceModuleId,
                                           connection->source().portId,
                                           targetModuleId,
                                           connection->target().portId,
                                           usedConnectionIds));
        object.insert(QStringLiteral("source"), QJsonObject{
            {QStringLiteral("module"), sourceModuleId},
            {QStringLiteral("port"), connection->source().portId}
        });
        object.insert(QStringLiteral("target"), QJsonObject{
            {QStringLiteral("module"), targetModuleId},
            {QStringLiteral("port"), connection->target().portId}
        });
        connections.append(object);
    }

    QJsonArray ipcoreState;
    ipcoreState.append(ipcoreStateObject(request.instance));

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("name"), request.designName.trimmed().isEmpty()
                                       ? QStringLiteral("design")
                                       : request.designName);
    root.insert(QStringLiteral("ipcore"), request.ipcore.id);
    root.insert(QStringLiteral("instance"), request.instance.instanceId);
    root.insert(QStringLiteral("ipcore_state"), ipcoreState);
    root.insert(QStringLiteral("modules"), modules);
    root.insert(QStringLiteral("connections"), connections);
    return {true, QJsonDocument(root), {}};
}
