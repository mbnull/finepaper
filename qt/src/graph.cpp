// Graph — implementation of the SoC/NoC topology data model.
// add/remove/take/insert follow a consistent ownership pattern:
//   add/insert  — take ownership of a unique_ptr and emit a signal
//   remove      — destroy in-place and emit a signal
//   take        — transfer ownership out (used by undo commands)
#include "graph.h"
#include "modulelabels.h"
#include "moduleregistry.h"
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFileInfo>
#include <QUuid>
#include <functional>
#include "portlayout.h"

namespace {

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) return nullptr;

    for (const auto& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

QString oppositeDirection(const QString& dir) {
    return PortLayout::oppositeRouterSide(PortLayout::routerSideId(dir));
}

QString newInternalId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
}

QString firstAvailablePort(const Graph* graph,
                           const Module* module,
                           Port::Direction direction,
                           const std::function<bool(const Port&)>& predicate) {
    if (!graph || !module) return {};

    for (const auto& port : module->ports()) {
        if (port.direction() != direction || !predicate(port)) continue;

        const bool occupied = std::any_of(graph->connections().begin(), graph->connections().end(),
            [&](const std::unique_ptr<Connection>& connection) {
                const PortRef& ref = direction == Port::Direction::Output
                    ? connection->source()
                    : connection->target();
                return ref.moduleId == module->id() && ref.portId == port.id();
            });

        if (!occupied) {
            return port.id();
        }
    }

    return {};
}

QJsonValue parameterToJson(const Parameter::Value& value) {
    if (std::holds_alternative<QString>(value)) return QJsonValue(std::get<QString>(value));
    if (std::holds_alternative<int>(value)) return QJsonValue(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return QJsonValue(std::get<double>(value));
    if (std::holds_alternative<bool>(value)) return QJsonValue(std::get<bool>(value));
    return QJsonValue();
}

std::optional<double> parameterAsDouble(const Module* module, const QString& name) {
    if (!module) return std::nullopt;

    auto it = module->parameters().find(name);
    if (it == module->parameters().end()) return std::nullopt;

    const auto& value = it.value().value();
    if (auto* i = std::get_if<int>(&value)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&value)) return *d;
    return std::nullopt;
}

std::pair<QString, QString> guessedRouterPorts(const Module* fromModule, const Module* toModule) {
    auto fromX = parameterAsDouble(fromModule, "x");
    auto fromY = parameterAsDouble(fromModule, "y");
    auto toX = parameterAsDouble(toModule, "x");
    auto toY = parameterAsDouble(toModule, "y");

    if (fromX && fromY && toX && toY) {
        const double dx = *toX - *fromX;
        const double dy = *toY - *fromY;

        if (std::abs(dx) >= std::abs(dy)) {
            const QString sourceSide = dx >= 0.0 ? "east" : "west";
            const QString targetSide = oppositeDirection(sourceSide);
            return {PortLayout::routerOutputPortId(sourceSide), PortLayout::routerInputPortId(targetSide)};
        }

        const QString sourceSide = dy >= 0.0 ? "south" : "north";
        const QString targetSide = oppositeDirection(sourceSide);
        return {PortLayout::routerOutputPortId(sourceSide), PortLayout::routerInputPortId(targetSide)};
    }

    return {};
}

bool isRouterLink(const Module* sourceModule,
                  const Port* sourcePort,
                  const Module* targetModule,
                  const Port* targetPort) {
    return sourceModule && targetModule &&
           sourcePort && targetPort &&
           sourceModule->type() == "XP" &&
           targetModule->type() == "XP" &&
           sourcePort->type() == "router" &&
           targetPort->type() == "router";
}

bool connectionUsesRouterSide(const Connection& connection,
                              const QString& moduleId,
                              const QString& side) {
    if (connection.source().moduleId == moduleId &&
        PortLayout::routerSideId(connection.source().portId) == side) {
        return true;
    }

    if (connection.target().moduleId == moduleId &&
        PortLayout::routerSideId(connection.target().portId) == side) {
        return true;
    }

    return false;
}

} // namespace

Graph::Graph(QObject* parent) : QObject(parent) {
}

