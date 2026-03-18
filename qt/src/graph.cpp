#include "graph.h"
#include <algorithm>

Graph::Graph(QObject* parent) : QObject(parent) {
}

void Graph::addModule(std::unique_ptr<Module> module) {
    Module* ptr = module.get();
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
