// Graph — implementation of the SoC/NoC topology data model.
// add/remove/take/insert follow a consistent ownership pattern:
//   add/insert  — take ownership of a unique_ptr and emit a signal
//   remove      — destroy in-place and emit a signal
//   take        — transfer ownership out (used by undo commands)
#include "graph/graph.h"
#include "modules/modulelabels.h"
#include "modules/moduletypemetadata.h"
#include "modules/moduleregistry.h"
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFileInfo>
#include <QXmlStreamWriter>
#include <QUuid>
#include <cmath>
#include <functional>
#include "common/portlayout.h"

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

bool isMeshRouterModule(const Module* module) {
    return ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router");
}

bool isEndpointModule(const Module* module) {
    return ModuleTypeMetadata::isInGraphGroup(module, u"endpoints");
}

QString newInternalId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
}

std::unique_ptr<Module> instantiateModule(const ModuleType& type, const QString& moduleId) {
    auto module = std::make_unique<Module>(moduleId, type.name);
    for (const auto& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    return module;
}

QString firstAvailablePort(const Graph* graph,
                           const Module* module,
                           Port::Direction direction,
                           const std::function<bool(const Port&)>& predicate) {
    if (!graph || !module) return {};

    for (const auto& port : module->ports()) {
        const bool matchesDirection =
            direction == Port::Direction::Output ? PortLayout::supportsOutput(port)
                                                 : PortLayout::supportsInput(port);
        if (!matchesDirection || !predicate(port)) continue;

        const bool occupied = std::any_of(graph->connections().begin(), graph->connections().end(),
            [&](const std::unique_ptr<Connection>& connection) {
                const bool usedAsSource =
                    connection->source().moduleId == module->id() && connection->source().portId == port.id();
                const bool usedAsTarget =
                    connection->target().moduleId == module->id() && connection->target().portId == port.id();
                if (port.direction() == Port::Direction::InOut) {
                    return usedAsSource || usedAsTarget;
                }
                return direction == Port::Direction::Output ? usedAsSource : usedAsTarget;
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

void writeJsonValueAsXml(QXmlStreamWriter& writer,
                         const QString& elementName,
                         const QJsonValue& value) {
    if (value.isObject()) {
        writer.writeStartElement(elementName);
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            writeJsonValueAsXml(writer, it.key(), it.value());
        }
        writer.writeEndElement();
        return;
    }

    if (value.isArray()) {
        writer.writeStartElement(elementName);
        const QJsonArray array = value.toArray();
        for (const QJsonValue& item : array) {
            writeJsonValueAsXml(writer, QStringLiteral("item"), item);
        }
        writer.writeEndElement();
        return;
    }

    writer.writeStartElement(elementName);
    if (value.isString()) {
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("string"));
        writer.writeCharacters(value.toString());
    } else if (value.isDouble()) {
        const double number = value.toDouble();
        const bool isInteger = std::floor(number) == number;
        writer.writeAttribute(QStringLiteral("type"), isInteger ? QStringLiteral("int")
                                                                : QStringLiteral("double"));
        writer.writeCharacters(QString::number(number, 'g', 15));
    } else if (value.isBool()) {
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("bool"));
        writer.writeCharacters(value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    } else if (value.isNull() || value.isUndefined()) {
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("null"));
    }
    writer.writeEndElement();
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

bool hasStoredPosition(const Module* module) {
    return parameterAsDouble(module, "x").has_value() && parameterAsDouble(module, "y").has_value();
}

void assignEndpointFallbackPosition(Module* endpointModule,
                                    const Module* xpModule,
                                    const QString& xpPortId) {
    if (!endpointModule || !xpModule || hasStoredPosition(endpointModule)) {
        return;
    }

    const double xpX = parameterAsDouble(xpModule, "x").value_or(0.0);
    const double xpY = parameterAsDouble(xpModule, "y").value_or(0.0);
    const int slot = std::clamp(PortLayout::endpointPortSlot(xpPortId), 0, PortLayout::kEndpointPortCount - 1);

    const double xpPortInset = ModuleTypeMetadata::expandedPortInset(xpModule);
    const double xpHeight = static_cast<double>(ModuleTypeMetadata::expandedNodeHeight(xpModule));
    const double endpointHeight = static_cast<double>(ModuleTypeMetadata::expandedNodeHeight(endpointModule));
    const double endpointXOffset = static_cast<double>(ModuleTypeMetadata::linkedEndpointOffsetX(xpModule));

    const double usableHeight = xpHeight - (xpPortInset * 2.0);
    const double slotHeight = usableHeight / static_cast<double>(PortLayout::kEndpointPortCount);
    const double endpointCenterY = xpY + xpPortInset + (static_cast<double>(slot) + 0.5) * slotHeight;

    endpointModule->setParameter("x", static_cast<int>(std::lround(xpX - endpointXOffset)));
    endpointModule->setParameter("y", static_cast<int>(std::lround(endpointCenterY - (endpointHeight / 2.0))));
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
           isMeshRouterModule(sourceModule) &&
           isMeshRouterModule(targetModule) &&
           PortLayout::isRouterPort(*sourcePort) &&
           PortLayout::isRouterPort(*targetPort);
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

Graph::~Graph() {
    for (auto it = m_moduleConnections.begin(); it != m_moduleConnections.end(); ++it) {
        disconnect(it.value());
    }
    m_moduleConnections.clear();
    m_connections.clear();
    m_modules.clear();
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
    qInfo() << "Added module"
            << "id" << moduleId
            << "type" << ptr->type()
            << "totalModules" << m_modules.size();
    emit moduleAdded(ptr);
    return true;
}

void Graph::removeModule(const QString& moduleId) {
    const std::size_t moduleCountBefore = m_modules.size();
    auto connIt = m_connections.begin();
    while (connIt != m_connections.end()) {
        if ((*connIt)->source().moduleId == moduleId || (*connIt)->target().moduleId == moduleId) {
            QString connId = (*connIt)->id();
            connIt = m_connections.erase(connIt);
            qInfo() << "Removed connection while deleting module"
                    << "connectionId" << connId
                    << "moduleId" << moduleId
                    << "totalConnections" << m_connections.size();
            emit connectionRemoved(connId);
        } else {
            ++connIt;
        }
    }

    auto it = std::remove_if(m_modules.begin(), m_modules.end(),
        [&moduleId](const std::unique_ptr<Module>& m) { return m->id() == moduleId; });

    if (it != m_modules.end()) {
        disconnect(m_moduleConnections.value(moduleId));
        m_moduleConnections.remove(moduleId);
        m_modules.erase(it, m_modules.end());
        qInfo() << "Removed module"
                << "id" << moduleId
                << "totalModules" << m_modules.size();
        emit moduleRemoved(moduleId);
    } else {
        qDebug() << "Requested removal for unknown module" << moduleId
                 << "totalModules" << moduleCountBefore;
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
        qInfo() << "Took module"
                << "id" << moduleId
                << "totalModules" << m_modules.size();
        emit moduleRemoved(moduleId);
        return module;
    }
    qDebug() << "Requested take for unknown module" << moduleId;
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
    qInfo() << "Inserted module"
            << "id" << moduleId
            << "type" << ptr->type()
            << "totalModules" << m_modules.size();
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
    qInfo() << "Added connection"
            << "id" << ptr->id()
            << "source" << ptr->source().moduleId << ptr->source().portId
            << "target" << ptr->target().moduleId << ptr->target().portId
            << "totalConnections" << m_connections.size();
    emit connectionAdded(ptr);
}

void Graph::removeConnection(const QString& connectionId) {
    const std::size_t connectionCountBefore = m_connections.size();
    auto it = std::remove_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const std::unique_ptr<Connection>& c) { return c->id() == connectionId; });

    if (it != m_connections.end()) {
        m_connections.erase(it, m_connections.end());
        qInfo() << "Removed connection"
                << "id" << connectionId
                << "totalConnections" << m_connections.size();
        emit connectionRemoved(connectionId);
    } else {
        qDebug() << "Requested removal for unknown connection" << connectionId
                 << "totalConnections" << connectionCountBefore;
    }
}

std::unique_ptr<Connection> Graph::takeConnection(const QString& connectionId) {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const std::unique_ptr<Connection>& c) { return c->id() == connectionId; });
    if (it != m_connections.end()) {
        std::unique_ptr<Connection> connection = std::move(*it);
        m_connections.erase(it);
        qInfo() << "Took connection"
                << "id" << connectionId
                << "totalConnections" << m_connections.size();
        emit connectionRemoved(connectionId);
        return connection;
    }
    qDebug() << "Requested take for unknown connection" << connectionId;
    return nullptr;
}

