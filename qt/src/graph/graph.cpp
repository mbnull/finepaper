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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>
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

QString parameterValueString(const Module* module, const QString& parameterName) {
    if (!module) return {};

    const auto it = module->parameters().find(parameterName);
    if (it == module->parameters().end()) return {};

    const auto& value = it.value().value();
    if (const auto* stringValue = std::get_if<QString>(&value)) return *stringValue;
    if (const auto* intValue = std::get_if<int>(&value)) return QString::number(*intValue);
    if (const auto* doubleValue = std::get_if<double>(&value)) return QString::number(*doubleValue, 'g', 15);
    if (const auto* boolValue = std::get_if<bool>(&value)) return *boolValue ? QStringLiteral("true")
                                                                             : QStringLiteral("false");
    return {};
}

QString canonicalInterfaceFieldValue(const QString& field, const QString& value) {
    if (field == "protocol" && value.compare(QStringLiteral("axi"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("axi4");
    }
    return value;
}

std::optional<ModuleInterfaceMetadata> interfaceMetadataFor(const Module* module, const Port* port) {
    if (!module || !port || port->interfaceId().isEmpty()) {
        return std::nullopt;
    }

    // Interface metadata lives on ModuleType because ports are copied into
    // Module instances while semantic compatibility remains type-level data.
    const ModuleType* moduleType = ModuleTypeMetadata::type(module);
    if (!moduleType) {
        return std::nullopt;
    }

    const auto it = moduleType->interfaceMetadata.find(port->interfaceId());
    return it != moduleType->interfaceMetadata.end()
        ? std::optional<ModuleInterfaceMetadata>(it.value())
        : std::nullopt;
}

QStringList interfaceFieldValues(const ModuleInterfaceMetadata& metadata,
                                 const Module* module,
                                 const QString& field) {
    const auto bindingIt = metadata.parameterBindings.find(field);
    if (bindingIt != metadata.parameterBindings.end()) {
        const QString value = parameterValueString(module, bindingIt.value());
        return value.isEmpty() ? QStringList{} : QStringList{canonicalInterfaceFieldValue(field, value)};
    }

    const auto acceptedIt = metadata.acceptedValues.find(field);
    if (acceptedIt != metadata.acceptedValues.end()) {
        QStringList values;
        for (const QString& value : acceptedIt.value()) {
            values.append(canonicalInterfaceFieldValue(field, value));
        }
        return values;
    }

    return {};
}

bool valuesOverlap(const QStringList& lhs, const QStringList& rhs) {
    for (const QString& value : lhs) {
        if (rhs.contains(value)) {
            return true;
        }
    }
    return false;
}

bool interfaceMetadataCompatible(const ModuleInterfaceMetadata& sourceInterface,
                                 const Module* sourceModule,
                                 const ModuleInterfaceMetadata& targetInterface,
                                 const Module* targetModule) {
    if (sourceInterface.bus != targetInterface.bus) {
        return false;
    }

    if (!sourceInterface.compatibleRoles.contains(targetInterface.role) ||
        !targetInterface.compatibleRoles.contains(sourceInterface.role)) {
        return false;
    }

    QStringList matchFields = sourceInterface.matchFields;
    for (const QString& field : targetInterface.matchFields) {
        if (!matchFields.contains(field)) {
            matchFields.append(field);
        }
    }

    // Both endpoints can require fields such as protocol or data width. A link
    // is valid only when every required field has an overlapping value.
    for (const QString& field : matchFields) {
        const QStringList sourceValues = interfaceFieldValues(sourceInterface, sourceModule, field);
        const QStringList targetValues = interfaceFieldValues(targetInterface, targetModule, field);
        if (sourceValues.isEmpty() || targetValues.isEmpty() || !valuesOverlap(sourceValues, targetValues)) {
            return false;
        }
    }

    return true;
}

bool interfacesCompatible(const Module* sourceModule,
                          const Port* sourcePort,
                          const Module* targetModule,
                          const Port* targetPort) {
    const std::optional<ModuleInterfaceMetadata> sourceInterface = interfaceMetadataFor(sourceModule, sourcePort);
    const std::optional<ModuleInterfaceMetadata> targetInterface = interfaceMetadataFor(targetModule, targetPort);

    if (!sourceInterface && !targetInterface) {
        return true;
    }
    if (!sourceInterface || !targetInterface) {
        return false;
    }

    return interfaceMetadataCompatible(*sourceInterface, sourceModule, *targetInterface, targetModule);
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
    // Validation deliberately stays in Graph because UI gestures, imports, and
    // command replay all need the same topology contract.
    // Disallow self-loops at graph level.
    if (source.moduleId == target.moduleId) return false;

    const Module* sourceModule = getModule(source.moduleId);
    const Module* targetModule = getModule(target.moduleId);
    // Both endpoints must refer to live modules before any port-level checks
    // can be meaningful.
    if (!sourceModule || !targetModule) return false;

    const Port* sourcePort = findPort(sourceModule, source.portId);
    const Port* targetPort = findPort(targetModule, target.portId);
    // Port IDs are intentionally not normalized here; callers must pass the
    // concrete IDs for the current module definitions.
    if (!sourcePort || !targetPort) return false;

    // Generic electrical/interface checks run before topology-specific mesh
    // constraints so every path observes the same base compatibility contract.
    if (!PortLayout::supportsOutput(*sourcePort)) return false;
    if (!PortLayout::supportsInput(*targetPort)) return false;
    if (!PortLayout::sameBusFamily(*sourcePort, *targetPort)) return false;
    if (!interfacesCompatible(sourceModule, sourcePort, targetModule, targetPort)) return false;

    if (isRouterLink(sourceModule, sourcePort, targetModule, targetPort)) {
        // Router-to-router links are constrained to one opposite-side pair and
        // one connection per side to preserve mesh semantics.
        const QString sourceSide = PortLayout::routerSideId(source.portId);
        const QString targetSide = PortLayout::routerSideId(target.portId);

        if (sourceSide == targetSide) {
            return false;
        }
        if (oppositeDirection(sourceSide) != targetSide) {
            // Mesh links must join opposite sides, for example east to west.
            return false;
        }

        for (const auto& existingConnection : m_connections) {
            const bool sameRouterPair =
                (existingConnection->source().moduleId == source.moduleId &&
                 existingConnection->target().moduleId == target.moduleId) ||
                (existingConnection->source().moduleId == target.moduleId &&
                 existingConnection->target().moduleId == source.moduleId);

            if (sameRouterPair) {
                // A pair of routers can have at most one edge between them.
                return false;
            }

            if (connectionUsesRouterSide(*existingConnection, source.moduleId, sourceSide) ||
                connectionUsesRouterSide(*existingConnection, target.moduleId, targetSide)) {
                // Router sides model physical channels and cannot fan out.
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

    // Targets use their natural direction for occupancy; InOut ports are
    // considered consumed regardless of whether an edge uses them as source or target.
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

    // The final duplicate check is still needed for ports that allow direction
    // reuse in future module definitions.
    return std::none_of(m_connections.begin(), m_connections.end(),
        [&](const std::unique_ptr<Connection>& c) {
            return c->source().moduleId == source.moduleId && c->source().portId == source.portId &&
                   c->target().moduleId == target.moduleId && c->target().portId == target.portId;
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
