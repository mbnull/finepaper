#include "graph.h"
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

Graph::Graph(QObject* parent) : QObject(parent) {
}

void Graph::addModule(std::unique_ptr<Module> module) {
    Module* ptr = module.get();
    connect(ptr, &Module::parameterChanged, this, [this, ptr](const QString& paramName) {
        emit parameterChanged(ptr->id(), paramName);
    });
    m_modules.push_back(std::move(module));
    emit moduleAdded(ptr);
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
        std::unique_ptr<Module> module = std::move(*it);
        m_modules.erase(it);
        emit moduleRemoved(moduleId);
        return module;
    }
    return nullptr;
}

void Graph::insertModule(std::unique_ptr<Module> module) {
    Module* ptr = module.get();
    connect(ptr, &Module::parameterChanged, this, [this, ptr](const QString& paramName) {
        emit parameterChanged(ptr->id(), paramName);
    });
    m_modules.push_back(std::move(module));
    emit moduleAdded(ptr);
}

void Graph::addConnection(std::unique_ptr<Connection> connection) {
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
        auto module = std::make_unique<Module>(id, "XP");

        if (xp.contains("x")) module->setParameter("x", xp["x"].toInt());
        if (xp.contains("y")) module->setParameter("y", xp["y"].toInt());

        QJsonObject config = xp["config"].toObject();
        if (config.contains("routing_algorithm")) module->setParameter("routing_algorithm", config["routing_algorithm"].toString());
        if (config.contains("vc_count")) module->setParameter("vc_count", config["vc_count"].toInt());
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());

        module->addPort(Port("in", Port::Direction::Input, "noc", "Input"));
        module->addPort(Port("out", Port::Direction::Output, "noc", "Output"));
        addModule(std::move(module));
    }

    QJsonArray eps = root["endpoints"].toArray();
    for (const auto& epVal : eps) {
        QJsonObject ep = epVal.toObject();
        QString id = ep["id"].toString();
        auto module = std::make_unique<Module>(id, "Endpoint");

        if (ep.contains("type")) module->setParameter("type", ep["type"].toString());
        if (ep.contains("protocol")) module->setParameter("protocol", ep["protocol"].toString());
        if (ep.contains("data_width")) module->setParameter("data_width", ep["data_width"].toInt());

        QJsonObject config = ep["config"].toObject();
        if (config.contains("buffer_depth")) module->setParameter("buffer_depth", config["buffer_depth"].toInt());
        if (config.contains("qos_enabled")) module->setParameter("qos_enabled", config["qos_enabled"].toBool());

        module->addPort(Port("port", Port::Direction::Output, "noc", "Port"));
        addModule(std::move(module));
    }

    QJsonArray conns = root["connections"].toArray();
    for (const auto& connVal : conns) {
        QJsonObject conn = connVal.toObject();
        QString from = conn["from"].toString();
        QString to = conn["to"].toString();
        auto connection = std::make_unique<Connection>(from + "_" + to, PortRef{from, "out"}, PortRef{to, "in"});
        addConnection(std::move(connection));
    }

    for (const auto& xpVal : xps) {
        QJsonObject xp = xpVal.toObject();
        QString xpId = xp["id"].toString();
        QJsonArray endpoints = xp["endpoints"].toArray();

        for (const auto& epIdVal : endpoints) {
            QString epId = epIdVal.toString();
            auto connection = std::make_unique<Connection>(epId + "_" + xpId, PortRef{epId, "port"}, PortRef{xpId, "in"});
            addConnection(std::move(connection));
        }
    }

    return true;
}