void Graph::insertConnection(std::unique_ptr<Connection> connection) {
    if (!isValidConnection(connection->source(), connection->target())) {
        qWarning() << "Cannot insert invalid connection:" << connection->id();
        return;
    }
    Connection* ptr = connection.get();
    m_connections.push_back(std::move(connection));
    qInfo() << "Inserted connection"
            << "id" << ptr->id()
            << "source" << ptr->source().moduleId << ptr->source().portId
            << "target" << ptr->target().moduleId << ptr->target().portId
            << "totalConnections" << m_connections.size();
    emit connectionAdded(ptr);
}

bool Graph::isValidConnection(const PortRef& source, const PortRef& target) const {
    // Disallow self-loops at graph level.
    if (source.moduleId == target.moduleId) return false;

    const Module* sourceModule = getModule(source.moduleId);
    const Module* targetModule = getModule(target.moduleId);
    if (!sourceModule || !targetModule) return false;

    const Port* sourcePort = findPort(sourceModule, source.portId);
    const Port* targetPort = findPort(targetModule, target.portId);
    if (!sourcePort || !targetPort) return false;

    if (!PortLayout::supportsOutput(*sourcePort)) return false;
    if (!PortLayout::supportsInput(*targetPort)) return false;
    if (!PortLayout::sameBusFamily(*sourcePort, *targetPort)) return false;

    if (isRouterLink(sourceModule, sourcePort, targetModule, targetPort)) {
        // Router-to-router links are constrained to one opposite-side pair and
        // one connection per side to preserve mesh semantics.
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

    // Occupancy is directional for regular ports, but bidirectional for InOut.
    const auto portIsOccupied = [&](const PortRef& portRef, const Port& port) {
        return std::any_of(m_connections.begin(), m_connections.end(),
            [&](const std::unique_ptr<Connection>& c) {
                const bool usedAsSource =
                    c->source().moduleId == portRef.moduleId && c->source().portId == portRef.portId;
                const bool usedAsTarget =
                    c->target().moduleId == portRef.moduleId && c->target().portId == portRef.portId;
                if (port.direction() == Port::Direction::InOut) {
                    return usedAsSource || usedAsTarget;
                }

                return usedAsSource;
            });
    };

    const bool sourceInUse = portIsOccupied(source, *sourcePort);
    if (sourceInUse) return false;

    const bool targetInUse = std::any_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            const bool usedAsSource =
                c->source().moduleId == target.moduleId && c->source().portId == target.portId;
            const bool usedAsTarget =
                c->target().moduleId == target.moduleId && c->target().portId == target.portId;
            if (targetPort->direction() == Port::Direction::InOut) {
                return usedAsSource || usedAsTarget;
            }

            return usedAsTarget;
        });
    if (targetInUse) return false;

    return std::none_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            return c->source().moduleId == source.moduleId && c->source().portId == source.portId &&
                   c->target().moduleId == target.moduleId && c->target().portId == target.portId;
        });
}

