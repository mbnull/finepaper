// Graph — implementation of the SoC/NoC topology data model.
// add/remove/take/insert follow a consistent ownership pattern:
//   add/insert  — take ownership of a unique_ptr and emit a signal
//   remove      — destroy in-place and emit a signal
//   take        — transfer ownership out (used by undo commands)
#include "graph/graph.h"
#include "modules/modulelabels.h"
#include "modules/moduleregistry.h"
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>

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

QJsonObject portToGenericJson(const Port& port) {
    QJsonObject object;
    object["id"] = port.id();
    object["direction"] = directionToJsonString(port.direction());
    object["type"] = port.type();
    object["name"] = port.name();
    if (!port.description().isEmpty()) object["description"] = port.description();
    if (!port.role().isEmpty()) object["role"] = port.role();
    if (!port.busType().isEmpty()) object["bus_type"] = port.busType();
    if (!port.interfaceId().isEmpty()) object["interface"] = port.interfaceId();
    return object;
}

QJsonObject parametersToGenericJson(const Module* module) {
    QJsonObject parameters;
    if (!module) return parameters;
    for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
        parameters.insert(it.key(), parameterToJson(it.value().value()));
    }
    return parameters;
}

QString safeArtifactToken(QString token, const QString& fallback) {
    token = token.trimmed();
    if (token.isEmpty()) {
        token = fallback.trimmed();
    }
    token.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_$]+")), QStringLiteral("_"));
    token.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    token = token.trimmed();
    while (token.startsWith(QStringLiteral("_"))) {
        token.remove(0, 1);
    }
    while (token.endsWith(QStringLiteral("_"))) {
        token.chop(1);
    }
    if (token.isEmpty()) {
        token = QStringLiteral("module");
    }
    if (!token.front().isLetter() && token.front() != QLatin1Char('_')) {
        token.prepend(QStringLiteral("m_"));
    }
    // Plugin-facing IDs become filenames or HDL-ish identifiers in downstream
    // tools, so keep them deterministic and conservative.
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

QString pluginModuleArtifactId(const Module* module, QSet<QString>& usedModuleIds) {
    const QString fallback = module ? module->type().toLower() : QStringLiteral("module");
    return uniqueArtifactToken(safeArtifactToken(ModuleLabels::externalId(module), fallback), usedModuleIds);
}

QString pluginConnectionArtifactId(const QString& sourceModuleId,
                                   const QString& sourcePortId,
                                   const QString& targetModuleId,
                                   const QString& targetPortId,
                                   QSet<QString>& usedConnectionIds) {
    const QString raw = QStringLiteral("%1_%2_to_%3_%4")
                            .arg(sourceModuleId, sourcePortId, targetModuleId, targetPortId);
    return uniqueArtifactToken(safeArtifactToken(raw, QStringLiteral("connection")), usedConnectionIds);
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

QJsonDocument Graph::toJsonDocument(const QString& designName,
                                    GraphJsonFlavor flavor,
                                    QHash<QString, QString>* externalToInternalIds) const {
    Q_UNUSED(flavor);
    if (externalToInternalIds) {
        // Callers such as DRCRunner use this map to translate generated artifact
        // IDs back to editor-internal IDs for selection/highlighting.
        externalToInternalIds->clear();
    }

    QJsonArray modules;
    QJsonArray connections;
    QHash<QString, QString> runtimeToArtifactIds;
    QSet<QString> usedModuleIds;

    for (const auto& module : m_modules) {
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        // External ID parameters are user/plugin visible; uniqueArtifactToken
        // protects against duplicates when several modules share a label.
        const QString artifactId = pluginModuleArtifactId(module.get(), usedModuleIds);
        runtimeToArtifactIds.insert(module->id(), artifactId);
        if (externalToInternalIds) {
            externalToInternalIds->insert(artifactId, module->id());
        }

        QJsonObject object;
        object["id"] = artifactId;
        object["plugin"] = type ? type->pluginId : QString();
        object["type"] = module->type();
        object["parameters"] = parametersToGenericJson(module.get());

        QJsonArray ports;
        for (const Port& port : module->ports()) {
            // Generic plugin consumers need the effective runtime port list,
            // not only the module type name.
            ports.append(portToGenericJson(port));
        }
        object["ports"] = ports;
        // Port and parameter snapshots are duplicated into the export so a
        // generator does not have to re-open module bundle metadata.
        modules.append(object);
    }

    QSet<QString> usedConnectionIds;
    for (const auto& connection : m_connections) {
        // Runtime IDs are editor-only UUIDs; plugin inputs receive stable,
        // human-readable artifact IDs instead.
        const QString sourceModuleId = runtimeToArtifactIds.value(connection->source().moduleId);
        const QString targetModuleId = runtimeToArtifactIds.value(connection->target().moduleId);
        if (sourceModuleId.isEmpty() || targetModuleId.isEmpty()) {
            continue;
        }

        QJsonObject object;
        object["id"] = pluginConnectionArtifactId(sourceModuleId,
                                                  connection->source().portId,
                                                  targetModuleId,
                                                  connection->target().portId,
                                                  usedConnectionIds);
        object["source"] = QJsonObject{
            {QStringLiteral("module"), sourceModuleId},
            {QStringLiteral("port"), connection->source().portId}
        };
        object["target"] = QJsonObject{
            {QStringLiteral("module"), targetModuleId},
            {QStringLiteral("port"), connection->target().portId}
        };
        connections.append(object);
    }

    QJsonObject root;
    root["schema"] = QStringLiteral("finepaper-plugin-graph-v1");
    root["name"] = designName.isEmpty() ? QStringLiteral("design") : designName;
    root["modules"] = modules;
    root["connections"] = connections;
    return QJsonDocument(root);
}

void Graph::onModuleParameterChanged(const QString& moduleId, const QString& paramName) {
    qDebug() << "Module parameter changed"
             << "moduleId" << moduleId
             << "parameter" << paramName;
    emit parameterChanged(moduleId, paramName);
}