bool Graph::addModule(std::unique_ptr<Module> module) {
    if (module->id().isEmpty()) {
        qWarning() << "Cannot add module with empty ID";
        return false;
    }
    if (getModule(module->id())) {
        qWarning() << "Cannot add module with duplicate ID:" << module->id();
        return false;
    }
    Module* ptr = module.get();
    QString moduleId = ptr->id();
    m_moduleConnections[moduleId] = connect(ptr, &Module::parameterChanged, this, [this, moduleId](const QString& paramName) {
        onModuleParameterChanged(moduleId, paramName);
    });
    m_modules.push_back(std::move(module));
    emit moduleAdded(ptr);
    return true;
}

void Graph::removeModule(const QString& moduleId) {
    auto connIt = m_connections.begin();
    while (connIt != m_connections.end()) {
        if ((*connIt)->source().moduleId == moduleId || (*connIt)->target().moduleId == moduleId) {
            QString connId = (*connIt)->id();
            connIt = m_connections.erase(connIt);
            emit connectionRemoved(connId);
        } else {
            ++connIt;
        }
    }

    auto it = std::remove_if(m_modules.begin(), m_modules.end(),
        [&moduleId](const std::unique_ptr<Module>& m) { return m->id() == moduleId; });

    if (it != m_modules.end()) {
        m_modules.erase(it, m_modules.end());
        emit moduleRemoved(moduleId);
    }
}

Module* Graph::getModule(const QString& moduleId) const {
    auto it = std::find_if(m_modules.begin(), m_modules.end(),
        [&moduleId](const std::unique_ptr<Module>& m) { return m->id() == moduleId; });
    return it != m_modules.end() ? it->get() : nullptr;
}

std::unique_ptr<Module> Graph::takeModule(const QString& moduleId) {
    auto it = std::find_if(m_modules.begin(), m_modules.end(),
        [&moduleId](const std::unique_ptr<Module>& m) { return m->id() == moduleId; });
    if (it != m_modules.end()) {
        disconnect(m_moduleConnections[moduleId]);
        m_moduleConnections.remove(moduleId);
        std::unique_ptr<Module> module = std::move(*it);
        m_modules.erase(it);
        emit moduleRemoved(moduleId);
        return module;
    }
    return nullptr;
}

bool Graph::insertModule(std::unique_ptr<Module> module) {
    if (module->id().isEmpty()) {
        qWarning() << "Cannot insert module with empty ID";
        return false;
    }
    if (getModule(module->id())) {
        qWarning() << "Cannot insert module with duplicate ID:" << module->id();
        return false;
    }
    Module* ptr = module.get();
    QString moduleId = ptr->id();
    m_moduleConnections[moduleId] = connect(ptr, &Module::parameterChanged, this, [this, moduleId](const QString& paramName) {
        onModuleParameterChanged(moduleId, paramName);
    });
    m_modules.push_back(std::move(module));
    emit moduleAdded(ptr);
    return true;
}

void Graph::addConnection(std::unique_ptr<Connection> connection) {
    if (!isValidConnection(connection->source(), connection->target())) {
        qWarning() << "Cannot add invalid connection:" << connection->id();
        return;
    }
    Connection* ptr = connection.get();
    m_connections.push_back(std::move(connection));
    emit connectionAdded(ptr);
}

void Graph::removeConnection(const QString& connectionId) {
    auto it = std::remove_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const std::unique_ptr<Connection>& c) { return c->id() == connectionId; });

    if (it != m_connections.end()) {
        m_connections.erase(it, m_connections.end());
        emit connectionRemoved(connectionId);
    }
}

std::unique_ptr<Connection> Graph::takeConnection(const QString& connectionId) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const std::unique_ptr<Connection>& c) { return c->id() == connectionId; });
    if (it != m_connections.end()) {
        std::unique_ptr<Connection> connection = std::move(*it);
        m_connections.erase(it);
        emit connectionRemoved(connectionId);
        return connection;
    }
    return nullptr;
}

void Graph::insertConnection(std::unique_ptr<Connection> connection) {
    if (!isValidConnection(connection->source(), connection->target())) {
        qWarning() << "Cannot insert invalid connection:" << connection->id();
        return;
    }
    Connection* ptr = connection.get();
    m_connections.push_back(std::move(connection));
    emit connectionAdded(ptr);
}

