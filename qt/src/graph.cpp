#include "graph.h"
#include "moduleregistry.h"
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

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
    if (source.moduleId == target.moduleId && source.portId == target.portId) return false;

    Module* sourceModule = getModule(source.moduleId);
    Module* targetModule = getModule(target.moduleId);
    if (!sourceModule || !targetModule) return false;

    auto sourcePortExists = std::any_of(sourceModule->ports().begin(), sourceModule->ports().end(),
        [&](const Port& p) { return p.id() == source.portId; });
    auto targetPortExists = std::any_of(targetModule->ports().begin(), targetModule->ports().end(),
        [&](const Port& p) { return p.id() == target.portId; });
    if (!sourcePortExists || !targetPortExists) return false;

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

    while (!m_modules.empty()) {
        removeModule(m_modules.front()->id());
    }

    QJsonArray xps = root["xps"].toArray();
    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString id = xp["id"].toString();

        const ModuleType* type = ModuleRegistry::instance().getType("XP");
        if (!type) continue;

        auto module = std::make_unique<Module>(id, "XP");

        for (const auto& port : type->defaultPorts) {
            module->addPort(port);
        }
        for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
            module->setParameter(it.key(), it.value().value());
        }

        if (xp.contains("x")) module->setParameter("x", xp["x"].toInt());
        if (xp.contains("y")) module->setParameter("y", xp["y"].toInt());

        QJsonObject config = xp["config"].toObject();
        if (config.contains("routing_algorithm")) module->setParameter("routing_algorithm", config["routing_algorithm"].toString());
        if (config.contains("vc_count")) module->setParameter("vc_count", config["vc_count"].toInt());
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());

        addModule(std::move(module));
    }

    QJsonArray eps = root["endpoints"].toArray();
    for (const auto& epVal : eps) {
        QJsonObject ep = epVal.toObject();
        QString id = ep["id"].toString();

        const ModuleType* type = ModuleRegistry::instance().getType("Endpoint");
        if (!type) continue;

        auto module = std::make_unique<Module>(id, "Endpoint");

        for (const auto& port : type->defaultPorts) {
            module->addPort(port);
        }
        for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
            module->setParameter(it.key(), it.value().value());
        }

        if (ep.contains("type")) module->setParameter("type", ep["type"].toString());
        if (ep.contains("protocol")) module->setParameter("protocol", ep["protocol"].toString());
        if (ep.contains("data_width")) module->setParameter("data_width", ep["data_width"].toInt());

        QJsonObject config = ep["config"].toObject();
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());
        if (config.contains("qos_enabled")) module->setParameter("qos_enabled", config["qos_enabled"].toBool());

        addModule(std::move(module));
    }

    QJsonArray conns = root["connections"].toArray();
    for (const auto& connVal : conns) {
        QJsonObject conn = connVal.toObject();
        QString from = conn["from"].toString();
        QString to = conn["to"].toString();
        QString dir = conn["dir"].toString();

        Module* fromModule = getModule(from);
        Module* toModule = getModule(to);
        if (!fromModule || !toModule) {
            qWarning() << "Skipping connection" << from << "->" << to << ": module not found";
            continue;
        }

        QString fromPort, toPort;

        if (!dir.isEmpty()) {
            QMap<QString, QString> oppositeDir = {
                {"north", "south"}, {"south", "north"},
                {"east", "west"}, {"west", "east"}
            };

            for (const auto& port : fromModule->ports()) {
                if (port.direction() == Port::Direction::Output && port.id() == dir) {
                    fromPort = port.id();
                    break;
                }
            }
            for (const auto& port : toModule->ports()) {
                if (port.direction() == Port::Direction::Input && port.id() == oppositeDir[dir]) {
                    toPort = port.id();
                    break;
                }
            }
        }

        if (fromPort.isEmpty()) {
            for (const auto& port : fromModule->ports()) {
                if (port.direction() == Port::Direction::Output) {
                    fromPort = port.id();
                    break;
                }
            }
        }
        if (toPort.isEmpty()) {
            for (const auto& port : toModule->ports()) {
                if (port.direction() == Port::Direction::Input) {
                    toPort = port.id();
                    break;
                }
            }
        }

        if (fromPort.isEmpty() || toPort.isEmpty()) {
            qWarning() << "Skipping connection" << from << "->" << to << ": ports not found";
            continue;
        }

        auto connection = std::make_unique<Connection>(from + "_" + to, PortRef{from, fromPort}, PortRef{to, toPort});
        if (!isValidConnection(connection->source(), connection->target())) {
            qWarning() << "Skipping invalid connection" << from << "->" << to;
            continue;
        }
        addConnection(std::move(connection));
    }

    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString xpId = xp["id"].toString();
        QJsonArray endpoints = xp["endpoints"].toArray();

        for (const auto& epIdVal : endpoints) {
            QString epId = epIdVal.toString();

            Module* epModule = getModule(epId);
            Module* xpModule = getModule(xpId);
            if (!epModule || !xpModule) {
                qWarning() << "Skipping connection" << epId << "->" << xpId << ": module not found";
                continue;
            }

            QString epPort, xpPort;
            for (const auto& port : xpModule->ports()) {
                if (port.direction() == Port::Direction::Output) {
                    xpPort = port.id();
                    break;
                }
            }
            for (const auto& port : epModule->ports()) {
                if (port.direction() == Port::Direction::Input) {
                    epPort = port.id();
                    break;
                }
            }

            if (epPort.isEmpty() || xpPort.isEmpty()) {
                qWarning() << "Skipping connection" << epId << "->" << xpId << ": ports not found";
                continue;
            }

            auto connection = std::make_unique<Connection>(xpId + "_" + epId, PortRef{xpId, xpPort}, PortRef{epId, epPort});
            if (!isValidConnection(connection->source(), connection->target())) {
                qWarning() << "Skipping invalid connection" << xpId << "->" << epId;
                continue;
            }
            addConnection(std::move(connection));
        }
    }

    return true;
}

void Graph::onModuleParameterChanged(const QString& moduleId, const QString& paramName) {
    emit parameterChanged(moduleId, paramName);
}