bool Graph::loadFromJson(const QString& jsonPath) {
    // Import pipeline:
    // 1) Parse JSON and rebuild modules while mapping external IDs -> internal IDs.
    // 2) Reconstruct explicit/derived connections with port fallback logic.
    // 3) Restore legacy xp->endpoint links that may be stored separately.
    qInfo() << "Starting graph import from" << jsonPath;
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open graph JSON for reading:" << jsonPath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Invalid graph JSON document:" << jsonPath;
        return false;
    }

    QJsonObject root = doc.object();
    QHash<QString, QString> externalToInternalIds;
    const ModuleType* meshRouterType = ModuleRegistry::instance().getTypeForGraphGroup("xps");
    const ModuleType* endpointType = ModuleRegistry::instance().getTypeForGraphGroup("endpoints");

    // Start from a clean model so imported state fully replaces in-memory topology.
    while (!m_modules.empty()) {
        removeModule(m_modules.front()->id());
    }

    // Pass 1: materialize all routers and remember external->internal ID mapping.
    QJsonArray xps = root["xps"].toArray();
    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString externalId = xp["id"].toString();

        if (!meshRouterType) continue;

        auto module = instantiateModule(*meshRouterType, newInternalId());
        module->setParameter("display_name", ModuleLabels::humanizeExternalId(meshRouterType->name, externalId));
        module->setParameter("external_id", externalId);

        if (xp.contains("x")) module->setParameter("x", xp["x"].toInt());
        if (xp.contains("y")) module->setParameter("y", xp["y"].toInt());

        QJsonObject config = xp["config"].toObject();
        if (config.contains("collapsed")) module->setParameter("collapsed", config["collapsed"].toBool());
        if (config.contains("routing_algorithm")) module->setParameter("routing_algorithm", config["routing_algorithm"].toString());
        if (config.contains("vc_count")) module->setParameter("vc_count", config["vc_count"].toInt());
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());

        // Keep stable lookup table so connection payload can resolve endpoints
        // regardless of internal UUIDs.
        externalToInternalIds.insert(externalId, module->id());
        addModule(std::move(module));
    }

    // Pass 2: materialize all endpoints and extend the same ID mapping table.
    QJsonArray eps = root["endpoints"].toArray();
    for (const auto& epVal : eps) {
        QJsonObject ep = epVal.toObject();
        QString externalId = ep["id"].toString();

        if (!endpointType) continue;

        auto module = instantiateModule(*endpointType, newInternalId());
        module->setParameter("display_name", ModuleLabels::humanizeExternalId(endpointType->name, externalId));
        module->setParameter("external_id", externalId);

        if (ep.contains("x")) module->setParameter("x", ep["x"].toInt());
        if (ep.contains("y")) module->setParameter("y", ep["y"].toInt());
        if (ep.contains("type")) module->setParameter("type", ep["type"].toString());
        if (ep.contains("protocol")) module->setParameter("protocol", ep["protocol"].toString());
        if (ep.contains("data_width")) module->setParameter("data_width", ep["data_width"].toInt());

        QJsonObject config = ep["config"].toObject();
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());
        if (config.contains("qos_enabled")) module->setParameter("qos_enabled", config["qos_enabled"].toBool());

        externalToInternalIds.insert(externalId, module->id());
        addModule(std::move(module));
    }

    // Pass 3: recreate declared connections.
    // Preference order for ports:
    // - explicit port IDs from JSON (if pair type is valid),
    // - direction-based router ports,
    // - guessed router-to-router ports,
    // - first available compatible fallback ports.
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
            // IDs in the connection payload reference a node that was not imported.
            qWarning() << "Skipping connection" << fromExternal << "->" << toExternal << ": module not found";
            continue;
        }

        if (!fromPort.isEmpty() && !toPort.isEmpty()) {
            const Port* explicitFromPort = findPort(fromModule, fromPort);
            const Port* explicitToPort = findPort(toModule, toPort);
            const bool explicitEndpointLink =
                explicitFromPort && explicitToPort &&
                PortLayout::isEndpointPort(*explicitFromPort) && PortLayout::isEndpointPort(*explicitToPort);
            const bool explicitRouterLink =
                explicitFromPort && explicitToPort &&
                PortLayout::isRouterPort(*explicitFromPort) && PortLayout::isRouterPort(*explicitToPort);

            if (!explicitEndpointLink && !explicitRouterLink) {
                // Ignore mixed/invalid explicit pairs and fall back to inferred ports below.
                fromPort.clear();
                toPort.clear();
            }
        }

        if (isMeshRouterModule(fromModule) && isEndpointModule(toModule)) {
            // Router->endpoint links must use endpoint-class ports on both sides.
            if (fromPort.isEmpty() || !findPort(fromModule, fromPort) || !PortLayout::isEndpointPort(*findPort(fromModule, fromPort))) {
                fromPort = firstAvailablePort(this, fromModule, Port::Direction::Output,
                    [](const Port& port) { return PortLayout::isEndpointPort(port); });
            }
            if (toPort.isEmpty() || !findPort(toModule, toPort) || !PortLayout::isEndpointPort(*findPort(toModule, toPort))) {
                toPort = firstAvailablePort(this, toModule, Port::Direction::Input,
                    [](const Port& port) { return PortLayout::isEndpointPort(port); });
            }
        } else if (!dir.isEmpty()) {
            // Legacy files may only store relative direction; derive canonical router port IDs.
            fromPort = PortLayout::routerOutputPortId(dir);
            toPort = PortLayout::routerInputPortId(oppositeDirection(dir));
        } else if (isMeshRouterModule(fromModule) && isMeshRouterModule(toModule)) {
            // As a last semantic guess, infer router-to-router side pairing from module placement.
            auto guessed = guessedRouterPorts(fromModule, toModule);
            if (!guessed.first.isEmpty() && !guessed.second.isEmpty()) {
                fromPort = guessed.first;
                toPort = guessed.second;
            }
        }

        if (fromPort.isEmpty()) {
            // Final fallback: pick the first free router-capable output.
            fromPort = firstAvailablePort(this, fromModule, Port::Direction::Output,
                [](const Port& port) { return PortLayout::isRouterPort(port); });
        }
        if (toPort.isEmpty()) {
            // Final fallback: pick the first free router-capable input.
            toPort = firstAvailablePort(this, toModule, Port::Direction::Input,
                [](const Port& port) { return PortLayout::isRouterPort(port); });
        }

        if (fromPort.isEmpty() || toPort.isEmpty()) {
            qWarning() << "Skipping connection" << fromExternal << "->" << toExternal << ": ports not found";
            continue;
        }

        // Connection IDs from import are deterministic labels; graph validity
        // checks are still enforced before insertion.
        auto connection = std::make_unique<Connection>(from + "_" + to, PortRef{from, fromPort}, PortRef{to, toPort});
        if (!isValidConnection(connection->source(), connection->target())) {
            // Keep import resilient: skip only the bad edge and continue.
            qWarning() << "Skipping invalid connection" << fromExternal << "->" << toExternal;
            continue;
        }
        addConnection(std::move(connection));
        if (isMeshRouterModule(fromModule) && isEndpointModule(toModule)) {
            // Place endpoint near its router if explicit coordinates are missing.
            assignEndpointFallbackPosition(toModule, fromModule, fromPort);
        }
    }

    // Pass 4: support legacy schema where router-attached endpoints are listed under each XP.
    // These links can coexist with/without explicit entries in "connections".
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
                // Legacy endpoint reference points to a module not present in this import.
                qWarning() << "Skipping connection" << epExternalId << "->" << xpExternalId << ": module not found";
                continue;
            }

            QString epPort, xpPort;
            xpPort = firstAvailablePort(this, xpModule, Port::Direction::Output,
                [](const Port& port) { return PortLayout::isEndpointPort(port); });
            epPort = firstAvailablePort(this, epModule, Port::Direction::Input,
                [](const Port& port) { return PortLayout::isEndpointPort(port); });

            if (epPort.isEmpty() || xpPort.isEmpty()) {
                qWarning() << "Skipping connection" << epExternalId << "->" << xpExternalId << ": ports not found";
                continue;
            }

            auto connection = std::make_unique<Connection>(xpId + "_" + epId, PortRef{xpId, xpPort}, PortRef{epId, epPort});
            if (!isValidConnection(connection->source(), connection->target())) {
                // Duplicate/conflicting legacy links are tolerated and skipped.
                qWarning() << "Skipping invalid connection" << xpExternalId << "->" << epExternalId;
                continue;
            }
            addConnection(std::move(connection));
            assignEndpointFallbackPosition(epModule, xpModule, xpPort);
        }
    }

    qInfo() << "Completed graph import from" << jsonPath
            << "modules" << m_modules.size()
            << "connections" << m_connections.size();
    return true;
}