bool Graph::isValidConnection(const PortRef& source, const PortRef& target) const {
    if (source.moduleId == target.moduleId) return false;

    const Module* sourceModule = getModule(source.moduleId);
    const Module* targetModule = getModule(target.moduleId);
    if (!sourceModule || !targetModule) return false;

    const Port* sourcePort = findPort(sourceModule, source.portId);
    const Port* targetPort = findPort(targetModule, target.portId);
    if (!sourcePort || !targetPort) return false;

    if (sourcePort->direction() != Port::Direction::Output) return false;
    if (targetPort->direction() != Port::Direction::Input) return false;
    if (sourcePort->type() != targetPort->type()) return false;

    if (isRouterLink(sourceModule, sourcePort, targetModule, targetPort)) {
        const QString sourceSide = PortLayout::routerSideId(source.portId);
        const QString targetSide = PortLayout::routerSideId(target.portId);

        if (sourceSide == targetSide) {
            return false;
        }

        for (const auto& existingConnection : m_connections) {
            const bool sameRouterPair =
                (existingConnection->source().moduleId == source.moduleId &&
                 existingConnection->target().moduleId == target.moduleId) ||
                (existingConnection->source().moduleId == target.moduleId &&
                 existingConnection->target().moduleId == source.moduleId);

            if (sameRouterPair) {
                return false;
            }

            if (connectionUsesRouterSide(*existingConnection, source.moduleId, sourceSide) ||
                connectionUsesRouterSide(*existingConnection, target.moduleId, targetSide)) {
                return false;
            }
        }
    }

    const bool sourceInUse = std::any_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            return c->source().moduleId == source.moduleId && c->source().portId == source.portId;
        });
    if (sourceInUse) return false;

    const bool targetInUse = std::any_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            return c->target().moduleId == target.moduleId && c->target().portId == target.portId;
        });
    if (targetInUse) return false;

    return std::none_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            return c->source().moduleId == source.moduleId && c->source().portId == source.portId &&
                   c->target().moduleId == target.moduleId && c->target().portId == target.portId;
        });
}

