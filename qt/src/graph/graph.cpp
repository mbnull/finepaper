// Graph — implementation of the SoC/NoC topology data model.
// add/remove/take/insert follow a consistent ownership pattern:
//   add/insert  — take ownership of a unique_ptr and emit a signal
//   remove      — destroy in-place and emit a signal
//   take        — transfer ownership out (used by undo commands)
#include "graph/graph.h"
#include <algorithm>
#include <QDebug>

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
    // Forward module-local parameter changes through Graph so UI and document
    // tracking do not need to connect to every Module individually.
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
    // Remove dependent edges first so observers never see a connection pointing
    // at a module that has already disappeared.
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

void Graph::clear() {
    while (!m_modules.empty()) {
        removeModule(m_modules.front()->id());
    }
}

Module* Graph::getModule(const QString& moduleId) const {
    auto it = std::find_if(m_modules.begin(), m_modules.end(),
        [&moduleId](const std::unique_ptr<Module>& m) { return m->id() == moduleId; });
    return it != m_modules.end() ? it->get() : nullptr;
}

Connection* Graph::getConnection(const QString& connectionId) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&connectionId](const std::unique_ptr<Connection>& c) { return c->id() == connectionId; });
    return it != m_connections.end() ? it->get() : nullptr;
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
    // insertModule mirrors addModule but is kept separate for undo paths where
    // ownership has been transferred out and back in.
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

bool Graph::setConnectionMetadata(const QString& connectionId,
                                  const QString& connectionClassId,
                                  const QString& status,
                                  QStringList alternatives) {
    Connection* connection = getConnection(connectionId);
    if (!connection) {
        qDebug() << "Requested metadata update for unknown connection" << connectionId;
        return false;
    }

    connection->setConnectionMetadata(connectionClassId, status, std::move(alternatives));
    qInfo() << "Updated connection metadata"
            << "id" << connectionId
            << "class" << connectionClassId
            << "status" << connection->status();
    emit connectionChanged(connection);
    return true;
}

bool Graph::isValidConnection(const PortRef& source, const PortRef& target) const {
    if (source.moduleId == target.moduleId) {
        return false;
    }

    const Module* sourceModule = getModule(source.moduleId);
    const Module* targetModule = getModule(target.moduleId);
    if (!sourceModule || !targetModule) {
        return false;
    }

    if (!findPort(sourceModule, source.portId) || !findPort(targetModule, target.portId)) {
        return false;
    }

    return std::none_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& connection) {
            return connection->source().moduleId == source.moduleId &&
                   connection->source().portId == source.portId &&
                   connection->target().moduleId == target.moduleId &&
                   connection->target().portId == target.portId;
        });
}

void Graph::onModuleParameterChanged(const QString& moduleId, const QString& paramName) {
    qDebug() << "Module parameter changed"
             << "moduleId" << moduleId
             << "parameter" << paramName;
    emit parameterChanged(moduleId, paramName);
}