bool Graph::saveToJson(const QString& jsonPath) const {
    qInfo() << "Starting graph export to" << jsonPath
            << "modules" << m_modules.size()
            << "connections" << m_connections.size();
    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open graph JSON for writing:" << jsonPath;
        return false;
    }
    file.write(toJsonDocument(QFileInfo(jsonPath).baseName(), GraphJsonFlavor::Editor).toJson());
    qInfo() << "Completed graph export to" << jsonPath;
    return true;
}

bool Graph::saveToXml(const QString& xmlPath) const {
    qInfo() << "Starting graph XML export to" << xmlPath
            << "modules" << m_modules.size()
            << "connections" << m_connections.size();

    QFile file(xmlPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open graph XML for writing:" << xmlPath;
        return false;
    }

    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writeJsonValueAsXml(writer,
                        QStringLiteral("graph"),
                        toJsonDocument(QFileInfo(xmlPath).baseName(), GraphJsonFlavor::Editor).object());
    writer.writeEndDocument();

    if (writer.hasError()) {
        qWarning() << "Failed while writing graph XML to" << xmlPath;
        return false;
    }

    qInfo() << "Completed graph XML export to" << xmlPath;
    return true;
}

QJsonDocument Graph::toJsonDocument(const QString& designName,
                                    GraphJsonFlavor flavor,
                                    QHash<QString, QString>* externalToInternalIds) const {
    if (externalToInternalIds) {
        externalToInternalIds->clear();
    }

    QJsonArray xps;
    QJsonArray eps;
    QJsonArray conns;
    QHash<QString, QJsonArray> xpEndpointMap;

    for (const auto& mod : m_modules) {
        const QString externalId = ModuleLabels::externalId(mod.get());
        if (externalToInternalIds) {
            externalToInternalIds->insert(externalId, mod->id());
        }

        QJsonObject obj;
        obj["id"] = externalId;

        const auto& params = mod->parameters();
        if (isMeshRouterModule(mod.get())) {
            obj["x"] = params.contains("x") ? parameterToJson(params["x"].value()) : QJsonValue(0);
            obj["y"] = params.contains("y") ? parameterToJson(params["y"].value()) : QJsonValue(0);

            QJsonObject config;
            if (flavor == GraphJsonFlavor::Editor && params.contains("collapsed")) {
                config["collapsed"] = parameterToJson(params["collapsed"].value());
            }
            if (params.contains("routing_algorithm")) config["routing_algorithm"] = parameterToJson(params["routing_algorithm"].value());
            if (params.contains("vc_count")) config["vc_count"] = parameterToJson(params["vc_count"].value());
            if (params.contains("buffer_depth")) config["buffer_depth"] = parameterToJson(params["buffer_depth"].value());
            obj["config"] = config;
            obj["endpoints"] = QJsonArray();
            xpEndpointMap.insert(externalId, QJsonArray());
            xps.append(obj);
        } else if (isEndpointModule(mod.get())) {
            if (params.contains("x")) obj["x"] = parameterToJson(params["x"].value());
            if (params.contains("y")) obj["y"] = parameterToJson(params["y"].value());
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
            isMeshRouterModule(sourceModule) &&
            isEndpointModule(targetModule) &&
            sourcePort && PortLayout::isEndpointPort(*sourcePort)) {
            // Router->endpoint links are represented in XP endpoint lists
            // instead of generic edge list entries.
            auto it = xpEndpointMap.find(sourceExternalId);
            if (it != xpEndpointMap.end()) {
                it.value().append(targetExternalId);
            }
            continue;
        }

        QJsonObject obj;
        obj["from"] = sourceExternalId;
        obj["to"] = targetExternalId;

        if (!(sourceModule && targetModule &&
              isMeshRouterModule(sourceModule) &&
              isMeshRouterModule(targetModule))) {
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
    root["name"] = designName.isEmpty() ? QStringLiteral("design") : designName;
    root["version"] = "1.0";
    root["parameters"] = QJsonObject();
    root["xps"] = xps;
    root["endpoints"] = eps;
    root["connections"] = conns;
    return QJsonDocument(root);
}

void Graph::onModuleParameterChanged(const QString& moduleId, const QString& paramName) {
    qDebug() << "Module parameter changed"
             << "moduleId" << moduleId
             << "parameter" << paramName;
    emit parameterChanged(moduleId, paramName);
}