bool Graph::loadFromJson(const QString& jsonPath) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QHash<QString, QString> externalToInternalIds;

    while (!m_modules.empty()) {
        removeModule(m_modules.front()->id());
    }

    QJsonArray xps = root["xps"].toArray();
    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString externalId = xp["id"].toString();

        const ModuleType* type = ModuleRegistry::instance().getType("XP");
        if (!type) continue;

        auto module = std::make_unique<Module>(newInternalId(), "XP");

        for (const auto& port : type->defaultPorts) {
            module->addPort(port);
        }
        for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
            module->setParameter(it.key(), it.value().value());
        }
        module->setParameter("display_name", ModuleLabels::humanizeExternalId("XP", externalId));
        module->setParameter("external_id", externalId);

        if (xp.contains("x")) module->setParameter("x", xp["x"].toInt());
        if (xp.contains("y")) module->setParameter("y", xp["y"].toInt());

        QJsonObject config = xp["config"].toObject();
        if (config.contains("collapsed")) module->setParameter("collapsed", config["collapsed"].toBool());
        if (config.contains("routing_algorithm")) module->setParameter("routing_algorithm", config["routing_algorithm"].toString());
        if (config.contains("vc_count")) module->setParameter("vc_count", config["vc_count"].toInt());
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());

        externalToInternalIds.insert(externalId, module->id());
        addModule(std::move(module));
    }

    QJsonArray eps = root["endpoints"].toArray();
    for (const auto& epVal : eps) {
        QJsonObject ep = epVal.toObject();
        QString externalId = ep["id"].toString();

        const ModuleType* type = ModuleRegistry::instance().getType("Endpoint");
        if (!type) continue;

        auto module = std::make_unique<Module>(newInternalId(), "Endpoint");

        for (const auto& port : type->defaultPorts) {
            module->addPort(port);
        }
        for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
            module->setParameter(it.key(), it.value().value());
        }
        module->setParameter("display_name", ModuleLabels::humanizeExternalId("Endpoint", externalId));
        module->setParameter("external_id", externalId);

        if (ep.contains("type")) module->setParameter("type", ep["type"].toString());
        if (ep.contains("protocol")) module->setParameter("protocol", ep["protocol"].toString());
        if (ep.contains("data_width")) module->setParameter("data_width", ep["data_width"].toInt());

        QJsonObject config = ep["config"].toObject();
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());
        if (config.contains("qos_enabled")) module->setParameter("qos_enabled", config["qos_enabled"].toBool());

        externalToInternalIds.insert(externalId, module->id());
        addModule(std::move(module));
    }

    QJsonArray conns = root["connections"].toArray();
    for (const auto& connVal : conns) {
        QJsonObject conn = connVal.toObject();
        QString fromExternal = conn["from"].toString();
        QString toExternal = conn["to"].toString();
        QString dir = conn["dir"].toString();
        QString fromPort = conn["from_port"].toString();
        QString toPort = conn["to_port"].toString();
        QString from = externalToInternalIds.value(fromExternal);
        QString to = externalToInternalIds.value(toExternal);

        Module* fromModule = getModule(from);
        Module* toModule = getModule(to);
        if (!fromModule || !toModule) {
            qWarning() << "Skipping connection" << fromExternal << "->" << toExternal << ": module not found";
            continue;
        }

        if (!fromPort.isEmpty() && !toPort.isEmpty()) {
            const Port* explicitFromPort = findPort(fromModule, fromPort);
            const Port* explicitToPort = findPort(toModule, toPort);
            const bool explicitEndpointLink =
                explicitFromPort && explicitToPort &&
                explicitFromPort->type() == "endpoint" && explicitToPort->type() == "endpoint";
            const bool explicitRouterLink =
                explicitFromPort && explicitToPort &&
                explicitFromPort->type() == "router" && explicitToPort->type() == "router";

            if (!explicitEndpointLink && !explicitRouterLink) {
                fromPort.clear();
                toPort.clear();
            }
        }

        if (fromModule->type() == "XP" && toModule->type() == "Endpoint") {
            if (fromPort.isEmpty() || !findPort(fromModule, fromPort) || !PortLayout::isEndpointPort(*findPort(fromModule, fromPort))) {
                fromPort = firstAvailablePort(this, fromModule, Port::Direction::Output,
                    [](const Port& port) { return PortLayout::isEndpointPort(port); });
            }
            if (toPort.isEmpty() || !findPort(toModule, toPort) || findPort(toModule, toPort)->type() != "endpoint") {
                toPort = firstAvailablePort(this, toModule, Port::Direction::Input,
                    [](const Port& port) { return port.type() == "endpoint"; });
            }
        } else if (!dir.isEmpty()) {
            fromPort = PortLayout::routerOutputPortId(dir);
            toPort = PortLayout::routerInputPortId(oppositeDirection(dir));
        } else if (fromModule->type() == "XP" && toModule->type() == "XP") {
            auto guessed = guessedRouterPorts(fromModule, toModule);
            if (!guessed.first.isEmpty() && !guessed.second.isEmpty()) {
                fromPort = guessed.first;
                toPort = guessed.second;
            }
        }

        if (fromPort.isEmpty()) {
            fromPort = firstAvailablePort(this, fromModule, Port::Direction::Output,
                [](const Port& port) { return PortLayout::isRouterPort(port); });
        }
        if (toPort.isEmpty()) {
            toPort = firstAvailablePort(this, toModule, Port::Direction::Input,
                [](const Port& port) { return PortLayout::isRouterPort(port); });
        }

        if (fromPort.isEmpty() || toPort.isEmpty()) {
            qWarning() << "Skipping connection" << fromExternal << "->" << toExternal << ": ports not found";
            continue;
        }

        auto connection = std::make_unique<Connection>(from + "_" + to, PortRef{from, fromPort}, PortRef{to, toPort});
        if (!isValidConnection(connection->source(), connection->target())) {
            qWarning() << "Skipping invalid connection" << fromExternal << "->" << toExternal;
            continue;
        }
        addConnection(std::move(connection));
    }

    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString xpExternalId = xp["id"].toString();
        QString xpId = externalToInternalIds.value(xpExternalId);
        QJsonArray endpoints = xp["endpoints"].toArray();

        for (const auto& epIdVal : endpoints) {
            QString epExternalId = epIdVal.toString();
            QString epId = externalToInternalIds.value(epExternalId);

            Module* epModule = getModule(epId);
            Module* xpModule = getModule(xpId);
            if (!epModule || !xpModule) {
                qWarning() << "Skipping connection" << epExternalId << "->" << xpExternalId << ": module not found";
                continue;
            }

            QString epPort, xpPort;
            xpPort = firstAvailablePort(this, xpModule, Port::Direction::Output,
                [](const Port& port) { return PortLayout::isEndpointPort(port); });
            epPort = firstAvailablePort(this, epModule, Port::Direction::Input,
                [](const Port& port) { return port.type() == "endpoint"; });

            if (epPort.isEmpty() || xpPort.isEmpty()) {
                qWarning() << "Skipping connection" << epExternalId << "->" << xpExternalId << ": ports not found";
                continue;
            }

            auto connection = std::make_unique<Connection>(xpId + "_" + epId, PortRef{xpId, xpPort}, PortRef{epId, epPort});
            if (!isValidConnection(connection->source(), connection->target())) {
                qWarning() << "Skipping invalid connection" << xpExternalId << "->" << epExternalId;
                continue;
            }
            addConnection(std::move(connection));
        }
    }

    return true;
}

