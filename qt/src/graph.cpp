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