bool Graph::saveToJson(const QString& jsonPath) const {
    QJsonArray xps, eps, conns;
    QHash<QString, QJsonArray> xpEndpointMap;

    for (const auto& mod : m_modules) {
        QJsonObject obj;
        obj["id"] = ModuleLabels::externalId(mod.get());

        const auto& params = mod->parameters();
        if (mod->type() == "XP") {
            obj["x"] = params.contains("x") ? parameterToJson(params["x"].value()) : QJsonValue(0);
            obj["y"] = params.contains("y") ? parameterToJson(params["y"].value()) : QJsonValue(0);

            QJsonObject config;
            if (params.contains("collapsed")) config["collapsed"] = parameterToJson(params["collapsed"].value());
            if (params.contains("routing_algorithm")) config["routing_algorithm"] = parameterToJson(params["routing_algorithm"].value());
            if (params.contains("vc_count")) config["vc_count"] = parameterToJson(params["vc_count"].value());
            if (params.contains("buffer_depth")) config["buffer_depth"] = parameterToJson(params["buffer_depth"].value());
            obj["config"] = config;
            obj["endpoints"] = QJsonArray();
            xpEndpointMap.insert(ModuleLabels::externalId(mod.get()), QJsonArray());
            xps.append(obj);
        } else {
            if (params.contains("type")) obj["type"] = parameterToJson(params["type"].value());
            if (params.contains("protocol")) obj["protocol"] = parameterToJson(params["protocol"].value());
            if (params.contains("data_width")) obj["data_width"] = parameterToJson(params["data_width"].value());

            QJsonObject config;
            if (params.contains("qos_enabled")) config["qos_enabled"] = parameterToJson(params["qos_enabled"].value());
            if (params.contains("buffer_depth")) config["buffer_depth"] = parameterToJson(params["buffer_depth"].value());
            if (!config.isEmpty()) obj["config"] = config;
            eps.append(obj);
        }
    }

    for (const auto& conn : m_connections) {
        const Module* sourceModule = getModule(conn->source().moduleId);
        const Module* targetModule = getModule(conn->target().moduleId);
        const Port* sourcePort = sourceModule ? findPort(sourceModule, conn->source().portId) : nullptr;
        const QString sourceExternalId = ModuleLabels::externalId(sourceModule);
        const QString targetExternalId = ModuleLabels::externalId(targetModule);

        if (sourceModule && targetModule &&
            sourceModule->type() == "XP" &&
            targetModule->type() == "Endpoint" &&
            sourcePort && PortLayout::isEndpointPort(*sourcePort)) {
            auto it = xpEndpointMap.find(sourceExternalId);
            if (it != xpEndpointMap.end()) {
                it.value().append(targetExternalId);
            }
            continue;
        }

        QJsonObject obj;
        obj["from"] = sourceExternalId;
        obj["to"] = targetExternalId;

        if (sourceModule && targetModule &&
            sourceModule->type() == "XP" &&
            targetModule->type() == "XP") {
            // XP-to-XP links are exported as undirected/bidirectional edges.
        } else {
            obj["from_port"] = conn->source().portId;
            obj["to_port"] = conn->target().portId;
        }

        conns.append(obj);
    }

    for (int i = 0; i < xps.size(); ++i) {
        QJsonObject xp = xps[i].toObject();
        xp["endpoints"] = xpEndpointMap.value(xp["id"].toString());
        xps[i] = xp;
    }

    QJsonObject root;
    root["name"] = QFileInfo(jsonPath).baseName();
    root["version"] = "1.0";
    root["xps"] = xps;
    root["endpoints"] = eps;
    root["connections"] = conns;

    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    return true;
}

void Graph::onModuleParameterChanged(const QString& moduleId, const QString& paramName) {
    emit parameterChanged(moduleId, paramName);
}
